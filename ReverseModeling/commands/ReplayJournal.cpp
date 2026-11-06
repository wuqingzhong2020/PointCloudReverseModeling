/* 文件描述：中途录制补写保留撤销/重做分支，排除既往磁盘导出等外部副作用。 */
#include "commands/ReplayJournal.h"
#include "commands/MacroRecorder.h"
#include <stdexcept>

namespace reverse
{
std::string canonicalHistoryCommand(const std::string &name)
{
    if (name == "cloud.undo") return "project.undo";
    if (name == "cloud.redo") return "project.redo";
    if (name == "cloud.reset") return "project.reset";
    return name;
}
ReplayPolicy replayPolicy(const std::string &name)
{
    if (name == "cloud.import") return ReplayPolicy::Import;
    if (name == "project.set_output_dir") return ReplayPolicy::OutputDirectory;
    for (const auto candidate : {"cloud.select", "cloud.select_indices", "cloud.crop", "cloud.delete",
             "model.reconstruct_local", "model.extend_to_plate", "model.trim_to_plate", "model.merge_to_plate", "model.split_to_plate", "model.crop_parts", "model.repair_holes", "model.add_holes",
             "model.reconstruct", "model.parametric", "model.delete", "model.fillet", "model.chamfer", "model.add_hole", "model.set_thickness", "model.expand_contour", "model.generate_cover_plate", "model.generate_base_plate", "model.mirror_complete", "model.move", "model.rotate", "model.deviation",
             "project.set_reference_plane", "project.clear_reference_plane", "project.section",
             "project.undo", "project.redo", "project.reset"})
        if (name == candidate) return ReplayPolicy::Include;
    for (const auto candidate : {"cloud.export", "model.export_step", "model.export_cwrm", "model.export_pcd", "file.convert", "project.export_and_exit", "project.snapshot"})
        if (name == candidate) return ReplayPolicy::Exclude;
    return ReplayPolicy::Unspecified;
}
void ReplayJournal::invalidate(std::string error) { m_error = std::move(error); }
const std::string &ReplayJournal::error() const { return m_error; }
void ReplayJournal::append(const std::string &name, std::string line)
{
    const auto policy = replayPolicy(name);
    if (policy == ReplayPolicy::Exclude) return;
    if (policy == ReplayPolicy::Unspecified)
    {
        invalidate("命令未声明会话补写策略：" + name);
        return;
    }
    if (policy == ReplayPolicy::OutputDirectory) { m_output = std::move(line); return; }
    if (policy == ReplayPolicy::Import)
    {
        // 新导入成功后才切换会话；重置保留全部旧过程，因为重置仍能撤销。
        std::vector<std::string> next;
        next.push_back(std::move(line));
        m_lines.swap(next);
        m_bytes = m_lines.front().size() + 1;
        m_error.clear();
        return;
    }
    if (!m_error.empty()) return;
    if (m_bytes + line.size() + 1 + MacroRecorder::header.size() > MacroRecorder::maxScriptBytes)
    {
        invalidate("当前会话命令超过 64 MiB，无法完整补写；请重新导入后从头录制");
        return;
    }
    m_lines.push_back(std::move(line));
    m_bytes += m_lines.back().size() + 1;
}
std::vector<std::string> ReplayJournal::prefix() const
{
    if (!m_error.empty()) throw std::runtime_error(m_error);
    const auto total = MacroRecorder::header.size() + m_bytes + (m_output ? m_output->size() + 1 : 0);
    if (total > MacroRecorder::maxScriptBytes) throw std::runtime_error("补写脚本超过 64 MiB，未开始录制");
    std::vector<std::string> result;
    result.reserve(m_lines.size() + (m_output ? 1 : 0));
    if (m_output) result.push_back(*m_output);
    result.insert(result.end(), m_lines.begin(), m_lines.end());
    return result;
}
}
