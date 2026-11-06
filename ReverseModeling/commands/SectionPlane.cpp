/* 文件描述：以识别底板的中面（未定厚时为观测面）或手动模型底板构造工程坐标平面；不依赖相机。 */
#include <algorithm>

#include "commands/CommandService.h"
#include "commands/SectionPlane.h"
#include "geometry/ModelTransform.h"
#include "geometry/ReferencePlane.h"

namespace reverse
{
namespace
{
ReferencePlane planeAt(const BeamFrame &frame, const Vec3 &center)
{
    const auto p = transformPoint(frame, center);
    const auto &x = frame.xAxis;
    const auto &n = frame.yAxis;
    const Vec3 y{n.y * x.z - n.z * x.y, n.z * x.x - n.x * x.z, n.x * x.y - n.y * x.x};
    return makeReferencePlane({p, {p.x + x.x, p.y + x.y, p.z + x.z},
                                 {p.x + y.x, p.y + y.y, p.z + y.z}});
}
Vec3 centerOf(const RecognizedPlate &plate)
{
    return {(plate.minimumMm.x + plate.maximumMm.x) / 2,
            (plate.minimumMm.y + plate.maximumMm.y) / 2,
            (plate.minimumMm.z + plate.maximumMm.z) / 2};
}
}

std::optional<ReferencePlane> bottomSectionPlane(const ProjectSnapshot &project)
{
    if (!project.model && project.recognition)
    {
        const auto &recognition = *project.recognition;
        const auto web = std::find_if(recognition.plates.begin(), recognition.plates.end(),
            [](const auto &plate) { return plate.id == "web" && plate.normalAxis == 1; });
        if (web != recognition.plates.end()) return planeAt(recognition.frame, centerOf(*web));
    }
    if (!project.model || !project.model->importedFormat.empty()) return {};
    const auto &model = *project.model;
    if (std::find(model.partIds.begin(), model.partIds.end(), "web") == model.partIds.end()) return {};
    const auto web = std::find_if(model.plates.begin(), model.plates.end(),
        [](const auto &plate) { return plate.id == "web" && plate.normalAxis == 1; });
    if (web == model.plates.end() && model.fromPointCloud) return {};
    return planeAt(modelPartFrame(model, "web"), web == model.plates.end() ? Vec3{} : centerOf(*web));
}

std::optional<ReferencePlane> selectedSectionPlane(const ProjectSnapshot &project, SectionPlaneSource source)
{
    if (source == SectionPlaneSource::Bottom) return bottomSectionPlane(project);
    if (source == SectionPlaneSource::Reference && project.referencePlane) return *project.referencePlane;
    return {};
}

PositioningView positioningView(const ProjectSnapshot &project)
{
    if (project.section.enabled) return PositioningView::Section;
    return project.model && project.model->importedFormat.empty() ? PositioningView::Bottom : PositioningView::Top;
}

std::optional<ReferencePlane> positioningPlane(const ProjectSnapshot &project)
{
    switch (positioningView(project))
    {
    case PositioningView::Section: return selectedSectionPlane(project, project.section.source);
    case PositioningView::Bottom: return bottomSectionPlane(project);
    case PositioningView::Top: return makeReferencePlane({Vec3{}, {1, 0, 0}, {0, 1, 0}});
    }
    return {};
}
} // namespace reverse
