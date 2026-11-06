/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::file
{
class ConvertCommand
{
public:
    struct Args
    {
        std::string input{};
        std::string output{};
        double spacing{};
    };
    static constexpr const char *name() { return "file.convert"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
