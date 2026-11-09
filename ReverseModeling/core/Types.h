/*
 * 文件描述：
 * 定义不依赖界面或第三方几何类型的点云、辅助建模、显示和质量数据契约。
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reverse {

enum class LengthUnit { Millimeter, Centimeter, Meter };

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct PointSample {
    Vec3 position; // 局部坐标，单位 mm；项目坐标 = position + CloudData::originMm。
    double intensity{};
    std::uint64_t sourceIndex{};
};

struct CloudData {
    std::vector<PointSample> points;
    std::vector<std::string> fields;
    std::uint64_t sourcePointCount{};
    std::uint64_t invalidPointCount{};
    bool hasIntensity{};
    LengthUnit sourceUnit{LengthUnit::Millimeter};
    double sourceScaleToMm{1.0};
    Vec3 originMm;
    Vec3 boundsMinMm; // 相对于 originMm。
    Vec3 boundsMaxMm;
};

enum class CloudSelectionMode
{
    Replace,
    Add,
    Remove,
    Clear
};
enum class CloudEditMode
{
    KeepSelection,
    DeleteSelection
};
enum class CloudHistoryAction
{
    Undo,
    Redo,
    Reset
};

// 矩阵作用于 CloudData::position 局部毫米坐标，按行存储；矩形为 NDC 坐标。
// 框选贯穿视锥深度，不因实体或前层点云遮挡而漏选。
struct CloudSelectionRect
{
    std::array<double, 16> viewProjection{};
    double minX{-1}, minY{-1}, maxX{1}, maxY{1};
    bool zeroToOneDepth{};
};

using CloudSelection = std::vector<std::size_t>;

// 重放选区时从显式初始索引开始，按顺序应用各次框选；不读取当前视口或当前选区。
struct CloudSelectionStep
{
    CloudSelectionRect rectangle;
    CloudSelectionMode mode{CloudSelectionMode::Replace};
};
struct CloudSelectionRecipe
{
    std::shared_ptr<const CloudSelection> initialIndices;
    std::vector<CloudSelectionStep> regions;
};

struct BeamFrame {
    Vec3 originMm; // 梁中心的项目坐标，单位 mm。
    Vec3 xAxis{1.0, 0.0, 0.0}; // 梁长方向。
    Vec3 yAxis{0.0, 1.0, 0.0}; // 翼板宽度方向。
    Vec3 zAxis{0.0, 0.0, 1.0}; // 截面高度方向。
};

struct BeamEstimate {
    BeamFrame frame;
    double length{};
    double width{};
    double height{};
    std::vector<std::string> warnings;
};

struct BeamParameters {
    double length{};
    double width{};
    double height{};
    double webThickness{};
    double flangeThickness{};
    double ribThickness{};
    std::vector<double> ribStations; // 距梁起点的距离；默认仅生成一侧肋板。
    bool swapCrossSection{}; // 交换框架的 y/z 方向；尺寸仍使用用户提交值。
    bool mirrorSymmetry{}; // 仅显式开启时补建对侧。
    bool operator==(const BeamParameters &) const = default;
};

struct PlateBoundarySegment {
    Vec3 firstMm, lastMm; // 识别框架内、板件法向最小坐标处的板面边界。
    std::optional<Vec3> arcMiddleMm; // 有值时使用经过三点的圆弧，否则为直线。
};

struct ObservedWebHole {
    Vec3 centerMm, direction{1, 0, 0}; // 识别坐标系；长轴位于腹板平面。
    double radiusMm{}; // 圆孔半径；长圆孔两端圆弧半径，即宽度的一半。
    double straightLengthMm{}; // 两端圆心距；0 为圆孔。
    double fitErrorMm{};
    std::uint64_t boundarySupport{};
};

struct RecognizedPlate {
    std::string id;
    Vec3 minimumMm; // 识别框架内的局部坐标。
    Vec3 maximumMm;
    int normalAxis{}; // x=0（肋板）、y=1（底板）、z=2（侧板）。
    std::optional<double> thicknessMm; // 识别或按 method 估算的板厚；无可用依据时为空，仅保留观测面。
    double thicknessSpreadMm{}; // 不同截面厚度估计的四分位差。
    std::uint64_t supportPoints{};
    std::string method;
    std::vector<PlateBoundarySegment> outline; // 空表示矩形；非空为有观测依据的闭合直线/圆弧轮廓。
    std::vector<ObservedWebHole> holes; // 俯视可见的腹板圆孔/长圆孔，不包含筋板角缺口。
    // 其他焊件的竖直连接组：底板面上的外环及内环，沿 +y 拉伸到 maximumMm.y。
    // 保留弯板/相接板组的占用轮廓；高度不是板厚，不能套用等厚平板局部重建。
    std::vector<std::vector<PlateBoundarySegment>> uprightFootprint;
};

struct PlateRecognition {
    BeamFrame frame;
    std::vector<RecognizedPlate> plates;
    std::vector<std::string> warnings;
    double scanSpacingMm{}; // 腹板支持面面积/点数给出的等效采样间距，不等同相机标定精度。
    double surfaceNoiseMm{}; // 腹板残差 MAD 的稳健尺度。
    double contourToleranceMm{}; // 由采样间距和噪声自动估计的拟合容差。
    std::string type{"hbeam"};
    std::vector<std::pair<std::string, BeamFrame>> plateFrames; // 存在时为该板件自身的项目坐标系。
};

struct PlateBuildOptions {
    bool mirrorSymmetry{};
    double endOverhangToleranceMm{30.0}; // 只收齐底板沿梁长方向的小突出；0 表示保留观测端部。
    bool recognizeWebHoles{true};
    double minimumHoleRadiusMm{5.0}; // 小于此半径的腹板孔忽略；0 仍受扫描分辨率限制。
    std::string type{"hbeam"}; // hbeam / tbar / other；后两者在重建时独立识别。
    bool onGround{true};
    bool regularUpperSurface{true};
    bool projectionOntoBorder{};
    bool perpendicularHypothesis{true};
    double maximumSlotWidthMm{50.0};
    double maximumBaseWidthMm{250.0};
    bool operator==(const PlateBuildOptions &) const = default;
};

struct ModelKeyPoint
{
    Vec3 positionMm;
    std::uint32_t partIndex{};
};

enum class PlateHoleKind { Weld, Bottom };
enum class PlateHoleShape { Circle, Triangle, Rectangle };
enum class HoleReference { Start, Middle, End };
struct PlateHoleBase
{
    std::uint32_t edge{}; // 部件内 BRep 边编号，从 1 开始。
    Vec3 firstMm, secondMm, inward, thicknessDirection;
    double thicknessMm{};
};
struct PlateHole
{
    std::string partId;
    std::uint32_t bottomEdge{}; // 明确底边，命令不依赖当前视角、选区或基准面。
    PlateHoleKind kind{PlateHoleKind::Weld};
    PlateHoleShape shape{PlateHoleShape::Circle};
    double sizeMm{20}; // 圆形半径 / 三角形两直角边长度。
    double widthMm{30}, heightMm{15}; // 矩形总宽、切入板内的高度。
    HoleReference reference{HoleReference::Middle};
    double offsetMm{}; // 端点向内为正，中点朝端点 2 为正。
    bool first{true}, second{true};
};

enum class EdgeTreatmentKind { Fillet, Chamfer };
struct EdgeTreatmentTarget
{
    std::string partId;
    std::vector<std::uint32_t> edges; // BRep 选边编号，从 1 开始；轮廓边解析为两端贯穿板厚的角边。
};
struct EdgeTreatment
{
    std::vector<EdgeTreatmentTarget> targets; // 按部件分组；整批计算成功后才提交。
    EdgeTreatmentKind kind{EdgeTreatmentKind::Fillet};
    double sizeMm{10};
};

struct ModelEdge
{
    std::vector<Vec3> pointsMm; // 真实 BRep 边的显示折线，不包含三角化内部对角线。
    std::uint32_t partIndex{};
    std::uint32_t topologyIndex{}; // 原始 BRep 边编号（1-based），不受显示采样跳过边影响。
};

struct MeshData {
    std::vector<Vec3> vertices; // 项目坐标，单位 mm；视图可减去局部原点。
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint32_t> trianglePartIndices;
    std::vector<std::string> partIds;
    std::vector<ModelKeyPoint> keyPoints; // BRep 拓扑顶点/边端点，不包含三角化细分节点。
    std::vector<ModelEdge> edges;
    std::vector<std::array<std::uint32_t, 2>> planarFaces; // {部件编号, 面编号}，由精确 BRep 曲面类型确定。
    std::vector<std::uint32_t> triangleFaceIndices; // 部件内 BRep 面编号，与 triangles 一一对应。
};

struct PlateThicknessInfo
{
    double thicknessMm{};
    Vec3 centerMm; // 当前项目坐标中的中面锚点。
    Vec3 normal; // 单位厚度方向，随部件刚体变换更新。
};
struct PlateThicknessChange
{
    std::vector<std::string> partIds;
    double thicknessMm{};
};

struct PlateContourExpansion
{
    std::vector<std::string> partIds;
    double distanceMm{10.0};
};

struct MirrorCompletion
{
    std::vector<std::string> partIds;
    std::string planePartId; // 显式指定底板（腹板），使用其真实等厚中面，不依赖相机或世界 Z。
};

enum class PlateGenerationKind { Cover, Base };
enum class BasePlateMode { Horizontal, VerticalPair };
struct PlateGeneration
{
    PlateGenerationKind kind{PlateGenerationKind::Cover};
    std::string partId;
    std::uint32_t faceIndex{}; // 贴板的主面编号，BRep 1-based。
    double offsetMm{30};
    double thicknessMm{10};
    bool reversed{}; // 贴板沿选中面的内法线拉伸。
    Vec3 up{0, 0, 1}; // 下底板固定世界 +Z；保留命令参数，其他方向明确拒绝。
    BasePlateMode baseMode{BasePlateMode::Horizontal};
    std::string secondPartId; // 两块竖直板方式必须指定不同的第二块板。
};
struct GeneratedPlateInfo
{
    PlateGenerationKind kind{};
    std::string sourcePartId;
    std::string secondSourcePartId;
    BasePlateMode baseMode{BasePlateMode::Horizontal};
};

// 按源部件关联的剩余点重建；不修改未选部件。
struct LocalReconstruction
{
    std::vector<std::string> partIds;
    std::string type{"plate"}; // plate 原板面方向、horizontal 指定水平法向、box 当前模型占用空间包围盒。
    std::string contour{"rectangle"};
    Vec3 up{0, 0, 1};
    double toleranceMm{2}; // 凹轮廓采样/简化尺度。
    double cornerMm{5}; // 倒角退距或圆弧角部半径。
    bool mirrorBottom{true}; // 三种角部矩形：按原竖板底边补齐下部，并镜像上角。
};

struct ObstacleInfo
{
    std::uint64_t supportPoints{};
    double resolutionMm{}, paddingMm{}; // 扫描包络的体素尺度和采样余量，不是板厚。
    bool boundingBox{}; // 已由局部重建转换为占用空间盒状障碍物。
};

struct ModelSummary {
    std::map<std::string, ObstacleInfo> obstacleParts; // 独立避障实体，不参与工件镜像/板件重建。
    std::map<std::string, std::vector<std::string>> editSources; // 分割/合并来源；不受相机影响。
    std::map<std::string, std::string> localSourceUnavailable;
    std::uint64_t nextSplitId{1};
    std::vector<std::string> reconstructableParts; // 含原始点索引关联的部件。
    std::vector<std::string> locallyReconstructedParts;

    std::map<std::string, std::string> mirroredParts; // 新增镜像部件 -> 源部件。
    std::vector<std::string> mirrorCompletedParts; // 已与镜像合并的原部件。
    std::uint64_t nextMirroredPartId{1};
    std::map<std::string, GeneratedPlateInfo> generatedPlates;
    std::uint64_t nextGeneratedPlateId{1};
    std::vector<std::string> contourExpandedParts; // 外轮廓已修改，不再适用原矩形板偏差算法。
    std::map<std::string, PlateThicknessInfo> importedThicknesses; // 导入实体的几何板厚与当前主面锚点，非手动修改。
    std::map<std::string, PlateThicknessInfo> thicknessOverrides; // 用户明确修改的板厚。
    std::vector<std::string> holeEditedParts; // 开孔后不再适用矩形板件偏差算法。
    std::vector<std::string> edgeTreatedParts; // 已做边处理的部件不适用矩形板件偏差算法。
    BeamFrame frame;
    std::map<std::string, BeamFrame> recognizedPartFrames; // T 排等各板件的初始项目坐标系。
    std::map<std::string, BeamFrame> partTransforms; // 部件相对生成/导入位置的累计项目坐标刚体变换。
    BeamParameters parameters;
    std::uint64_t solidCount{};
    double volumeMm3{};
    std::vector<std::string> partIds;
    std::vector<RecognizedPlate> plates;
    std::uint64_t surfaceCount{};
    bool fromPointCloud{};
    std::string importedFormat; // 空值为本地生成；step/cwrm 为导入几何，板厚由等厚实体主面计算。
    PlateBuildOptions recognitionOptions;
    std::vector<std::string> warnings;
};

struct CloudExportResult {
    std::uint64_t pointCount{};
    std::uint64_t fileBytes{};
    bool hasIntensity{};
};

struct StepExportResult {
    std::uint64_t solidCount{};
    double volumeMm3{};
    std::uint64_t fileBytes{};
    bool valid{};
    std::uint64_t surfaceCount{};
};

struct DeviationResult {
    std::string method{"analytic_finite_plate_surface"};
    std::uint64_t pointCount{};
    double meanMm{};
    double rmsMm{};
    double p95Mm{};
    double maximumMm{};
    std::vector<double> distancesMm; // 与 CloudData::points 顺序一致，无显示采样。
    std::vector<std::uint32_t> nearestPartIndices;
};

} // namespace reverse
