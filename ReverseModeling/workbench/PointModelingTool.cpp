/* 文件描述：选点建模的交互状态机，预览与最终提交均通过命令服务。 */
#include "workbench/PointModelingTool.h"
#include "workbench/DockWorkspace.h"
#include "commands/SectionPlane.h"
#include "geometry/ReferencePlane.h"
#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QToolButton>
#include <QToolTip>

namespace reverse::workbench
{
PointModelingTool::PointModelingTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    setObjectName("pointModelingTool");
    auto *layout = new QVBoxLayout(this);
    m_description = new QLabel(QStringLiteral("选择障碍物点云，生成贴合参考面的盒状障碍物。"), this);
    m_description->setWordWrap(true);
    layout->addWidget(m_description);
    const auto help = [this](const QString &text) {
        auto *button = new QToolButton(this);
        button->setText(QStringLiteral("?"));
        button->setToolTip(text);
        button->setAccessibleName(QStringLiteral("使用说明"));
        connect(button, &QToolButton::clicked, button, [button, text] {
            QToolTip::showText(button->mapToGlobal(QPoint(0, button->height())), text, button);
        });
        return button;
    };
    auto *referenceTitle = new QHBoxLayout;
    m_stage = new QLabel(this); referenceTitle->addWidget(m_stage); referenceTitle->addStretch();
    m_referenceHelp = help(QStringLiteral("<b>参考面是什么？</b><br>在三维模型上选择一个平面，例如障碍物所在的支承面。"
        "点云点、棱线和曲面不能作为参考面。<br><br>1. 开启点云剖视后，只显示偏移平面一侧的点云。"
        "<br>2. 生成的障碍物从原参考面延伸到所选点云，贴着参考面。"
        "<br>3. 剖视偏移只用于过滤点云，不移动障碍物的贴合面。"));
    referenceTitle->addWidget(m_referenceHelp); layout->addLayout(referenceTitle);
    m_form = new QFormLayout; layout->addLayout(m_form);
    const auto combo = [&](const char *label, const char *name) {
        auto *c = new QComboBox(this); c->setObjectName(name); m_form->addRow(QString::fromUtf8(label), c); return c;
    };
    m_operation = combo("操作", "pointModel.operation");
    m_operation->addItem(QStringLiteral("添加"), false); m_operation->addItem(QStringLiteral("切除"), true);
    m_direction = combo("拉伸方向", "pointModel.direction");
    m_direction->addItem(QStringLiteral("垂直于底板"), "bottom");
    m_direction->addItem(QStringLiteral("沿世界 Z 轴"), "world");
    m_direction->addItem(QStringLiteral("垂直于基准面"), "reference");
    m_direction->addItem(QStringLiteral("垂直于指定面"), "face");
    m_referenceRow = new QWidget(this);
    auto *referenceRow = new QHBoxLayout(m_referenceRow); referenceRow->setContentsMargins(0,0,0,0);
    m_face = new QPushButton(QStringLiteral("请选择一个参考面"),this);
    m_face->setObjectName("pointModel.referenceFace"); m_face->setCheckable(true);
    m_face->setToolTip(QStringLiteral("点击这里进入选面状态，再左键单击三维模型上的平面。"));
    m_clearReference = new QPushButton(QStringLiteral("×"),this);
    m_clearReference->setObjectName("pointModel.clearReference"); m_clearReference->setFixedWidth(26);
    m_clearReference->setToolTip(QStringLiteral("清除参考面和障碍物预览，重新选面"));
    referenceRow->addWidget(m_face,1); referenceRow->addWidget(m_clearReference);
    m_form->addRow(m_referenceRow);
    m_shape = combo("形状", "pointModel.shape");
    m_shape->addItem(QStringLiteral("矩形"),"rectangle"); m_shape->addItem(QStringLiteral("圆"),"circle");
    m_shape->addItem(QStringLiteral("多边形"),"polygon"); m_shape->addItem(QStringLiteral("多段矩形"),"polyline");
    m_method = combo("选点方式", "pointModel.method");
    const auto number = [&](const char *label, const char *name, double value) {
        auto *s = new QDoubleSpinBox(this); s->setObjectName(name); s->setRange(0.01,1000000);
        s->setDecimals(2); s->setValue(value); s->setSuffix(" mm"); m_form->addRow(QString::fromUtf8(label),s);
        connect(s,&QDoubleSpinBox::valueChanged,this,[this] { changed(); }); return s;
    };
    m_length=number("长度","pointModel.length",100); m_width=number("宽度 / 厚度","pointModel.width",20);
    m_diameter=number("直径","pointModel.diameter",50); m_height=number("高度","pointModel.height",100);
    m_toPlane = new QCheckBox(QStringLiteral("自动拉伸至参考面"),this); m_form->addRow(m_toPlane);
    m_section = new QCheckBox(QStringLiteral("点云剖视"),this); m_section->setObjectName("pointModel.section");
    m_section->setChecked(true); m_form->addRow(m_section);
    m_offset=number("剖视面偏移距离","pointModel.offset",10); m_offset->setRange(-1000000,1000000);
    m_offset->setToolTip(QStringLiteral("沿参考面法线偏移，仅过滤点云。可设为 0；偏移过大可能隐藏障碍物点云。"));
    m_reverse = new QCheckBox(QStringLiteral("剖视面反向"),this); m_reverse->setObjectName("pointModel.reverse");
    m_reverse->setToolTip(QStringLiteral("切换保留哪一侧点云；若障碍物点云消失，可切换反向或关闭点云剖视。"));
    m_form->addRow(m_reverse);
    m_scope=combo("切除范围","pointModel.scope");
    m_scope->addItem(QStringLiteral("不包括底板"),"no_base"); m_scope->addItem(QStringLiteral("全部"),"all");
    m_scope->addItem(QStringLiteral("手动选择"),"manual");
    m_obstacleGroup = new QWidget(this);
    auto *obstacleLayout = new QVBoxLayout(m_obstacleGroup); obstacleLayout->setContentsMargins(0,12,0,0);
    auto *obstacleTitle = new QHBoxLayout;
    obstacleTitle->addWidget(new QLabel(QStringLiteral("3  生成障碍物"),this)); obstacleTitle->addStretch();
    obstacleTitle->addWidget(help(QStringLiteral("先选择参考面，再点击“请框选障碍物点云”。"
        "<br>在三维视图中按住左键拖出矩形，框住所需的点云后松开。"
        "<br>工具栏可加选 / 减选；选中点云会变色，自动生成绿色预览。"
        "<br>灰色外形是模型，不是点云；可点击视图工具栏“实体”隐藏模型，查看彩色点云。"
        "<br>框选点数是完整点云数量，剖视后可用点数才参与生成。确认包围范围后点击应用。")));
    obstacleLayout->addLayout(obstacleTitle);
    m_selectCloud = new QPushButton(QStringLiteral("请框选障碍物点云"),this);
    m_selectCloud->setObjectName("pointModel.selectCloud"); m_selectCloud->setCheckable(true);
    obstacleLayout->addWidget(m_selectCloud);
    m_selectionInfo = new QLabel(this);
    m_selectionInfo->setObjectName("pointModel.selectionInfo"); m_selectionInfo->setWordWrap(true);
    obstacleLayout->addWidget(m_selectionInfo);
    m_previewAllPoints = new QPushButton(QStringLiteral("关闭剖视并预览"), this);
    m_previewAllPoints->setObjectName("pointModel.previewAllPoints");
    m_previewAllPoints->setToolTip(QStringLiteral("保留当前框选，使用其中的全部点云生成预览；不会修改参考面或原模型。"));
    obstacleLayout->addWidget(m_previewAllPoints); layout->addWidget(m_obstacleGroup);
    m_instructions = new QLabel(this); m_instructions->setWordWrap(true); layout->addWidget(m_instructions);
    m_hint = new QLabel(this); m_hint->setObjectName("pointModel.status"); m_hint->setWordWrap(true); layout->addWidget(m_hint);
    m_redraw = new QPushButton(QStringLiteral("重绘上一点"),this); layout->addWidget(m_redraw);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"),this); layout->addWidget(m_original);
    layout->addStretch();
    auto *steps = new QHBoxLayout; layout->addLayout(steps);
    m_back = new QPushButton(QStringLiteral("上一步"),this); m_next = new QPushButton(QStringLiteral("下一步"),this);
    steps->addWidget(m_back); steps->addWidget(m_next);
    auto *buttons = new QHBoxLayout; layout->addLayout(buttons);
    m_previewButton = new QPushButton(QStringLiteral("预览"),this); m_apply=new QPushButton(QStringLiteral("应用"),this);
    m_apply->setObjectName("pointModel.apply");
    auto *exit = new QPushButton(QStringLiteral("退出"),this);
    buttons->addWidget(m_previewButton); buttons->addWidget(m_apply); buttons->addWidget(exit);
    m_debounce = new QTimer(this); m_debounce->setSingleShot(true); m_debounce->setInterval(250);
    connect(m_debounce,&QTimer::timeout,this,[this] { if (m_service.isIdle() && m_step>0) preview(); });
    connect(m_shape,&QComboBox::currentIndexChanged,this,[this] { restart(); });
    connect(m_direction,&QComboBox::currentIndexChanged,this,[this] { if (!m_loading) restart(); });
    connect(m_method,&QComboBox::currentIndexChanged,this,[this] {
        if (m_loading) return; invalidate(); m_points.clear(); m_viewport.setReferencePreview({}); updateUi();
    });
    connect(m_operation,&QComboBox::currentIndexChanged,this,[this] { changed(); });
    connect(m_scope,&QComboBox::currentIndexChanged,this,[this] { changed(); });
    for (auto *c : {m_toPlane,m_section,m_reverse}) connect(c,&QCheckBox::toggled,this,[this] { changed(); });
    connect(m_face,&QPushButton::clicked,this,[this] { selectReference(false); });
    connect(m_clearReference,&QPushButton::clicked,this,[this] { selectReference(true); });
    connect(m_selectCloud,&QPushButton::clicked,this,[this] { selectObstaclePoints(); });
    connect(m_previewAllPoints, &QPushButton::clicked, this, [this] { m_section->setChecked(false); });
    connect(m_redraw,&QPushButton::clicked,this,[this] {
        invalidate(); if (!m_points.empty()) m_points.pop_back(); m_step=0; updateUi();
    });
    connect(m_back,&QPushButton::clicked,this,[this] { invalidate(); --m_step; updateUi(); });
    connect(m_next,&QPushButton::clicked,this,[this] { next(); });
    connect(m_previewButton,&QPushButton::clicked,this,[this] { preview(); });
    connect(m_apply,&QPushButton::clicked,this,[this] { apply(); });
    connect(exit,&QPushButton::clicked,this,[this] { cancel(); });
    connect(m_original,&QCheckBox::toggled,this,[this](bool checked) { if (!m_loading && m_preview) showModel(checked); });
    m_viewport.onModelingPicked=[this](const auto &points,const auto &error) { picked(points,error); };
}
Vec3 PointModelingTool::displayOrigin() const
{
    const auto p=m_service.project(); return p.cloud ? p.cloud->originMm : p.model ? p.model->frame.originMm : Vec3{};
}
void PointModelingTool::begin(bool obstacle)
{
    cancel(); m_active=true; m_obstacle=obstacle;
    const auto p=m_service.project(); m_revision=p.revision; m_cloudId=p.cloudId; m_modelId=p.modelId;
    m_selection=p.selection;
    if (auto *items=qobject_cast<QStandardItemModel *>(m_direction->model()))
    {
        items->item(0)->setEnabled(bottomSectionPlane(p).has_value());
        items->item(2)->setEnabled(bool(p.referencePlane));
        items->item(3)->setEnabled(bool(p.model));
    }
    m_loading=true; m_operation->setCurrentIndex(0);
    m_obstacleSelection.reset(); m_retainedPoints=0; m_referenceSelection.clear(); m_hint->clear();
    if (m_obstacle) { m_section->setChecked(true); m_offset->setValue(10); m_reverse->setChecked(false); }
    m_direction->setCurrentIndex(bottomSectionPlane(p) ? 0 : 1); m_loading=false;
    restart();
    if (onChanged) onChanged();
}
void PointModelingTool::restart()
{
    if (!m_active || m_loading) return;
    invalidate(); m_clipDirty=true; m_step=0; m_points.clear(); m_plane.reset(); m_loading=true;
    m_method->clear();
    if (m_shape->currentData()=="rectangle")
    {
        m_method->addItem(QStringLiteral("中轴线（两端点）"),"axis");
        m_method->addItem(QStringLiteral("对角线（两个角点）"),"diagonal");
        m_method->addItem(QStringLiteral("三点边角（三个角点）"),"corner");
    }
    else if (m_shape->currentData()=="circle")
    {
        m_method->addItem(QStringLiteral("三点圆弧"),"three");
        m_method->addItem(QStringLiteral("圆心＋圆周一点"),"center");
    }
    const auto project=m_service.project();
    const auto direction=m_direction->currentData().toString();
    // 障碍物必须由用户明确选择模型平面，不能继承选点建模的底板 / 基准面。
    if (!m_obstacle && direction=="bottom") m_plane=bottomSectionPlane(project);
    if (!m_obstacle && direction=="reference" && project.referencePlane) m_plane=*project.referencePlane;
    m_facePicking=m_obstacle || direction=="face";
    m_toPlane->setChecked(direction!="world"); m_loading=false;
    updateUi();
    m_hint->setText(m_obstacle ? QString() : m_facePicking ? QStringLiteral("第 1 步：点击模型上的平面作为参考面。") :
        direction!="world" && !m_plane ? QStringLiteral("当前没有此参考面，请选择世界 Z 轴或指定面。") :
        QStringLiteral("依次左键选择点云点或模型特征点。右键旋转、中键平移、滚轮缩放；Esc 退出。"));
}
void PointModelingTool::selectReference(bool clear)
{
    if (!m_active || !m_service.isIdle()) return;
    invalidate(); m_hint->clear(); m_step=0; m_facePicking=true; m_clipDirty=true;
    if (clear || !m_obstacle) { m_plane.reset(); m_referenceSelection.clear(); }
    m_obstacleSelection.reset(); m_retainedPoints=0;
    m_viewport.setCloudSelection({});
    updateUi();
    if (onChanged) onChanged();
}
void PointModelingTool::selectObstaclePoints()
{
    if (!m_active || !m_obstacle || !m_plane || !m_service.isIdle()) return;
    m_step=1; m_facePicking=false; m_hint->clear();
    m_selection=m_service.project().selection;
    updateUi();
    if (onChanged) onChanged();
}
void PointModelingTool::picked(const std::vector<Vec3> &points,const QString &error)
{
    if (!m_active || !m_service.isIdle() || m_step) return;
    if (!error.isEmpty()) { m_hint->setText(error); return; }
    const auto o=displayOrigin(); std::vector<Vec3> world;
    for (auto p : points) world.push_back({p.x+o.x,p.y+o.y,p.z+o.z});
    if (m_facePicking)
    {
        if (world.size()!=3) return;
        invalidate(); m_clipDirty=true; m_plane=makeReferencePlane({world[0],world[1],world[2]});
        m_referenceSelection=m_viewport.selectedObjects(); m_obstacleSelection.reset(); m_retainedPoints=0;
        m_facePicking=m_obstacle;
        if (!m_obstacle) m_viewport.setPointModelingPicking(false);
        m_points.clear();
        m_hint->setText(m_obstacle ? QStringLiteral("参考平面已选择。点击第 3 步“请框选障碍物点云”开始框选。") :
            QStringLiteral("参考面已选定，请依次选择轮廓点。"));
        if (m_obstacle) m_viewport.setCloudSelection({});
        updateUi(); return;
    }
    const auto shape=m_shape->currentData().toString(), method=m_method->currentData().toString();
    const std::size_t limit=(shape=="rectangle" || shape=="circle") ? (method=="corner" || method=="three" ? 3 : 2) : 0;
    if (limit && world.size()>limit) world={world.back()};
    m_points=std::move(world); invalidate(); updateUi();
    m_hint->setText(QStringLiteral("已选择 %1 个点；选足后点击下一步调整尺寸。").arg(m_points.size()));
}
PointModeling PointModelingTool::request(bool inferred) const
{
    PointModeling r; r.shape=m_obstacle ? "box" : m_shape->currentData().toString().toStdString();
    r.method=m_method->currentData().toString().toStdString(); if (r.method.empty()) r.method="axis";
    r.points=m_points; r.width=m_width->value(); r.length=inferred ? 0 : m_length->value();
    if (inferred && r.shape=="rectangle" && r.method!="axis") r.width=0;
    r.diameter=inferred ? 0 : m_diameter->value(); r.height=m_height->value();
    r.toPlane=m_toPlane->isChecked();
    if (m_plane)
    {
        r.origin=m_plane->pointsMm[0]; r.normal=m_plane->normal; r.xAxis=m_plane->xAxis;
        if (!m_obstacle && m_direction->currentData()=="bottom" && r.toPlane)
        {
            r.origin.x-=10*r.normal.x; r.origin.y-=10*r.normal.y; r.origin.z-=10*r.normal.z;
        }
    }
    else if (m_obstacle || m_direction->currentData()!="world")
        throw GeometryError("no_reference_plane","请先选择有效的参考平面。");
    r.cut=!m_obstacle && m_operation->currentData().toBool();
    r.cutScope=m_scope->currentData().toString().toStdString(); r.targets=m_viewport.selectedParts();
    if (m_obstacle)
    {
        if (!m_obstacleSelection || m_obstacleSelection->size()<3)
            throw GeometryError("no_selected_points","框内点云不足，请扩大矩形框住一簇点云，无需逐个点击点。");
        r.indices=*m_obstacleSelection;
        r.section=m_section->isChecked(); r.offset=m_offset->value(); r.reversed=m_reverse->isChecked();
    }
    return r;
}
void PointModelingTool::next()
{
    try
    {
        if (!m_obstacle && m_step==0)
        {
            const auto profile=pointModelProfile(request(true));
            m_loading=true;
            if (profile.length>0) m_length->setValue(profile.length);
            if (profile.width>0) m_width->setValue(profile.width);
            if (profile.diameter>0) m_diameter->setValue(profile.diameter);
            m_height->setValue(profile.height); m_loading=false;
        }
        invalidate(); m_clipDirty=true; ++m_step; updateUi();
        if (!m_obstacle && !(m_step==2 && m_scope->currentData()=="manual")) preview();
    }
    catch (const std::exception &e) { m_loading=false; m_hint->setText(QString::fromUtf8(e.what())); }
}
void PointModelingTool::changed()
{
    if (m_loading || !m_active) return;
    invalidate(); m_hint->clear(); m_clipDirty=true; updateObstacleSelection(); updateUi();
    if (m_step && (!m_obstacle || m_retainedPoints>=3) &&
        !(m_operation->currentData().toBool() && m_scope->currentData()=="manual")) m_debounce->start();
}
void PointModelingTool::updateObstacleSelection()
{
    m_retainedPoints = 0;
    if (!m_obstacle || !m_plane || !m_obstacleSelection)
    {
        return;
    }
    const auto project = m_service.project();
    if (!project.cloud)
    {
        return;
    }
    const auto local = localReferencePlane(*m_plane, project.cloud->originMm);
    const auto equation = sectionEquation(local, {m_section->isChecked(), m_offset->value(), m_reverse->isChecked()});
    for (const auto index : *m_obstacleSelection)
    {
        if (index >= project.cloud->points.size())
        {
            continue;
        }
        const auto &p = project.cloud->points[index].position;
        if (!m_section->isChecked() || equation[0]*p.x + equation[1]*p.y + equation[2]*p.z + equation[3] >= 0)
        {
            ++m_retainedPoints;
        }
    }
    const auto count = m_obstacleSelection->size();
    if (count == 0)
    {
        m_hint->setText(QStringLiteral("框内没有原始点云。灰色外形是模型；可隐藏“实体”查看彩色点云后重新框选。"));
    }
    else if (count < 3)
    {
        m_hint->setText(QStringLiteral("框内只有 %1 个点云点，不能形成障碍物。请扩大框选范围，无需逐个点击点。")
            .arg(count));
    }
    else if (m_retainedPoints < 3)
    {
        m_hint->setText(QStringLiteral("已框选 %1 个点，但当前剖视过滤后只剩 %2 个，无法生成障碍物。"
            "可点击“关闭剖视并预览”，或调整偏移 / 反向；无需重新选点。")
            .arg(count).arg(m_retainedPoints));
    }
}
void PointModelingTool::updateUi()
{
    const bool ready=m_active && m_service.isIdle() && m_task.empty() && m_applyTask.empty();
    const bool first=m_step==0, dimensions=m_step==1 && !m_obstacle;
    const auto shape=m_shape->currentData().toString();
    m_description->setVisible(m_obstacle); m_referenceHelp->setVisible(m_obstacle);
    m_obstacleGroup->setVisible(m_obstacle); m_instructions->setVisible(m_obstacle);
    m_stage->setText(!m_active ? QStringLiteral("选点建模已退出") : m_obstacle ?
        QStringLiteral("1  参考面") :
        first ? QStringLiteral("1  选点建模") : m_step==1 ? QStringLiteral("2  尺寸调整") : QStringLiteral("3  切除范围"));
    for (auto *c : {m_shape,m_method,m_direction,m_operation,m_scope}) c->setEnabled(ready);
    m_form->setRowVisible(m_operation,!m_obstacle); m_form->setRowVisible(m_direction,!m_obstacle && first);
    m_form->setRowVisible(m_shape,!m_obstacle && first);
    m_form->setRowVisible(m_method,!m_obstacle && first && (shape=="rectangle" || shape=="circle"));
    m_form->setRowVisible(m_referenceRow,m_obstacle || (first && m_direction->currentData()=="face"));
    m_face->setText(m_plane ? QStringLiteral("已选择参考平面（点击重选）") : QStringLiteral("请选择一个参考面"));
    m_face->setChecked(m_active && m_facePicking);
    m_clearReference->setVisible(m_plane.has_value()); m_clearReference->setEnabled(ready);
    const auto count=m_obstacleSelection ? m_obstacleSelection->size() : 0;
    m_selectionInfo->setText(QStringLiteral("框选：%1 点；%2：%3 点。")
        .arg(count).arg(m_section->isChecked() ? QStringLiteral("剖视后可用") : QStringLiteral("可用"))
        .arg(m_retainedPoints));
    m_selectionInfo->setVisible(m_active && m_obstacle && bool(m_obstacleSelection));
    m_previewAllPoints->setVisible(m_active && m_obstacle && m_section->isChecked() && count>=3 && m_retainedPoints<3);
    m_previewAllPoints->setEnabled(ready);
    m_selectCloud->setText(count ? QStringLiteral("已框选 %1 点（点击继续框选）").arg(count) : QStringLiteral("请框选障碍物点云"));
    m_selectCloud->setEnabled(ready && m_plane.has_value());
    m_selectCloud->setChecked(selectingObstaclePoints());
    if (m_obstacle)
        m_instructions->setText(!m_plane ? QStringLiteral("当前：选择参考面。左键点击三维模型上的平面；仅点云或曲面不能选为参考面。")
            : m_facePicking ? QStringLiteral("参考面已选定，可设置剖视。点击第 3 步切换到点云框选。")
            : QStringLiteral("当前：点云框选。左键拖框圈住一簇彩色点云，无需逐点点击。"
                "灰色外形是模型，可隐藏“实体”查看点云；点云被剖视隐藏时可反向或关闭剖视。"));
    m_section->setText(QStringLiteral("2  点云剖视"));
    m_form->setRowVisible(m_length,dimensions && shape=="rectangle");
    m_form->setRowVisible(m_width,dimensions && (shape=="rectangle" || shape=="polyline"));
    m_form->setRowVisible(m_diameter,dimensions && shape=="circle");
    m_form->setRowVisible(m_height,dimensions); m_height->setEnabled(ready && !m_toPlane->isChecked());
    m_form->setRowVisible(m_toPlane,dimensions && m_plane.has_value());
    for (QWidget *w : std::array<QWidget *,3>{m_section,m_offset,m_reverse}) m_form->setRowVisible(w,m_obstacle);
    m_form->setRowVisible(m_scope,!m_obstacle && m_operation->currentData().toBool() && m_step>0);
    m_redraw->setVisible(!m_obstacle && first); m_redraw->setEnabled(ready && !m_points.empty());
    m_back->setVisible(!m_obstacle); m_back->setEnabled(ready && m_step>0);
    const auto method=m_method->currentData().toString();
    const auto required=(shape=="polygon" || shape=="polyline" || method=="three" || method=="corner") ? 3u : 2u;
    m_next->setVisible(!m_obstacle && (first || (m_step==1 && m_operation->currentData().toBool())));
    m_next->setEnabled(ready && !m_facePicking && (m_obstacle ? m_plane.has_value() : m_points.size()>=required));
    m_face->setEnabled(ready); m_previewButton->setEnabled(ready && m_step>0 && (!m_obstacle || m_retainedPoints>=3));
    m_apply->setEnabled(ready && bool(m_preview)); m_original->setEnabled(ready && bool(m_preview));
    m_original->setVisible(!m_obstacle || bool(m_preview));
    for (auto *w : {m_width,m_length,m_diameter,m_offset}) w->setEnabled(ready);
    for (auto *w : {m_toPlane,m_section,m_reverse}) w->setEnabled(ready);
    if (m_obstacle)
    {
        m_section->setEnabled(ready && m_plane.has_value());
        m_offset->setEnabled(ready && m_plane.has_value() && m_section->isChecked());
        m_reverse->setEnabled(ready && m_plane.has_value() && m_section->isChecked());
    }
    if (!m_active) return;
    const bool pick=ready && first && (!m_obstacle || m_facePicking);
    m_viewport.setPointModelingPicking(pick,m_facePicking);
    if (m_obstacle && m_plane && m_viewport.selectedObjects()!=m_referenceSelection)
        m_viewport.setSelectedObjects(m_referenceSelection);
    if (pick && !m_facePicking)
    {
        const auto o=displayOrigin(); std::vector<Vec3> local;
        for (auto p:m_points) local.push_back({p.x-o.x,p.y-o.y,p.z-o.z});
        m_viewport.setReferencePreview(std::move(local));
    }
    m_viewport.setCloudEditingEnabled(ready && m_obstacle && !first);
    m_viewport.setModelPickingEnabled(ready && !m_obstacle && m_step>0 && m_operation->currentData().toBool() && m_scope->currentData()=="manual");
    if (m_step>0 && !m_obstacle) m_viewport.setModelPickMode(ModelPickMode::Body);
    if (m_obstacle && m_clipDirty)
    {
        const auto p=m_service.project();
        m_viewport.setReferencePlane(p.referencePlane,{m_plane.has_value() && m_section->isChecked(),m_offset->value(),m_reverse->isChecked()},
            displayOrigin(),m_plane);
        m_clipDirty=false; m_clipApplied=true;
    }
}
void PointModelingTool::showModel(bool original)
{
    const auto p=m_service.project(); const auto selected=m_viewport.selectedObjects(); m_loading=true;
    if (!original && m_preview)
    {
        auto parts=makeModelDisplayParts(m_preview->mesh(),m_preview->summary(),displayOrigin());
        for (auto &part:parts) if (!p.model || std::find(p.model->partIds.begin(),p.model->partIds.end(),part.id)==p.model->partIds.end())
            part.colorSrgb={0.12,0.85,0.45};
        m_viewport.setModel(std::move(parts));
    }
    else m_viewport.setModel(p.model && p.mesh ? makeModelDisplayParts(*p.mesh,*p.model,displayOrigin()) : std::vector<ModelDisplayPart>{});
    m_viewport.setSelectedObjects(selected);
    m_loading=false;
}
void PointModelingTool::invalidate()
{
    m_pendingValid=false; m_debounce->stop();
    if (m_preview) { m_preview.reset(); showModel(true); }
    const QSignalBlocker blocker(m_original); m_original->setChecked(false); m_apply->setEnabled(false);
}
void PointModelingTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_step) return;
    if (m_obstacle && m_retainedPoints<3)
    {
        updateObstacleSelection(); updateUi(); return;
    }
    try
    {
        const auto r=request(); invalidate(); m_revision=m_service.project().revision;
        m_submitted=r; m_pendingValid=true; m_task=m_service.submitPointModelingPreview(r,m_revision);
        m_hint->setText(QStringLiteral("正在计算预览…")); updateUi(); if (onChanged) onChanged();
    }
    catch (const std::exception &e) { m_hint->setText(QString::fromUtf8(e.what())); }
}
void PointModelingTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle()) return;
    try { m_applyTask=m_service.submitPointModeling(m_submitted,m_revision); updateUi(); if (onChanged) onChanged(); }
    catch (const std::exception &e) { m_hint->setText(QString::fromUtf8(e.what())); }
}
void PointModelingTool::cancel()
{
    if (!m_active) return;
    m_active=false; m_clipApplied=false; m_clipDirty=true; if (!m_task.empty()) (void)m_service.cancel(m_task);
    invalidate(); m_viewport.setPointModelingPicking(false);
    m_viewport.setSelectedObjects({}); m_referenceSelection.clear(); m_obstacleSelection.reset();
    const auto p=m_service.project();
    m_viewport.setReferencePlane(p.referencePlane,p.section,displayOrigin(),selectedSectionPlane(p,p.section.source));
    updateUi(); if (onChanged) onChanged();
}
void PointModelingTool::sync(const ProjectSnapshot &project,bool idle)
{
    if (m_loading) return;
    if (m_active && (project.cloudId!=m_cloudId || project.modelId!=m_modelId) && m_applyTask.empty()) cancel();
    if (m_active && project.selection!=m_selection)
    {
        m_selection=project.selection; invalidate(); m_revision=project.revision;
        if (selectingObstaclePoints())
        {
            m_obstacleSelection=m_selection;
            m_hint->setText(QStringLiteral("点云已框选，正在准备预览…"));
            updateObstacleSelection();
            if (m_retainedPoints>=3) m_debounce->start();
        }
    }
    if (!m_task.empty() && m_service.task(m_task).finished())
    {
        const auto task=m_service.task(m_task); auto result=m_service.takeEdgePreview(m_task); m_task.clear();
        if (m_active && m_pendingValid && project.revision==m_revision)
        {
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
            if (result && task.state==TaskState::Succeeded) { m_preview=std::move(result); showModel(false); }
        }
    }
    if (!m_applyTask.empty() && m_service.task(m_applyTask).finished())
    {
        const auto task=m_service.task(m_applyTask); m_applyTask.clear(); cancel();
        if (task.result.committed && onCommitted) onCommitted();
        m_hint->setText(QString::fromUtf8(task.message.c_str()));
    }
    if (m_active && idle && m_task.empty() && !m_preview) m_revision=project.revision;
    updateUi();
}
void PointModelingTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this)) cancel(); QWidget::hideEvent(event);
}
}
