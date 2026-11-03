/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::cloud
{
class SelectCommand
{
public:
    enum class ModeValue { Value0, Value1, Value2, Value3 };
    struct Args
    {
        rm::Arguments rectangle{};
        ModeValue mode{};
    };
    static constexpr const char *name() { return "cloud.select"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
