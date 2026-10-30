/*
 * 文件描述：
 * 定义仅属于 Qt 桌面层的 OCCT 视图，接收与几何内核无关的显示数据。
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <QWidget>

#include "workbench/ModelDisplayData.h"
#include "workbench/PointCloudDisplayData.h"

namespace reverse::workbench {

class OccViewport final : public QWidget {
public:
    explicit OccViewport(QWidget* parent = nullptr);
    ~OccViewport() override;

    void setPointCloud(std::vector<CloudDisplayPoint> points);
    void setCloudHeightColors(bool enabled);
    void setModel(std::vector<ModelDisplayPart> parts);
    void setCloudVisible(bool visible);
    void setModelVisible(bool visible);
    void setThicknessVisible(bool visible);
    void fitAll();
    void showBottomView(const Vec3 &normal, const Vec3 &longitudinal);
    std::function<void(const QString&)> onError;

protected:
    QPaintEngine* paintEngine() const override;
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct State;
    std::unique_ptr<State> state_;
    void initialize();
    void release() noexcept;
    void rebuildCloud();
    void rebuildMesh();
    void redraw();
    void createViewCube();
    void updateViewCubeScale();
    void advanceNavigationAnimation();
    void stopNavigationAnimation();
    void fail(const QString& message);
};

} // namespace reverse::workbench
