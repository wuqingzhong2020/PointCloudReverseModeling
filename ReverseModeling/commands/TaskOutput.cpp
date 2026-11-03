/*
 * 文件描述：锁定任务输出目录，初始化失败标志；验证后的 STEP 先原子替换，成功 JSON 最后原子提交。
 * 失败或取消不改变成功标志；文件系统操作由服务工作线程调用，不占用 GUI 线程。
 */
#include <fstream>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "commands/TaskOutput.h"
#include "geometry/StagedFile.h"

namespace reverse
{
namespace
{
std::string pathText(const std::filesystem::path &path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
void replaceFile(const std::filesystem::path &source, const std::filesystem::path &target)
{
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        throw GeometryError("output_commit_failed", "无法替换文件（可能被占用或无写权限）：" + pathText(target) +
                                                       "；系统错误 " + std::to_string(GetLastError()));
    }
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    if (error)
    {
        throw GeometryError("output_commit_failed", "无法替换文件：" + pathText(target) + "；" + error.message());
    }
#endif
}
void writeResult(const std::filesystem::path &directory, bool success)
{
    detail::StagedFile temporary(directory);
    temporary.file = temporary.directory / "result.json";
    const auto json = nlohmann::json{{"model_file", "reserveModel.step"}, {"export_success", success}}.dump(2) + '\n';
    std::ofstream file(temporary.file, std::ios::binary | std::ios::trunc);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    file.flush();
    if (!file)
    {
        throw GeometryError("result_write_failed", "结果 JSON 写入失败：" + pathText(directory / "result.json"));
    }
    file.close();
    if (file.fail())
    {
        throw GeometryError("result_write_failed", "结果 JSON 关闭失败：" + pathText(directory / "result.json"));
    }
    replaceFile(temporary.file, directory / "result.json");
}
void checkCancelled(std::stop_token stop)
{
    if (stop.stop_requested())
    {
        throw GeometryError("cancelled", "导出并退出已取消，尚未提交成功结果。");
    }
}
} // namespace

struct TaskOutput::Impl
{
    std::filesystem::path directory;
    bool initialized{};
    bool delivered{};
#ifdef _WIN32
    HANDLE lock{INVALID_HANDLE_VALUE};
#else
    int lock{-1};
#endif
    ~Impl()
    {
#ifdef _WIN32
        if (lock != INVALID_HANDLE_VALUE)
        {
            CloseHandle(lock);
        }
#else
        if (lock >= 0)
        {
            // 保留锁文件路径，避免另一个进程已锁定旧 inode 时删除路径造成竞争。
            flock(lock, LOCK_UN);
            close(lock);
        }
#endif
    }
    void acquire()
    {
        const auto path = directory / ".reverse-output.lock";
#ifdef _WIN32
        lock = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (lock == INVALID_HANDLE_VALUE)
#else
        lock = open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0)
#endif
        {
            throw GeometryError("output_directory_busy", "输出目录被其他实例使用，或无法写入：" + pathText(directory));
        }
    }
};
TaskOutput::TaskOutput() : m_impl(std::make_unique<Impl>())
{
}
TaskOutput::~TaskOutput() = default;
void TaskOutput::initialize(const std::filesystem::path &directory)
{
    if (m_impl->initialized)
    {
        std::error_code error;
        if (!m_impl->delivered && std::filesystem::equivalent(directory, m_impl->directory, error) && !error)
        {
            return;
        }
        throw GeometryError("output_already_initialized", "本次输出目录已设置为 " + pathText(m_impl->directory) +
                            "，不能重新设置为 " + pathText(directory));
    }
    if (directory.empty())
    {
        throw GeometryError("invalid_output", "输出目录不能为空。");
    }
    auto next = std::make_unique<Impl>();
    next->directory = std::filesystem::absolute(directory).lexically_normal();
    std::error_code error;
    std::filesystem::create_directories(next->directory, error);
    if (error || !std::filesystem::is_directory(next->directory))
    {
        throw GeometryError("invalid_output", "输出路径不是目录或无法创建：" + pathText(next->directory) +
                                                 "；" + error.message());
    }
    // canonical 使目录别名/符号链接也争用同一把文件锁。
    next->directory = std::filesystem::canonical(next->directory);
    next->acquire();
    writeResult(next->directory, false);
    next->initialized = true;
    m_impl = std::move(next);
}
bool TaskOutput::initialized() const
{
    return m_impl->initialized && !m_impl->delivered;
}
std::filesystem::path TaskOutput::modelPath() const
{
    return m_impl->directory / "reserveModel.step";
}
StepExportResult TaskOutput::deliver(
    const std::function<StepExportResult(const std::filesystem::path &)> &writer, std::stop_token stop)
{
    if (!initialized())
    {
        throw GeometryError("output_not_initialized", "本次结果输出未初始化，不能导出并退出。");
    }
    checkCancelled(stop);
    detail::StagedFile temporary(m_impl->directory);
    temporary.file = temporary.directory / "reserveModel.step";
    const auto result = writer(temporary.file);
    std::error_code error;
    const auto bytes = std::filesystem::file_size(temporary.file, error);
    if (error || bytes == 0 || result.fileBytes != bytes || !result.valid ||
        (result.solidCount == 0 && result.surfaceCount == 0))
    {
        throw GeometryError("step_write_failed", "临时 STEP 未完整写入，无法提交结果。");
    }
    checkCancelled(stop);
    replaceFile(temporary.file, modelPath());
    checkCancelled(stop);
    writeResult(m_impl->directory, true);
    m_impl->delivered = true;
    return result;
}
} // namespace reverse
