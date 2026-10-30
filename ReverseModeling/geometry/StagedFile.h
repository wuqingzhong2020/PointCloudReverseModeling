/*
 * 文件描述：
 * 同目录暂存导出文件，检查完成后原子发布且不覆盖已有目标；异常时清理自建临时文件。
 */
#pragma once
#include <filesystem>
#include <random>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "geometry/Geometry.h"
namespace reverse::detail
{
struct StagedFile
{
    std::filesystem::path directory;
    std::filesystem::path file;
    explicit StagedFile(const std::filesystem::path &parent)
    {
        std::random_device random;
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            std::ostringstream name;
            name << ".reverse-export-" << std::hex << random() << random();
            directory = parent / name.str();
            std::error_code error;
            if (std::filesystem::create_directory(directory, error))
            {
                file = directory / "verified.data";
                return;
            }
            if (error && error != std::errc::file_exists)
                throw GeometryError("output_not_writable", "无法在输出目录中建立临时导出文件");
        }
        throw GeometryError("output_not_writable", "无法分配独立的临时导出目录");
    }
    ~StagedFile()
    {
        std::error_code ignored;
        if (!file.empty())
            std::filesystem::remove(file, ignored);
        if (!directory.empty())
            std::filesystem::remove(directory, ignored);
    }
};

inline void commitWithoutOverwrite(const std::filesystem::path &temporary, const std::filesystem::path &target)
{
#ifdef _WIN32
    // 不设置 MOVEFILE_REPLACE_EXISTING；同盘移动在目标已存在时失败。
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        throw GeometryError("output_commit_failed", "导出文件提交失败：目标已存在或输出目录不可写");
#else
    // 同文件系统硬链接原子创建目标名，已有目标不会被替换。
    std::error_code error;
    std::filesystem::create_hard_link(temporary, target, error);
    if (error)
        throw GeometryError("output_commit_failed", "导出文件提交失败：目标已存在或文件系统不支持原子提交");
#endif
}

} // namespace reverse::detail
