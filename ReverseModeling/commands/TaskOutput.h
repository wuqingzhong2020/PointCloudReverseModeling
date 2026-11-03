/*
 * 文件描述：单次外部任务的输出目录锁及 STEP/结果 JSON 提交；仅由命令工作线程使用。
 */
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>

#include "core/Types.h"

namespace reverse
{
class TaskOutput final
{
public:
    TaskOutput();
    ~TaskOutput();
    TaskOutput(const TaskOutput &) = delete;
    TaskOutput &operator=(const TaskOutput &) = delete;

    void initialize(const std::filesystem::path &directory);
    // writer 必须完成现有 STEP 导出及回读验证。成功提交后不再响应取消。
    [[nodiscard]] StepExportResult deliver(
        const std::function<StepExportResult(const std::filesystem::path &)> &writer,
        std::stop_token stop = {});
    [[nodiscard]] bool initialized() const;
    [[nodiscard]] std::filesystem::path modelPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace reverse
