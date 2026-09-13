#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

#include "../env.h"
#include "../includes.h"
#include "../loaders.h"
#include "../nucleas.h"
#include "../yw_internal.h"
#include "../system/inivals.h"

namespace
{

constexpr int GROUND_DECAL_DEFAULT_LIMIT = 256;
constexpr int GROUND_DECAL_MAX_TRIANGLES = 128;
constexpr int GROUND_DECAL_HARD_LIMIT = 1024;
constexpr float GROUND_DECAL_SURFACE_BIAS = 1.5f;
constexpr float GROUND_DECAL_MIN_NORMAL_Y = 0.05f;
constexpr float GROUND_DECAL_FLAT_TERRAIN_EPSILON = 2.0f;
constexpr float GROUND_DECAL_PI2 = 6.28318530717958647692f;

struct TGroundDecalClipVertex
{
    vec3d pos;
    float u = 0.0f;
    float v = 0.0f;
};

struct TGroundDecalShapePoint
{
    float u = 0.5f;
    float v = 0.5f;
};

static bool GroundDecalFinitePoint(const vec3d &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

static int GroundDecalLimit()
{
    int value = System::IniConf::GfxGroundDecalLimit.Get<int32_t>();
    if ( value < 0 )
        value = GROUND_DECAL_DEFAULT_LIMIT;

    return std::min(value, GROUND_DECAL_HARD_LIMIT);
}

static bool GroundDecalTerrainCell(NC_STACK_ypaworld *world,
                                   const Common::Point &cellId)
{
    // Decal eligibility is intentionally independent from PurposeType. The
    // authoritative rule is the actual world-collision surface hit at the
    // decal centre; special sectors therefore use the same terrain path as
    // ordinary sectors.
    return world->IsGamePlaySector(cellId);
}

static bool GroundDecalTerrainLocation(NC_STACK_ypaworld *world,
                                       const Common::Point &cellId,
                                       int collisionType)
{
    if ( !GroundDecalTerrainCell(world, cellId) )
        return false;

    if ( collisionType == 1 )
        return true;
    if ( collisionType == 2 )
        return GroundDecalTerrainCell(world, cellId + Common::Point(-1, 0));
    if ( collisionType == 3 )
        return GroundDecalTerrainCell(world, cellId + Common::Point(0, -1));
    if ( collisionType == 4 )
    {
        return GroundDecalTerrainCell(world, cellId + Common::Point(-1, 0)) &&
               GroundDecalTerrainCell(world, cellId + Common::Point(0, -1)) &&
               GroundDecalTerrainCell(world, cellId + Common::Point(-1, -1));
    }

    return false;
}

static bool GroundDecalFlatLegoTerrain(NC_STACK_ypaworld *world,
                                       const Common::Point &cellId,
                                       const std::vector<vec3d> &points)
{
    if ( points.size() < 3 )
        return false;

    const float terrainY = world->_cells(cellId).height;
    for (const vec3d &point : points)
    {
        if ( !GroundDecalFinitePoint(point) ||
             std::fabs(point.y - terrainY) > GROUND_DECAL_FLAT_TERRAIN_EPSILON )
            return false;
    }

    return true;
}

static bool GroundDecalCentralHitIsTerrain(NC_STACK_ypaworld *world,
                                           const ypaworld_arg136 &hit,
                                           const std::vector<vec3d> &points,
                                           const vec3d &normal)
{
    if ( points.size() < 3 ||
         !GroundDecalTerrainLocation(world, hit.hitCell, hit.hitCollisionType) ||
         std::fabs(normal.y) < GROUND_DECAL_MIN_NORMAL_Y )
        return false;

    // Type 1 LEGO collision skeletons contain both the sector ground and the
    // geometry built on top of it. Only the flat base at cell.height is a
    // valid decal surface. Terrain fillers (types 2/3/4) remain accepted so
    // normal sector height transitions/depressions keep working.
    if ( hit.hitCollisionType == 1 &&
         !GroundDecalFlatLegoTerrain(world, hit.hitCell, points) )
        return false;

    return true;
}

static bool GroundDecalPolygonIsTerrain(NC_STACK_ypaworld *world,
                                        const TSectorCollision &location,
                                        const std::vector<vec3d> &points,
                                        const vec3d &normal,
                                        const vec3d &centralNormal)
{
    if ( points.size() < 3 || std::fabs(normal.y) < GROUND_DECAL_MIN_NORMAL_Y ||
         normal.y * centralNormal.y <= 0.0f )
        return false;

    if ( location.CollisionType == 1 &&
         !GroundDecalFlatLegoTerrain(world, location.Cell, points) )
        return false;

    return true;
}

static uint32_t GroundDecalMixSeed(uint32_t seed, uint32_t value)
{
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
}

static float GroundDecalRotation(const NC_STACK_ypaworld *world,
                                 const ypaworld_arg136 &hit,
                                 bool randomRotation)
{
    if ( !randomRotation )
        return 0.0f;

    uint32_t seed = 0x811c9dc5u;
    seed = GroundDecalMixSeed(seed, (uint32_t)world->_timeStamp);
    seed = GroundDecalMixSeed(seed, (uint32_t)(int32_t)std::lround(hit.isectPos.x));
    seed = GroundDecalMixSeed(seed, (uint32_t)(int32_t)std::lround(hit.isectPos.z));
    seed = GroundDecalMixSeed(seed, (uint32_t)hit.polyID);
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;

    return (float)(seed & 0x00ffffffu) * (GROUND_DECAL_PI2 / 16777216.0f);
}

static uint32_t GroundDecalNextRandom(uint32_t *seed)
{
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

static void GroundDecalBuildShape(int points,
                                  float jaggedness,
                                  uint32_t seed,
                                  std::vector<TGroundDecalShapePoint> *shape)
{
    shape->clear();
    shape->reserve((size_t)points);

    const float center = 0.5f;
    const float baseRadius = 0.42f;
    jaggedness = std::max(0.0f, std::min(jaggedness, 1.0f));

    std::vector<float> variations((size_t)points);
    for (float &variation : variations)
    {
        const float random = (float)(GroundDecalNextRandom(&seed) & 0x00ffffffu) /
                             16777216.0f;
        variation = random * 2.0f - 1.0f;
    }

    // Correlate neighbouring radii so jaggedness creates an irregular stain
    // instead of a saw-tooth star. Two passes retain broad variation while
    // removing isolated spikes.
    std::vector<float> smoothed(variations.size());
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < points; ++i)
        {
            const int previous = (i + points - 1) % points;
            const int next = (i + 1) % points;
            smoothed[(size_t)i] = variations[(size_t)previous] * 0.25f +
                                  variations[(size_t)i] * 0.50f +
                                  variations[(size_t)next] * 0.25f;
        }
        variations.swap(smoothed);
    }

    // Smoothing also shrinks the requested variation. Recenter and restore its
    // range so jaggedness remains visible as broad lobes, not sharp spikes.
    float variationAverage = 0.0f;
    for (float variation : variations)
        variationAverage += variation;
    variationAverage /= (float)points;

    float variationExtent = 0.0f;
    for (float variation : variations)
        variationExtent = std::max(variationExtent,
                                   std::fabs(variation - variationAverage));

    for (int i = 0; i < points; ++i)
    {
        const float angleRandom = (float)(GroundDecalNextRandom(&seed) & 0x00ffffffu) /
                                  16777216.0f;
        const float angleStep = GROUND_DECAL_PI2 / (float)points;
        const float angleJitter = (angleRandom * 2.0f - 1.0f) *
                                  angleStep * jaggedness * 0.20f;
        const float variation = variationExtent > 0.0001f
                              ? (variations[(size_t)i] - variationAverage) /
                                    variationExtent
                              : 0.0f;
        const float radius = std::max(0.20f,
                                      std::min(baseRadius *
                                                   (1.0f + variation *
                                                               jaggedness * 0.75f),
                                               0.47f));
        const float angle = angleStep * (float)i + angleJitter;

        TGroundDecalShapePoint point;
        point.u = center + std::cos(angle) * radius;
        point.v = center + std::sin(angle) * radius;
        shape->push_back(point);
    }
}

static uint32_t GroundDecalShapeSeed(const ypaworld_arg136 &hit, int points)
{
    uint32_t seed = 0x6d2b79f5u;
    seed = GroundDecalMixSeed(seed, (uint32_t)(int32_t)std::lround(hit.isectPos.x));
    seed = GroundDecalMixSeed(seed, (uint32_t)(int32_t)std::lround(hit.isectPos.z));
    seed = GroundDecalMixSeed(seed, (uint32_t)hit.polyID);
    seed = GroundDecalMixSeed(seed, (uint32_t)points);
    return seed ? seed : 0x1u;
}

static float GroundDecalCross2D(float ax, float ay, float bx, float by)
{
    return ax * by - ay * bx;
}

static float GroundDecalShapeBoundaryRadius(
    const std::vector<TGroundDecalShapePoint> &shape,
    float u,
    float v)
{
    const float center = 0.5f;
    const float dx = u - center;
    const float dy = v - center;
    const float radius = std::sqrt(dx * dx + dy * dy);
    if ( radius <= 0.0001f )
        return 1.0f;

    float boundary = 1.0f;
    for (size_t i = 0; i < shape.size(); ++i)
    {
        const TGroundDecalShapePoint &a = shape[i];
        const TGroundDecalShapePoint &b = shape[(i + 1) % shape.size()];
        const float edgeX = b.u - a.u;
        const float edgeY = b.v - a.v;
        const float denominator = GroundDecalCross2D(dx, dy, edgeX, edgeY);
        if ( std::fabs(denominator) <= 0.000001f )
            continue;

        const float offsetX = a.u - center;
        const float offsetY = a.v - center;
        const float alongRay = GroundDecalCross2D(offsetX, offsetY, edgeX, edgeY) /
                               denominator;
        const float alongEdge = GroundDecalCross2D(offsetX, offsetY, dx, dy) /
                                denominator;
        if ( alongRay >= 0.0f && alongEdge >= -0.0001f && alongEdge <= 1.0001f )
            boundary = std::min(boundary, alongRay);
    }

    return std::max(boundary, radius);
}

static GFX::TGLColor GroundDecalProceduralColor(
    const std::vector<TGroundDecalShapePoint> &shape,
    const TGroundDecalClipVertex &point,
    float edgeFade)
{
    const float dx = point.u - 0.5f;
    const float dy = point.v - 0.5f;
    const float radius = std::sqrt(dx * dx + dy * dy);
    const float boundary = GroundDecalShapeBoundaryRadius(shape, point.u, point.v);
    const float normalized = std::max(0.0f, std::min(radius / boundary, 1.0f));
    const float radial = normalized * normalized * (3.0f - 2.0f * normalized);
    const float darkness = 0.10f + radial * 0.12f;
    const bool inside = radius <= boundary + 0.0001f;
    float alpha = inside ? 1.0f : 0.0f;
    // edgeFade 0 = legacy hard edge; 10 = smooth falloff across the whole decal.
    const float fade = std::max(0.0f, std::min(edgeFade / 10.0f, 1.0f));
    if ( inside && fade > 0.0f )
    {
        const float start = 1.0f - fade;
        float t = (normalized - start) / std::max(fade, 0.0001f);
        t = std::max(0.0f, std::min(t, 1.0f));
        alpha = 1.0f - t * t * (3.0f - 2.0f * t);
    }
    return GFX::TGLColor(darkness, darkness, darkness, alpha);
}

static TGroundDecalClipVertex GroundDecalMakeClipVertex(const vec3d &point,
                                                        const vec3d &center,
                                                        float size,
                                                        float cosine,
                                                        float sine)
{
    const float dx = point.x - center.x;
    const float dz = point.z - center.z;

    TGroundDecalClipVertex out;
    out.pos = point;
    out.u = 0.5f + (dx * cosine + dz * sine) / size;
    out.v = 0.5f + (-dx * sine + dz * cosine) / size;
    return out;
}

static void GroundDecalClipConvexEdge(
    std::vector<TGroundDecalClipVertex> *polygon,
    const TGroundDecalShapePoint &edgeStart,
    const TGroundDecalShapePoint &edgeEnd)
{
    if ( polygon->empty() )
        return;

    std::vector<TGroundDecalClipVertex> output;
    output.reserve(polygon->size() + 1);

    TGroundDecalClipVertex previous = polygon->back();
    const float edgeX = edgeEnd.u - edgeStart.u;
    const float edgeY = edgeEnd.v - edgeStart.v;
    float previousSide = GroundDecalCross2D(edgeX, edgeY,
                                            previous.u - edgeStart.u,
                                            previous.v - edgeStart.v);
    bool previousInside = previousSide >= -0.000001f;

    for (const TGroundDecalClipVertex &current : *polygon)
    {
        const float currentSide = GroundDecalCross2D(edgeX, edgeY,
                                                     current.u - edgeStart.u,
                                                     current.v - edgeStart.v);
        const bool currentInside = currentSide >= -0.000001f;

        if ( currentInside != previousInside )
        {
            const float denominator = previousSide - currentSide;
            if ( std::fabs(denominator) > 0.000001f )
            {
                float amount = previousSide / denominator;
                amount = std::max(0.0f, std::min(amount, 1.0f));

                TGroundDecalClipVertex intersection;
                intersection.pos = previous.pos + (current.pos - previous.pos) * amount;
                intersection.u = previous.u + (current.u - previous.u) * amount;
                intersection.v = previous.v + (current.v - previous.v) * amount;
                output.push_back(intersection);
            }
        }

        if ( currentInside )
            output.push_back(current);

        previous = current;
        previousSide = currentSide;
        previousInside = currentInside;
    }

    polygon->swap(output);
}

static bool GroundDecalAppendClippedRegion(
    const std::vector<TGroundDecalClipVertex> &source,
    const std::vector<TGroundDecalShapePoint> &region,
    const vec3d &normal,
    const vec3d &center,
    const std::vector<TGroundDecalShapePoint> &shape,
    float edgeFade,
    int maxTriangles,
    std::vector<GFX::TVertex> *vertices,
    std::vector<GFX::IndexType> *indices,
    bool *limitExceeded)
{
    std::vector<TGroundDecalClipVertex> polygon = source;
    for (size_t i = 0; i < region.size() && !polygon.empty(); ++i)
    {
        GroundDecalClipConvexEdge(&polygon, region[i],
                                  region[(i + 1) % region.size()]);
    }
    if ( polygon.size() < 3 )
        return false;

    const vec3d bias = normal * (normal.y >= 0.0f
                                ? -GROUND_DECAL_SURFACE_BIAS
                                : GROUND_DECAL_SURFACE_BIAS);
    bool appended = false;
    for (size_t i = 1; i + 1 < polygon.size(); ++i)
    {
        TGroundDecalClipVertex triangle[3] = {polygon[0], polygon[i], polygon[i + 1]};
        vec3d edgeNormal = (triangle[1].pos - triangle[0].pos) *
                           (triangle[2].pos - triangle[0].pos);
        if ( !GroundDecalFinitePoint(edgeNormal) || edgeNormal.length() <= 0.0001f )
            continue;

        if ( edgeNormal.dot(normal) < 0.0f )
            std::swap(triangle[1], triangle[2]);

        if ( indices->size() / 3 >= (size_t)maxTriangles )
        {
            *limitExceeded = true;
            return false;
        }

        const GFX::IndexType first = (GFX::IndexType)vertices->size();
        for (const TGroundDecalClipVertex &point : triangle)
        {
            const vec3d biasedPoint = point.pos + bias;
            vertices->emplace_back(vec3f(biasedPoint - center),
                                   tUtV(point.u, point.v),
                                   GroundDecalProceduralColor(shape, point, edgeFade));
        }

        indices->push_back(first);
        indices->push_back(first + 2);
        indices->push_back(first + 1);
        appended = true;
    }

    return appended;
}

static bool GroundDecalAppendClippedTriangle(const vec3d &a,
                                             const vec3d &b,
                                             const vec3d &c,
                                             const vec3d &normal,
                                             const vec3d &center,
                                             float size,
                                             float cosine,
                                             float sine,
                                             const std::vector<TGroundDecalShapePoint> &shape,
                                             float edgeFade,
                                             int maxTriangles,
                                             std::vector<GFX::TVertex> *vertices,
                                             std::vector<GFX::IndexType> *indices,
                                             bool *limitExceeded)
{
    std::vector<TGroundDecalClipVertex> source;
    source.reserve(3);
    source.push_back(GroundDecalMakeClipVertex(a, center, size, cosine, sine));
    source.push_back(GroundDecalMakeClipVertex(b, center, size, cosine, sine));
    source.push_back(GroundDecalMakeClipVertex(c, center, size, cosine, sine));

    const TGroundDecalShapePoint shapeCenter;
    bool appended = false;

    for (size_t shapeIndex = 0; shapeIndex < shape.size(); ++shapeIndex)
    {
        const TGroundDecalShapePoint &shapeStart = shape[shapeIndex];
        const TGroundDecalShapePoint &shapeEnd = shape[(shapeIndex + 1) % shape.size()];
        const std::vector<TGroundDecalShapePoint> fullWedge = {
            shapeCenter, shapeStart, shapeEnd};

        if ( GroundDecalAppendClippedRegion(source, fullWedge, normal, center,
                                            shape, edgeFade, maxTriangles, vertices, indices,
                                            limitExceeded) )
            appended = true;

        if ( *limitExceeded )
            return false;
    }

    return appended;
}

static bool GroundDecalAppendPolygon(const std::vector<vec3d> &points,
                                     const vec3d &normal,
                                     const vec3d &center,
                                     float size,
                                     float cosine,
                                     float sine,
                                     const std::vector<TGroundDecalShapePoint> &shape,
                                     float edgeFade,
                                     int maxTriangles,
                                     std::vector<GFX::TVertex> *vertices,
                                     std::vector<GFX::IndexType> *indices,
                                     bool *limitExceeded)
{
    bool appended = false;
    for (size_t i = 1; i + 1 < points.size(); ++i)
    {
        if ( GroundDecalAppendClippedTriangle(points[0], points[i], points[i + 1],
                                              normal, center, size, cosine, sine,
                                              shape, edgeFade, maxTriangles, vertices, indices,
                                              limitExceeded) )
            appended = true;

        if ( *limitExceeded )
            return false;
    }

    return appended;
}

static int GroundDecalMicroX(float worldX)
{
    return (int)((worldX + 150.0f) / 300.0f);
}

static int GroundDecalMicroZ(float worldZ)
{
    return (int)((-worldZ + 150.0f) / 300.0f);
}

static bool GroundDecalBuildGeometry(NC_STACK_ypaworld *world,
                                     const ypaworld_arg136 &hit,
                                     float size,
                                     float angle,
                                     int shapePoints,
                                     float jaggedness,
                                     float edgeFade,
                                     int maxTriangles,
                                     std::vector<GFX::TVertex> *vertices,
                                     std::vector<GFX::IndexType> *indices)
{
    if ( !hit.skel || hit.polyID < 0 ||
         (size_t)hit.polyID >= hit.skel->polygons.size() )
        return false;

    const UAskeleton::Polygon &centralPolygon = hit.skel->polygons[hit.polyID];
    if ( centralPolygon.num_vertices < 3 ||
         (size_t)centralPolygon.num_vertices > centralPolygon.v.size() )
        return false;

    vec3d centralNormal = centralPolygon.Normal();
    if ( !GroundDecalFinitePoint(centralNormal) ||
         centralNormal.normalise() <= 0.001f ||
         !GroundDecalFinitePoint(centralNormal) )
        return false;

    // Copy the temporary collision polygon before any filler/slurp preparation
    // can mutate the shared skeleton used by ypaworld_func136.
    std::vector<vec3d> centralPoints;
    centralPoints.reserve(centralPolygon.num_vertices);
    for (int i = 0; i < centralPolygon.num_vertices; ++i)
    {
        int vertexId = centralPolygon.v[i];
        if ( vertexId < 0 || (size_t)vertexId >= hit.skel->POO.size() )
            return false;

        vec3d point = hit.hitSkelPos + hit.skel->POO[vertexId];
        if ( !GroundDecalFinitePoint(point) )
            return false;
        centralPoints.push_back(point);
    }

    if ( !GroundDecalCentralHitIsTerrain(world, hit, centralPoints,
                                         centralNormal) )
        return false;

    std::vector<TGroundDecalShapePoint> shape;
    GroundDecalBuildShape(shapePoints, jaggedness,
                           GroundDecalShapeSeed(hit, shapePoints), &shape);
    if ( shape.size() < 3 )
        return false;

    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    bool limitExceeded = false;

    GroundDecalAppendPolygon(centralPoints, centralNormal, hit.isectPos, size,
                             cosine, sine, shape, edgeFade, maxTriangles,
                             vertices, indices, &limitExceeded);
    if ( limitExceeded )
        return false;

    const float halfSize = size * 0.5f;
    const float horizontalExtent = halfSize * (std::fabs(cosine) + std::fabs(sine));
    const float mapExtent = 1200.0f * (float)std::max(world->_mapSize.x,
                                                      world->_mapSize.y);
    if ( !std::isfinite(horizontalExtent) || horizontalExtent <= 0.0f ||
         !std::isfinite(mapExtent) || mapExtent <= 0.0f ||
         horizontalExtent > mapExtent )
        return false;

    int minMicroX = GroundDecalMicroX(hit.isectPos.x - horizontalExtent) - 1;
    int maxMicroX = GroundDecalMicroX(hit.isectPos.x + horizontalExtent) + 1;
    int minMicroZ = GroundDecalMicroZ(hit.isectPos.z + horizontalExtent) - 1;
    int maxMicroZ = GroundDecalMicroZ(hit.isectPos.z - horizontalExtent) + 1;

    minMicroX = std::max(minMicroX, 1);
    minMicroZ = std::max(minMicroZ, 1);
    maxMicroX = std::min(maxMicroX, 4 * world->_mapSize.x - 2);
    maxMicroZ = std::min(maxMicroZ, 4 * world->_mapSize.y - 2);
    if ( minMicroX > maxMicroX || minMicroZ > maxMicroZ )
        return false;

    const int64_t locationCount = (int64_t)(maxMicroX - minMicroX + 1) *
                                  (int64_t)(maxMicroZ - minMicroZ + 1);
    const int64_t locationLimit = (int64_t)maxTriangles * 16 + 64;
    if ( locationCount > locationLimit )
        return false;

    const int centerMicroX = GroundDecalMicroX(hit.isectPos.x);
    const int centerMicroZ = GroundDecalMicroZ(hit.isectPos.z);
    typedef std::tuple<int, int, int, int, const NC_STACK_skeleton *> TLocationKey;
    std::set<TLocationKey> visited;

    for (int microZ = minMicroZ; microZ <= maxMicroZ; ++microZ)
    {
        for (int microX = minMicroX; microX <= maxMicroX; ++microX)
        {
            TSectorCollision location = world->sub_44DBF8(centerMicroX, centerMicroZ,
                                                          microX, microZ, 0);
            if ( !location.CollisionType || !location.sklt ||
                 !GroundDecalTerrainLocation(world, location.Cell, location.CollisionType) )
                continue;

            TLocationKey key(location.CollisionType,
                             (int)std::lround(location.pos.x),
                             (int)std::lround(location.pos.y),
                             (int)std::lround(location.pos.z),
                             location.sklt);
            if ( !visited.insert(key).second )
                continue;

            if ( location.CollisionType != 1 )
                world->sub_44E07C(location);
            if ( !location.sklt )
                continue;

            UAskeleton::Data *skeleton = location.sklt->GetSkelet();
            if ( !skeleton )
                continue;

            const bool centralLocation = skeleton == hit.skel &&
                                         (location.pos - hit.hitSkelPos).length() <= 0.001f;

            for (size_t polygonId = 0; polygonId < skeleton->polygons.size(); ++polygonId)
            {
                if ( centralLocation && (int)polygonId == hit.polyID )
                    continue;

                const UAskeleton::Polygon &polygon = skeleton->polygons[polygonId];
                if ( polygon.num_vertices < 3 ||
                     (size_t)polygon.num_vertices > polygon.v.size() )
                    continue;

                vec3d normal = polygon.Normal();
                if ( !GroundDecalFinitePoint(normal) ||
                     normal.normalise() <= 0.001f ||
                     !GroundDecalFinitePoint(normal) )
                    continue;

                std::vector<vec3d> points;
                points.reserve(polygon.num_vertices);
                bool valid = true;
                for (int i = 0; i < polygon.num_vertices; ++i)
                {
                    int vertexId = polygon.v[i];
                    if ( vertexId < 0 || (size_t)vertexId >= skeleton->POO.size() )
                    {
                        valid = false;
                        break;
                    }

                    vec3d point = location.pos + skeleton->POO[vertexId];
                    if ( !GroundDecalFinitePoint(point) )
                    {
                        valid = false;
                        break;
                    }
                    points.push_back(point);
                }

                if ( !valid ||
                     !GroundDecalPolygonIsTerrain(world, location, points,
                                                  normal, centralNormal) )
                    continue;

                GroundDecalAppendPolygon(points, normal, hit.isectPos, size,
                                         cosine, sine, shape, edgeFade, maxTriangles,
                                         vertices, indices, &limitExceeded);
                if ( limitExceeded )
                    return false;
            }
        }
    }

    return !indices->empty();
}

static NC_STACK_bitmap *GroundDecalTexture(NC_STACK_ypaworld *world,
                                           const std::string &path)
{
    auto found = world->_groundDecalTextures.find(path);
    if ( found != world->_groundDecalTextures.end() )
        return found->second;

    std::string oldRsrc = Common::Env.SetPrefix("rsrc", "data:");
    NC_STACK_bitmap *texture = Utils::ProxyLoadImage({
        {NC_STACK_rsrc::RSRC_ATT_NAME, path},
        {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
        {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1}});
    Common::Env.SetPrefix("rsrc", oldRsrc);

    if ( texture && texture->GetBitmap() )
        texture->PrepareTexture(false);
    else
        Common::DeleteAndNull(&texture);

    world->_groundDecalTextures[path] = texture;
    return texture;
}

static NC_STACK_bitmap *GroundDecalBakedEdgeFadeTexture(NC_STACK_ypaworld *world,
                                                        const std::string &path,
                                                        NC_STACK_bitmap *loaded,
                                                        float edgeFade)
{
    // Bakes the radial edge fade into a cached texture copy, pixel by pixel.
    // Unlike the per-vertex fade, this does not depend on terrain tessellation
    // density, so the falloff stays exact even when the whole decal sits inside
    // a single large collision polygon. Static single-frame textures only; any
    // failure falls back to the unmodified texture without breaking the decal.
    if ( !world || !loaded || !loaded->GetBitmap() || !(edgeFade > 0.0f) ||
         loaded->GetFramesCount() != 1 )
        return loaded;

    const std::string key = path + "#edgefade=" + std::to_string(edgeFade);
    auto found = world->_groundDecalTextures.find(key);
    if ( found != world->_groundDecalTextures.end() )
        return found->second;

    NC_STACK_bitmap *result = loaded;
    SDL_Surface *src = loaded->GetSwTex();

    if ( src && src->w > 0 && src->h > 0 )
    {
        SDL_Surface *work = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
        if ( work )
        {
            const float fade = std::max(0.0f, std::min(edgeFade / 10.0f, 1.0f));
            const float start = 1.0f - fade;
            const float span = std::max(fade, 0.0001f);

            bool ok = !SDL_MUSTLOCK(work);
            if ( !ok )
                ok = SDL_LockSurface(work) == 0;

            if ( ok )
            {
                for (int y = 0; y < work->h; ++y)
                {
                    Uint8 *row = (Uint8 *)work->pixels + (size_t)y * (size_t)work->pitch;
                    for (int x = 0; x < work->w; ++x)
                    {
                        Uint32 *slot = (Uint32 *)(row + (size_t)x * 4u);
                        Uint8 r, g, b, a;
                        SDL_GetRGBA(*slot, work->format, &r, &g, &b, &a);
                        const float dx = ((float)x + 0.5f) / (float)work->w - 0.5f;
                        const float dy = ((float)y + 0.5f) / (float)work->h - 0.5f;
                        float rn = std::sqrt(dx * dx + dy * dy) * 2.0f;
                        rn = std::max(0.0f, std::min(rn, 1.0f));
                        float t = (rn - start) / span;
                        t = std::max(0.0f, std::min(t, 1.0f));
                        const float mult = 1.0f - t * t * (3.0f - 2.0f * t);
                        a = (Uint8)((float)a * mult + 0.5f);
                        *slot = SDL_MapRGBA(work->format, r, g, b, a);
                    }
                }

                if ( SDL_MUSTLOCK(work) )
                    SDL_UnlockSurface(work);

                NC_STACK_bitmap *baked = Nucleus::CInit<NC_STACK_bitmap>({
                    {NC_STACK_rsrc::RSRC_ATT_NAME, key},
                    {NC_STACK_rsrc::RSRC_ATT_TRYSHARED, (int32_t)1},
                    {NC_STACK_bitmap::BMD_ATT_WIDTH, (int32_t)work->w},
                    {NC_STACK_bitmap::BMD_ATT_HEIGHT, (int32_t)work->h}});
                if ( baked && baked->GetSwTex() )
                {
                    // Copy semantics: the fade must be transferred exactly,
                    // never blended with the fresh surface.
                    SDL_SetSurfaceBlendMode(work, SDL_BLENDMODE_NONE);
                    if ( SDL_BlitSurface(work, NULL, baked->GetSwTex(), NULL) == 0 )
                    {
                        baked->PrepareTexture(false);
                        result = baked;
                    }
                    else
                    {
                        baked->Delete();
                    }
                }
                else if ( baked )
                {
                    baked->Delete();
                }
            }

            SDL_FreeSurface(work);
        }
    }

    // Cache only genuinely new bitmaps: storing the fallback twice would
    // double-delete it in ClearGroundDecals.
    if ( result != loaded )
        world->_groundDecalTextures[key] = result;
    return result;
}

}

bool NC_STACK_ypaworld::SpawnGroundDecal(const World::TChainFXConfig &config,
                                         const ypaworld_arg136 &hit)
{
    const int limit = GroundDecalLimit();
    const int maxTriangles = GROUND_DECAL_MAX_TRIANGLES;
    if ( _isNetGame || limit <= 0 || maxTriangles <= 0 ||
         config.mode != World::TChainFXConfig::MODE_GROUND_DECAL ||
         config.trigger != World::TChainFXConfig::TRIGGER_IMPACT_WORLD ||
         config.ground_decal_points < 3 || config.ground_decal_points > 32 ||
         !std::isfinite(config.ground_decal_size) || config.ground_decal_size <= 0.0f ||
         !std::isfinite(config.ground_decal_jaggedness) ||
         config.ground_decal_jaggedness < 0.0f || config.ground_decal_jaggedness > 1.0f ||
         config.duration <= 0 || config.ground_decal_tint.a <= 0.0f ||
         !hit.isect || !GroundDecalFinitePoint(hit.isectPos) )
        return false;

    const float angle = GroundDecalRotation(this, hit,
                                            config.ground_decal_random_rotation);
    const float edgeFade = std::isfinite(config.ground_decal_edge_fade)
                             ? std::max(0.0f, std::min(config.ground_decal_edge_fade, 10.0f))
                             : 0.0f;
    float buildFade = edgeFade;
    NC_STACK_bitmap *texture = NULL;
    // Textured decals carry the edge fade inside the baked texture copy, so
    // the falloff is pixel-exact at any terrain tessellation; the vertex mask
    // then stays hard to avoid fading twice. Untextured decals only have the
    // per-vertex fade.
    if ( !config.ground_decal_texture.empty() )
    {
        texture = GroundDecalTexture(this, config.ground_decal_texture);
        if ( texture && texture->GetBitmap() && edgeFade > 0.0f )
        {
            NC_STACK_bitmap *baked = GroundDecalBakedEdgeFadeTexture(
                this, config.ground_decal_texture, texture, edgeFade);
            if ( baked && baked != texture )
            {
                texture = baked;
                buildFade = 0.0f;
            }
        }
        if ( !texture || !texture->GetBitmap() )
            return false;
    }

    std::vector<GFX::TVertex> vertices;
    std::vector<GFX::IndexType> indices;
    vertices.reserve((size_t)maxTriangles * 3);
    indices.reserve((size_t)maxTriangles * 3);

    if ( !GroundDecalBuildGeometry(this, hit, config.ground_decal_size, angle,
                                   config.ground_decal_points,
                                   config.ground_decal_jaggedness,
                                   buildFade,
                                   maxTriangles, &vertices, &indices) )
        return false;

    if ( texture )
    {
        // Textured decals preserve the procedural alpha mask but leave RGB
        // neutral so the scorch detail is not darkened a second time.
        for (GFX::TVertex &vertex : vertices)
        {
            vertex.Color.r = 1.0f;
            vertex.Color.g = 1.0f;
            vertex.Color.b = 1.0f;
        }
    }

    while ( _groundDecals.size() >= (size_t)limit )
        _groundDecals.pop_front();

    _groundDecals.emplace_back();
    TGroundDecal &decal = _groundDecals.back();
    decal.pos = hit.isectPos;
    decal.radius = config.ground_decal_size * 0.70710678118f;
    decal.startTime = _timeStamp;
    decal.duration = config.duration;
    decal.fadeOut = std::min(std::max(config.fade_out, 0), decal.duration);
    decal.fadeIn = std::min(std::max(config.fade_in, 0), decal.duration - decal.fadeOut);
    decal.tint = config.ground_decal_tint;
    decal.tint.Clamp();
    decal.mesh.Vertexes = std::move(vertices);
    decal.mesh.Indixes = std::move(indices);
    uint32_t renderFlags = GFX::RFLAGS_FOG |
                                        GFX::RFLAGS_DISABLE_ZWRITE |
                                        GFX::RFLAGS_ALPHABLEND;
    if ( texture )
        renderFlags |= GFX::RFLAGS_TEXTURED;
    decal.mesh.Mat = GFX::TRenderParams(renderFlags);
    decal.mesh.Mat.Tex = texture ? texture->GetBitmap() : NULL;
    decal.mesh.RecalcBoundBox();
    GFX::Engine.MeshMakeVBO(&decal.mesh);
    return true;
}

void NC_STACK_ypaworld::RenderGroundDecals(baseRender_msg *arg)
{
    const int limit = GroundDecalLimit();
    if ( limit <= 0 )
    {
        _groundDecals.clear();
        return;
    }

    while ( _groundDecals.size() > (size_t)limit )
        _groundDecals.pop_front();

    TF::TForm3D *view = TF::Engine.GetViewPoint();
    if ( !view )
        return;

    for (auto it = _groundDecals.begin(); it != _groundDecals.end(); )
    {
        const int32_t age = std::max(0, _timeStamp - it->startTime);
        if ( age >= it->duration )
        {
            it = _groundDecals.erase(it);
            continue;
        }

        mat4x4 world;
        world += it->pos;
        mat4x4 transform = view->CalcSclRot;
        transform *= (world - view->CalcPos);
        const float distance = transform.getTranslate().length();

        if ( distance <= (float)_normalVizLimit + it->radius )
        {
            const float fade = World::ComputeVPFadeEnvelope((double)age,
                                                             (double)it->duration,
                                                             (double)it->fadeIn,
                                                             (double)it->fadeOut);
            GFX::TRenderNode &render = GFX::Engine.AllocRenderNode();
            render = GFX::TRenderNode(GFX::TRenderNode::TYPE_MESH);
            render.Mesh = &it->mesh;
            render.Tex = it->mesh.Mat.Tex;
            render.Flags = it->mesh.Mat.Flags | arg->flags;
            render.Color = it->mesh.Mat.Color;
            render.ColorMul = GFX::TGLColor(it->tint.r, it->tint.g, it->tint.b,
                                            it->tint.a * fade);
            render.Colorize = it->tint.ColorizesRGB();
            render.TForm = transform;
            render.Distance = distance;
            render.TimeStamp = arg->globTime;
            render.FrameTime = arg->frameTime;
            render.FogStart = (float)(_normalVizLimit - _normalFadeLength);
            render.FogLength = (float)_normalFadeLength;

            arg->adeCount += it->mesh.Indixes.size() / 3;
            GFX::Engine.QueueRenderMesh(&render);
        }

        ++it;
    }
}

void NC_STACK_ypaworld::ClearGroundDecals()
{
    _groundDecals.clear();

    for (auto &entry : _groundDecalTextures)
    {
        if ( entry.second )
            entry.second->Delete();
    }
    _groundDecalTextures.clear();
}
