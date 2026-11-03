/* 文件描述：命令工作线程发布的录制状态，供界面只读查询。 */
#pragma once
#include <cstdint>
#include <string>
namespace reverse
{
struct RecordingState
{
    bool active{};
    std::string pathUtf8;
    std::uint64_t commandCount{};
    std::string error;
};
} // namespace reverse
