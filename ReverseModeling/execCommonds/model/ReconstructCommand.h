/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class ReconstructCommand
{
public:
    struct Args
    {
        bool mirror{};
        double end_tolerance{};
        bool web_holes{};
        double min_hole_radius{};
    };
    static constexpr const char *name() { return "model.reconstruct"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
