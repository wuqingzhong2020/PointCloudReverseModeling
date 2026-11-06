/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class RotateCommand
{
public:
    enum class AxisValue { Value0, Value1, Value2 };
    struct Args
    {
        rm::Array parts{};
        AxisValue axis{};
        double angle{};
        rm::Array center{};
    };
    static constexpr const char *name() { return "model.rotate"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
