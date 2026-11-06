/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::project
{
class SectionCommand
{
public:
    struct Args
    {
        bool enabled{};
        double offset{};
        bool reverse{};
    };
    static constexpr const char *name() { return "project.section"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
