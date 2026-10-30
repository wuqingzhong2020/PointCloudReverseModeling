/*
 * 文件描述：安全比较鼠标命中对象；空白区域没有 owner，不能调用 OCCT 的 DetectedInteractive。
 */
#pragma once

#include <AIS_InteractiveObject.hxx>
#include <SelectMgr_EntityOwner.hxx>

namespace reverse::workbench
{
[[nodiscard]] inline bool isDetectedInteractive(const Handle(SelectMgr_EntityOwner) &owner,
                                              const Handle(AIS_InteractiveObject) &object)
{
    return !owner.IsNull() && !object.IsNull() && owner->Selectable() == object;
}
}
