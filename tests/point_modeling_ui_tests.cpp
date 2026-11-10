/* 文件描述：真实障碍物向导控件验证明确选面、主动框选、异步预览与错误保留。 */
#include <gtest/gtest.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <chrono>
#include <fstream>
#include <thread>
#include "commands/SectionPlane.h"
#include "commands/ApplicationLog.h"
#include "workbench/PointModelingTool.h"

namespace
{
using namespace reverse;
using namespace reverse::workbench;

void ensureApplication()
{
    if (QApplication::instance()) return;
    static int argc=1;
    static char name[]="point-modeling-ui-tests";
    static char *argv[]={name,nullptr};
    static QApplication application(argc,argv);
}
TaskSnapshot finished(CommandService &service,const std::string &id)
{
    for (int i=0;i<3000;++i)
    {
        auto result=service.task(id);
        if (result.finished()) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("task timeout");
}
class PointModelingUi : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureApplication();
        const auto file=std::filesystem::path(directory.path().toStdWString())/"obstacle.pcd";
        std::ofstream output(file);
        output << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 4\nHEIGHT 1\nPOINTS 4\nDATA ascii\n"
                  "0 0 12\n30 0 30\n0 40 30\n30 40 25\n";
        output.close();
        ASSERT_EQ(finished(service,service.submitImport(file)).state,TaskState::Succeeded);
        ASSERT_EQ(finished(service,service.submitReconstruct({100,40,60,4,5,3,{},false,false},
            service.project().revision)).state,TaskState::Succeeded);
        ASSERT_TRUE(bottomSectionPlane(service.project()));
        viewport=std::make_unique<OccViewport>();
        tool=std::make_unique<PointModelingTool>(service,*viewport,nullptr);
        tool->begin(true);
    }
    template<class T> T *control(const char *name) { return tool->findChild<T *>(QString::fromUtf8(name)); }
    void choosePlane()
    {
        const auto o=service.project().cloud->originMm;
        viewport->onModelingPicked({{-o.x,-o.y,-o.z},{100-o.x,-o.y,-o.z},{-o.x,100-o.y,-o.z}},{});
    }
    void pump(int milliseconds=600)
    {
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        do
        {
            QApplication::processEvents();
            tool->sync(service.project(),service.isIdle());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now()<until);
    }
    QPushButton *button(const QString &text)
    {
        for (auto *candidate : tool->findChildren<QPushButton *>())
        {
            if (candidate->text() == text)
            {
                return candidate;
            }
        }
        throw std::runtime_error("Wizard button not found: " + text.toStdString());
    }
    bool pumpUntil(const std::function<bool()> &ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            QApplication::processEvents();
            tool->sync(service.project(), service.isIdle());
            if (ready())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    void chooseWorldDirection()
    {
        tool->begin(false);
        auto *direction = control<QComboBox>("pointModel.direction");
        direction->setCurrentIndex(direction->findData("world"));
    }
    void pickWorldPoints(std::vector<Vec3> points)
    {
        const auto origin = service.project().cloud->originMm;
        for (auto &point : points)
        {
            point.x -= origin.x;
            point.y -= origin.y;
            point.z -= origin.z;
        }
        viewport->onModelingPicked(points, {});
    }
    QTemporaryDir directory;
    CommandService service;
    std::unique_ptr<OccViewport> viewport;
    std::unique_ptr<PointModelingTool> tool;
};

TEST_F(PointModelingUi, EntryDoesNotInheritBottomPlaneOrPreviousSettings)
{
    EXPECT_EQ(control<QPushButton>("pointModel.referenceFace")->text(),QStringLiteral("请选择一个参考面"));
    EXPECT_TRUE(control<QPushButton>("pointModel.referenceFace")->isChecked());
    EXPECT_FALSE(control<QPushButton>("pointModel.selectCloud")->isEnabled());
    EXPECT_FALSE(control<QCheckBox>("pointModel.section")->isEnabled());
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    EXPECT_TRUE(control<QPushButton>("pointModel.previewAllPoints")->isHidden());
    EXPECT_FALSE(tool->selectingObstaclePoints());
    choosePlane();
    control<QDoubleSpinBox>("pointModel.offset")->setValue(15);
    tool->begin(true);
    EXPECT_DOUBLE_EQ(control<QDoubleSpinBox>("pointModel.offset")->value(),10);
    EXPECT_FALSE(control<QPushButton>("pointModel.selectCloud")->isEnabled());
}
TEST_F(PointModelingUi, FaceAndPointSelectionAreExplicitAndClearResetsBoth)
{
    const auto model=service.project().model;
    viewport->onModelingPicked({},QStringLiteral("未选中模型平面"));
    pump(80);
    EXPECT_EQ(control<QLabel>("pointModel.status")->text(),QStringLiteral("未选中模型平面"));
    choosePlane();
    EXPECT_TRUE(control<QPushButton>("pointModel.selectCloud")->isEnabled());
    EXPECT_FALSE(tool->selectingObstaclePoints());
    control<QPushButton>("pointModel.selectCloud")->click();
    EXPECT_TRUE(tool->selectingObstaclePoints());
    EXPECT_TRUE(control<QPushButton>("pointModel.selectCloud")->isChecked());
    EXPECT_FALSE(control<QPushButton>("pointModel.referenceFace")->isChecked());
    control<QPushButton>("pointModel.clearReference")->click();
    EXPECT_FALSE(tool->selectingObstaclePoints());
    EXPECT_FALSE(control<QPushButton>("pointModel.selectCloud")->isEnabled());
    EXPECT_FALSE(control<QCheckBox>("pointModel.section")->isEnabled());
    EXPECT_EQ(service.project().model,model);
}
TEST_F(PointModelingUi, SelectedCloudPreviewsAndFailureStaysVisibleUntilCorrected)
{
    choosePlane();
    control<QPushButton>("pointModel.selectCloud")->click();
    CloudSelectionRect rectangle;
    rectangle.viewProjection={.01,0,0,0,0,.01,0,0,0,0,.01,0,0,0,0,1};
    ASSERT_EQ(finished(service,service.submitCloudSelection(rectangle,CloudSelectionMode::Replace,
        service.project().revision)).state,TaskState::Succeeded);
    const auto original=service.project().model;
    pump();
    EXPECT_TRUE(tool->showingPreview());
    EXPECT_TRUE(control<QPushButton>("pointModel.apply")->isEnabled());
    EXPECT_EQ(service.project().model,original);
    control<QDoubleSpinBox>("pointModel.offset")->setValue(1000);
    pump();
    EXPECT_FALSE(tool->showingPreview());
    const auto failure=control<QLabel>("pointModel.status")->text();
    EXPECT_TRUE(failure.contains(QStringLiteral("已框选 4 个点")));
    EXPECT_TRUE(failure.contains(QStringLiteral("只剩 0 个")));
    EXPECT_EQ(control<QLabel>("pointModel.selectionInfo")->text(),QStringLiteral("框选：4 点；剖视后可用：0 点。"));
    EXPECT_FALSE(control<QPushButton>("pointModel.previewAllPoints")->isHidden());
    pump(100);
    EXPECT_EQ(control<QLabel>("pointModel.status")->text(),failure);
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    control<QDoubleSpinBox>("pointModel.offset")->setValue(10);
    pump();
    ASSERT_TRUE(control<QPushButton>("pointModel.apply")->isEnabled());
    control<QPushButton>("pointModel.apply")->click();
    pump();
    EXPECT_EQ(service.project().model->obstacleParts.size(),1);
    EXPECT_EQ(finished(service,service.submitLine("project.undo")).state,TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount,original->solidCount);
}
TEST_F(PointModelingUi, ClosingSectionRecoversPreviewWithoutReselectingCloud)
{
    choosePlane();
    control<QPushButton>("pointModel.selectCloud")->click();
    CloudSelectionRect rectangle;
    rectangle.viewProjection={.01,0,0,0,0,.01,0,0,0,0,.01,0,0,0,0,1};
    ASSERT_EQ(finished(service,service.submitCloudSelection(rectangle,CloudSelectionMode::Replace,
        service.project().revision)).state,TaskState::Succeeded);
    pump();
    const auto original=service.project().model;
    const auto selected=service.project().selection;
    control<QDoubleSpinBox>("pointModel.offset")->setValue(27);
    pump();
    EXPECT_EQ(control<QLabel>("pointModel.selectionInfo")->text(),QStringLiteral("框选：4 点；剖视后可用：2 点。"));
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    control<QPushButton>("pointModel.previewAllPoints")->click();
    pump();
    EXPECT_FALSE(control<QCheckBox>("pointModel.section")->isChecked());
    EXPECT_EQ(service.project().selection,selected);
    EXPECT_EQ(service.project().model,original);
    EXPECT_EQ(control<QLabel>("pointModel.selectionInfo")->text(),QStringLiteral("框选：4 点；可用：4 点。"));
    EXPECT_TRUE(control<QPushButton>("pointModel.previewAllPoints")->isHidden());
    EXPECT_TRUE(tool->showingPreview());
    EXPECT_TRUE(control<QPushButton>("pointModel.apply")->isEnabled());
    control<QCheckBox>("pointModel.section")->setChecked(true);
    control<QDoubleSpinBox>("pointModel.offset")->setValue(1000);
    control<QCheckBox>("pointModel.reverse")->setChecked(true);
    pump();
    EXPECT_EQ(control<QLabel>("pointModel.selectionInfo")->text(),QStringLiteral("框选：4 点；剖视后可用：4 点。"));
    EXPECT_TRUE(tool->showingPreview());
    EXPECT_EQ(service.project().selection,selected);
}

TEST_F(PointModelingUi, ChangedSourceEndsWizardAndDoesNotReusePreview)
{
    choosePlane();
    control<QPushButton>("pointModel.selectCloud")->click();
    const auto file = std::filesystem::path(directory.path().toStdWString()) / "obstacle.pcd";
    ASSERT_EQ(finished(service, service.submitImport(file)).state, TaskState::Succeeded);
    tool->sync(service.project(), true);
    EXPECT_FALSE(tool->active());
    EXPECT_FALSE(tool->showingPreview());
    EXPECT_FALSE(tool->selectingObstaclePoints());
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
}

TEST_F(PointModelingUi, DiagnosticFileExplainsSelectionFilteringAndPreviewReadiness)
{
    struct FileLog
    {
        ~FileLog() { logging::stop(); }
    } fileLog;
    ASSERT_TRUE(logging::start(directory.path() + QStringLiteral("/diagnostics")));
    const auto logPath = std::filesystem::path(logging::filePath().toStdWString());
    choosePlane();
    control<QPushButton>("pointModel.selectCloud")->click();
    CloudSelectionRect rectangle;
    rectangle.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
    ASSERT_EQ(finished(service, service.submitCloudSelection(rectangle, CloudSelectionMode::Replace,
        service.project().revision)).state, TaskState::Succeeded);
    pump();
    ASSERT_TRUE(tool->showingPreview());
    control<QDoubleSpinBox>("pointModel.offset")->setValue(1000);
    pump();
    logging::stop();
    std::ifstream file(logPath, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    const std::string log((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(log.find("point_modeling.selection revision="), std::string::npos);
    EXPECT_NE(log.find("selected=4 retained=4"), std::string::npos);
    EXPECT_NE(log.find("selected=4 retained=0"), std::string::npos);
    EXPECT_NE(log.find("point_modeling.preview_ready revision="), std::string::npos);
}

TEST_F(PointModelingUi, RectangleWizardEditsDimensionsPreviewsAndRestoresHistory)
{
    chooseWorldDirection();
    const auto before = service.project();
    auto *next = button(QStringLiteral("下一步"));
    EXPECT_FALSE(next->isEnabled());
    pickWorldPoints({{0, 0, 50}});
    EXPECT_FALSE(next->isEnabled());
    pickWorldPoints({{0, 0, 50}, {100, 0, 50}});
    ASSERT_TRUE(next->isEnabled());
    next->click();
    ASSERT_TRUE(pumpUntil([&] { return tool->showingPreview(); }));
    EXPECT_EQ(service.project().model, before.model);
    EXPECT_EQ(service.project().historySteps, before.historySteps);
    EXPECT_DOUBLE_EQ(control<QDoubleSpinBox>("pointModel.length")->value(), 100);
    control<QDoubleSpinBox>("pointModel.width")->setValue(30);
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    ASSERT_TRUE(pumpUntil([&] { return tool->showingPreview(); }));
    control<QPushButton>("pointModel.apply")->click();
    ASSERT_TRUE(pumpUntil([&] { return service.isIdle() && !tool->active(); }));
    const auto after = service.project();
    ASSERT_TRUE(after.model);
    EXPECT_EQ(after.model->solidCount, before.model->solidCount + 1);
    EXPECT_NEAR(after.model->volumeMm3 - before.model->volumeMm3, 300000, 1e-4);
    ASSERT_EQ(finished(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model, before.model);
    ASSERT_EQ(finished(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model, after.model);
}

TEST_F(PointModelingUi, CircleWizardInfersDiameterAndChangingDirectionClearsPreview)
{
    chooseWorldDirection();
    const auto before = service.project();
    auto *shape = control<QComboBox>("pointModel.shape");
    shape->setCurrentIndex(shape->findData("circle"));
    auto *method = control<QComboBox>("pointModel.method");
    method->setCurrentIndex(method->findData("center"));
    pickWorldPoints({{0, 0, 50}, {15, 0, 50}});
    button(QStringLiteral("下一步"))->click();
    ASSERT_TRUE(pumpUntil([&] { return tool->showingPreview(); }));
    EXPECT_DOUBLE_EQ(control<QDoubleSpinBox>("pointModel.diameter")->value(), 30);
    auto *direction = control<QComboBox>("pointModel.direction");
    direction->setCurrentIndex(direction->findData("face"));
    EXPECT_FALSE(tool->showingPreview());
    EXPECT_EQ(tool->inputPickMode(), ModelPickMode::Face);
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    EXPECT_FALSE(button(QStringLiteral("下一步"))->isEnabled());
    EXPECT_EQ(service.project().model, before.model);
    EXPECT_EQ(service.project().revision, before.revision);
}

TEST_F(PointModelingUi, CancellingPendingPreviewDoesNotReopenItOrChangeProject)
{
    chooseWorldDirection();
    const auto before = service.project();
    pickWorldPoints({{0, 0, 50}, {100, 0, 50}});
    button(QStringLiteral("下一步"))->click();
    tool->cancel();
    ASSERT_TRUE(pumpUntil([&] { return service.isIdle(); }));
    tool->sync(service.project(), true);
    EXPECT_FALSE(tool->active());
    EXPECT_FALSE(tool->showingPreview());
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    EXPECT_EQ(service.project().model, before.model);
    EXPECT_EQ(service.project().revision, before.revision);
    EXPECT_EQ(service.project().historySteps, before.historySteps);
    // 重新进入时不得复用上一次任务的轮廓、结果或应用状态。
    chooseWorldDirection();
    EXPECT_FALSE(tool->showingPreview());
    EXPECT_FALSE(button(QStringLiteral("下一步"))->isEnabled());
}

TEST_F(PointModelingUi, InvalidRectangleCanBeCorrectedWithoutLeavingWizard)
{
    chooseWorldDirection();
    const auto before = service.project();
    pickWorldPoints({{0, 0, 50}, {0, 0, 50}});
    button(QStringLiteral("下一步"))->click();
    EXPECT_FALSE(tool->showingPreview());
    EXPECT_FALSE(control<QPushButton>("pointModel.apply")->isEnabled());
    EXPECT_FALSE(control<QLabel>("pointModel.status")->text().isEmpty());
    EXPECT_EQ(service.project().model, before.model);
    pickWorldPoints({{0, 0, 50}, {100, 0, 50}});
    button(QStringLiteral("下一步"))->click();
    ASSERT_TRUE(pumpUntil([&] { return tool->showingPreview(); }));
    EXPECT_TRUE(control<QPushButton>("pointModel.apply")->isEnabled());
}
}
