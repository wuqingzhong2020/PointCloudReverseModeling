/* 文件描述：附加板参数示意图，用矢量绘制区分原板、新板以及 D/T 尺寸方向。 */
#pragma once

#include <QWidget>

#include "core/Types.h"

namespace reverse::workbench
{
class PlateGenerationDiagram final : public QWidget
{
  public:
    explicit PlateGenerationDiagram(QWidget *parent = nullptr);
    void setKind(PlateGenerationKind kind);
    void setBaseMode(BasePlateMode mode);
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    PlateGenerationKind m_kind{PlateGenerationKind::Cover};
    BasePlateMode m_baseMode{BasePlateMode::Horizontal};
};
} // namespace reverse::workbench
