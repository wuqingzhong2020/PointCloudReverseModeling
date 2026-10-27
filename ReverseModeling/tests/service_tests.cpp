/*
 * 文件描述：
 * 验证点云单位与属性、有限板件几何、STEP 回读，以及异步命令失败时的版本和结果保护。
 */
#include "reverse/CommandService.h"
#include "reverse/Geometry.h"
#include "reverse/Json.h"
#include "reverse/ParameterDefaults.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
template<class F> void expectError(F action, const std::string& code) {
    try { action(); }
    catch (const reverse::GeometryError& error) {
        if (error.code() != code) throw std::runtime_error("Unexpected error code: " + error.code() + " (expected " + code + ")");
        return;
    }
    throw std::runtime_error("Expected failure: " + code);
}
reverse::TaskSnapshot wait(reverse::CommandService& service, const std::string& id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        const auto task = service.task(id);
        if (task.finished()) return task;
        if (std::chrono::steady_clock::now() > deadline) {
            (void)service.cancel(id);
            throw std::runtime_error("Task timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void success(reverse::CommandService& service, const std::string& id) {
    const auto task = wait(service, id);
    if (task.state != reverse::TaskState::Succeeded) throw std::runtime_error(task.errorCode + ": " + task.message);
}

struct TemporaryDirectory {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("reverse-modeling-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    TemporaryDirectory() { require(std::filesystem::create_directory(path), "Could not create isolated test directory"); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

void writeCloud(const std::filesystem::path& file) {
    std::ofstream out(file);
    constexpr int count = 11 * 5 * 2 + 1;
    out << "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\nWIDTH "
        << count << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " << count << "\nDATA ascii\n";
    for (int x = -50; x <= 50; x += 10)
        for (int y = -20; y <= 20; y += 10)
            for (int z : {-30, 30}) out << x << ' ' << y << ' ' << z << " 42\n";
    out << "nan 0 0 7\n";
    require(out.good(), "Could not write fixture");
}
} // namespace

int main() {
    try {
        using namespace reverse;
        TemporaryDirectory temp;
        const auto input = temp.path / std::filesystem::path(u8"中文点云.pcd");
        writeCloud(input);
        auto cloud = loadPointCloud(input, LengthUnit::Millimeter);
        require(cloud->sourcePointCount == 111 && cloud->invalidPointCount == 1 && cloud->points.size() == 110,
                "Invalid point filtering/counts are wrong");
        require(cloud->hasIntensity && cloud->points.front().intensity == 42 && cloud->points.back().sourceIndex == 109,
                "Intensity or original source index lost");
        auto meterCloud = loadPointCloud(input, LengthUnit::Meter);
        close(meterCloud->boundsMaxMm.x - meterCloud->boundsMinMm.x, 100000, 1e-5, "Unit conversion failed");
        const auto estimate = estimateBeam(*cloud);
        close(estimate.length, 100, 1e-6, "PCA length initial guess failed");

        BeamEstimate largeEstimate;
        largeEstimate.length = 9111.5683;
        largeEstimate.width = 627.8323;
        largeEstimate.height = 186.4941;
        const auto suggested = suggestBeamParameters(largeEstimate);
        require(suggested && suggested->parameters.swapCrossSection, "Reference orientation should choose taller section");
        close(suggested->parameters.width, 186.4941, 1e-8, "Suggested width must match swapped frame");
        close(suggested->parameters.height, 627.8323, 1e-8, "Suggested height must match swapped frame");
        close(suggested->parameters.webThickness, 8, 1e-8, "Default web thickness unavailable");
        close(suggested->parameters.flangeThickness, 12, 1e-8, "Default flange thickness unavailable");
        close(suggested->parameters.ribThickness, 8, 1e-8, "Default rib thickness unavailable");
        close(suggested->parameters.ribStations.at(0), 3037.1894, 1e-8, "First reference station is wrong");
        close(suggested->parameters.ribStations.at(1), 6074.3789, 1e-8, "Second reference station is wrong");
        require(buildBeam(largeEstimate.frame, suggested->parameters)->summary().solidCount == 7,
                "Reference defaults must generate a valid seven-plate initial model");
        BeamEstimate smallEstimate;
        smallEstimate.length = 30;
        smallEstimate.width = 10;
        smallEstimate.height = 20;
        const auto smallSuggestion = suggestBeamParameters(smallEstimate);
        require(smallSuggestion && !smallSuggestion->parameters.swapCrossSection, "Small cloud reference orientation failed");
        close(smallSuggestion->parameters.webThickness, 2.5, 1e-8, "Small cloud thickness must scale down");
        require(buildBeam(smallEstimate.frame, smallSuggestion->parameters)->summary().solidCount == 7,
                "Small cloud defaults must not overlap or collapse plates");
        auto badEstimate = smallEstimate;
        badEstimate.width = std::numeric_limits<double>::quiet_NaN();
        require(!suggestBeamParameters(badEstimate), "NaN dimensions must not create defaults");
        badEstimate.width = 0.001;
        require(!suggestBeamParameters(badEstimate), "Sub-resolution dimensions must not create zero-thickness defaults");
        badEstimate.width = 1e10;
        require(!suggestBeamParameters(badEstimate), "Unsupported dimensions must not be silently clamped");

        BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false};
        const BeamFrame frame;
        auto model = buildBeam(frame, parameters);
        require(model->summary().solidCount == 7, "Expected three main plates plus four ribs");
        close(model->summary().volumeMm3, 70800, 1e-6, "Plate volume is wrong");
        CloudData query;
        query.points = {{{0, 0, 30}, 0, 0}, {{0, 0, 0}, 0, 1}, {{60, 0, 0}, 0, 2}};
        const auto deviation = evaluateDeviation(query, *model);
        close(deviation.distancesMm.at(0), 0, 1e-10, "Surface point distance is wrong");
        close(deviation.distancesMm.at(1), 2, 1e-10, "Interior point must have distance to surface, not zero");
        close(deviation.distancesMm.at(2), 10, 1e-10, "Finite end face distance is wrong");

        std::stop_source cancelled;
        cancelled.request_stop();
        expectError([&] { (void)loadPointCloud(input, LengthUnit::Millimeter, cancelled.get_token()); }, "cancelled");
        expectError([&] { (void)parseLengthUnit("unknown"); }, "invalid_unit");
        expectError([&] { (void)parseBeamParameters(nlohmann::json{{"length", 100}}); }, "invalid_parameters");
        const auto malformed = temp.path / "malformed.pcd";
        {
            std::ofstream out(malformed);
            out << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 4294967295 1 1\n"
                   "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA binary\n";
        }
        expectError([&] { (void)loadPointCloud(malformed, LengthUnit::Millimeter); }, "invalid_pcd");

        CommandService service;
        success(service, service.submitImport(input, LengthUnit::Millimeter));
        const auto imported = service.project();
        require(imported.revision == 1 && imported.cloud && imported.estimate, "Import did not commit snapshot");
        require(imported.suggestedParameters && imported.suggestedParameters->parameters.webThickness > 0,
                "Successful import should expose usable shared reference parameters");
        const auto importJson = toJson(imported);
        require(importJson.at("suggestedParameters").at("source") == "reference_preset_not_measurement",
                "Reference parameters must not be serialized as measured geometry");
        expectError([&] { (void)service.submitReconstruct(parameters, 0); }, "stale_revision");
        success(service, service.submitReconstruct(parameters, imported.revision, frame));
        const auto built = service.project();
        require(built.revision == 2 && built.model && built.mesh && built.model->solidCount == 7, "Model commit failed");
        close(built.model->parameters.webThickness, 4, 1e-8, "Building must preserve edited parameters instead of resetting defaults");
        auto invalid = parameters;
        invalid.webThickness = 0;
        const auto failed = wait(service, service.submitReconstruct(invalid, built.revision));
        require(failed.state == TaskState::Failed, "Invalid dimensions should fail");
        require(service.project().revision == built.revision && service.project().modelId == built.modelId,
                "Failed reconstruction changed prior model");
        success(service, service.submitDeviation(built.revision));
        require(service.project().deviation->pointCount == 110, "Deviation discarded valid points");
        const auto output = temp.path / std::filesystem::path(u8"中文模型.step");
        success(service, service.submitExport(output, built.revision));
        const auto exported = service.project();
        require(exported.lastExport && exported.lastExport->valid && exported.lastExport->solidCount == 7,
                "STEP independent readback did not validate solids");
        close(exported.lastExport->volumeMm3, 70800, 1e-4, "STEP changed volume");
        const auto beforeBytes = std::filesystem::file_size(output);
        require(wait(service, service.submitExport(output, built.revision)).state == TaskState::Failed,
                "Export unexpectedly overwrote existing STEP");
        require(std::filesystem::file_size(output) == beforeBytes, "Failed export changed file");
        require(wait(service, service.submitImport(temp.path / "missing.pcd", LengthUnit::Millimeter)).state == TaskState::Failed,
                "Missing cloud must fail");
        require(service.project().modelId == built.modelId, "Failed import discarded model");
        const auto json = toJson(service.project());
        require(json.at("schemaVersion") == 1 && json.at("model").at("solidCount") == 7 && !json.at("cloud").contains("points"),
                "Serializable summary contract failed");
        require(nlohmann::json::parse(json.dump()) == json, "JSON roundtrip failed");
        const auto planar = temp.path / "planar.pcd";
        {
            std::ofstream out(planar);
            out << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
                   "WIDTH 3\nHEIGHT 1\nPOINTS 3\nDATA ascii\n0 0 0\n10 0 0\n0 10 0\n";
        }
        success(service, service.submitImport(planar, LengthUnit::Millimeter));
        const auto planarProject = service.project();
        require(planarProject.cloud && !planarProject.estimate && !planarProject.suggestedParameters && !planarProject.model && !planarProject.warnings.empty(),
                "Valid planar cloud must import even without a beam frame");
        expectError([&] { (void)service.submitReconstruct(parameters, planarProject.revision); }, "no_frame");
        std::cout << "Passed: units/attributes, geometry, STEP roundtrip, deviation, revision and failure preservation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
