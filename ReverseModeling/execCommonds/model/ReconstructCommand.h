/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class ReconstructCommand
{
public:
    enum class TypeValue { Value0, Value1, Value2 };
    struct Args
    {
        TypeValue type{};
        bool on_ground{};
        bool regular_upper_surface{};
        bool projection_onto_border{};
        bool perpendicular{};
        double max_slot_width{};
        double max_base_width{};
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
