/*
 * 文件描述：
 * 创建 Qt 桌面应用及逆向建模工作台；业务初始化由命令服务负责。
 */
#include "reverse/workbench/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ReverseModeler"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("ReverseModeling"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    reverse::workbench::MainWindow window;
    window.show();
    return application.exec();
}
