/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class RepairHolesCommand
{
public:
    struct Args
    {
        rm::Array parts{};
        bool boundary{};
        double max_hole_area{};
        double notch_width{};
        double notch_depth{};
        double simplify_tolerance{};
    };
    static constexpr const char *name() { return "model.repair_holes"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
