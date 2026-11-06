/* 文件描述：固定十步工程历史，重做直接恢复结果，不再次运行几何算法。 */
#include "commands/ProjectHistory.h"
#include "commands/SectionPlane.h"
#include "geometry/Geometry.h"
#include <set>
#include <type_traits>

namespace reverse
{
namespace
{
void clearTransient(ProjectSnapshot &value)
{
    value.selection.reset();
    value.selectionRecipe.reset();
    value.deviation.reset();
    value.lastExport.reset();
    value.lastCloudExport.reset();
    value.exportedPathUtf8.clear();
    value.exportedCloudPathUtf8.clear();
    value.canUndo = value.canRedo = value.canReset = false;
    value.canUndoCloud = value.canRedoCloud = value.canResetCloud = false;
    value.undoLabel.clear();
    value.redoLabel.clear();
    value.historySteps = value.historyArrayBytes = 0;
}
bool samePlane(const std::shared_ptr<const ReferencePlane> &a, const std::shared_ptr<const ReferencePlane> &b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    for (std::size_t i = 0; i < a->pointsMm.size(); ++i)
    {
        const auto &p = a->pointsMm[i];
        const auto &q = b->pointsMm[i];
        if (p.x != q.x || p.y != q.y || p.z != q.z) return false;
    }
    return true;
}
}

ProjectContent ProjectContent::capture(const ProjectSnapshot &snapshot, std::shared_ptr<const Model> geometry)
{
    auto content = snapshot;
    clearTransient(content);
    content.section = {};
    content.revision = content.historyRestoreRevision = 0;
    return {std::move(content), std::move(geometry)};
}
bool ProjectContent::sameContent(const ProjectContent &other) const
{
    return snapshot.cloud == other.snapshot.cloud && geometry == other.geometry &&
           snapshot.sourcePathUtf8 == other.snapshot.sourcePathUtf8 &&
           snapshot.committedParameters == other.snapshot.committedParameters &&
           snapshot.committedRecognitionOptions == other.snapshot.committedRecognitionOptions &&
           samePlane(snapshot.referencePlane, other.snapshot.referencePlane);
}
ProjectSnapshot ProjectContent::restore(const ProjectSnapshot &current, bool reset) const
{
    auto result = snapshot;
    result.revision = current.revision + 1;
    result.historyRestoreRevision = result.revision;
    result.section = !reset && selectedSectionPlane(result, current.section.source) ? current.section : PlaneSection{};
    return result;
}
void ProjectHistory::establishBaseline(ProjectContent content)
{
    static_assert(std::is_nothrow_move_assignable_v<ProjectContent>);
    m_baseline = std::move(content);
    m_entries.clear();
    m_cursor = 0;
}
void ProjectHistory::push(ProjectContent before, ProjectContent after, std::string label)
{
    // 在副本中完成分配，失败时工程和历史分支均不改变。
    std::vector<Entry> next(m_entries.begin(), m_entries.begin() + m_cursor);
    next.push_back({std::move(before), std::move(after), std::move(label)});
    if (next.size() > capacity) next.erase(next.begin());
    m_entries.swap(next);
    m_cursor = m_entries.size();
}
const ProjectContent &ProjectHistory::undoContent() const
{
    if (!canUndo()) throw GeometryError("no_history", "没有可撤销的工程编辑。");
    return m_entries[m_cursor - 1].before;
}
const ProjectContent &ProjectHistory::redoContent() const
{
    if (!canRedo()) throw GeometryError("no_history", "没有可重做的工程编辑。");
    return m_entries[m_cursor].after;
}
const ProjectContent &ProjectHistory::baseline() const
{
    if (!m_baseline) throw GeometryError("no_baseline", "尚无可恢复的导入状态。");
    return *m_baseline;
}
void ProjectHistory::undo() noexcept { --m_cursor; }
void ProjectHistory::redo() noexcept { ++m_cursor; }
bool ProjectHistory::canUndo() const noexcept { return m_cursor != 0; }
bool ProjectHistory::canRedo() const noexcept { return m_cursor < m_entries.size(); }
bool ProjectHistory::hasBaseline() const noexcept { return m_baseline.has_value(); }
std::string ProjectHistory::undoLabel() const { return canUndo() ? m_entries[m_cursor - 1].label : ""; }
std::string ProjectHistory::redoLabel() const { return canRedo() ? m_entries[m_cursor].label : ""; }
std::size_t ProjectHistory::size() const noexcept { return m_entries.size(); }
std::uint64_t ProjectHistory::retainedArrayBytes() const
{
    std::set<const void *> seen;
    std::uint64_t bytes{};
    const auto count = [&](const ProjectContent &content) {
        const auto &s = content.snapshot;
        if (s.cloud && seen.insert(s.cloud.get()).second)
            bytes += s.cloud->points.capacity() * sizeof(PointSample);
        if (s.mesh && seen.insert(s.mesh.get()).second)
        {
            const auto &mesh = *s.mesh;
            bytes += mesh.vertices.capacity() * sizeof(Vec3) + mesh.triangles.capacity() * sizeof(mesh.triangles[0]) +
                     mesh.keyPoints.capacity() * sizeof(ModelKeyPoint) + mesh.edges.capacity() * sizeof(ModelEdge) +
                     (mesh.trianglePartIndices.capacity() + mesh.triangleFaceIndices.capacity()) * sizeof(std::uint32_t);
            for (const auto &edge : mesh.edges) bytes += edge.pointsMm.capacity() * sizeof(Vec3);
        }
    };
    if (m_baseline) count(*m_baseline);
    for (const auto &entry : m_entries) { count(entry.before); count(entry.after); }
    return bytes; // 共享点数组/显示网格的估算；不声称覆盖 OCCT 内部内存。
}
}
