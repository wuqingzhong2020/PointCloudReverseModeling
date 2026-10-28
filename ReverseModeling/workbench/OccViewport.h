/*
 * 文件描述：
 * 定义仅属于 Qt 桌面层的 OCCT 视图，接收与几何内核无关的显示数据。
 */
#pragma once

#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace reverse::workbench {

class OccViewport final : public QWidget {
public:
    using Vertex = std::array<double, 3>;
    using Triangle = std::array<std::uint32_t, 3>;

    explicit OccViewport(QWidget* parent = nullptr);
    ~OccViewport() override;

    void setPointCloud(const std::vector<Vertex>& points);
    void setMesh(const std::vector<Vertex>& vertices, const std::vector<Triangle>& triangles);
    void setCloudVisible(bool visible);
    void setModelVisible(bool visible);
    void fitAll();
    std::function<void(const QString&)> onError;

protected:
    QPaintEngine* paintEngine() const override;
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct State;
    std::unique_ptr<State> state_;
    void initialize();
    void release() noexcept;
    void rebuildCloud();
    void rebuildMesh();
    void fail(const QString& message);
};

} // namespace reverse::workbench
