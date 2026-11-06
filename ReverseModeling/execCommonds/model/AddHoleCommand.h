/* 文件描述：由 rm_define 生成的参数与描述接口；不要在此添加业务实现。 */
#pragma once
#include "commandManager/CommandManager.h"
namespace rmcmd::model
{
class AddHoleCommand
{
public:
    enum class KindValue { Value0, Value1 };
    enum class ShapeValue { Value0, Value1, Value2 };
    enum class ReferenceValue { Value0, Value1, Value2 };
    struct Args
    {
        std::string part{};
        std::int64_t edge{};
        KindValue kind{};
        ShapeValue shape{};
        double size{};
        double width{};
        double height{};
        ReferenceValue reference{};
        double offset{};
        bool first{};
        bool second{};
    };
    static constexpr const char *name() { return "model.add_hole"; }
    static rm::CommandDefinition definition();
    static Args arguments(const rm::Arguments &values);
    static void registerDefinition(rm::CommandManager &manager) { manager.registerDefinition(definition()); }
};
}
