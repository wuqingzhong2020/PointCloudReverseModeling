/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class MergeToPlateCommand
{
public:
    struct Args
    {
        rm::Array parts{};
        std::string target{};
        double normal_angle{};
        double inplane_angle{};
        double max_gap{};
    };
    static constexpr const char *name() { return "model.merge_to_plate"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
