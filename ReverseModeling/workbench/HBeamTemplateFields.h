/*
 * 文件描述：
 * 声明 H 型钢模板的识别参数预览组件，未接入项不提交给几何服务。
 */
#pragma once

class QWidget;

namespace reverse::workbench
{
QWidget *createHBeamTemplateFields(QWidget *parent);
}
