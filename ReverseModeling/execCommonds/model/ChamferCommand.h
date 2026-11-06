/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class ChamferCommand
{
public:
    struct Args
    {
        std::optional<std::string> part{};
        std::optional<rm::Array> edges{};
        std::optional<rm::Array> targets{};
        double size{};
    };
    static constexpr const char *name() { return "model.chamfer"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
