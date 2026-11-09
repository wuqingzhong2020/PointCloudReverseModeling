/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class SetAngleSteelCommand
{
public:
    struct Args
    {
        std::string part{};
        std::int64_t face{};
        double thickness{};
        bool auto_height{};
        double height{};
    };
    static constexpr const char *name() { return "model.set_angle_steel"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
