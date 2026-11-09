/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class OptimizeEndCapCommand
{
public:
    enum class JointValue { Value0, Value1 };
    struct Args
    {
        std::string part{};
        std::int64_t edge{};
        JointValue joint{};
        bool auto_distance{};
        double distance{};
    };
    static constexpr const char *name() { return "model.optimize_end_cap"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
