/* 文件描述：产品工程编辑历史；共享不可变几何，运行状态不进入历史。 */
#pragma once

#include "commands/CommandService.h"
#include <optional>
#include <vector>

namespace reverse
{
class Model;
struct ProjectContent
{
    ProjectSnapshot snapshot;
    std::shared_ptr<const Model> geometry;

    static ProjectContent capture(const ProjectSnapshot &snapshot, std::shared_ptr<const Model> geometry);
    [[nodiscard]] bool sameContent(const ProjectContent &other) const;
    [[nodiscard]] ProjectSnapshot restore(const ProjectSnapshot &current, bool reset) const;
};

// 由 ProjectOperations 在状态锁内调用；先完成可能失败的分配，再移动游标/提交工程。
class ProjectHistory
{
  public:
    static constexpr std::size_t capacity = 10;
    void establishBaseline(ProjectContent content);
    void push(ProjectContent before, ProjectContent after, std::string label);
    [[nodiscard]] const ProjectContent &undoContent() const;
    [[nodiscard]] const ProjectContent &redoContent() const;
    [[nodiscard]] const ProjectContent &baseline() const;
    void undo() noexcept;
    void redo() noexcept;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool hasBaseline() const noexcept;
    [[nodiscard]] std::string undoLabel() const;
    [[nodiscard]] std::string redoLabel() const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t retainedArrayBytes() const;

  private:
    struct Entry
    {
        ProjectContent before;
        ProjectContent after;
        std::string label;
    };
    std::vector<Entry> m_entries;
    std::size_t m_cursor{};
    std::optional<ProjectContent> m_baseline;
};
}
