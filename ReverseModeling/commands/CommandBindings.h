/* 文件描述：手写产品业务绑定与中立值/JSON 适配，不进入生成文件。 */
#pragma once
#include "commandManager/CommandManager.h"
#include <filesystem>
#include <nlohmann/json.hpp>
namespace reverse
{
class ProjectOperations;
std::filesystem::path commandPath(std::string_view utf8);
rm::Value commandValue(const nlohmann::json &value);
nlohmann::json commandJson(const rm::Value &value);
void registerApplicationCommandHandlers(rm::CommandManager &manager, ProjectOperations &operations);
} // namespace reverse
