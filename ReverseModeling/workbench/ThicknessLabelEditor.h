/*
 * 文件描述：板厚标签的非模态就地数值编辑器，仅回车提交，Esc 或失去弹出焦点取消。
 */
#pragma once

#include <functional>
#include <string>

#include <QDoubleSpinBox>

namespace reverse::workbench
{
class ThicknessLabelEditor final : public QDoubleSpinBox
{
public:
    explicit ThicknessLabelEditor(QWidget *parent);
    void open(const std::string &partId, double thicknessMm, const QPoint &globalCenter, bool unresolved = false);
    std::function<void(const std::string &, double)> onSubmitted;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void submit();
    std::string m_partId;
    double m_initialValue{};
    bool m_initialUnresolved{};
};
}
