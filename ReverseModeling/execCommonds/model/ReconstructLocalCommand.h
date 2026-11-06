/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class ReconstructLocalCommand
{
public:
    enum class TypeValue { Value0, Value1, Value2 };
    enum class ContourValue { Value0, Value1, Value2, Value3, Value4, Value5 };
    struct Args
    {
        rm::Array parts{};
        TypeValue type{};
        ContourValue contour{};
        rm::Array up{};
        double tolerance{};
        double corner{};
    };
    static constexpr const char *name() { return "model.reconstruct_local"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
