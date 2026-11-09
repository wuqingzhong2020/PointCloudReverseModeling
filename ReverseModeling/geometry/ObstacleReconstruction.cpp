/*
 * 文件描述：对工件表面之外的点进行体素连通分组，保留有足够扫描支持的独立障碍物。
 * 每组生成包含观测点及采样余量的凸包实体；包络不表示未扫描背面的精确外形。
 * BVH 只检索工件三角面；点在实体内部时不重复生成障碍物。所有坐标计算使用扫描局部原点。
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <Eigen/Dense>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Wire.hxx>
#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>
#include "geometry/Geometry.h"
#include "geometry/ModelTransform.h"
#include "geometry/ObstacleReconstruction.h"

namespace reverse::detail
{
namespace
{
using Point = Eigen::Vector3d;
using Key = std::array<std::int64_t, 3>;
Point point(Vec3 p) { return {p.x, p.y, p.z}; }
void check(std::stop_token stop)
{
    if (stop.stop_requested()) throw GeometryError("cancelled", "障碍物重建已取消。");
}
struct KeyHash
{
    std::size_t operator()(const Key &key) const noexcept
    {
        std::size_t value = 0;
        for (auto n : key) value ^= std::hash<std::int64_t>{}(n) + 0x9e3779b9 + (value << 6) + (value >> 2);
        return value;
    }
};
Key keyOf(const Point &p, double step)
{
    return {static_cast<std::int64_t>(std::floor(p.x() / step)),
            static_cast<std::int64_t>(std::floor(p.y() / step)),
            static_cast<std::int64_t>(std::floor(p.z() / step))};
}
struct Cell
{
    Point low = Point::Constant(std::numeric_limits<double>::infinity());
    Point high = -low;
    Point sum = Point::Zero();
    Eigen::Matrix3d scatter = Eigen::Matrix3d::Zero(); // 原始点的中心二阶矩；不以体素中心代替扫描宽度。
    std::uint64_t count{};
    std::size_t firstPoint = std::numeric_limits<std::size_t>::max(); // 原始点链，供距离过滤逐点判定。
    void add(const Point &p)
    {
        const Point delta = count ? Point(p - sum / count) : Point::Zero();
        low = low.cwiseMin(p); high = high.cwiseMax(p); sum += p; ++count;
        const Point remaining = p - sum / count;
        scatter.noalias() += delta * remaining.transpose();
    }
    void merge(const Cell &other)
    {
        if (count && other.count)
        {
            const Point delta = other.sum / other.count - sum / count;
            const double weight = double(count) * double(other.count) / double(count + other.count);
            scatter.noalias() += weight * delta * delta.transpose();
        }
        scatter += other.scatter;
        low = low.cwiseMin(other.low); high = high.cwiseMax(other.high); sum += other.sum; count += other.count;
    }
};
// 使用收齐、裁剪前的观测范围识别同一板件的边缘残差，不把装配修正当成新物体。
// 断开的簇还须符合板边方向的一维扫描条带，保留具有独立面/体支持的小物体。
bool isPlateResidual(const std::vector<Cell> &cells, std::uint64_t count, Vec3 origin,
    const PlateRecognition *recognition, bool connected, double tolerance, double edgeTolerance,
    double spreadTolerance, double normalSpreadTolerance, std::stop_token stop)
{
    if (!recognition) return false;
    Cell distribution;
    if (!connected) for (const auto &cell : cells) distribution.merge(cell);
    for (const auto &plate : recognition->plates)
    {
        check(stop);
        // 异形轮廓的包围范围含有真实空区，不能把整个包围盒当作板件证据。
        if (!plate.outline.empty() || !plate.uprightFootprint.empty() ||
            plate.normalAxis < 0 || plate.normalAxis > 2) continue;
        const auto found = std::find_if(recognition->plateFrames.begin(), recognition->plateFrames.end(),
            [&](const auto &p) { return p.first == plate.id; });
        const auto &frame = found == recognition->plateFrames.end() ? recognition->frame : found->second;
        const Point low = point(plate.minimumMm), high = point(plate.maximumMm);
        std::uint64_t explained = 0, edgeSupport = 0;
        for (std::size_t i = 0; i < cells.size(); ++i)
        {
            if (i % 1024 == 0) check(stop);
            const auto &cell = cells[i];
            const Point p = cell.sum / cell.count + point(origin);
            const Point local = point(inverseTransformPoint(frame, {p.x(),p.y(),p.z()}));
            if ((local.array() >= low.array() - tolerance).all() && (local.array() <= high.array() + tolerance).all())
            {
                explained += cell.count;
                bool nearEdge = false;
                for (int axis = 0; axis < 3; ++axis)
                    if (axis != plate.normalAxis && (std::abs(local[axis]-low[axis]) <= tolerance ||
                        std::abs(local[axis]-high[axis]) <= tolerance)) nearEdge = true;
                if (nearEdge) edgeSupport += cell.count;
            }
        }
        // 要求同一块板解释至少 98% 的原始扫描支持；不以障碍物大小作统一删除阈值。
        // 超出范围的显著凸起、相接地面和夹具继续按残余簇生成包络。
        if (double(explained) < .98 * double(count) || double(edgeSupport) < .5 * double(count)) continue;
        if (connected) return true;
        const Point center = distribution.sum / count + point(origin);
        const Point local = point(inverseTransformPoint(frame, {center.x(),center.y(),center.z()}));
        const Point axes[]{point(frame.xAxis),point(frame.yAxis),point(frame.zAxis)};
        double projectedVariance[3];
        for (int axis=0;axis<3;++axis)
            projectedVariance[axis]=std::max(0.0,axes[axis].dot(distribution.scatter*axes[axis])/count);
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> fit(distribution.scatter / count);
        if (fit.info() != Eigen::Success) continue;
        const auto variance = fit.eigenvalues().cwiseMax(0.0).eval();
        const Point direction = fit.eigenvectors().col(2);
        const double noiseVariance = spreadTolerance * spreadTolerance;
        // 拟合实际扫描条带方向，容许斜视侧面与理想板边略有夹角。
        // 两个较小主方差仍须符合面噪声/采样宽度，不能将小实体按线条处理。
        if (variance[0] > normalSpreadTolerance * normalSpreadTolerance || variance[1] > noiseVariance ||
            variance[2] < 4 * variance[1] || variance[2] < noiseVariance) continue;
        for (int edgeAxis = 0; edgeAxis < 3; ++edgeAxis)
        {
            if (edgeAxis == plate.normalAxis) continue;
            const int alongEdge = 3 - edgeAxis - plate.normalAxis;
            // 很短的稀疏条带主方向易受几个采样列影响，保留板件坐标下的窄条证据。
            const bool aligned = projectedVariance[plate.normalAxis] <= normalSpreadTolerance * normalSpreadTolerance &&
                projectedVariance[edgeAxis] <= noiseVariance && projectedVariance[alongEdge] >= noiseVariance &&
                projectedVariance[alongEdge] >= 4 * std::max(projectedVariance[edgeAxis], projectedVariance[plate.normalAxis]);
            // 10° 是长条残差归属的方向容差，不是猜测或修改相机安装角。
            if ((aligned || std::abs(direction.dot(axes[alongEdge])) >= std::cos(10.0 * 3.141592653589793 / 180.0)) &&
                std::min(std::abs(local[edgeAxis]-low[edgeAxis]),std::abs(local[edgeAxis]-high[edgeAxis])) <= edgeTolerance)
                return true;
        }
    }
    return false;
}
struct Triangle { Point a, b, c, low, high; std::uint32_t part{}; };
double triangleDistanceSquared(const Point &p, const Triangle &t)
{
    // Use scalar OCCT vectors here: long Eigen expressions with 3-double temporaries
    // cause an unaligned SSE load in MSVC's optimized distance calculation.
    const auto xyz = [](const Point &v) { return gp_XYZ(v.x(), v.y(), v.z()); };
    const gp_XYZ a = xyz(t.a), b = xyz(t.b), c = xyz(t.c), query = xyz(p);
    const gp_XYZ ab = b - a, ac = c - a, ap = query - a;
    const double d1 = ab.Dot(ap), d2 = ac.Dot(ap);
    if (d1 <= 0 && d2 <= 0) return ap.SquareModulus();
    const gp_XYZ bp = query - b;
    const double d3 = ab.Dot(bp), d4 = ac.Dot(bp);
    if (d3 >= 0 && d4 <= d3) return bp.SquareModulus();
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return (ap - ab * (d1 / (d1 - d3))).SquareModulus();
    const gp_XYZ cp = query - c;
    const double d5 = ab.Dot(cp), d6 = ac.Dot(cp);
    if (d6 >= 0 && d5 <= d6) return cp.SquareModulus();
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return (ap - ac * (d2 / (d2 - d6))).SquareModulus();
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 >= d3 && d5 >= d6)
        return (bp - (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))).SquareModulus();
    return (ap - ab * (vb / (va + vb + vc)) - ac * (vc / (va + vb + vc))).SquareModulus();
}
class WorkpieceIndex
{
    struct Node { Point low, high; std::size_t begin{}, end{}; int left{-1}, right{-1}; };
    std::vector<Triangle> triangles;
    std::vector<Node> nodes;
    const std::vector<bool> &solids;
    const Point ray = Point(1, 0.3713906763541037, 0.213743167247).normalized();
    int build(std::size_t begin, std::size_t end)
    {
        const int index = static_cast<int>(nodes.size());
        Node node{Point::Constant(std::numeric_limits<double>::infinity()), Point::Constant(-std::numeric_limits<double>::infinity()), begin, end};
        for (auto i = begin; i < end; ++i) { node.low = node.low.cwiseMin(triangles[i].low); node.high = node.high.cwiseMax(triangles[i].high); }
        nodes.push_back(node);
        if (end - begin > 8)
        {
            Eigen::Index axis; (node.high - node.low).maxCoeff(&axis);
            const auto middle = begin + (end - begin) / 2;
            std::nth_element(triangles.begin() + begin, triangles.begin() + middle, triangles.begin() + end,
                [axis](const auto &a, const auto &b) { return (a.low[axis] + a.high[axis]) < (b.low[axis] + b.high[axis]); });
            const int left = build(begin, middle), right = build(middle, end);
            nodes[index].left = left; nodes[index].right = right;
        }
        return index;
    }
    bool near(int index, const Point &p, double distanceSquared) const
    {
        const auto &node = nodes[index];
        if (((node.low - p).cwiseMax(p - node.high).cwiseMax(0.0)).squaredNorm() > distanceSquared) return false;
        if (node.left >= 0) return near(node.left, p, distanceSquared) || near(node.right, p, distanceSquared);
        for (auto i = node.begin; i < node.end; ++i) if (triangleDistanceSquared(p, triangles[i]) <= distanceSquared) return true;
        return false;
    }
    void rayHits(int index, const Point &p, std::vector<std::vector<double>> &hits) const
    {
        const auto &node = nodes[index];
        double entry = 0, exit = std::numeric_limits<double>::infinity();
        for (int a = 0; a < 3; ++a)
        {
            const double first = (node.low[a] - p[a]) / ray[a], second = (node.high[a] - p[a]) / ray[a];
            entry = std::max(entry, std::min(first, second)); exit = std::min(exit, std::max(first, second));
        }
        if (entry > exit) return;
        if (node.left >= 0) { rayHits(node.left, p, hits); rayHits(node.right, p, hits); return; }
        for (auto i = node.begin; i < node.end; ++i)
        {
            const auto &t = triangles[i];
            if (!solids[t.part]) continue;
            const Point e1 = t.b - t.a, e2 = t.c - t.a, h = ray.cross(e2), s = p - t.a;
            const double det = e1.dot(h);
            if (std::abs(det) < 1e-12) continue;
            const double u = s.dot(h) / det;
            if (u < -1e-9 || u > 1 + 1e-9) continue;
            const Point q = s.cross(e1);
            const double v = ray.dot(q) / det, distance = e2.dot(q) / det;
            if (v >= -1e-9 && u + v <= 1 + 1e-9 && distance > 1e-8) hits[t.part].push_back(distance);
        }
    }
  public:
    WorkpieceIndex(const MeshData &mesh, const Vec3 &origin, const std::vector<bool> &solidParts) : solids(solidParts)
    {
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
        {
            const auto &v = mesh.triangles[i];
            const Point a = point(mesh.vertices[v[0]]) - point(origin), b = point(mesh.vertices[v[1]]) - point(origin), c = point(mesh.vertices[v[2]]) - point(origin);
            if ((b - a).cross(c - a).squaredNorm() > 1e-20)
                triangles.push_back({a, b, c, a.cwiseMin(b).cwiseMin(c), a.cwiseMax(b).cwiseMax(c), mesh.trianglePartIndices[i]});
        }
        if (!triangles.empty()) build(0, triangles.size());
    }
    bool covers(const Point &p, double tolerance) const
    {
        if (nodes.empty()) return false;
        if (near(0, p, tolerance * tolerance)) return true;
        std::vector<std::vector<double>> hits(solids.size());
        rayHits(0, p, hits);
        for (auto &part : hits)
        {
            std::sort(part.begin(), part.end());
            const auto end = std::unique(part.begin(), part.end(), [](double a, double b) { return std::abs(a - b) < 1e-7; });
            if (std::distance(part.begin(), end) % 2 != 0) return true;
        }
        return false;
    }
};

TopoDS_Shape envelope(std::vector<Cell> cells, double &resolution, double padding, Vec3 origin, std::stop_token stop)
{
    // 大型地面/台架只在包络阶段合并体素，既限制凸包输入量，又包含全部原扫描范围。
    while (cells.size() > 4096)
    {
        check(stop); resolution *= 2;
        std::unordered_map<Key, Cell, KeyHash> coarse;
        for (const auto &cell : cells) coarse[keyOf((cell.low + cell.high) * 0.5, resolution)].merge(cell);
        cells.clear(); cells.reserve(coarse.size());
        std::vector<Key> keys; for (const auto &[key, cell] : coarse) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto &key : keys) cells.push_back(coarse.at(key));
    }
    std::vector<double> coordinates;
    for (const auto &cell : cells)
        for (int corner = 0; corner < 8; ++corner)
            for (int axis = 0; axis < 3; ++axis)
                coordinates.push_back(corner & (1 << axis) ? cell.high[axis] + padding : cell.low[axis] - padding);
    check(stop);
    std::ostringstream errors;
    orgQhull::Qhull hull;
    hull.setErrorStream(&errors);
    hull.runQhull("obstacle envelope", 3, static_cast<int>(coordinates.size() / 3), coordinates.data(), "Qt");
    BRepBuilderAPI_Sewing sewing(1e-6);
    for (const auto &facet : hull.facetList())
    {
        check(stop);
        if (!facet.isGood()) continue;
        std::vector<Point> points;
        for (const auto &vertex : facet.vertices())
        {
            const auto p = vertex.point();
            points.emplace_back(p[0], p[1], p[2]);
        }
        if (points.size() != 3) throw GeometryError("obstacle_failed", "障碍物包络三角化失败，模型未提交。");
        const auto normal = facet.hyperplane();
        if ((points[1] - points[0]).cross(points[2] - points[0]).dot(Point(normal[0], normal[1], normal[2])) < 0)
            std::swap(points[1], points[2]);
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto &p : points) polygon.Add(gp_Pnt(p.x() + origin.x, p.y() + origin.y, p.z() + origin.z));
        polygon.Close();
        sewing.Add(BRepBuilderAPI_MakeFace(polygon.Wire()).Face());
    }
    sewing.Perform(); check(stop);
    const auto shell = sewing.SewedShape();
    if (shell.ShapeType() != TopAbs_SHELL) throw GeometryError("obstacle_failed", "障碍物包络未闭合，模型未提交。");
    auto solid = BRepBuilderAPI_MakeSolid(TopoDS::Shell(shell)).Solid();
    if (!BRepLib::OrientClosedSolid(solid) || !BRepCheck_Analyzer(solid).IsValid())
        throw GeometryError("obstacle_failed", "障碍物包络未生成有效实体，模型未提交。");
    ShapeUpgrade_UnifySameDomain unify(solid, true, true, false);
    unify.Build();
    const auto result = unify.Shape();
    if (result.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer(result).IsValid()) return solid;
    return result;
}
} // namespace

std::vector<ObstacleShape> reconstructObstacleShapes(const CloudData &cloud, const MeshData &workpiece,
    const std::vector<bool> &solidParts, double spacingMm, double noiseMm, std::stop_token stop, const ProgressCallback &progress,
    const PlateRecognition *recognition, double exclusionDistanceMm)
{
    check(stop);
    if (!std::isfinite(exclusionDistanceMm) || exclusionDistanceMm < 0 || exclusionDistanceMm > 1000)
        throw GeometryError("invalid_parameters", "钢板附近排除距离必须在 0–1000 mm 范围内。");
    const double spacing = std::clamp(spacingMm > 0 ? spacingMm : 1.0, 0.25, 20.0);
    const double resolution = std::max(1.0, 2 * spacing);
    const double tolerance = std::max({1.0, 1.5 * spacing, 4 * noiseMm});
    const double padding = std::max({0.25, 0.5 * spacing, 3 * noiseMm});
    if (progress) progress(0, "归集工件以外的障碍物点云");
    std::unordered_map<Key, Cell, KeyHash> grid;
    std::vector<std::size_t> nextPoint(cloud.points.size(), std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0; i < cloud.points.size(); ++i)
    {
        if (i % 4096 == 0) check(stop);
        const auto p = point(cloud.points[i].position);
        if (p.allFinite())
        {
            auto &cell = grid[keyOf(p, resolution)];
            nextPoint[i] = cell.firstPoint;
            cell.firstPoint = i;
            cell.add(p);
        }
    }
    WorkpieceIndex index(workpiece, cloud.originMm, solidParts);
    std::vector<Key> keys; keys.reserve(grid.size());
    for (const auto &[key, cell] : grid) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    std::vector<bool> covered(keys.size(), false);
    std::unordered_map<Key, std::size_t, KeyHash> lookup;
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i % 2048 == 0) { check(stop); if (progress) progress(0.1 + 0.4 * i / keys.size(), "区分工件与障碍物扫描点"); }
        const auto &cell = grid.at(keys[i]);
        covered[i] = index.covers(cell.sum / cell.count, tolerance);
        lookup.emplace(keys[i], i);
    }
    // 先标记原始扫描连通簇的归属，避免剔除板面后把其噪声残差误当独立物体。
    std::vector<bool> visited(keys.size(), false), workpieceGroup(keys.size(), false);
    for (std::size_t seed = 0; seed < keys.size(); ++seed)
    {
        check(stop);
        if (visited[seed]) continue;
        std::vector<std::size_t> queue{seed}; visited[seed] = true;
        std::uint64_t count = 0, workpieceCount = 0;
        bool hasSurfaceSupport = exclusionDistanceMm <= 0;
        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            if (head % 1024 == 0) check(stop);
            const auto i = queue[head]; const auto &key = keys[i]; const auto &cell = grid.at(key);
            count += cell.count;
            if (covered[i]) workpieceCount += cell.count;
            if (!hasSurfaceSupport && covered[i])
                hasSurfaceSupport = index.covers(cell.sum / cell.count, exclusionDistanceMm);
            for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) for (int z = -2; z <= 2; ++z)
            {
                const auto found = lookup.find(Key{key[0] + x, key[1] + y, key[2] + z});
                if (found != lookup.end() && !visited[found->second]) { visited[found->second] = true; queue.push_back(found->second); }
            }
        }
        if (hasSurfaceSupport && workpieceCount * 2 >= count)
            for (const auto i : queue) workpieceGroup[i] = true;
    }
    // 原始连通性只用于判断板边噪声归属；距离过滤按原始点执行，不能以体素中心
    // 代替，也不能因物体的一部分靠近钢板而删掉整个物体。过滤后再重新分组。
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i % 2048 == 0) check(stop);
        auto &cell = grid.at(keys[i]);
        if (exclusionDistanceMm <= 0)
        {
            if (workpieceGroup[i] && covered[i]) cell = Cell{};
            continue;
        }
        Cell residual;
        for (auto pointIndex = cell.firstPoint; pointIndex != std::numeric_limits<std::size_t>::max();
             pointIndex = nextPoint[pointIndex])
        {
            if (pointIndex % 4096 == 0) check(stop);
            const auto p = point(cloud.points[pointIndex].position);
            // 已归属钢板的连续扫描仍使用原有面噪声容差；独立物体仅按用户距离过滤。
            const double distance = workpieceGroup[i] ? std::max(exclusionDistanceMm, tolerance) : exclusionDistanceMm;
            if (!index.covers(p, distance)) residual.add(p);
        }
        cell = std::move(residual);
    }
    std::fill(visited.begin(), visited.end(), false);
    const double attachedTolerance = std::max(3 * tolerance, 6 * spacing);
    std::vector<ObstacleShape> result;
    for (std::size_t seed = 0; seed < keys.size(); ++seed)
    {
        check(stop);
        if (visited[seed] || grid.at(keys[seed]).count == 0) continue;
        std::vector<std::size_t> queue{seed}; visited[seed] = true;
        std::vector<Cell> cells;
        std::uint64_t count = 0, coreCount = 0;
        std::size_t coreCells = 0;
        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            if (head % 1024 == 0) check(stop);
            const auto i = queue[head]; const auto &key = keys[i]; const auto &cell = grid.at(key);
            count += cell.count; cells.push_back(cell);
            if (!workpieceGroup[i] || !index.covers(cell.sum / cell.count, attachedTolerance))
            { coreCount += cell.count; ++coreCells; }
            for (int x = -2; x <= 2; ++x) for (int y = -2; y <= 2; ++y) for (int z = -2; z <= 2; ++z)
            {
                const auto found = lookup.find(Key{key[0] + x, key[1] + y, key[2] + z});
                if (found == lookup.end()) continue;
                const auto j = found->second;
                if (!visited[j] && grid.at(keys[j]).count != 0) { visited[j] = true; queue.push_back(j); }
            }
        }
        // 独立物体保留距离过滤后的扫描范围。与工件连通的地面、夹具等仍需保留，
        // 但必须在更宽的扫描误差带以外有连续支持，避免生成板边噪声包络。
        if (count < 8 || cells.size() < 3 || coreCount < 8 || coreCells < 3) continue;
        if (isPlateResidual(cells, count, cloud.originMm, recognition, workpieceGroup[seed], attachedTolerance,
            tolerance, std::max(.25, std::hypot(noiseMm, spacing)), std::max({.25, noiseMm, .5 * spacing}), stop)) continue;
        if (progress) progress(0.5 + 0.49 * seed / std::max<std::size_t>(1, keys.size()), "生成独立避障包络：" + std::to_string(result.size() + 1));
        double hullResolution = resolution;
        auto shape = envelope(std::move(cells), hullResolution, padding, cloud.originMm, stop);
        result.push_back({std::move(shape), {count, hullResolution, padding}});
    }
    check(stop);
    if (progress) progress(1, "障碍物包络重建完成");
    return result;
}
} // namespace reverse::detail
