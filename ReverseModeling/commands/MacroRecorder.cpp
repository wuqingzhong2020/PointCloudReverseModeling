/* 文件描述：每条成功命令立即写入并刷新；写入失败停止录制并保留完整前缀。 */
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include "commands/MacroRecorder.h"
#include "commands/CommandFeedback.h"
namespace reverse
{
rm::CommandResult MacroRecorder::start(const std::filesystem::path &path, bool overwrite)
{
    if (m_state.active)
    {
        return rm::CommandResult::error("already_recording", "已在录制中，请先停止录制。");
    }
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (extension != ".py")
    {
        return rm::CommandResult::error("invalid_script_path", "录制文件必须使用 .py 扩展名。");
    }
    const auto target = std::filesystem::absolute(path);
    const bool replacing = std::filesystem::exists(target);
    if (replacing && !overwrite)
    {
        return rm::CommandResult::error("output_exists", "脚本文件已存在，未覆盖。");
    }
    std::error_code directoryError;
    std::filesystem::create_directories(target.parent_path(), directoryError);
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return rm::CommandResult::error("recording_open_failed", "无法创建录制脚本，请检查目录和写入权限。");
    }
    file << "# ReverseModeler 命令脚本（UTF-8）\n"
            "# 每行一条命令，与命令窗口相同；失败停止，前序提交不自动回滚。\n\n";
    file.flush();
    if (!file)
    {
        return rm::CommandResult::error("recording_write_failed", "无法写入录制脚本。");
    }
    m_file = std::move(file);
    m_path = target;
    const auto utf8 = target.u8string();
    m_state = {true, std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size()), 0, {}};
    return {rm::ResultKind::Success, {}, (replacing ? "录制文件已存在，已覆盖：" : "开始录制：") + m_state.pathUtf8, {}, false};
}
rm::CommandResult MacroRecorder::stop()
{
    if (!m_state.active)
    {
        return rm::CommandResult::error("not_recording", "当前没有正在录制的脚本。");
    }
    m_file.flush();
    m_file.close();
    m_state.active = false;
    if (m_file.fail())
    {
        m_state.error = "关闭录制脚本失败，请检查文件：" + m_state.pathUtf8;
        return rm::CommandResult::error("recording_write_failed", m_state.error);
    }
    return {rm::ResultKind::Success,
            {},
            "已停止录制，共 " + std::to_string(m_state.commandCount) + " 条命令：" + m_state.pathUtf8,
            {},
            false};
}
void MacroRecorder::fail(std::string message)
{
    if (m_file.is_open())
    {
        m_file.close();
    }
    m_state.active = false;
    m_state.error = "录制已停止：" + std::move(message) + "。已执行的命令不会撤销。文件：" + m_state.pathUtf8;
}
void MacroRecorder::record(const rm::CommandDefinition &definition, const rm::ValidatedCommand &command)
{
    if (!recording())
    {
        return;
    }
    const auto offset = m_file.tellp();
    try
    {
        rm::Array positional;
        rm::Arguments options;
        std::map<std::size_t, rm::Value> ordered;
        for (const auto &parameter : definition.parameters)
        {
            const auto value = command.arguments.find(parameter.name);
            if (value == command.arguments.end())
            {
                continue;
            }
            if (parameter.binding == rm::Binding::Option)
            {
                options.emplace(value->first, value->second);
            }
            else
            {
                ordered.emplace(parameter.position, value->second);
            }
        }
        for (const auto &[index, value] : ordered)
        {
            positional.push_back(value);
        }
        const auto line = commandText(definition, positional, options).native + '\n';
        if (line.find_first_of("\r\n") != line.size() - 1)
        {
            throw std::runtime_error("命令参数含换行，无法按每行一条命令录制");
        }
        m_file.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_file.flush();
        if (!m_file)
        {
            throw std::runtime_error("无法写入脚本，请检查磁盘空间和权限");
        }
        ++m_state.commandCount;
    }
    catch (const std::exception &error)
    {
        fail(error.what());
        std::error_code rollbackError;
        if (offset >= 0)
        {
            std::filesystem::resize_file(m_path, static_cast<std::uintmax_t>(offset), rollbackError);
        }
        if (offset < 0 || rollbackError)
        {
            m_state.error += "；文件末尾可能不完整，请检查后再回放。";
        }
    }
}
} // namespace reverse
