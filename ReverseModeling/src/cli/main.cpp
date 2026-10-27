/*
 * 文件描述：
 * 提供不创建 Qt 窗口的 PCD 检查与辅助建模命令，执行 STEP 回读及偏差计算并输出 JSON 报告。
 */
#include "reverse/CommandService.h"
#include "reverse/Geometry.h"
#include "reverse/Json.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interruptHandler(int) { interrupted = 1; }

std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool sameDestination(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto first = std::filesystem::weakly_canonical(std::filesystem::absolute(a));
    const auto second = std::filesystem::weakly_canonical(std::filesystem::absolute(b));
#ifdef _WIN32
    return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return first == second;
#endif
}

// 先独占创建临时报告，写完后以不覆盖语义发布；并发创建的用户文件不会被截断。
void writeNewReport(const std::filesystem::path& destination, const std::string& contents) {
    std::random_device random;
    auto temporary = destination;
    temporary += ".reverse-report-" + std::to_string(random()) + "-" + std::to_string(random()) + ".tmp";
#ifdef _WIN32
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw reverse::GeometryError("file_write_error", "Could not create report temporary file.");
    bool written = true;
    for (std::size_t offset = 0; offset < contents.size();) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(contents.size() - offset, 1024 * 1024));
        DWORD actual{};
        if (!WriteFile(file, contents.data() + offset, count, &actual, nullptr) || actual != count) { written = false; break; }
        offset += actual;
    }
    if (!FlushFileBuffers(file)) written = false;
    if (!CloseHandle(file)) written = false;
    const bool published = written && MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
#else
    const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) throw reverse::GeometryError("file_write_error", "Could not create report temporary file.");
    bool written = true;
    for (std::size_t offset = 0; offset < contents.size();) {
        const auto actual = ::write(file, contents.data() + offset, contents.size() - offset);
        if (actual < 0 && errno == EINTR) continue;
        if (actual <= 0) { written = false; break; }
        offset += static_cast<std::size_t>(actual);
    }
    if (::fsync(file) != 0) written = false;
    if (::close(file) != 0) written = false;
    const bool published = written && ::link(temporary.c_str(), destination.c_str()) == 0;
#endif
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (!published) throw reverse::GeometryError("file_write_error", "Report write or no-overwrite commit failed; existing destination was preserved.");
}

void usage() {
    std::cout << "ReverseCmd inspect --input cloud.pcd --unit mm|cm|m [--report report.json]\n"
                 "ReverseCmd build --input cloud.pcd --unit mm|cm|m --parameters parameters.json\n"
                 "                 --output result.step [--report report.json]\n\n"
                 "inspect: load PCD and estimate PCA frame/bounds; does not infer plate thickness.\n"
                 "build: user-confirmed H-beam/rib parameters, STEP export/readback and finite-plate deviation.\n"
                 "All model parameters are mm. Existing output/report files are never overwritten.\n"
                 "Ctrl+C requests cancellation; OCCT operations may finish their current phase first.\n";
}

class BackendConsole {
public:
    BackendConsole() : previous_(std::cout.rdbuf(std::cerr.rdbuf())) {}
    ~BackendConsole() { std::cout.rdbuf(previous_); }
private:
    std::streambuf* previous_;
};

reverse::TaskSnapshot waitFor(reverse::CommandService& service, const std::string& id) {
    int lastPercentage = -1;
    for (;;) {
        auto task = service.task(id);
        if (task.finished()) return task;
        if (interrupted && !task.cancellationRequested) (void)service.cancel(id);
        const int percentage = static_cast<int>(task.progress * 100.0);
        if (percentage / 10 != lastPercentage / 10 || lastPercentage < 0) {
            std::cerr << task.operation << ": " << percentage << "% " << task.message << '\n';
            lastPercentage = percentage;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

int run(const std::vector<std::string>& args) {
    using namespace reverse;
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        usage();
        return args.size() < 2 ? 2 : 0;
    }
    const auto command = args[1];
    if (command != "inspect" && command != "build") throw GeometryError("invalid_command", "Expected inspect or build.");
    std::map<std::string, std::string> options;
    for (std::size_t i = 2; i < args.size(); i += 2) {
        if (i + 1 == args.size()) throw GeometryError("invalid_arguments", "Missing option value.");
        const auto& key = args[i];
        if (key != "--input" && key != "--unit" && key != "--parameters" && key != "--output" && key != "--report")
            throw GeometryError("invalid_arguments", "Unknown option: " + key);
        if (!options.emplace(key, args[i + 1]).second) throw GeometryError("invalid_arguments", "Duplicate option: " + key);
    }
    const auto require = [&](const char* key) -> const std::string& {
        const auto it = options.find(key);
        if (it == options.end() || it->second.empty()) throw GeometryError("invalid_arguments", std::string("Required: ") + key);
        return it->second;
    };
    const auto input = pathFromUtf8(require("--input"));
    const auto unit = parseLengthUnit(require("--unit"));
    std::optional<BeamParameters> parameters;
    std::optional<BeamFrame> frame;
    std::filesystem::path output;
    std::filesystem::path report;
    if (command == "build") {
        std::ifstream stream(pathFromUtf8(require("--parameters")), std::ios::binary);
        if (!stream) throw GeometryError("file_read_error", "Cannot open parameter JSON.");
        const auto data = nlohmann::json::parse(stream);
        parameters = parseBeamParameters(data);
        frame = parseFrameOverride(data);
        output = pathFromUtf8(require("--output"));
        if (std::filesystem::exists(output)) throw GeometryError("output_exists", "STEP output already exists.");
    } else if (options.contains("--parameters") || options.contains("--output")) {
        throw GeometryError("invalid_arguments", "inspect does not accept --parameters or --output.");
    }
    if (options.contains("--report")) {
        report = pathFromUtf8(require("--report"));
        if (std::filesystem::exists(report)) throw GeometryError("output_exists", "JSON report already exists.");
        if (!output.empty() && sameDestination(report, output))
            throw GeometryError("invalid_arguments", "STEP output and JSON report must be different files.");
    }
    std::signal(SIGINT, interruptHandler);
    nlohmann::json tasks = nlohmann::json::array();
    CommandService service;
    TaskSnapshot last;
    bool pipelineCancelled = false;
    {
        BackendConsole backendConsole;
        auto execute = [&](auto submit) {
            if (interrupted) { pipelineCancelled = true; return false; }
            last = waitFor(service, submit());
            tasks.push_back(toJson(last));
            return last.state == TaskState::Succeeded;
        };
        bool success = execute([&] { return service.submitImport(input, unit); });
        if (success && parameters) success = execute([&] { return service.submitReconstruct(*parameters, service.project().revision, frame); });
        if (success && parameters) success = execute([&] { return service.submitDeviation(service.project().revision); });
        if (success && parameters) execute([&] { return service.submitExport(output, service.project().revision); });
    }
    nlohmann::json result{{"schemaVersion", 1}, {"success", !pipelineCancelled && last.state == TaskState::Succeeded},
                          {"tasks", std::move(tasks)}, {"project", toJson(service.project())}};
    if (pipelineCancelled) result["errorCode"] = "cancelled";
    const auto text = result.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!report.empty()) {
        writeNewReport(report, text + '\n');
    }
    std::cout << text << '\n';
    return pipelineCancelled || last.state == TaskState::Cancelled ? 130 : last.state == TaskState::Succeeded ? 0 : 1;
}

int safeRun(const std::vector<std::string>& args) {
    try { return run(args); }
    catch (const reverse::GeometryError& error) {
        std::cout << nlohmann::json{{"schemaVersion", 1}, {"success", false}, {"errorCode", error.code()}, {"message", error.what()}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
    } catch (const std::exception& error) {
        std::cout << nlohmann::json{{"schemaVersion", 1}, {"success", false}, {"errorCode", "invalid_request"}, {"message", error.what()}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
    }
    return 2;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const auto text = std::filesystem::path(argv[i]).u8string();
        args.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return safeRun(args);
}
#else
int main(int argc, char* argv[]) { return safeRun({argv, argv + argc}); }
#endif
