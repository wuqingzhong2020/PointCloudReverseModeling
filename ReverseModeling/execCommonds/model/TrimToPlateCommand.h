/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class TrimToPlateCommand
{
public:
    struct Args
    {
        rm::Array parts{};
        std::string target{};
        rm::Array contacts{};
        rm::Array cuts{};
        double clearance{};
    };
    static constexpr const char *name() { return "model.trim_to_plate"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
