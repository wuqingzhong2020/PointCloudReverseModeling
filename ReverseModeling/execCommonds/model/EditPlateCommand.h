/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class EditPlateCommand
{
public:
    enum class OperationValue { Value0, Value1, Value2, Value3, Value4, Value5 };
    struct Args
    {
        OperationValue operation{};
        rm::Array parts{};
        std::string target{};
        rm::Array cuts{};
        bool all{};
    };
    static constexpr const char *name() { return "model.edit_plate"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
