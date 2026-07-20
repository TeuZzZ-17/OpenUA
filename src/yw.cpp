#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include "includes.h"
#include "yw.h"

#include "yw_internal.h"
#include "loaders.h"

#include "button.h"
#include "amesh.h"
#include "font.h"
#include "yparobo.h"
#include "windp.h"
#include "wav.h"
#include "yw_net.h"
#include "gui/uacommon.h"
#include "gui/uamsgbox.h"
#include "env.h"
#include "system/inivals.h"
#include "system/system.h"
#include "locale/locale.h"
#include "utils.h"

extern yw_infolog info_log;

int vertMenuSpace;
int dword_5A50B2;
int dword_5A50B2_h;
int word_5A50AE;
int word_5A50BC;
int word_5A50BA;
int word_5A50BE;
int buttonsSpace;
int checkBoxWidth;

int dword_5A50B6;
int dword_5A50B6_h;

int scaledFontHeight;
int bottomButtonsY;
int button1LineWidth;
int bottomSecondBtnPosX;
int bottomThirdBtnPosX;

GuiList stru_5C91D0;

uint32_t bact_id = 0x10000;

static constexpr uint32_t GEM_NEW_UI_NOTIFICATION_DURATION_MS = 8000;

// method 169
uint32_t dword_5A7A80;

static int yw_SelectMimicVehicleID(std::vector<World::TVhclProto> &protos, int shellVehicleId)
{
    if ( shellVehicleId <= 0 || (size_t)shellVehicleId >= protos.size() )
        return 0;

    World::TVhclProto &shell = protos[shellVehicleId];
    if ( !shell.is_mimic || shell.mimic_vehicle_list.empty() )
        return 0;

    std::vector<int16_t> validIds;
    validIds.reserve(shell.mimic_vehicle_list.size());

    for (int16_t vehicleId : shell.mimic_vehicle_list)
    {
        if ( vehicleId <= 0 || vehicleId == shellVehicleId || (size_t)vehicleId >= protos.size() )
            continue;

        World::TVhclProto &candidate = protos[vehicleId];
        if ( candidate.is_mimic || candidate.model_id == BACT_TYPES_NOPE )
            continue;

        validIds.push_back(vehicleId);
    }

    if ( validIds.empty() )
        return 0;

    size_t randomIndex = (size_t)(rand() % (int)validIds.size());
    return validIds[randomIndex];
}

struct yw_ModelPointCloud
{
    std::vector<vec3d> points;
    vec3d min;
    vec3d max;
    bool valid = false;
};

static mat3x3 yw_BuildVPRotationMatrix(const vec3d &degrees)
{
    vec3d angle = degrees * C_PI_180;
    mat3x3 rot = mat3x3::Ident();

    if ( angle.x != 0.0 )
        rot *= mat3x3::RotateX(angle.x);
    if ( angle.y != 0.0 )
        rot *= mat3x3::RotateY(angle.y);
    if ( angle.z != 0.0 )
        rot *= mat3x3::RotateZ(angle.z);

    return rot;
}

static vec3d yw_SafeVPScale(const vec3d &scale)
{
    return vec3d(scale.x > 0.001 ? scale.x : 1.0,
                 scale.y > 0.001 ? scale.y : 1.0,
                 scale.z > 0.001 ? scale.z : 1.0);
}

static void yw_AddModelPoint(yw_ModelPointCloud *cloud, const vec3d &point)
{
    if ( !cloud->valid )
    {
        cloud->min = point;
        cloud->max = point;
        cloud->valid = true;
    }
    else
    {
        cloud->min.x = std::min(cloud->min.x, point.x);
        cloud->min.y = std::min(cloud->min.y, point.y);
        cloud->min.z = std::min(cloud->min.z, point.z);
        cloud->max.x = std::max(cloud->max.x, point.x);
        cloud->max.y = std::max(cloud->max.y, point.y);
        cloud->max.z = std::max(cloud->max.z, point.z);
    }

    cloud->points.push_back(point);
}

static void yw_CollectVPModelPoints(NC_STACK_base *base, const mat3x3 &rot, const vec3d &pos,
                                    yw_ModelPointCloud *cloud, bool solidMaterialsOnly)
{
    if ( !base )
        return;

    if ( NC_STACK_skeleton *skel = base->GetSkeleton() )
    {
        UAskeleton::Data *data = skel->GetSkelet();
        if ( data )
        {
            bool sampledPolygons = false;

            std::vector<bool> eligiblePolygons;
            if ( solidMaterialsOnly )
            {
                eligiblePolygons.resize(data->polygons.size(), false);

                for (NC_STACK_ade *ade : *base->GetAdeList())
                {
                    NC_STACK_area *area = dynamic_cast<NC_STACK_area *>(ade);
                    if ( !area || area->getAREA_tracy() == 2 )
                        continue;

                    if ( NC_STACK_amesh *amesh = dynamic_cast<NC_STACK_amesh *>(area) )
                    {
                        for (const NC_STACK_amesh::ATTS &att : amesh->atts)
                        {
                            if ( att.polyID >= 0 && (size_t)att.polyID < eligiblePolygons.size() )
                                eligiblePolygons[att.polyID] = true;
                        }
                    }
                    else
                    {
                        int polyID = area->getADE_poly();
                        if ( polyID >= 0 && (size_t)polyID < eligiblePolygons.size() )
                            eligiblePolygons[polyID] = true;
                    }
                }
            }

            for (size_t polyID = 0; polyID < data->polygons.size(); polyID++)
            {
                if ( solidMaterialsOnly && !eligiblePolygons[polyID] )
                    continue;

                const UAskeleton::Polygon &poly = data->polygons[polyID];
                if ( poly.num_vertices < 3 )
                    continue;

                vec3d center(0.0, 0.0, 0.0);
                bool valid = true;
                for (int i = 0; i < poly.num_vertices; i++)
                {
                    int index = poly.v[i];
                    if ( index < 0 || (size_t)index >= data->POO.size() )
                    {
                        valid = false;
                        break;
                    }
                    center += static_cast<vec3d>(data->POO[index]);
                }

                if ( !valid )
                    continue;

                center /= (float)poly.num_vertices;
                yw_AddModelPoint(cloud, pos + rot.Transform(center));

                vec3d a = static_cast<vec3d>(data->POO[poly.v[0]]);
                for (int triangle = 1; triangle < poly.num_vertices - 1; triangle++)
                {
                    vec3d b = static_cast<vec3d>(data->POO[poly.v[triangle]]);
                    vec3d c = static_cast<vec3d>(data->POO[poly.v[triangle + 1]]);
                    float longestEdge = std::max((float)(b - a).length(),
                                                 std::max((float)(c - b).length(), (float)(a - c).length()));
                    int divisions = (int)ceil(longestEdge / 18.0f);
                    if ( divisions < 1 )
                        divisions = 1;
                    if ( divisions > 8 )
                        divisions = 8;

                    for (int row = 0; row <= divisions; row++)
                    {
                        for (int column = 0; column <= divisions - row; column++)
                        {
                            float u = (float)row / (float)divisions;
                            float v = (float)column / (float)divisions;
                            vec3d sample = a + (b - a) * u + (c - a) * v;
                            yw_AddModelPoint(cloud, pos + rot.Transform(sample));
                        }
                    }
                }

                sampledPolygons = true;
            }

            // A few simple legacy VPs have no polygon table. POO is the only
            // useful geometry in that case; SEN remains excluded because it is
            // metadata and often lies far outside the rendered model.
            if ( !sampledPolygons && !solidMaterialsOnly )
            {
                for (const UAskeleton::Vertex &v : data->POO)
                    yw_AddModelPoint(cloud, pos + rot.Transform(static_cast<vec3d>(v)));
            }
        }
    }

    for (NC_STACK_base *kid : base->GetKidList())
    {
        TF::TForm3D &tf = kid->TForm();
        mat3x3 kidRot = rot * tf.SclRot;
        vec3d kidPos = pos + rot.Transform(tf.Pos);
        yw_CollectVPModelPoints(kid, kidRot, kidPos, cloud, solidMaterialsOnly);
    }
}

static void yw_CollectVPPointCloud(NC_STACK_base *vp, const vec3d &scale, const vec3d &orientation,
                                   yw_ModelPointCloud *cloud, bool solidMaterialsOnly)
{
    if ( !vp )
        return;

    yw_ModelPointCloud raw;
    yw_CollectVPModelPoints(vp, mat3x3::Ident(), vec3d(0.0, 0.0, 0.0), &raw, solidMaterialsOnly);

    // Some energy effects and legacy VPs have no opaque/chroma-keyed material
    // geometry. Preserve their previous automatic collision as a safe fallback.
    if ( solidMaterialsOnly && !raw.valid )
        yw_CollectVPModelPoints(vp, mat3x3::Ident(), vec3d(0.0, 0.0, 0.0), &raw, false);

    if ( !raw.valid )
        return;

    mat3x3 vpTransform = yw_BuildVPRotationMatrix(orientation);
    vpTransform *= mat3x3::Scale(yw_SafeVPScale(scale));

    for (const vec3d &p : raw.points)
        yw_AddModelPoint(cloud, vpTransform.Transform(p));
}

static vec3d yw_Cross3(const vec3d &a, const vec3d &b)
{
    return vec3d(a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
}

static bool yw_IsFinitePoint(const vec3d &point)
{
    return isfinite(point.x) && isfinite(point.y) && isfinite(point.z);
}

static void yw_AddAttachedFXTriangle(NC_STACK_ypaworld::TAttachedFXGeometryCache *cache,
                                     yw_ModelPointCloud *bounds,
                                     const vec3d &a, const vec3d &b, const vec3d &c)
{
    if ( !cache || !bounds || !yw_IsFinitePoint(a) || !yw_IsFinitePoint(b) || !yw_IsFinitePoint(c) )
        return;

    float area = (float)yw_Cross3(b - a, c - a).length() * 0.5f;
    if ( !isfinite(area) || area <= 0.0001f )
        return;

    cache->totalArea += area;
    NC_STACK_ypaworld::TAttachedFXTriangle triangle;
    triangle.a = a;
    triangle.b = b;
    triangle.c = c;
    triangle.cumulativeArea = cache->totalArea;
    cache->triangles.push_back(triangle);

    yw_AddModelPoint(bounds, a);
    yw_AddModelPoint(bounds, b);
    yw_AddModelPoint(bounds, c);
}

static void yw_CollectAttachedFXTriangles(NC_STACK_base *base, const mat3x3 &rot, const vec3d &pos,
                                          NC_STACK_ypaworld::TAttachedFXGeometryCache *cache,
                                          yw_ModelPointCloud *bounds)
{
    if ( !base || !cache || !bounds )
        return;

    for (const GFX::TMesh &mesh : base->Meshes)
    {
        for (size_t index = 0; index + 2 < mesh.Indixes.size(); index += 3)
        {
            size_t ia = mesh.Indixes[index];
            size_t ib = mesh.Indixes[index + 1];
            size_t ic = mesh.Indixes[index + 2];
            if ( ia >= mesh.Vertexes.size() || ib >= mesh.Vertexes.size() || ic >= mesh.Vertexes.size() )
                continue;

            vec3d a = pos + rot.Transform(static_cast<vec3d>(mesh.Vertexes[ia].Pos));
            vec3d b = pos + rot.Transform(static_cast<vec3d>(mesh.Vertexes[ib].Pos));
            vec3d c = pos + rot.Transform(static_cast<vec3d>(mesh.Vertexes[ic].Pos));
            yw_AddAttachedFXTriangle(cache, bounds, a, b, c);
        }
    }

    for (NC_STACK_base *kid : base->GetKidList())
    {
        TF::TForm3D &transform = kid->TForm();
        mat3x3 kidRot = rot * transform.SclRot;
        vec3d kidPos = pos + rot.Transform(transform.Pos);
        yw_CollectAttachedFXTriangles(kid, kidRot, kidPos, cache, bounds);
    }
}

static int yw_GetWeldedVertexID(const vec3d &point, double weld,
                                std::map<std::tuple<int64_t, int64_t, int64_t>, int> *vertices)
{
    std::tuple<int64_t, int64_t, int64_t> key(
        (int64_t)llround(point.x / weld),
        (int64_t)llround(point.y / weld),
        (int64_t)llround(point.z / weld));

    auto found = vertices->find(key);
    if ( found != vertices->end() )
        return found->second;

    int id = (int)vertices->size();
    (*vertices)[key] = id;
    return id;
}

static bool yw_IsClosedAttachedFXMesh(const NC_STACK_ypaworld::TAttachedFXGeometryCache &cache,
                                      const yw_ModelPointCloud &bounds)
{
    if ( !bounds.valid || cache.triangles.size() < 4 )
        return false;

    double weld = std::max(0.001, (double)(bounds.max - bounds.min).length() * 0.00001);
    std::map<std::tuple<int64_t, int64_t, int64_t>, int> vertices;
    std::map<std::pair<int, int>, int> edges;

    for (const NC_STACK_ypaworld::TAttachedFXTriangle &triangle : cache.triangles)
    {
        int ids[3] = {
            yw_GetWeldedVertexID(triangle.a, weld, &vertices),
            yw_GetWeldedVertexID(triangle.b, weld, &vertices),
            yw_GetWeldedVertexID(triangle.c, weld, &vertices)
        };

        for (int edge = 0; edge < 3; edge++)
        {
            int first = ids[edge];
            int second = ids[(edge + 1) % 3];
            if ( first == second )
                return false;
            if ( first > second )
                std::swap(first, second);
            edges[std::make_pair(first, second)]++;
        }
    }

    if ( edges.empty() )
        return false;

    for (const auto &edge : edges)
    {
        if ( edge.second != 2 )
            return false;
    }

    return true;
}

static bool yw_RayIntersectsAttachedFXTriangle(const vec3d &origin, const vec3d &direction,
                                                const NC_STACK_ypaworld::TAttachedFXTriangle &triangle)
{
    const double epsilon = 0.000001;
    vec3d edge1 = triangle.b - triangle.a;
    vec3d edge2 = triangle.c - triangle.a;
    vec3d p = yw_Cross3(direction, edge2);
    double determinant = edge1.dot(p);
    if ( fabs(determinant) <= epsilon )
        return false;

    double inverse = 1.0 / determinant;
    vec3d t = origin - triangle.a;
    double u = t.dot(p) * inverse;
    if ( u < 0.0 || u > 1.0 )
        return false;

    vec3d q = yw_Cross3(t, edge1);
    double v = direction.dot(q) * inverse;
    if ( v < 0.0 || u + v > 1.0 )
        return false;

    return edge2.dot(q) * inverse > epsilon;
}

static bool yw_IsPointInsideAttachedFXMesh(const vec3d &point,
                                           const NC_STACK_ypaworld::TAttachedFXGeometryCache &cache)
{
    const vec3d rayDirection(0.922766, 0.342094, 0.174731);
    int intersections = 0;
    for (const NC_STACK_ypaworld::TAttachedFXTriangle &triangle : cache.triangles)
    {
        if ( yw_RayIntersectsAttachedFXTriangle(point, rayDirection, triangle) )
            intersections++;
    }
    return (intersections & 1) != 0;
}

static float yw_NextAttachedFXCacheRandom(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)((*state >> 8) & 0x00ffffffu) / 16777216.0f;
}

static void yw_BuildAttachedFXVolumeCache(NC_STACK_ypaworld::TAttachedFXGeometryCache *cache,
                                          const yw_ModelPointCloud &bounds)
{
    if ( !cache || !yw_IsClosedAttachedFXMesh(*cache, bounds) )
        return;

    vec3d size = bounds.max - bounds.min;
    if ( size.x <= 0.001 || size.y <= 0.001 || size.z <= 0.001 )
        return;

    const size_t targetPoints = 2048;
    const int maxAttempts = 65536;
    uint32_t randomState = 0x9e3779b9u ^ (uint32_t)cache->triangles.size();
    cache->volumePoints.reserve(targetPoints);

    for (int attempt = 0; attempt < maxAttempts && cache->volumePoints.size() < targetPoints; attempt++)
    {
        vec3d point(bounds.min.x + size.x * yw_NextAttachedFXCacheRandom(&randomState),
                    bounds.min.y + size.y * yw_NextAttachedFXCacheRandom(&randomState),
                    bounds.min.z + size.z * yw_NextAttachedFXCacheRandom(&randomState));
        if ( yw_IsPointInsideAttachedFXMesh(point, *cache) )
            cache->volumePoints.push_back(point);
    }

    // Sparse or anomalous results are not a trustworthy occupied-volume cache.
    if ( cache->volumePoints.size() < 64 )
        cache->volumePoints.clear();
}

static bool yw_SameAttachedFXTransform(const vec3d &a, const vec3d &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool NC_STACK_ypaworld::SampleAttachedFXLocalPosition(NC_STACK_ypabact *owner,
                                                       float randomOffsetPercent,
                                                       vec3d *localPosition)
{
    if ( !owner || !localPosition )
        return false;

    if ( randomOffsetPercent <= 0.0f )
    {
        *localPosition = vec3d(0.0, 0.0, 0.0);
        return true;
    }

    if ( randomOffsetPercent > 100.0f )
        randomOffsetPercent = 100.0f;

    NC_STACK_base *source = owner->_vp_normal;
    if ( !source ) source = owner->_vp_wait;
    if ( !source ) source = owner->_vp_fire;
    if ( !source ) source = owner->_vp_genesis;
    if ( !source )
        return false;

    TAttachedFXGeometryCache *cache = NULL;
    for (TAttachedFXGeometryCache &candidate : _attachedFXGeometryCache)
    {
        if ( candidate.source == source &&
             yw_SameAttachedFXTransform(candidate.scale, owner->_vp_scale) &&
             yw_SameAttachedFXTransform(candidate.orientation, owner->_vp_orientation) )
        {
            cache = &candidate;
            break;
        }
    }

    if ( !cache )
    {
        _attachedFXGeometryCache.emplace_back();
        cache = &_attachedFXGeometryCache.back();
        cache->source = source;
        cache->scale = owner->_vp_scale;
        cache->orientation = owner->_vp_orientation;

        yw_ModelPointCloud bounds;
        mat3x3 transform = yw_BuildVPRotationMatrix(owner->_vp_orientation);
        transform *= mat3x3::Scale(yw_SafeVPScale(owner->_vp_scale));
        yw_CollectAttachedFXTriangles(source, transform, vec3d(0.0, 0.0, 0.0), cache, &bounds);
        if ( !cache->triangles.empty() )
            yw_BuildAttachedFXVolumeCache(cache, bounds);
    }

    if ( !cache->volumePoints.empty() )
    {
        size_t index = (size_t)(rand() % (int)cache->volumePoints.size());
        *localPosition = cache->volumePoints[index];
    }
    else
    {
        if ( cache->triangles.empty() || cache->totalArea <= 0.0f )
            return false;

        float selectedArea = ((float)rand() / ((float)RAND_MAX + 1.0f)) * cache->totalArea;
        const TAttachedFXTriangle *selected = &cache->triangles.back();
        for (const TAttachedFXTriangle &triangle : cache->triangles)
        {
            if ( selectedArea < triangle.cumulativeArea )
            {
                selected = &triangle;
                break;
            }
        }

        float r1 = (float)rand() / ((float)RAND_MAX + 1.0f);
        float r2 = (float)rand() / ((float)RAND_MAX + 1.0f);
        float root = sqrt(r1);
        *localPosition = selected->a * (1.0f - root) +
                         selected->b * (root * (1.0f - r2)) +
                         selected->c * (root * r2);
    }

    *localPosition = *localPosition * (randomOffsetPercent / 100.0f);

    return true;
}

static void yw_MakePointCloudBilateralX(yw_ModelPointCloud *cloud)
{
    if ( !cloud || !cloud->valid || cloud->points.empty() )
        return;

    float symmetryAxisX = (cloud->min.x + cloud->max.x) * 0.5f;
    size_t originalCount = cloud->points.size();
    cloud->points.reserve(originalCount * 2);

    for (size_t i = 0; i < originalCount; i++)
    {
        const vec3d &source = cloud->points[i];
        if ( fabs(source.x - symmetryAxisX) <= 0.001f )
            continue;

        vec3d mirrored = source;
        mirrored.x = symmetryAxisX - (source.x - symmetryAxisX);
        yw_AddModelPoint(cloud, mirrored);
    }
}

static float yw_ClampFloat(float v, float minv, float maxv)
{
    if ( v < minv )
        return minv;
    if ( v > maxv )
        return maxv;
    return v;
}

static int yw_AutoCollCellCount(float extent, float targetSpan, float verticalScale)
{
    if ( extent < 8.0 )
        return 1;

    int count = (int)ceil(extent / (targetSpan * verticalScale));
    if ( count < 1 )
        count = 1;
    if ( count > 8 )
        count = 8;
    return count;
}

static void yw_LimitAutoCollGrid(const vec3d &size, int *nx, int *ny, int *nz)
{
    const int maxSpheres = 16;

    while ((*nx) * (*ny) * (*nz) > maxSpheres)
    {
        float xCell = *nx > 1 ? size.x / (float)(*nx) : std::numeric_limits<float>::max();
        float yCell = *ny > 1 ? size.y / (float)(*ny) : std::numeric_limits<float>::max();
        float zCell = *nz > 1 ? size.z / (float)(*nz) : std::numeric_limits<float>::max();

        if ( xCell <= yCell && xCell <= zCell )
            (*nx)--;
        else if ( yCell <= zCell )
            (*ny)--;
        else
            (*nz)--;
    }
}

static bool yw_HasApproxBilateralXSymmetry(const yw_ModelPointCloud &cloud)
{
    if ( !cloud.valid || cloud.points.size() < 12 )
        return false;

    const int binCount = 8;
    int leftBins[binCount] = {0};
    int rightBins[binCount] = {0};
    float symmetryAxisX = (cloud.min.x + cloud.max.x) * 0.5f;
    float halfWidth = (cloud.max.x - cloud.min.x) * 0.5f;
    if ( halfWidth < 6.0f )
        return false;

    int leftTotal = 0;
    int rightTotal = 0;
    for (const vec3d &point : cloud.points)
    {
        float offset = point.x - symmetryAxisX;
        if ( fabs(offset) < halfWidth * 0.02f )
            continue;

        int bin = (int)((fabs(offset) / halfWidth) * binCount);
        if ( bin < 0 )
            bin = 0;
        else if ( bin >= binCount )
            bin = binCount - 1;

        if ( offset < 0.0f )
        {
            leftBins[bin]++;
            leftTotal++;
        }
        else
        {
            rightBins[bin]++;
            rightTotal++;
        }
    }

    if ( leftTotal < 6 || rightTotal < 6 )
        return false;

    int matched = 0;
    for (int i = 0; i < binCount; i++)
        matched += std::min(leftBins[i], rightBins[i]) * 2;

    int total = leftTotal + rightTotal;
    return ((float)matched / (float)total) >= 0.68f;
}

static void yw_RepairAutoCollBilateralSymmetry(const yw_ModelPointCloud &cloud,
                                                World::rbcolls *colls, size_t maxSpheres)
{
    if ( !colls || colls->roboColls.empty() || colls->roboColls.size() >= maxSpheres ||
         !yw_HasApproxBilateralXSymmetry(cloud) )
        return;

    const float symmetryAxisX = (cloud.min.x + cloud.max.x) * 0.5f;
    const size_t originalCount = colls->roboColls.size();

    struct MirrorCandidate
    {
        World::TRoboColl sphere;
        float lateralOffset;
    };

    std::vector<MirrorCandidate> candidates;
    candidates.reserve(originalCount);

    auto hasEquivalentSphere = [&](const World::TRoboColl &candidate)
    {
        for (const World::TRoboColl &existing : colls->roboColls)
        {
            float positionTolerance = std::max(4.0f,
                std::min(existing.robo_coll_radius, candidate.robo_coll_radius) * 0.55f);
            if ( (existing.coll_pos - candidate.coll_pos).length() <= positionTolerance )
                return true;
        }
        return false;
    };

    for (size_t i = 0; i < originalCount; i++)
    {
        const World::TRoboColl &source = colls->roboColls[i];
        if ( source.robo_coll_radius <= 3.0f )
            continue;

        float lateralOffset = fabs(source.coll_pos.x - symmetryAxisX);
        if ( lateralOffset < std::max(6.0f, source.robo_coll_radius * 0.40f) )
            continue;

        World::TRoboColl mirror = source;
        mirror.coll_pos.x = symmetryAxisX - (source.coll_pos.x - symmetryAxisX);
        if ( hasEquivalentSphere(mirror) )
            continue;

        // Add a mirrored sphere only when sampled visible geometry actually
        // exists there. This repairs one-sided grid/filter omissions on wings
        // without inventing collision volume on genuinely asymmetric models.
        int nearbyPoints = 0;
        float nearestPoint = std::numeric_limits<float>::max();
        float farthestPoint = 0.0f;
        float searchRadius = source.robo_coll_radius * 1.35f;
        for (const vec3d &point : cloud.points)
        {
            float distance = (point - mirror.coll_pos).length();
            nearestPoint = std::min(nearestPoint, distance);
            if ( distance <= searchRadius )
            {
                nearbyPoints++;
                farthestPoint = std::max(farthestPoint, distance);
            }
        }

        if ( nearbyPoints < 6 || farthestPoint < 3.0f ||
             nearestPoint > std::max(4.0f, source.robo_coll_radius * 0.65f) )
            continue;

        // Never make the repaired side larger than the accepted source side.
        // If the mirrored geometry is more compact, shrink the sphere to match.
        mirror.robo_coll_radius = std::min(source.robo_coll_radius,
                                           std::max(3.0f, farthestPoint * 0.82f));

        MirrorCandidate candidate;
        candidate.sphere = mirror;
        candidate.lateralOffset = lateralOffset;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const MirrorCandidate &a, const MirrorCandidate &b)
              {
                  return a.lateralOffset > b.lateralOffset;
              });

    for (const MirrorCandidate &candidate : candidates)
    {
        if ( colls->roboColls.size() >= maxSpheres )
            break;
        if ( !hasEquivalentSphere(candidate.sphere) )
            colls->roboColls.push_back(candidate.sphere);
    }
}

static void yw_ForceAutoCollBilateralSymmetry(const yw_ModelPointCloud &cloud,
                                               World::rbcolls *colls, size_t maxSpheres)
{
    if ( !colls || colls->roboColls.empty() || maxSpheres == 0 )
        return;

    struct BilateralGroup
    {
        World::TRoboColl first;
        World::TRoboColl second;
        bool paired = false;
        float score = 0.0f;
    };

    const float symmetryAxisX = (cloud.min.x + cloud.max.x) * 0.5f;
    const float centerTolerance = std::max(1.0f,
        (float)(cloud.max.x - cloud.min.x) * 0.01f);
    const std::vector<World::TRoboColl> source = colls->roboColls;
    std::vector<bool> used(source.size(), false);
    std::vector<BilateralGroup> groups;
    groups.reserve(source.size());

    for (size_t i = 0; i < source.size(); i++)
    {
        if ( used[i] )
            continue;

        used[i] = true;
        const World::TRoboColl &sphere = source[i];
        float offset = sphere.coll_pos.x - symmetryAxisX;

        BilateralGroup group;
        if ( fabs(offset) <= centerTolerance )
        {
            group.first = sphere;
            group.first.coll_pos.x = symmetryAxisX;
            group.score = sphere.robo_coll_radius;
            groups.push_back(group);
            continue;
        }

        World::TRoboColl expected = sphere;
        expected.coll_pos.x = symmetryAxisX - offset;
        size_t bestMatch = source.size();
        float bestDistance = std::numeric_limits<float>::max();

        for (size_t j = i + 1; j < source.size(); j++)
        {
            if ( used[j] || (source[j].coll_pos.x - symmetryAxisX) * offset >= 0.0f )
                continue;

            float distance = (source[j].coll_pos - expected.coll_pos).length();
            float tolerance = std::max(4.0f,
                std::min(source[j].robo_coll_radius, sphere.robo_coll_radius) * 0.75f);
            if ( distance <= tolerance && distance < bestDistance )
            {
                bestDistance = distance;
                bestMatch = j;
            }
        }

        float lateralOffset = fabs(offset);
        float centerY = sphere.coll_pos.y;
        float centerZ = sphere.coll_pos.z;
        float radius = sphere.robo_coll_radius;
        if ( bestMatch < source.size() )
        {
            used[bestMatch] = true;
            const World::TRoboColl &match = source[bestMatch];
            lateralOffset = (lateralOffset + fabs(match.coll_pos.x - symmetryAxisX)) * 0.5f;
            centerY = (centerY + match.coll_pos.y) * 0.5f;
            centerZ = (centerZ + match.coll_pos.z) * 0.5f;
            radius = std::max(radius, match.robo_coll_radius);
        }

        group.paired = true;
        group.first.coll_pos = vec3d(symmetryAxisX - lateralOffset, centerY, centerZ);
        group.first.robo_coll_radius = radius;
        group.second = group.first;
        group.second.coll_pos.x = symmetryAxisX + lateralOffset;
        group.score = lateralOffset + radius;
        groups.push_back(group);
    }

    std::sort(groups.begin(), groups.end(),
              [](const BilateralGroup &a, const BilateralGroup &b)
              {
                  return a.score > b.score;
              });

    colls->roboColls.clear();
    colls->roboColls.reserve(std::min(maxSpheres, source.size() * 2));
    for (const BilateralGroup &group : groups)
    {
        size_t needed = group.paired ? 2 : 1;
        if ( colls->roboColls.size() + needed > maxSpheres )
            continue;

        colls->roboColls.push_back(group.first);
        if ( group.paired )
            colls->roboColls.push_back(group.second);
    }
}

static void yw_AddHiddenAutoCollBridges(World::rbcolls *colls)
{
    if ( !colls || colls->roboColls.size() < 2 )
        return;

    struct BridgeEdge
    {
        size_t first;
        size_t second;
        float distance;
        float score;
    };

    const size_t sourceCount = colls->roboColls.size();
    const size_t maxTotalSpheres = 48;
    std::vector<BridgeEdge> edges;
    std::vector<int> degree(sourceCount, 0);

    for (size_t i = 0; i + 1 < sourceCount; i++)
    {
        const World::TRoboColl &a = colls->roboColls[i];
        for (size_t j = i + 1; j < sourceCount; j++)
        {
            const World::TRoboColl &b = colls->roboColls[j];
            float distance = (float)(b.coll_pos - a.coll_pos).length();
            float minRadius = std::min(a.robo_coll_radius, b.robo_coll_radius);
            float radiusSum = a.robo_coll_radius + b.robo_coll_radius;
            float minBridgeRadius = std::max(3.0f, minRadius * 0.70f);

            if ( minRadius <= 0.01f || distance <= minRadius * 0.45f ||
                 distance > radiusSum * 1.6f || distance > minBridgeRadius * 7.5f )
                continue;

            BridgeEdge edge;
            edge.first = i;
            edge.second = j;
            edge.distance = distance;
            edge.score = distance / std::max(1.0f, radiusSum);
            edges.push_back(edge);
        }
    }

    std::sort(edges.begin(), edges.end(),
              [](const BridgeEdge &a, const BridgeEdge &b)
              {
                  return a.score < b.score;
              });

    for (const BridgeEdge &edge : edges)
    {
        if ( colls->roboColls.size() >= maxTotalSpheres )
            break;
        if ( degree[edge.first] >= 3 || degree[edge.second] >= 3 )
            continue;

        const World::TRoboColl a = colls->roboColls[edge.first];
        const World::TRoboColl b = colls->roboColls[edge.second];
        float minBridgeRadius = std::max(3.0f,
            std::min(a.robo_coll_radius, b.robo_coll_radius) * 0.70f);
        int bridgeCount = std::max(1,
            (int)ceil(edge.distance / (minBridgeRadius * 1.5f)) - 1);
        bridgeCount = std::min(bridgeCount, 4);
        bridgeCount = std::min(bridgeCount,
            (int)(maxTotalSpheres - colls->roboColls.size()));

        for (int bridgeIndex = 1; bridgeIndex <= bridgeCount; bridgeIndex++)
        {
            float t = (float)bridgeIndex / (float)(bridgeCount + 1);
            World::TRoboColl bridge;
            bridge.coll_pos = a.coll_pos + (b.coll_pos - a.coll_pos) * t;
            bridge.robo_coll_radius = std::max(3.0f,
                (a.robo_coll_radius +
                 (b.robo_coll_radius - a.robo_coll_radius) * t) * 0.70f);
            bridge.debug_visible = false;
            colls->roboColls.push_back(bridge);
        }

        degree[edge.first]++;
        degree[edge.second]++;
    }
}

static World::rbcolls yw_GenerateAutoCollSpheresFromVP(NC_STACK_base *vp, const vec3d &scale,
                                                       const vec3d &orientation, bool essentialHull,
                                                       bool forceBilateral, bool solidMaterialsOnly)
{
    World::rbcolls out;
    const int maxSpheres = 16;

    yw_ModelPointCloud cloud;
    yw_CollectVPPointCloud(vp, scale, orientation, &cloud, solidMaterialsOnly);
    if ( !cloud.valid || cloud.points.size() < 3 )
        return out;

    if ( forceBilateral )
        yw_MakePointCloudBilateralX(&cloud);

    vec3d size = cloud.max - cloud.min;
    float shortHorizontal = std::min(size.x, size.z);
    if ( shortHorizontal < 1.0 )
        shortHorizontal = std::max(size.x, size.z);

    float targetSpan = yw_ClampFloat(shortHorizontal * 0.55f, 24.0f, 48.0f);
    int nx = yw_AutoCollCellCount(size.x, targetSpan, 1.0f);
    int ny = yw_AutoCollCellCount(size.y, targetSpan, 1.35f);
    int nz = yw_AutoCollCellCount(size.z, targetSpan, 1.0f);
    yw_LimitAutoCollGrid(size, &nx, &ny, &nz);

    std::vector<yw_ModelPointCloud> cells(nx * ny * nz);
    for (const vec3d &p : cloud.points)
    {
        int ix = size.x > 0.001 ? (int)(((p.x - cloud.min.x) / size.x) * nx) : 0;
        int iy = size.y > 0.001 ? (int)(((p.y - cloud.min.y) / size.y) * ny) : 0;
        int iz = size.z > 0.001 ? (int)(((p.z - cloud.min.z) / size.z) * nz) : 0;

        if ( ix >= nx ) ix = nx - 1;
        if ( iy >= ny ) iy = ny - 1;
        if ( iz >= nz ) iz = nz - 1;

        yw_AddModelPoint(&cells[(iy * nz + iz) * nx + ix], p);
    }

    out.roboColls.reserve(cells.size());
    float thinThreshold = yw_ClampFloat(targetSpan * 0.10f, 2.5f, 5.0f);
    yw_ModelPointCloud acceptedBounds;

    for (const yw_ModelPointCloud &cell : cells)
    {
        if ( !cell.valid )
            continue;

        vec3d cellSize = cell.max - cell.min;
        float minDimension = std::min(cellSize.x, std::min(cellSize.y, cellSize.z));
        if ( essentialHull && minDimension < thinThreshold )
            continue;

        yw_AddModelPoint(&acceptedBounds, cell.min);
        yw_AddModelPoint(&acceptedBounds, cell.max);

        vec3d center = (cell.min + cell.max) * 0.5;
        float radius = 0.0;
        for (const vec3d &p : cell.points)
            radius = std::max(radius, (float)(p - center).length());

        if ( essentialHull )
            radius *= 0.82f;
        else
            radius += yw_ClampFloat(radius * 0.05f, 1.5f, 4.0f);

        if ( radius < 3.0f )
            radius = 3.0f;

        World::TRoboColl sphere;
        sphere.coll_pos = center;
        sphere.robo_coll_radius = radius;
        out.roboColls.push_back(sphere);
    }

    const yw_ModelPointCloud &physicalBounds = acceptedBounds.valid ? acceptedBounds : cloud;
    out.modelBoundsValid = physicalBounds.valid;
    if ( out.modelBoundsValid )
    {
        out.modelMin = physicalBounds.min;
        out.modelMax = physicalBounds.max;
    }

    if ( essentialHull && !out.roboColls.empty() )
    {
        if ( forceBilateral )
        {
            yw_ForceAutoCollBilateralSymmetry(cloud, &out, (size_t)maxSpheres);
            yw_AddHiddenAutoCollBridges(&out);
        }
        else
            yw_RepairAutoCollBilateralSymmetry(cloud, &out, (size_t)maxSpheres);
    }

    if ( out.roboColls.empty() )
    {
        World::TRoboColl sphere;
        sphere.coll_pos = (cloud.min + cloud.max) * 0.5;
        if ( essentialHull )
        {
            float largestDimension = std::max(size.x, std::max(size.y, size.z));
            sphere.robo_coll_radius = std::max(3.0f, largestDimension * 0.32f);
        }
        else
        {
            sphere.robo_coll_radius = std::max(4.0f, (float)size.length() * 0.5f);
        }
        out.roboColls.push_back(sphere);
    }

    return out;
}

static bool yw_GetVehicleAutoCollisionBounds(const World::rbcolls &colls,
                                             int modelType,
                                             float *radius, float *viewerRadius,
                                             float *overeof)
{
    float bodyRadius = 0.0f;
    float groundOffset = 0.0f;
    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
    bool foundSphere = false;

    for (const World::TRoboColl &sphere : colls.roboColls)
    {
        if ( sphere.robo_coll_radius <= 0.01f )
            continue;

        foundSphere = true;
        bodyRadius = std::max(bodyRadius,
                              (float)sphere.coll_pos.length() + sphere.robo_coll_radius);
        groundOffset = std::max(groundOffset,
                                (float)sphere.coll_pos.y + sphere.robo_coll_radius);
        minX = std::min(minX, (float)sphere.coll_pos.x - sphere.robo_coll_radius);
        maxX = std::max(maxX, (float)sphere.coll_pos.x + sphere.robo_coll_radius);
        minZ = std::min(minZ, (float)sphere.coll_pos.z - sphere.robo_coll_radius);
        maxZ = std::max(maxZ, (float)sphere.coll_pos.z + sphere.robo_coll_radius);
    }

    if ( !foundSphere )
        return false;

    // These remain runtime compatibility bounds for legacy terrain, landing
    // and broad-phase code. auto_collision supplies any missing script value,
    // but an explicitly authored bound remains authoritative.
    if ( radius )
        *radius = std::max(bodyRadius, 1.0f);
    if ( viewerRadius )
    {
        float extentX = std::max(fabs(minX), fabs(maxX));
        float extentZ = std::max(fabs(minZ), fabs(maxZ));
        bool usesGroundFootprint = modelType == BACT_TYPES_TANK ||
                                   modelType == BACT_TYPES_CAR ||
                                   modelType == BACT_TYPES_HOVER;

        // Tank/car alignment casts terrain probes around the vehicle, so use
        // the long footprint axis. Airborne vehicle collision still consumes
        // vwr_radius as one sphere, where the short axis is the stable body
        // core and avoids turning wings or a long nose into distant impacts.
        float footprintRadius = usesGroundFootprint ? std::max(extentX, extentZ)
                                                    : std::min(extentX, extentZ);
        *viewerRadius = std::max(footprintRadius, 1.0f);
    }
    if ( overeof )
    {
        // Generated spheres circumscribe their source cells and therefore
        // extend below the actual mesh. Use the accepted model bottom when
        // available; manual/fallback compounds retain the sphere-derived bound.
        float modelBottom = colls.modelBoundsValid ? (float)colls.modelMax.y : groundOffset;
        *overeof = std::max(modelBottom, 1.0f);
    }
    return true;
}

static World::TRoboColl yw_MergeCollisionSpheres(const World::TRoboColl &a, const World::TRoboColl &b)
{
    vec3d delta = b.coll_pos - a.coll_pos;
    float distance = delta.length();

    if ( a.robo_coll_radius >= distance + b.robo_coll_radius )
        return a;
    if ( b.robo_coll_radius >= distance + a.robo_coll_radius )
        return b;

    World::TRoboColl merged;
    merged.robo_coll_radius = (distance + a.robo_coll_radius + b.robo_coll_radius) * 0.5f;
    merged.coll_pos = a.coll_pos;
    if ( distance > 0.001f )
        merged.coll_pos += delta * ((merged.robo_coll_radius - a.robo_coll_radius) / distance);
    return merged;
}

static void yw_SimplifyAutoWeaponCollSpheres(World::rbcolls *colls)
{
    if ( !colls )
        return;

    std::vector<World::TRoboColl> &spheres = colls->roboColls;
    const size_t maxWeaponSpheres = 4;

    while ( spheres.size() > 1 )
    {
        size_t bestA = 0;
        size_t bestB = 0;
        float bestCost = std::numeric_limits<float>::max();
        bool foundRedundantPair = false;

        for (size_t i = 0; i + 1 < spheres.size(); i++)
        {
            for (size_t j = i + 1; j < spheres.size(); j++)
            {
                float distance = (spheres[j].coll_pos - spheres[i].coll_pos).length();
                float minRadius = std::min(spheres[i].robo_coll_radius, spheres[j].robo_coll_radius);
                bool redundant = distance <= minRadius * 0.45f;
                World::TRoboColl merged = yw_MergeCollisionSpheres(spheres[i], spheres[j]);
                float growth = merged.robo_coll_radius -
                               std::max(spheres[i].robo_coll_radius, spheres[j].robo_coll_radius);

                if ( (redundant && !foundRedundantPair) ||
                     (redundant == foundRedundantPair && growth < bestCost) )
                {
                    foundRedundantPair = redundant;
                    bestCost = growth;
                    bestA = i;
                    bestB = j;
                }
            }
        }

        if ( !foundRedundantPair && spheres.size() <= maxWeaponSpheres )
            break;

        spheres[bestA] = yw_MergeCollisionSpheres(spheres[bestA], spheres[bestB]);
        spheres.erase(spheres.begin() + bestB);
    }
}

static float yw_GetWeaponAutoCollisionScale()
{
    std::string value = System::IniConf::GameWeaponAutoCollisionScale.Get<std::string>();
    if ( value.empty() || value.find(',') != std::string::npos )
        return 1.0f;

    try
    {
        size_t pos = 0;
        float scale = std::stof(value, &pos);
        if ( value.find_first_not_of(" \t\r\n", pos) != std::string::npos || !isfinite(scale) )
            return 1.0f;

        return yw_ClampFloat(scale, 1.0f, 10.0f);
    }
    catch (...)
    {
        return 1.0f;
    }
}

static std::string yw_TrimConfigValue(std::string s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();

    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static bool yw_ParseOptionalInt(std::string s, int32_t *out)
{
    s = yw_TrimConfigValue(s);
    if (s.empty())
        return false;

    try
    {
        size_t pos = 0;
        int32_t v = (int32_t)std::stol(s, &pos, 0);
        size_t rest = s.find_first_not_of(" \t\r\n", pos);
        if (rest != std::string::npos)
            return false;

        *out = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool yw_ParseOptionalBool(std::string s, bool *out)
{
    s = yw_TrimConfigValue(s);
    if (s.empty())
        return false;

    *out = StrGetBool(s);
    return true;
}

static int32_t yw_ClampRenderSectors(int32_t sectors)
{
    if (sectors < 3)
        sectors = 3;
    if (sectors > YW_RENDER_SECTORS_DEF * 2 - 1)
        sectors = YW_RENDER_SECTORS_DEF * 2 - 1;
    if ((sectors & 1) == 0)
        sectors -= 1;

    return sectors;
}

static void yw_ApplyNucleusViewDistanceOverrides(NC_STACK_ypaworld *yw)
{
    int32_t v = 0;
    bool b = false;

    if (yw_ParseOptionalInt(System::IniConf::GfxRenderSectors.Get<std::string>(), &v))
        yw->_renderSectors = yw_ClampRenderSectors(v);
    if (yw_ParseOptionalInt(System::IniConf::GfxNormalVisualLimit.Get<std::string>(), &v) && v > 0)
        yw->_normalVizLimit = v;
    if (yw_ParseOptionalInt(System::IniConf::GfxNormalFadeLength.Get<std::string>(), &v) && v > 0)
        yw->_normalFadeLength = v;
    if (yw_ParseOptionalInt(System::IniConf::GfxSkyVisualLimit.Get<std::string>(), &v) && v > 0)
        yw->_skyVizLimit = v;
    if (yw_ParseOptionalInt(System::IniConf::GfxSkyFadeLength.Get<std::string>(), &v) && v > 0)
        yw->_skyFadeLength = v;
    if (yw_ParseOptionalInt(System::IniConf::GfxSkyHeight.Get<std::string>(), &v))
        yw->_skyHeight = v;
    if (yw_ParseOptionalBool(System::IniConf::GfxSkyRender.Get<std::string>(), &b))
        yw->_skyRender = b;
}

void NC_STACK_ypaworld::ApplyNucleusViewDistanceOverrides()
{
    yw_ApplyNucleusViewDistanceOverrides(this);
}

NC_STACK_ypaworld::NC_STACK_ypaworld()
: _unitsList(this, NC_STACK_ypabact::GetKidRefNode, World::BLIST_UNITS)
, _deadCacheList(this, NC_STACK_ypabact::GetKidRefNode, World::BLIST_CACHE)
, _history(4096)
{
}

static bool yw_IsUsableGameplayName(const std::string &name)
{
    if ( name == "<NO NAME>" )
        return false;

    for (unsigned char ch : name)
    {
        if ( !std::isspace(ch) )
            return true;
    }

    return false;
}

std::string NC_STACK_ypaworld::GetVehicleName(uint32_t id) const
{
    return GetVehicleName(_vhclProtos[id]);
}

std::string NC_STACK_ypaworld::GetVehicleName(const World::TVhclProto &proto) const
{
    if ( yw_IsUsableGameplayName(proto.name) )
        return proto.name;

    if (proto.Index != -1)
        return Locale::Text::VehicleName(proto.Index, proto.name);

    return proto.name;
}

std::string NC_STACK_ypaworld::GetBuildingName(uint32_t id, bool net) const
{
    return GetBuildingName(_buildProtos[id], net);
}

std::string NC_STACK_ypaworld::GetBuildingName(const World::TBuildingProto &proto, bool net) const
{
    if ( yw_IsUsableGameplayName(proto.Name) )
        return proto.Name;

    if (proto.Index != -1)
    {
        if (net)
            return Locale::Text::NetBuildingName(proto.Index, proto.Name);
        else
            return Locale::Text::BuildingName(proto.Index, proto.Name);
    }

    return proto.Name;
}

std::string NC_STACK_ypaworld::GetLevelName(uint32_t id) const
{
    const TMapRegionInfo &region = _globalMapRegions.MapRegions[id];

    if ( yw_IsUsableGameplayName(region.MapName) )
        return region.MapName;

    return Locale::Text::LevelName(id, region.MapName);
}

std::string NC_STACK_ypaworld::GetLevelName(const TLevelInfo &lvl) const
{
    if ( yw_IsUsableGameplayName(lvl.MapName) )
        return lvl.MapName;

    return Locale::Text::LevelName(lvl.LevelID, lvl.MapName);
}

std::string NC_STACK_ypaworld::ResolveGameplayVehicleName(uint32_t id) const
{
    if ( id < _vhclProtos.size() )
        return ResolveGameplayVehicleName(_vhclProtos[id]);

    return std::string();
}

std::string NC_STACK_ypaworld::ResolveGameplayVehicleName(const World::TVhclProto &proto) const
{
    if ( yw_IsUsableGameplayName(proto.name) )
        return proto.name;

    return GetVehicleName(proto);
}

std::string NC_STACK_ypaworld::ResolveGameplayVehicleName(const NC_STACK_ypabact *bact, const World::TVhclProto &proto) const
{
    if ( bact && yw_IsUsableGameplayName(bact->_gunDisplayName) )
        return bact->_gunDisplayName;

    return ResolveGameplayVehicleName(proto);
}

std::string NC_STACK_ypaworld::ResolveGameplayWeaponName(uint32_t id) const
{
    if ( id < _weaponProtos.size() )
        return ResolveGameplayWeaponName(_weaponProtos[id]);

    return std::string();
}

std::string NC_STACK_ypaworld::ResolveGameplayWeaponName(const World::TWeapProto &proto) const
{
    if ( yw_IsUsableGameplayName(proto.name) )
        return proto.name;

    return std::string();
}

std::string NC_STACK_ypaworld::ResolveGameplayBuildingName(uint32_t id, bool net) const
{
    if ( id < _buildProtos.size() )
        return ResolveGameplayBuildingName(_buildProtos[id], net);

    return std::string();
}

std::string NC_STACK_ypaworld::ResolveGameplayBuildingName(const World::TBuildingProto &proto, bool net) const
{
    if ( yw_IsUsableGameplayName(proto.Name) )
        return proto.Name;

    return GetBuildingName(proto, net);
}

namespace World
{
int AssignParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
	if ( !StriCmp(p1, "end") )
	{
		return ScriptParser::RESULT_SCOPE_END;
	}
	else if ( !p1.empty() && !p2.empty() )
	{
		Common::Env.SetPrefix(p1, p2);
		ypa_log_out("parsing assign.txt: set assign %s to %s\n", p1.c_str(), p2.c_str());
		return ScriptParser::RESULT_OK;
	}
	else
	{
		return ScriptParser::RESULT_BAD_DATA;
	}

    return ScriptParser::RESULT_UNKNOWN;
}

bool ParseAssignFile(const std::string &file)
{
	ScriptParser::HandlersList hndls{
		AssignParser::MakeParser()
	};

	return ScriptParser::ParseFile(file, hndls, 0);
}

}


bool NC_STACK_ypaworld::LoadProtosScript(const std::string &filename)
{
    std::string buf = Common::Env.SetPrefix("rsrc", "data:");

    ScriptParser::HandlersList parsers {
        new World::Parsers::VhclProtoParser(this),
        new World::Parsers::WeaponProtoParser(this),
        new World::Parsers::BuildProtoParser(this)
    };

    bool res = ScriptParser::ParseFile(filename, parsers, ScriptParser::FLAG_NO_SCOPE_SKIP);
    Common::Env.SetPrefix("rsrc", buf);

    return res;
}

bool NC_STACK_ypaworld::ProtosInit()
{
    _vhclProtos.resize(NUM_VHCL_PROTO);
    _weaponProtos.resize(NUM_WEAPON_PROTO);
    _buildProtos.resize(NUM_BUILD_PROTO);

    _roboProtos.reserve(NUM_ROBO_PROTO);
    _roboProtos.resize(1);

    if ( !LoadProtosScript(_initScriptFilePath) )
        return false;

    return true;
}

int yw_InitSceneRecorder(NC_STACK_ypaworld *yw)
{
    yw->_replayRecorder = new TGameRecorder();
    return yw->_replayRecorder != NULL;
}

void yw_setInitScriptLoc(NC_STACK_ypaworld *yw)
{
    bool ok = false;
    FSMgr::FileHandle *fil = NULL;

    // Optional legacy override. Modern layouts use data:scripts/startup.scr.
    if ( uaFileExist("env:startup.def") )
        fil = uaOpenFileAlloc("env:startup.def", "r");

    if (fil)
    {
        std::string line;
        if ( fil->ReadLine(&line) )
        {
            size_t en = line.find_first_of("\n;");
            if (en != std::string::npos)
                line.erase(en);

            yw->_initScriptFilePath = line;
            ok = true;
        }


        delete fil;
    }

    if (!ok)
        yw->_initScriptFilePath = "data:scripts/startup.scr";
}

size_t NC_STACK_ypaworld::Init(IDVList &stak)
{
    if ( !NC_STACK_nucleus::Init(stak) )
    {
        ypa_log_out("yw_main.c/OM_NEW: _supermethoda() failed!\n");
        return 0;
    }

    Common::Env.SetPrefix("rsrc", "mc2res");
    Common::Env.SetPrefix("data", "Data");
    Common::Env.SetPrefix("save", "Save");
    Common::Env.SetPrefix("help", "Help");
    Common::Env.SetPrefix("mov", "Data:mov");
    Common::Env.SetPrefix("levels", "Levels");
    Common::Env.SetPrefix("mbpix", "levels:mbpix");
    Common::Env.SetPrefix("locale", "locale");
    Common::Env.SetPrefix("scripts", "data:scripts");

    if ( !World::ParseAssignFile("env:assign.txt") )
        ypa_log_out("Warning, no env:assign.txt script.\n");

    yw_setInitScriptLoc(this);

    System::IniConf::ReadFromNucleusIni();
    Locale::Text::SetLangDefault();
    ypaworld_func166("language"); // Load lang strings

//		if ( !make_CD_CHECK(1, 1, v10, v9) )
//		{
//			func1();
//			return NULL;
//		}

    _vhclModels.clear();
    _attachedFXGeometryCache.clear();
    ClearOverrideModels();

    if ( !ProtosInit() )
    {
        ypa_log_out("ERROR: couldn't initialize prototypes.\n");
        Deinit();
        return 0;
    }

    _screenSize = GFX::Engine.GetScreenSize();

    _unitsList.clear();
    _guiActive.clear();
    _deadCacheList.clear();
    _transientVPs.clear();
    _nextTransientVPId = 1;
    _damageHoverTargets.clear();
    _fxLimit = 16;
    _renderSectors = stak.Get<int32_t>(YW_ATT_VISSECTORS, 9);
    _normalVizLimit = stak.Get<int32_t>(YW_ATT_NORMVISLIMIT, 3100);
    _normalFadeLength = stak.Get<int32_t>(YW_ATT_FADELENGTH, 2100);
    _skyVizLimit = stak.Get<int32_t>(YW_ATT_SKYVISLIMIT, 4200);
    _skyFadeLength = stak.Get<int32_t>(YW_ATT_SKYFADELENGTH, 2400);
    _mapSize.x = stak.Get<int32_t>(YW_ATT_MAPMAX_X, 64);
    _mapSize.y = stak.Get<int32_t>(YW_ATT_MAPMAX_Y, 64);
    _skyHeight = stak.Get<int32_t>(YW_ATT_SKYHEIGHT, -550);
    _skyRender = stak.Get<bool>(YW_ATT_SKYRENDER, true);
    ApplyNucleusViewDistanceOverrides();
    _doEnergyRecalc = stak.Get<bool>(YW_ATT_DOENERGYRECALC, true);

    _buildDate = stak.Get<std::string>(YW_ATT_BUILD_DATE, "");

    for (char &chr : _buildDate)
    {
        chr = toupper(chr);
        if ( chr < ' ' || chr > 'Z' )
            chr = '*';
    }



    _buildHealthModelId[0] = 3;
    for (int i = 1; i <= 100; i++ )
        _buildHealthModelId[i] = 2;
    for (int i = 101; i <= 200; i++ )
        _buildHealthModelId[i] = 1;
    for (int i = 201; i < 256; i++ )
        _buildHealthModelId[i] = 0;

    for (int j = 0; j < 64; j++)
    {
        for (int i = 0; i < 64; i++)
        {
            _sqrtTable(j,i) = sqrt(j * j + i * i);
        }
    }

    _cells.Clear();

    if ( !yw_InitSceneRecorder(this) )
    {
        ypa_log_out("yw_main.c/OM_NEW: init scene recorder failed!\n");
        Deinit();
        return 0;
    }

    _shellGfxMode = Common::Point( GFX::DEFAULT_WIDTH, GFX::DEFAULT_HEIGHT );
    _gfxMode = Common::Point( GFX::DEFAULT_WIDTH, GFX::DEFAULT_HEIGHT );



    _shellDefaultRes = Common::Point(640, 480);
    _gameDefaultRes = Common::Point(640, 480);

    if ( !InitMapRegionsNet() )
    {
        ypa_log_out("yw_main.c/OM_NEW: yw_InitLevelNet() failed!\n");
        Deinit();
        return 0;
    }

    if ( !yw_InitNetwork(this) )
    {
        ypa_log_out("yw_main.c/OM_NEW: yw_InitNetwork() failed!\n");
        Deinit();
        return 0;
    }

    _doNotRender = false;

    UpdateGuiSettings();

    return 1;
}


size_t NC_STACK_ypaworld::Deinit()
{
    _debugAoeRings.clear();
    FreeGameDataCursors();
    dprintf("MAKE ME %s\n","ypaworld_func1");
    return NC_STACK_nucleus::Deinit();
}

void sub_445230(NC_STACK_ypaworld *yw)
{
    if ( yw->UpdateSpectatorFollowCamera(NULL) )
        return;

    if ( yw->_viewerBact->getBACT_extraViewer() )
    {
        NC_STACK_ypabact *v4 = yw->_viewerBact;

        if ( v4->IsCockpitCameraActive() )
            yw->_viewerPosition = v4->GetCockpitCameraPosition();
        else
            yw->_viewerPosition = v4->_position + v4->_rotation.Transpose().Transform(v4->_viewer_position);

        yw->_viewerRotation = yw->_viewerBact->_viewer_rotation;
    }
    else
    {
        if ( yw->_viewerBact->IsCockpitCameraActive() )
            yw->_viewerPosition = yw->_viewerBact->GetCockpitCameraPosition();
        else
            yw->_viewerPosition = yw->_viewerBact->_position;

        yw->_viewerRotation = yw->_viewerBact->_rotation;
    }
}

static void yw_UpdateCockpitCameraToggle(NC_STACK_ypaworld *yw, TInputState *inpt)
{
    if ( !yw || !inpt || !yw->_GameShell || !yw->_userUnit )
        return;

    const UserData::TInputConf &bind = yw->_GameShell->InputConfig[World::INPUT_BIND_COCKPIT_CAMERA];
    if ( bind.Type != World::INPUT_BIND_TYPE_HOTKEY || inpt->HotKeyID != bind.KeyID )
        return;

    if ( yw->_userUnit->IsCockpitCameraAvailable() )
    {
        yw->_userUnit->ToggleCockpitCameraMode();
        inpt->HotKeyID = -1;
    }
}

size_t NC_STACK_ypaworld::Process(base_64arg *arg)
{
    extern GuiList gui_lstvw; //In yw_game_ui.cpp
    extern GuiList lstvw2; //In yw_game_ui.cpp
    extern bool SPEED_DOWN_NET; //In yw_net.cpp

    if ( (gui_lstvw.IsClosed() && lstvw2.IsClosed())
            || (arg->field_8->KbdLastHit != Input::KC_RETURN && arg->field_8->KbdLastHit != Input::KC_ESCAPE) )
    {
        _kbdLastKeyHit = Input::KC_NONE;
    }
    else
    {
        _kbdLastKeyHit = arg->field_8->KbdLastHit;
        arg->field_8->KbdLastHit = Input::KC_NONE;
        arg->field_8->KbdLastDown = Input::KC_NONE;
        arg->field_8->HotKeyID = -1;
    }

    if ( !ypaworld_func64__sub4(this, arg) )
    {
        uint32_t v92 = profiler_begin();

        _netChatSystem = false;

        if ( _screenShotSeq )
        {
            arg->TimeStamp -= arg->DTime;
            arg->field_8->Period = 40;
            arg->DTime = 40;
            arg->TimeStamp += arg->DTime;
        }

        if ( _userUnit )
        {
            if ( _framesElapsed == 1 )
            {
                _viewerPosition = _userUnit->_position;
                _viewerRotation = _userUnit->_rotation;
            }

            vec3d a3 = _userUnit->_fly_dir * _userUnit->_fly_dir_length;

            SFXEngine::SFXe.sub_423EFC(arg->DTime, _viewerPosition, a3, _viewerRotation);
        }

        if ( _framesElapsed == 1 )
        {
            yw_arg159 arg159;
            arg159.unit = _userRobo;
            arg159.Priority = 128;
            arg159.MsgID = 41;

            ypaworld_func159(&arg159);
        }

        ypaworld_func64__sub6(this);
        ypaworld_func64__sub2(this);

        if ( !_doNotRender )
        {
            ypaworld_func64__sub1(arg->field_8); //Precompute input (add mouse turn)
            yw_UpdateCockpitCameraToggle(this, arg->field_8);

            TClickBoxInf *winp = &arg->field_8->ClickInf;

            if ( _mouseCursorHidden )
            {
                if ( winp->move.ScreenPos != _mouseCursorHidePos )
                    _mouseCursorHidden = false;
            }
            else
            {
                if ( arg->field_8->KbdLastHit != Input::KC_NONE )
                {
                    if ( arg->field_8->KbdLastHit != Input::KC_LMB && arg->field_8->KbdLastHit != Input::KC_RMB && arg->field_8->KbdLastHit != Input::KC_MMB && !(arg->field_8->Buttons.Is(4)) )
                    {
                        _mouseCursorHidden = true;
                        _mouseCursorHidePos = winp->move.ScreenPos;
                    }
                }
            }
        }

        bool openUADebug = System::IniConf::IsGameNewDebugEnabled();
        if ( !openUADebug )
            _debugGameplayFrozen = false;
        else if ( arg->field_8 && arg->field_8->KbdLastHit == Input::KC_F12 )
            _debugGameplayFrozen = !_debugGameplayFrozen;

        bool gameplayFrozen = openUADebug && _debugGameplayFrozen;

        if ( !gameplayFrozen )
            _timeStamp += arg->DTime;

        _frameTime = arg->DTime;
        _framesElapsed++;

        _updateMessage.user_action = World::DOACTION_0;
        _updateMessage.gTime = _timeStamp;
        _updateMessage.frameTime = arg->DTime;
        _updateMessage.units_count = 0;
        _updateMessage.inpt = arg->field_8;
        _FPS = 1024 / arg->DTime;
        _profileVals[PFID_FPS] = _FPS;

        if ( !gameplayFrozen )
            HistoryEventAdd(World::History::Frame(_timeStamp));

        uint32_t v22 = profiler_begin();

        if ( _isNetGame && !gameplayFrozen )
            yw_NetMsgHndlLoop(this);

        if ( !_isNetGame || _levelInfo.State != TLevelInfo::STATE_ABORTED )
        {
            _profileVals[PFID_NETTIME] = profiler_end(v22);

            uint32_t v23 = profiler_begin();

            for (cellArea &cell : _cells)
            {
                cell.view_mask = cellArea::ViewMask(cell.owner);
                cell.UnhideMask = 0;
            }

            for (NC_STACK_ypabact* &unit : _unitsList)
                unit->MarkSectorsForView();

            _profileVals[PFID_MARKTIME] = profiler_end(v23);

            if ( !_doNotRender )
            {
                GFX::Engine.BeginFrame();
                /*_win3d->setRSTR_BGpen(0);
                _win3d->raster_func192(NULL);*/
            }

            UpdateSpectatorFollowCamera(arg->field_8);

            if ( !gameplayFrozen )
            {
                ypaworld_func64__sub15(this);
                ypaworld_func64__sub16(this);
                ypaworld_func64__sub17(this);
            }

            sub_4C40AC();

            if ( !gameplayFrozen )
            {
                ypaworld_func64__sub9(this);
                ypaworld_func64__sub19();
                BuildingConstructUpdate(arg->DTime);
                BuildingDecorationFXUpdate();
            }

            if ( !_doNotRender )
            {
                uint32_t v33 = profiler_begin();

                ypaworld_func64__sub8(this);
                ypaworld_func64__sub7(arg->field_8);

                sub_445230(this);

                ypaworld_func64__sub14(this);
                ypaworld_func64__sub21(arg->field_8);

                _profileVals[PFID_GUITIME] = profiler_end(v33);
            }

            if ( _doEnergyRecalc && !gameplayFrozen )
                DoSectorsEnergyRecalc();

            if ( _replayRecorder->do_record && !gameplayFrozen )
                recorder_update_time(this, arg->DTime);

            _guiVisor.field_0 = 0;
            _guiVisor.field_4 = 0;

            _ownerOldCellUserUnit = _userUnit->_pSector->owner;

            uint32_t v37 = profiler_begin();

            // Do user commands before any unit state can be changed
            if (_userRobo)
            {
                if (_userRobo->_bact_type == BACT_TYPES_ROBO)
                    ((NC_STACK_yparobo *)_userRobo)->HandleUserCommands(&_updateMessage);
            }

            if ( gameplayFrozen )
            {
                auto updateFrozenUserUnit = [&](NC_STACK_ypabact *unit)
                {
                    if ( !unit ||
                         unit == _userRobo ||
                         unit->_status == BACT_STATUS_DEAD ||
                         unit->_bact_type == BACT_TYPES_NOPE )
                    {
                        return;
                    }

                    unit->Update(&_updateMessage);
                    _updateMessage.units_count++;
                };

                updateFrozenUserUnit(_userUnit);

                if ( _viewerBact != _userUnit && _viewerBact && _viewerBact->getBACT_inputting() )
                    updateFrozenUserUnit(_viewerBact);
            }
            else
            {
                for ( NC_STACK_ypabact *unit : _unitsList.safe_iter() )
                {
                    if (_isNetGame && unit != _userRobo && unit->_bact_type == BACT_TYPES_ROBO)
                        unit->NetUpdate(&_updateMessage);
                    else
                        unit->Update(&_updateMessage);

                    _updateMessage.units_count++;
                }
            }

            _profileVals[PFID_UPDATETIME] = profiler_end(v37);

            sub_445230(this);

            uint32_t v41 = profiler_begin();

            if ( _isNetGame && !gameplayFrozen )
            {
                if ( arg->DTime == 1 )
                    _netFlushTimer -= 20;
                else
                    _netFlushTimer -= arg->DTime;

                if ( _netFlushTimer <= 0 )
                {

                    uint32_t v44 = _netDriver->FlushBroadcastBuffer();

                    _GameShell->netsend_count += v44;

                    if ( !_GameShell->net_packet_min || v44 < _GameShell->net_packet_min )
                        _GameShell->net_packet_min = v44;

                    if ( v44 > _GameShell->net_packet_max )
                        _GameShell->net_packet_max = v44;

                    if ( v44 )
                        _GameShell->net_packet_cnt++;

                    if ( SPEED_DOWN_NET )
                        _netFlushTimer = 1500;
                    else
                        _netFlushTimer = _GameShell->flush_time_norm;
                }
            }
            _profileVals[PFID_NETTIME] += profiler_end(v41);

            if ( _userUnit )
            {
                if ( _GameShell )
                {
                    _GameShell->samples1_info.Position = _userUnit->_position;
                    _GameShell->samples1_info.Vector = _userUnit->_fly_dir * _userUnit->_fly_dir_length;
                    SFXEngine::SFXe.UpdateSoundCarrier(&_GameShell->samples1_info);
                }
            }

            //ypaworld_func64__sub22(this); // scene events

            if (_script && !gameplayFrozen)
                _script->CallUpdate(_timeStamp, arg->DTime);

            if ( !gameplayFrozen )
                VoiceMessageUpdate(); // update sound messages

            const mat3x3 &v57 = SFXEngine::SFXe.sb_0x424c74();
            TF::TForm3D *v58 = TF::Engine.GetViewPoint();

            v58->SclRot = v57 * v58->SclRot;

            if ( _replayRecorder->do_record && !gameplayFrozen )
                recorder_write_frame();

            ypaworld_func64__sub3(this);

            if ( !_doNotRender )
            {
                uint32_t v62 = profiler_begin();

                if ( _userUnit->_cellId ) // if cell is not 0,0
                {
                    RenderGame(arg, 1);

                    if ( _isNetGame )
                        yw_NetDrawStats(this);
                }

                debug_info_draw(arg->field_8);

                GFX::Engine.EndFrame();

                _profileVals[PFID_RENDERTIME] = profiler_end(v62);
            }

            FFeedback_Update(); // Do vibrate joystick

            sb_0x447720(this, arg->field_8); // Snaps/ start/stop recording

            _profileVals[PFID_FRAMETIME] = profiler_end(v92);
            _profileVals[PFID_POLYGONS] = _polysDraw;

            ProfileCalcValues();

            if ( !_doNotRender )
            {
                if ( _netChatSystem )
                {
                    _doNotRender = true;

                    dprintf("MAKE ME %s (multiplayer part Messaging)\n", "ypaworld_func64");

//          v66 = get_lang_string(650, "MESSAGE TO");
//
//          sprintf(&v68, "%s %s", v66, yw->field_762E);
//
//          memset(&dlgBox, 0, 40);
//
//          dlgBox.title = &v68;
//          dlgBox.ok = get_lang_string(2, "OK");
//          dlgBox.cancel = get_lang_string(3, "CANCEL");
//          dlgBox.result = 0;
//          dlgBox.timer_context = yw;
//          dlgBox.maxLen = 64;
//          dlgBox.time = 250;
//          dlgBox.startText = "";
//          dlgBox.timer_func = sub_44674C;
//
//          yw->win3d->windd_func322(&dlgBox);
//
                    _doNotRender = false;
//
//          if ( dlgBox.result )
//            sub_4D9418(yw, dlgBox.result, yw->field_762E, yw->field_762A);
                }
            }

            // OpenUA: legacy online help pages are obsolete/dead.
            // Clear pending help URLs instead of launching an external browser.
            if ( !_helpURL.empty() )
                _helpURL.clear();

            //exit(1);
        }
    }

    return 1;
}

static bool yw_VehicleReferencesWeapon(const World::TVhclProto &vehicle, int32_t weaponId)
{
    if ( vehicle.weapon == weaponId )
        return true;

    for (int16_t extraWeaponId : vehicle.extra_weapons)
    {
        if ( extraWeaponId == weaponId )
            return true;
    }

    if ( vehicle.lowhp_weapon_enable && vehicle.lowhp_weapon == weaponId )
        return true;

    if ( vehicle.mgun_set && vehicle.mgun == weaponId )
        return true;

    if ( vehicle.seek_and_explode && vehicle.seek_and_explode_weapon == weaponId )
        return true;

    return vehicle.proximity_defense_enable && vehicle.proximity_defense_weapon == weaponId;
}

void NC_STACK_ypaworld::BeginGemNotificationCapture()
{
    _gemNotificationEntries.clear();
    _gemNotificationActionOrder = 0;
    _gemNotificationCaptureActive = true;
}

void NC_STACK_ypaworld::ClearGemNotificationCapture()
{
    _gemNotificationCaptureActive = false;
    _gemNotificationActionOrder = 0;
    _gemNotificationEntries.clear();
}

bool NC_STACK_ypaworld::IsGemNotificationCaptureActive() const
{
    return _gemNotificationCaptureActive;
}

bool NC_STACK_ypaworld::HasActiveNewGemNotification() const
{
    return System::IniConf::GameGemUnlockNewUI.Get<bool>() &&
           _upgradeId != -1 &&
           _timeStamp - _upgradeTimeStamp < GEM_NEW_UI_NOTIFICATION_DURATION_MS;
}

bool NC_STACK_ypaworld::IsNewGemNotificationBlockingPlayerWeapons(const NC_STACK_ypabact *bact) const
{
    return bact && bact == _userUnit && HasActiveNewGemNotification();
}

void NC_STACK_ypaworld::RecordGemNotificationChange(uint8_t targetKind, int32_t targetProtoId,
                                                     uint8_t changeKind, int32_t previousRawValue,
                                                     int32_t newRawValue, bool newlyEnabled)
{
    if ( !_gemNotificationCaptureActive )
        return;

    if ( (targetKind == TGemNotificationEntry::TARGET_VEHICLE &&
          (targetProtoId < 0 || (size_t)targetProtoId >= _vhclProtos.size())) ||
         (targetKind == TGemNotificationEntry::TARGET_WEAPON &&
          (targetProtoId < 0 || (size_t)targetProtoId >= _weaponProtos.size())) ||
         (targetKind == TGemNotificationEntry::TARGET_BUILDING &&
          (targetProtoId < 0 || (size_t)targetProtoId >= _buildProtos.size())) )
        return;

    TGemNotificationEntry entry;
    entry.TargetKind = targetKind;
    entry.ChangeKind = changeKind;
    entry.TargetProtoId = targetProtoId;
    entry.PreviousRawValue = previousRawValue;
    entry.NewRawValue = newRawValue;
    entry.ActionOrder = _gemNotificationActionOrder++;
    entry.NewlyEnabled = changeKind == TGemNotificationEntry::CHANGE_ENABLE && newlyEnabled;
    entry.AlreadyUnlocked = (changeKind == TGemNotificationEntry::CHANGE_ENABLE &&
                             previousRawValue != 0 && newRawValue != 0);
    _gemNotificationEntries.push_back(entry);
}

void NC_STACK_ypaworld::FinishGemNotificationCapture(bool successful)
{
    _gemNotificationCaptureActive = false;

    if ( !successful )
    {
        _gemNotificationEntries.clear();
        return;
    }

    for (TGemNotificationEntry &entry : _gemNotificationEntries)
    {
        if ( entry.TargetKind != TGemNotificationEntry::TARGET_WEAPON )
            continue;

        std::vector<int32_t> candidates;
        for (const TGemNotificationEntry &other : _gemNotificationEntries)
        {
            if ( other.TargetKind != TGemNotificationEntry::TARGET_VEHICLE ||
                 other.TargetProtoId < 0 ||
                 (size_t)other.TargetProtoId >= _vhclProtos.size() ||
                 !yw_VehicleReferencesWeapon(_vhclProtos[other.TargetProtoId], entry.TargetProtoId) )
                continue;

            if ( std::find(candidates.begin(), candidates.end(), other.TargetProtoId) == candidates.end() )
                candidates.push_back(other.TargetProtoId);
        }

        if ( candidates.size() == 1 )
        {
            entry.RelatedVehicleId = candidates.front();
            continue;
        }

        if ( !candidates.empty() )
            continue;

        for (size_t vehicleId = 0; vehicleId < _vhclProtos.size(); ++vehicleId)
        {
            if ( yw_VehicleReferencesWeapon(_vhclProtos[vehicleId], entry.TargetProtoId) )
                candidates.push_back(vehicleId);

            if ( candidates.size() > 1 )
                break;
        }

        if ( candidates.size() == 1 )
            entry.RelatedVehicleId = candidates.front();
    }
}

void NC_STACK_ypaworld::PlayConfiguredGemUnlockSound()
{
    if ( !_GameShell )
        return;

    const std::string path = System::IniConf::GameGemUnlockSound.Get<std::string>();
    if ( path.empty() )
        return;

    const size_t soundId = World::SOUND_ID_GEM_UNLOCK;
    if ( soundId >= _GameShell->samples1.size() || soundId >= _GameShell->samples1_info.Sounds.size() )
        return;

    NC_STACK_sample *&sample = _GameShell->samples1[soundId];
    TSoundSource &source = _GameShell->samples1_info.Sounds[soundId];

    if ( _gemUnlockSoundAttemptedPath != path )
    {
        if ( sample )
        {
            SFXEngine::SFXe.sub_424000(&_GameShell->samples1_info, soundId);
            SFXEngine::SFXe.ForceStopSource(&_GameShell->samples1_info, soundId);
            sample->Delete();
            sample = NULL;
            source.PSample = NULL;
            source.SampleVariants.clear();
        }

        _gemUnlockSoundAttemptedPath = path;
        std::string previousRsrc = Common::Env.SetPrefix("rsrc", "data:");
        NC_STACK_wav *wav = Nucleus::CInit<NC_STACK_wav>({{NC_STACK_rsrc::RSRC_ATT_NAME, path}});
        Common::Env.SetPrefix("rsrc", previousRsrc);

        if ( !wav )
        {
            ypa_log_out("Warning: Could not load GEM unlock sample %s. Using vanilla GEM audio only.\n", path.c_str());
            return;
        }

        sample = wav;
        source.PSample = wav->GetSampleData();
    }

    if ( !sample )
        return;

    source.PSample = sample->GetSampleData();
    source.Volume = 100;
    source.Pitch = 0;
    SFXEngine::SFXe.startSound(&_GameShell->samples1_info, soundId);
}

void sub_47C1EC(NC_STACK_ypaworld *yw, TMapGem *gemProt, int *a3, int *a4)
{
    switch ( yw->_GameShell->netPlayerOwner )
    {
    case 1:
        *a3 = gemProt->NwVprotoNum1;
        *a4 = gemProt->NwBprotoNum1;
        break;

    case 6:
        *a3 = gemProt->NwVprotoNum2;
        *a4 = gemProt->NwBprotoNum2;
        break;

    case 3:
        *a3 = gemProt->NwVprotoNum3;
        *a4 = gemProt->NwBprotoNum3;
        break;

    case 4:
        *a3 = gemProt->NwVprotoNum4;
        *a4 = gemProt->NwBprotoNum4;
        break;

    default:
        *a3 = 0;
        *a4 = 0;
        break;
    }
}

void sub_47C29C(NC_STACK_ypaworld *yw, cellArea *cell, int a3)
{
    TMapGem &gem = yw->_techUpgrades[a3];

    int a3a, a4;
    sub_47C1EC(yw, &gem, &a3a, &a4);

    yw->_upgradeId = a3;
    yw->_upgradeTimeStamp = yw->_timeStamp;
    yw->_upgradeVehicleId = a3a;
    yw->_upgradeWeaponId = 0;
    yw->_upgradeBuildId = a4;

    bool newUiCapture = System::IniConf::GameGemUnlockNewUI.Get<bool>();
    if ( newUiCapture )
        yw->BeginGemNotificationCapture();
    else
        yw->ClearGemNotificationCapture();

    if ( a3a )
    {
        bool wasEnabled = (yw->_vhclProtos[a3a].disable_enable_bitmask &
                           (1 << yw->_GameShell->netPlayerOwner)) != 0;
        yw->_vhclProtos[a3a].disable_enable_bitmask = 0;
        yw->_vhclProtos[a3a].disable_enable_bitmask |= 1 << yw->_GameShell->netPlayerOwner;
        yw->RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, a3a,
                                        TGemNotificationEntry::CHANGE_ENABLE,
                                        wasEnabled ? 1 : 0, 1, !wasEnabled);
    }

    if ( a4 )
    {
        bool wasEnabled = (yw->_buildProtos[a4].EnableMask &
                           (1 << yw->_GameShell->netPlayerOwner)) != 0;
        yw->_buildProtos[a4].EnableMask = 0;
        yw->_buildProtos[a4].EnableMask |= 1 << yw->_GameShell->netPlayerOwner;
        yw->RecordGemNotificationChange(TGemNotificationEntry::TARGET_BUILDING, a4,
                                        TGemNotificationEntry::CHANGE_ENABLE,
                                        wasEnabled ? 1 : 0, 1, !wasEnabled);
    }

    if ( newUiCapture )
        yw->FinishGemNotificationCapture(true);

    yw_arg159 v14;
    v14.txt = Locale::Text::Feedback(Locale::FEEDBACK_TECHUP);
    v14.unit = 0;
    v14.Priority = 48;

    if ( gem.Type )
        v14.MsgID = World::Log::GetUpgradeLogID(gem.Type);
    else
        v14.MsgID = 0;

    yw->ypaworld_func159(&v14);
    yw->PlayConfiguredGemUnlockSound();

    if ( yw->_isNetGame && yw->_netExclusiveGem )
    {
        uamessage_upgrade upMsg;
        upMsg.msgID = UAMSG_UPGRADE;
        upMsg.owner = yw->_GameShell->netPlayerOwner;
        upMsg.enable = 1;
        upMsg.upgradeID = a3;

        yw->NetBroadcastMessage(&upMsg, sizeof(upMsg), true);
    }

    cell->PurposeType = cellArea::PT_TECHDEACTIVE;
}

void ypaworld_func129__sub1(NC_STACK_ypaworld *yw, cellArea *cell, int a3)
{
    TMapGem &gem = yw->_techUpgrades[a3];

    int a3a;
    int a4;

    sub_47C1EC(yw, &gem, &a3a, &a4);

    if ( a3a )
        yw->_vhclProtos[a3a].disable_enable_bitmask = 0;

    if ( a4 )
        yw->_buildProtos[a4].EnableMask = 0;

    std::string v13 = Locale::Text::Feedback(Locale::FEEDBACK_TECHDOWN) + gem.MsgDefault;

    yw_arg159 arg159;
    arg159.unit = 0;
    arg159.Priority = 80;
    arg159.txt = v13;
    arg159.MsgID = 29;

    yw->ypaworld_func159(&arg159);

    if ( yw->_isNetGame )
    {
        uamessage_upgrade upMsg;
        upMsg.msgID = UAMSG_UPGRADE;
        upMsg.owner = yw->_GameShell->netPlayerOwner;
        upMsg.enable = 0;
        upMsg.upgradeID = a3;

        yw->NetBroadcastMessage(&upMsg, sizeof(upMsg), true);
    }

    cell->PurposeIndex = 0;
    cell->PurposeType = cellArea::PT_NONE;
}

void NC_STACK_ypaworld::yw_ActivateWunderstein(cellArea *cell, int gemid)
{
    _upgradeVehicleId = 0;
    _upgradeBuildId = 0;
    _upgradeWeaponId = 0;

    _upgradeId = gemid;
    _upgradeTimeStamp = _timeStamp;

    bool newUiCapture = System::IniConf::GameGemUnlockNewUI.Get<bool>();
    if ( newUiCapture )
        BeginGemNotificationCapture();
    else
        ClearGemNotificationCapture();

    TMapGem &gem = _techUpgrades[gemid];
    bool parseSuccessful = false;

    if ( !gem.ScriptFile.empty() )
    {
        parseSuccessful = LoadProtosScript(gem.ScriptFile);
        if ( !parseSuccessful )
            ypa_log_out("yw_ActivateWunderstein: ERROR parsing script %s.\n", gem.ScriptFile.c_str());
    }
    else
    {
        std::string tmp = Common::Env.SetPrefix("rsrc", "data:");

        ScriptParser::HandlersList parsers {
            new World::Parsers::VhclProtoParser(this),
            new World::Parsers::WeaponProtoParser(this),
            new World::Parsers::BuildProtoParser(this)
        };

        parseSuccessful = ScriptParser::ParseStringList(gem.ActionsList, parsers, ScriptParser::FLAG_NO_SCOPE_SKIP);
        Common::Env.SetPrefix("rsrc", tmp);
    }

    if ( newUiCapture )
        FinishGemNotificationCapture(parseSuccessful);

    yw_arg159 arg159;
    arg159.unit = NULL;
    arg159.Priority = 48;
    arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_TECHUP);

    if ( gem.Type )
        arg159.MsgID = World::Log::GetUpgradeLogID(gem.Type);
    else
        arg159.MsgID = 0;

    ypaworld_func159(&arg159);

    if ( parseSuccessful )
        PlayConfiguredGemUnlockSound();

    cell->PurposeType = cellArea::PT_TECHDEACTIVE;
}

void sub_44FD6C(NC_STACK_ypaworld *yw, const cellArea &cell, int bldX, int bldY)
{
    Common::Point dist = yw->_viewerBact->_cellId.AbsDistance( cell.CellId );

    if ( dist.x + dist.y <= (yw->_renderSectors - 1) / 2 )
    {
        const TLego &v10 = yw->_legoArray[  yw->GetLegoBld(&cell, bldX, bldY) ];

        vec3d ttt = World::SectorIDToCenterPos3( cell.CellId );
        ttt.y = cell.height;

        if ( cell.SectorType != 1 )
        {
            ttt.x += (bldX - 1) * 300.0;
            ttt.z += (bldY - 1) * 300.0;
        }

        for (const TLego::ExFX &fx : v10.Explosions)
        {
            if ( fx.Index >= yw->_fxLimit )
                break;

            ypaworld_arg146 arg146;
            arg146.vehicle_id = fx.ObjectID;
            arg146.pos = fx.Position + ttt;

            NC_STACK_ypabact *boom = yw->ypaworld_func146(&arg146);

            if ( boom )
            {
                boom->_owner = 0;

                setState_msg arg78;
                arg78.newStatus = BACT_STATUS_DEAD;
                arg78.setFlags = BACT_STFLAG_DEATH1;
                arg78.unsetFlags = 0;
                boom->SetState(&arg78);

                yw->ypaworld_func134(boom);

                bact_arg83 arg83;
                arg83.pos = arg146.pos;
                arg83.energ = 40000;
                arg83.pos2.x = fx.Position.x;
                arg83.pos2.y = -150.0;
                arg83.pos2.z = fx.Position.z;

                float tmp = arg83.pos2.length();

                if ( tmp > 0.1 )
                    arg83.pos2 /= tmp;

                arg83.force = 30.0;
                arg83.mass = 50.0;

                boom->ApplyImpulse(&arg83);
            }
        }
    }
}

void ypaworld_func129__sub0(NC_STACK_ypaworld *yw, const cellArea &cell, yw_arg129 *arg)
{
    if ( cell.PurposeType == cellArea::PT_POWERSTATION )
    {
        if ( cell.owner == yw->_userRobo->_owner )
        {
            if ( arg->unit )
            {
                if ( yw->_userRobo->_owner != arg->unit->_owner && yw->_timeStamp - yw->_msgTimestampPSUnderAtk > 5000 )
                {
                    yw->_msgTimestampPSUnderAtk = yw->_timeStamp;

                    yw_arg159 arg159;
                    arg159.unit = NULL;
                    arg159.Priority = 77;
                    arg159.MsgID = 33;

                    yw->ypaworld_func159(&arg159);
                }
            }
        }
    }
}



void NC_STACK_ypaworld::ypaworld_func129(yw_arg129 *arg)
{
    if ( arg->unit && IsSpectatorBact(arg->unit) )
        return;

    Common::Point sec = World::PositionToSectorID( arg->pos );
    cellArea &cell = _cells.At(sec);

    if ( cell.IsGamePlaySector() && cell.PurposeType != cellArea::PT_CONSTRUCTING )
    {
        int v8 = (int)(arg->pos.x / 150.0) % 8;

        int v10;

        if ( v8 < 3 )
            v10 = 1;
        else if ( v8 < 5)
            v10 = 2;
        else
            v10 = 3;

        v8 = (int)(-arg->pos.z / 150.0) % 8;

        int v14;

        if ( v8 < 3 )
            v14 = 1;
        else if ( v8 < 5)
            v14 = 2;
        else
            v14 = 3;

        if ( v10 && v14 )
        {
            int bldY;
            int bldX;

            if ( cell.SectorType == 1 )
            {
                bldY = 0;
                bldX = 0;
            }
            else
            {
                bldX = v10 - 1;
                bldY = 2 - (v14 - 1);
            }

            int v16 = cell.buildings_health.At(bldX, bldY);

            int v34 = v16 - arg->field_10 * (100 - _legoArray[  GetLegoBld(&cell, bldX, bldY)  ].Shield ) / 100 / 400;

            if ( v34 < 0 )
                v34 = 0;
            else if ( v34 > 255 )
                v34 = 255;

            int v18 = _buildHealthModelId[v16];
            int v36 = _buildHealthModelId[v34];

            if ( v18 > v36 )
            {
                while ( v18 > v36 )
                {
                    sub_44FD6C(this, cell, bldX, bldY);

                    v18--;
                }
            }
            else if ( v18 < v36 )
            {
                while ( v18 < v36 )
                {
                    sub_44FD6C(this, cell, bldX, bldY);

                    v18++;
                }
            }

            cell.buildings_health.At(bldX, bldY) = v34;

            ypaworld_func129__sub0(this, cell, arg);

            CellCheckHealth(&cell, arg->OwnerID, arg->unit);

            if ( cell.PurposeType == cellArea::PT_TECHUPGRADE )
            {
                if ( _userRobo && _userRobo->_owner == cell.owner )
                {
                    if ( _isNetGame )
                        sub_47C29C(this, &cell, cell.PurposeIndex);
                    else
                        yw_ActivateWunderstein(&cell, cell.PurposeIndex);

                    HistoryEventAdd( World::History::Upgrade(sec.x, sec.y, cell.owner, _techUpgrades[ _upgradeId ].Type, _upgradeVehicleId, _upgradeWeaponId, _upgradeBuildId) );
                }
            }
            else if ( cell.PurposeType == cellArea::PT_TECHDEACTIVE )
            {
                if ( _isNetGame )
                {
                    int v27 = 0;

                    for (auto hlth : cell.buildings_health)
                        v27 += hlth;

                    if ( !v27 )
                        ypaworld_func129__sub1(this, &cell, cell.PurposeIndex);
                }
            }
        }
    }
}


size_t NC_STACK_ypaworld::GetSectorInfo(yw_130arg *arg)
{
    arg->CellId = World::PositionToSectorID( arg->pos_x, arg->pos_z );

    if ( !IsSector( arg->CellId ) )
    {
        ypa_log_out("YWM_GETSECTORINFO %d %d max: %d %d\n", arg->CellId.x, arg->CellId.y, _mapSize.x, _mapSize.y);
        ypa_log_out("YWM_GETSECTORINFO ausserhalb!!!\n");

        arg->pcell = NULL;

        return 0;
    }

    arg->pcell = &_cells(arg->CellId);
    return 1;
}


void NC_STACK_ypaworld::ypaworld_func131(NC_STACK_ypabact *bact)
{
    _viewerBact = bact;

    setYW_userVehicle(bact);
}


void NC_STACK_ypaworld::ypaworld_func132(void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func132");
}


void NC_STACK_ypaworld::ypaworld_func133(void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func133");
}


void NC_STACK_ypaworld::ypaworld_func134(NC_STACK_ypabact *bact)
{
    newMaster_msg arg73;

    arg73.bact = NULL;
    arg73.list = &_unitsList;

    bact->SetNewMaster(&arg73);
}


void NC_STACK_ypaworld::ypaworld_func135(void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func135");
}


void NC_STACK_ypaworld::ypaworld_func136(ypaworld_arg136 *arg)
{
    arg->tVal = 2.0;
    arg->isect = 0;

    vec3d stpos = arg->stPos;

    float pos_xx = stpos.x + arg->vect.x;
    float pos_zz = stpos.z + arg->vect.z;

    int dx = (stpos.x + 150) / 300;
    int dxx = (pos_xx + 150) / 300;

    int dz = (-stpos.z + 150) / 300;

    int dzz = (-pos_zz + 150) / 300;

    int elems = 0;
    TSectorCollision a6[4];

    if ( dx == dxx && dz == dzz )
    {
        elems = 1;
        a6[0] = sub_44DBF8(dx, dz, dx, dz, arg->flags);
    }
    else if ( dx == dxx || dz == dzz )
    {
        elems = 2;
        a6[0] = sub_44DBF8(dx, dz, dx,  dz,  arg->flags);
        a6[1] = sub_44DBF8(dx, dz, dxx, dzz, arg->flags);
    }
    else
    {
        elems = 4;
        a6[0] = sub_44DBF8(dx, dz, dx,  dz,  arg->flags);
        a6[1] = sub_44DBF8(dx, dz, dx,  dzz, arg->flags);
        a6[2] = sub_44DBF8(dx, dz, dxx, dz,  arg->flags);
        a6[3] = sub_44DBF8(dx, dz, dxx, dzz, arg->flags);
    }

    for (int i = 0; i < elems; i++)
    {
        if ( a6[i].CollisionType )
        {
            if ( a6[i].CollisionType != 1)
                sub_44E07C(a6[i]);

            arg->stPos = stpos - a6[i].pos;

            sub_44D8B8(arg, a6[i]);

            if ( arg->isect )
                break;
        }
    }
}


void NC_STACK_ypaworld::ypaworld_func137(ypaworld_arg137 *arg)
{
    arg->coll_count = 0;

    vec3d pos = arg->pos;

    int dxx = (pos.x + 150) / 300;
    int dzz = (-pos.z + 150) / 300;
    int xxmr = (pos.x - arg->radius + 150) / 300;
    int zzmr = (-(pos.z - arg->radius) + 150) / 300;
    int xxpr = (pos.x + arg->radius + 150) / 300;
    int zzpr = (-(pos.z + arg->radius) + 150) / 300;

    TSectorCollision a6;

    for (int i = 0; i < 9; i++)
    {
        a6.CollisionType = 0;

        switch ( i )
        {
        case 0:
            a6 = sub_44DBF8(dxx, dzz, dxx, dzz, arg->field_30);
            break;

        case 1:
            if ( dxx != xxmr )
                a6 = sub_44DBF8(dxx, dzz, xxmr, dzz, arg->field_30);
            break;

        case 2:
            if ( dxx != xxpr )
                a6 = sub_44DBF8(dxx, dzz, xxpr, dzz, arg->field_30);
            break;

        case 3:
            if ( dzz != zzmr )
                a6 = sub_44DBF8(dxx, dzz, dxx, zzmr, arg->field_30);
            break;

        case 4:
            if ( dzz != zzpr )
                a6 = sub_44DBF8(dxx, dzz, dxx, zzpr, arg->field_30);
            break;

        case 5:
            if ( dxx != xxmr && dzz != zzmr )
                a6 = sub_44DBF8(dxx, dzz, xxmr, zzmr, arg->field_30);
            break;

        case 6:
            if ( dxx != xxpr && dzz != zzmr )
                a6 = sub_44DBF8(dxx, dzz, xxpr, zzmr, arg->field_30);
            break;

        case 7:
            if ( dxx != xxpr && dzz != zzpr )
                a6 = sub_44DBF8(dxx, dzz, xxpr, zzpr, arg->field_30);
            break;

        case 8:
            if ( dxx != xxmr && dzz != zzpr )
                a6 = sub_44DBF8(dxx, dzz, xxmr, zzpr, arg->field_30);
            break;
        }

        if ( a6.CollisionType )
        {
            if ( a6.CollisionType != 1 )
                sub_44E07C(a6);

            arg->pos = pos - a6.pos;

            ypaworld_func137__sub0(arg, a6);
        }
    }
}


void NC_STACK_ypaworld::ypaworld_func138(void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func138");
}


void NC_STACK_ypaworld::ypaworld_func139(GuiBase *lstvw)
{
    if ( !(lstvw->flags & GuiBase::FLAG_WITH_ICON) )
        lstvw->flags &= ~GuiBase::FLAG_ICONIFED;

    if ( lstvw->flags & GuiBase::FLAG_IN_LIST )
        ypaworld_func140(lstvw);

    lstvw->Attach(_guiActive);

    lstvw->flags |= GuiBase::FLAG_IN_LIST;

    if ( lstvw->flags & GuiBase::FLAG_WITH_ICON )
        lstvw->iconBox.pobject = lstvw;

    lstvw->pobject = lstvw;

    if ( lstvw->flags & GuiBase::FLAG_ICONIFED )
        Input::Engine.AddClickBoxFront(&lstvw->iconBox);
    else if ( lstvw->IsOpen() )
        Input::Engine.AddClickBoxFront(lstvw);
}


void NC_STACK_ypaworld::ypaworld_func140(GuiBase *lstvw)
{
    if ( lstvw->flags & GuiBase::FLAG_IN_LIST )
    {
        lstvw->Detach();

        lstvw->flags &= ~GuiBase::FLAG_IN_LIST;

        if ( lstvw->flags & GuiBase::FLAG_ICONIFED )
            Input::Engine.RemClickBox(&lstvw->iconBox);
        else if ( lstvw->IsOpen() )
            Input::Engine.RemClickBox(lstvw);
    }
}


void NC_STACK_ypaworld::ypaworld_func143(void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func143");
}


void NC_STACK_ypaworld::ypaworld_func144(NC_STACK_ypabact *bacto)
{
    if ( bacto->_bact_type == BACT_TYPES_MISSLE )
    {
        if ( bacto->_primTtype )
            ypa_log_out("OH NO! The DEATH CACHE BUG is back!\n");
    }

    SFXEngine::SFXe.StopCarrier(&bacto->_soundcarrier);

    newMaster_msg cache;
    cache.bact = NULL;
    cache.list = &_deadCacheList;

    bacto->SetNewMaster(&cache);

    bact_arg80 v6;
    v6.pos.x = 600.0;
    v6.pos.y = -50000.0;
    v6.pos.z = -600.0;
    v6.field_C = 2;

    bacto->SetPosition(&v6);

    bacto->_status_flg |= BACT_STFLAG_NORENDER;
}


size_t NC_STACK_ypaworld::ypaworld_func145(NC_STACK_ypabact *bact)
{
    if ( _viewerBact )
    {
        Common::Point dist = _viewerBact->_cellId.AbsDistance( bact->_cellId );
        if ( dist.x + dist.y <= (_renderSectors - 1) / 2 )
            return 1;
    }

    for ( NC_STACK_ypabact* &station : _unitsList ) //Robos
    {
        if ( station->_status_flg & BACT_STFLAG_ISVIEW )
        {
            Common::Point dist = station->_cellId.AbsDistance( bact->_cellId );
            if ( dist.x + dist.y <= (_renderSectors - 1) / 2 )
                return 1;
        }


        for ( NC_STACK_ypabact* &comm : station->_kidList ) // Squad comms
        {
            if ( comm->_status_flg & BACT_STFLAG_ISVIEW )
            {
                Common::Point dist = comm->_cellId.AbsDistance( bact->_cellId );
                if ( dist.x + dist.y <= (_renderSectors - 1) / 2 )
                    return 1;
            }


            for ( NC_STACK_ypabact* &unit : comm->_kidList ) // Squad units
            {
                if ( unit->_status_flg & BACT_STFLAG_ISVIEW )
                {
                    Common::Point dist = unit->_cellId.AbsDistance( bact->_cellId );
                    if ( dist.x + dist.y <= (_renderSectors - 1) / 2 )
                        return 1;
                }
            }
        }
    }

    return 0;
}


NC_STACK_ypabact * NC_STACK_ypaworld::ypaworld_func146(ypaworld_arg146 *vhcl_id)
{
    if ( vhcl_id->vehicle_id <= 0 || (size_t)vhcl_id->vehicle_id >= _vhclProtos.size() )
        return NULL;

    World::TVhclProto &requestedVhcl = _vhclProtos[vhcl_id->vehicle_id];
    int mimicDisguiseVehicleId = requestedVhcl.is_mimic ?
        yw_SelectMimicVehicleID(_vhclProtos, vhcl_id->vehicle_id) :
        0;
    World::TVhclProto *spawnProto = mimicDisguiseVehicleId > 0 ?
        &_vhclProtos[mimicDisguiseVehicleId] :
        (requestedVhcl.is_mimic ? NULL : &requestedVhcl);

    if ( !spawnProto )
        return NULL;

    World::TVhclProto &vhcl = *spawnProto;
    World::TVhclProto &deathProto = requestedVhcl.is_mimic ? requestedVhcl : vhcl;

    NC_STACK_ypabact *bacto = yw_createUnit(vhcl.model_id);

    if ( bacto )
    {
        bacto->_energy = vhcl.energy;
        bacto->_energy_max = vhcl.energy;
        bacto->_invulnerable = vhcl.invulnerable;
        bacto->_shield = vhcl.shield;
        bacto->_mass = vhcl.mass;
        bacto->_base_force = vhcl.force;
        bacto->_base_maxrot = vhcl.maxrot;
        bacto->_force = vhcl.force;
        bacto->_maxrot = vhcl.maxrot;
        bacto->_height = vhcl.height;
        bacto->_radius = vhcl.radius;
        bacto->_viewer_radius = vhcl.vwr_radius;

        // OpenUA: universal compound collision spheres. Authored coll_* spheres
        // win. Missing radius selects automatic collision implicitly, while
        // auto_collision selects it explicitly and retains radius as metadata.
        World::rbcolls spawnColl = vhcl.coll;
        bool useAutoCollisionSpheres = vhcl.model_id != BACT_TYPES_ROBO &&
                                       !vhcl.coll_defined &&
                                       (vhcl.auto_collision || !vhcl.radius_defined);
        if ( useAutoCollisionSpheres && spawnColl.roboColls.empty() )
        {
            auto getModel = [this](int16_t id) -> NC_STACK_base *
            {
                if ( id < 0 || (size_t)id >= _vhclModels.size() )
                    return NULL;
                return _vhclModels.at(id);
            };

            NC_STACK_base *autoCollVP = getModel(vhcl.vp_normal);
            if ( !autoCollVP )
                autoCollVP = getModel(vhcl.vp_wait);
            if ( !autoCollVP )
                autoCollVP = getModel(vhcl.vp_fire);
            if ( !autoCollVP )
                autoCollVP = getModel(vhcl.vp_genesis);

            spawnColl = yw_GenerateAutoCollSpheresFromVP(autoCollVP, vhcl.vp_scale,
                                                         vhcl.vp_orientation, true, true, false);

            // Cache successful generation in the shared prototype. This keeps
            // repeated spawns cheap while preserving coll_defined=false, so the
            // data still knows these were automatic, not authored.
            if ( !spawnColl.roboColls.empty() )
                vhcl.coll = spawnColl;
        }

        // Automatic collision must never fall back invisibly to the red legacy
        // body. VPs without usable polygon geometry use one compound fallback
        // sphere based on the compatibility radius.
        if ( useAutoCollisionSpheres && spawnColl.roboColls.empty() &&
             vhcl.radius > 0.01 )
        {
            World::TRoboColl fallback;
            fallback.robo_coll_radius = vhcl.radius;
            spawnColl.roboColls.push_back(fallback);
            vhcl.coll = spawnColl;
        }

        bacto->_collNodes = spawnColl;
        bacto->_autoCollisionSpheres = useAutoCollisionSpheres && !spawnColl.roboColls.empty();
        bacto->_overeof = vhcl.overeof;
        bacto->_viewer_overeof = vhcl.vwr_overeof;

        float automaticRadius = 0.0f;
        float automaticViewerRadius = 0.0f;
        float automaticOvereof = 0.0f;
        bool useAutomaticBounds = vhcl.auto_collision && vhcl.model_id != BACT_TYPES_ROBO &&
                                  yw_GetVehicleAutoCollisionBounds(spawnColl, vhcl.model_id,
                                                                  &automaticRadius,
                                                                  &automaticViewerRadius,
                                                                  &automaticOvereof);

        if ( useAutomaticBounds )
        {
            static std::vector<bool> loggedAutomaticBounds;
            if ( loggedAutomaticBounds.size() <= (size_t)vhcl_id->vehicle_id )
                loggedAutomaticBounds.resize((size_t)vhcl_id->vehicle_id + 1, false);
            if ( !loggedAutomaticBounds[vhcl_id->vehicle_id] )
            {
                loggedAutomaticBounds[vhcl_id->vehicle_id] = true;
                float largestSphere = 0.0f;
                float minX = std::numeric_limits<float>::max();
                float maxX = -std::numeric_limits<float>::max();
                float minZ = std::numeric_limits<float>::max();
                float maxZ = -std::numeric_limits<float>::max();
                float highestCenterY = -std::numeric_limits<float>::max();
                for (const World::TRoboColl &sphere : spawnColl.roboColls)
                {
                    if ( sphere.robo_coll_radius <= 0.01f )
                        continue;
                    largestSphere = std::max(largestSphere, sphere.robo_coll_radius);
                    minX = std::min(minX, (float)sphere.coll_pos.x - sphere.robo_coll_radius);
                    maxX = std::max(maxX, (float)sphere.coll_pos.x + sphere.robo_coll_radius);
                    minZ = std::min(minZ, (float)sphere.coll_pos.z - sphere.robo_coll_radius);
                    maxZ = std::max(maxZ, (float)sphere.coll_pos.z + sphere.robo_coll_radius);
                    highestCenterY = std::max(highestCenterY, (float)sphere.coll_pos.y);
                }

                ypa_log_out("[AUTO_BOUNDS] vehicle=%d model=%d spheres=%u"
                            " legacy=(radius=%.3f vwr_radius=%.3f overeof=%.3f vwr_overeof=%.3f)"
                            " automatic=(radius=%.3f vwr_radius=%.3f overeof=%.3f vwr_overeof=%.3f)"
                            " compound=(largest_sphere=%.3f span_x=%.3f span_z=%.3f max_center_y=%.3f)"
                            " model_y=(valid=%d min=%.3f max=%.3f)\n",
                            vhcl_id->vehicle_id, vhcl.model_id,
                            (unsigned)spawnColl.roboColls.size(),
                            vhcl.radius, vhcl.vwr_radius, vhcl.overeof, vhcl.vwr_overeof,
                            automaticRadius, automaticViewerRadius,
                            automaticOvereof, automaticOvereof,
                            largestSphere, maxX - minX, maxZ - minZ, highestCenterY,
                            spawnColl.modelBoundsValid,
                            spawnColl.modelMin.y, spawnColl.modelMax.y);
            }

            if ( !vhcl.radius_defined )
                bacto->_radius = automaticRadius;
            if ( !vhcl.vwr_radius_defined )
                bacto->_viewer_radius = automaticViewerRadius;
            if ( !vhcl.overeof_defined )
                bacto->_overeof = automaticOvereof;
            if ( !vhcl.vwr_overeof_defined )
                bacto->_viewer_overeof = automaticOvereof;
        }
        else
        {
            for (const World::TRoboColl &cs : bacto->_collNodes.roboColls)
            {
                if ( cs.robo_coll_radius > 0.01 )
                {
                    float ext = cs.coll_pos.length() + cs.robo_coll_radius;
                    if ( ext > bacto->_radius )
                        bacto->_radius = ext;
                }
            }
        }
        bacto->_airconst = vhcl.airconst;
        bacto->_airconst_static = vhcl.airconst;
        bacto->_adist_sector = vhcl.adist_sector;
        bacto->_adist_bact = vhcl.adist_bact;
        bacto->_sdist_sector = vhcl.sdist_sector;
        bacto->_sdist_bact = vhcl.sdist_bact;
        bacto->_radar = vhcl.radar;
        bacto->_gun_radius = vhcl.gun_radius;
        bacto->_gun_power = vhcl.gun_power;
        bacto->_pitch_max = vhcl.max_pitch;
        bacto->_vehicleID = vhcl_id->vehicle_id;
        bacto->_mimic_disguise_vehicleID = mimicDisguiseVehicleId > 0 ? mimicDisguiseVehicleId : 0;
        bacto->_isDummy = vhcl.is_dummy != 0; // OpenUA: model = dummy -> inert module
        bacto->_weapon = vhcl.weapon;
        bacto->_extra_weapons = vhcl.extra_weapons;
        bacto->_weapon_switch_mode = vhcl.weapon_switch_mode;
        bacto->_weapon_slot_index = 0;
        bacto->_current_weapon_id = vhcl.weapon;
        bacto->_lowhp_weapon_enable = vhcl.lowhp_weapon_enable;
        bacto->_lowhp_threshold = vhcl.lowhp_threshold > 0.0 ? vhcl.lowhp_threshold : 0.30;
        bacto->_lowhp_weapon = vhcl.lowhp_weapon > 0 ? vhcl.lowhp_weapon : 0;

        if ( vhcl.weapon == -1 )
            bacto->_weapon_flags = 0;
        else
            bacto->_weapon_flags = _weaponProtos.at( vhcl.weapon )._weaponFlags;

        bacto->_mgun = vhcl.mgun;
        bacto->_mgun_set = vhcl.mgun_set;
        bacto->_num_mguns = vhcl.num_mguns > 0 ? vhcl.num_mguns : 1;
        bacto->_mgun_shot_time = vhcl.mgun_shot_time;
        bacto->_mgun_shot_time_user = vhcl.mgun_shot_time_user;
        bacto->_mgun_vp_dead = vhcl.mgun_vp_dead;
        bacto->_mgun_vp_megadeth = vhcl.mgun_vp_megadeth;
        bacto->_mgun_power = vhcl.mgun_power;
        bacto->_mgun_angle = vhcl.mgun_angle;
        bacto->_mgun_power_set = vhcl.mgun_power_set;
        bacto->_mgun_angle_set = vhcl.mgun_angle_set;
        bacto->_mgun_ai_fire_alignment = vhcl.mgun_ai_fire_alignment;
        bacto->_mgun_damage_sectors = vhcl.mgun_damage_sectors;
        bacto->_mgun_sector_damage_accum = 0.0;
        bacto->_weapon_spread_x = vhcl.weapon_spread_x;
        bacto->_weapon_spread_y = vhcl.weapon_spread_y;
        bacto->_mgun_spread_x = vhcl.mgun_spread_x;
        bacto->_mgun_spread_y = vhcl.mgun_spread_y;
        bacto->_weapon_spread_x_user = vhcl.weapon_spread_x_user;
        bacto->_weapon_spread_y_user = vhcl.weapon_spread_y_user;
        bacto->_weapon_spread_x_user_set = vhcl.weapon_spread_x_user_set;
        bacto->_weapon_spread_y_user_set = vhcl.weapon_spread_y_user_set;
        bacto->_fire_pos.x = vhcl.fire_x;
        bacto->_fire_pos.y = vhcl.fire_y;
        bacto->_fire_pos.z = vhcl.fire_z;
        bacto->_fire_x_mode = vhcl.fire_x_mode;
        bacto->_fire_x_start = vhcl.fire_x_start;
        bacto->_fire_x_step = vhcl.fire_x_step;
        bacto->_fire_x_slots = vhcl.fire_x_slots;
        bacto->_fire_x_advanced = vhcl.fire_x_advanced;
        bacto->_cockpit_camera_enable = vhcl.cockpit_camera_enable;
        bacto->_cockpit_camera_offset = vhcl.cockpit_camera_offset;
        bacto->_mgun_pov_fx_enable = vhcl.mgun_pov_fx_enable;
        bacto->_mgun_pov_fx_vp = vhcl.mgun_pov_fx_vp;
        bacto->_mgun_pov_num_mguns_fx = vhcl.mgun_pov_num_mguns_fx;
        bacto->_mgun_pov_fx_scale = vhcl.mgun_pov_fx_scale;
        bacto->_mgun_pov_fx_offset = vhcl.mgun_pov_fx_offset;
        bacto->_mgun_pov_fx_rot = vhcl.mgun_pov_fx_rot;
        bacto->_gun_angle = vhcl.gun_angle;
        bacto->_gun_angle_user = vhcl.gun_angle;
        bacto->_num_weapons = vhcl.num_weapons;
        bacto->_kill_after_shot = vhcl.kill_after_shot;
        bacto->_vp_normal = _vhclModels.at( vhcl.vp_normal );
        bacto->_vp_fire = _vhclModels.at( vhcl.vp_fire );
        bacto->_vp_dead = _vhclModels.at( vhcl.vp_dead );
        bacto->_vp_wait = _vhclModels.at( vhcl.vp_wait );
        bacto->_vp_megadeth = _vhclModels.at( vhcl.vp_megadeth );
        bacto->_vp_genesis = _vhclModels.at( vhcl.vp_genesis );
        bacto->_vp_scale = vhcl.vp_scale;
        bacto->_vp_tint = vhcl.vp_tint;
        if ( requestedVhcl.is_mimic && !requestedVhcl.mimic_vp_tint.IsNeutral() )
        {
            bacto->_vp_tint.r *= requestedVhcl.mimic_vp_tint.r;
            bacto->_vp_tint.g *= requestedVhcl.mimic_vp_tint.g;
            bacto->_vp_tint.b *= requestedVhcl.mimic_vp_tint.b;
            bacto->_vp_tint.a *= requestedVhcl.mimic_vp_tint.a;
            bacto->_vp_tint.Clamp();
        }
        bacto->_vp_orientation = vhcl.vp_orientation;
        bacto->_vp_spin_strength = vhcl.vp_spin;
        bacto->_vp_trail_scale = vec3d(1.0, 1.0, 1.0);
        bacto->_vp_trail_tint = World::TVisualTint();
        bacto->_vp_trail_spin_strength = vec3d(0.0, 0.0, 0.0);
        bacto->_damaged_fx = vhcl.damaged_fx;
        bacto->_damaged_fx_next_time = 0;
        bacto->_decoration_fx = vhcl.decoration_fx;
        bacto->_decoration_fx_next_time = 0;
        bacto->_damaged_force_malus = vhcl.damaged_force_malus;
        bacto->_damaged_maxrot_malus = vhcl.damaged_maxrot_malus;
        bacto->_damaged_snd_pitch_mult = vhcl.damaged_snd_pitch_mult;
        bacto->_damaged_fx_active = false;
        bacto->_damaged_shake_carrier.Clear();
        bacto->_spawn_units = vhcl.spawn_units;
        bacto->_spawn_vehicle = vhcl.spawn_vehicle;
        bacto->_spawn_interval = vhcl.spawn_interval > 0 ? vhcl.spawn_interval : 5000;
        bacto->_spawn_trigger_radius = vhcl.spawn_trigger_radius > 0.0 ? vhcl.spawn_trigger_radius : 0.0;
        bacto->_spawn_random_pos = vhcl.spawn_random_pos > 0.0 ? vhcl.spawn_random_pos : 0.0;
        bacto->_spawn_max_active = vhcl.spawn_max_active > 0 ? vhcl.spawn_max_active : 0;
        bacto->_spawn_count = vhcl.spawn_count > 0 ? vhcl.spawn_count : 1;
        bacto->_spawn_instant = vhcl.spawn_instant ? 1 : 0;
        bacto->_spawn_last_time = 0;
        bacto->_spawn_at_death_units = deathProto.spawn_at_death_units;
        bacto->_spawn_at_death_vehicle = deathProto.spawn_at_death_vehicle;
        bacto->_spawn_at_death_count = deathProto.spawn_at_death_count > 0 ? deathProto.spawn_at_death_count : 1;
        if ( bacto->_spawn_at_death_count > 8 )
            bacto->_spawn_at_death_count = 8;
        bacto->_spawn_at_death_random_pos = deathProto.spawn_at_death_random_pos > 0.0 ? deathProto.spawn_at_death_random_pos : 0.0;
        bacto->_spawn_at_death_instant = deathProto.spawn_at_death_instant ? 1 : 0;
        bacto->_spawn_at_death_immunity_time = deathProto.spawn_at_death_immunity_time > 0 ? deathProto.spawn_at_death_immunity_time : 0;
        bacto->_spawn_at_death_done = false;
        bacto->_spawn_at_death_protection_end_time = 0;
        bacto->_spawn_at_death_restore_vulnerable = false;
        bacto->_death_damage = vhcl.death_damage > 0 ? vhcl.death_damage : 0;
        bacto->_death_damage_radius = vhcl.death_damage_radius > 0.0 ? vhcl.death_damage_radius : 0.0;
        bacto->_death_damage_applied_dead = false;
        bacto->_death_damage_applied_megadeth = false;
        bacto->_carrier_spawn_root_gid = 0;
        bacto->_carrier_spawn_root_vehicle = 0;
        bacto->_carrier_spawned_gids.clear();
        bacto->_proximity_defense_enable = vhcl.proximity_defense_enable;
        bacto->_proximity_defense_weapon = vhcl.proximity_defense_weapon;
        bacto->_proximity_defense_trigger_radius = vhcl.proximity_defense_trigger_radius > 0.0 ? vhcl.proximity_defense_trigger_radius : 0.0;
        bacto->_proximity_defense_interval = vhcl.proximity_defense_interval > 0 ? vhcl.proximity_defense_interval : 1000;
        bacto->_proximity_defense_shots = vhcl.proximity_defense_shots > 0 ? vhcl.proximity_defense_shots : 1;
        bacto->_proximity_defense_fire_pos = vhcl.proximity_defense_fire_pos;
        bacto->_proximity_defense_vp_launch = vhcl.proximity_defense_vp_launch;
        bacto->_proximity_defense_fire_mode = vhcl.proximity_defense_fire_mode;
        bacto->_proximity_defense_sequence_delay = vhcl.proximity_defense_sequence_delay > 0 ? vhcl.proximity_defense_sequence_delay : 100;
        bacto->_proximity_defense_at_death = vhcl.proximity_defense_at_death ? 1 : 0;
        bacto->_proximity_defense_random_yaw_set = vhcl.proximity_defense_random_yaw_set;
        bacto->_proximity_defense_random_yaw_min = vhcl.proximity_defense_random_yaw_min;
        bacto->_proximity_defense_random_yaw_max = vhcl.proximity_defense_random_yaw_max;
        if ( bacto->_proximity_defense_random_yaw_min > bacto->_proximity_defense_random_yaw_max )
        {
            float tmpYaw = bacto->_proximity_defense_random_yaw_min;
            bacto->_proximity_defense_random_yaw_min = bacto->_proximity_defense_random_yaw_max;
            bacto->_proximity_defense_random_yaw_max = tmpYaw;
        }
        bacto->_proximity_defense_random_pitch_set = vhcl.proximity_defense_random_pitch_set;
        bacto->_proximity_defense_random_pitch_min = vhcl.proximity_defense_random_pitch_min;
        bacto->_proximity_defense_random_pitch_max = vhcl.proximity_defense_random_pitch_max;
        if ( bacto->_proximity_defense_random_pitch_min > bacto->_proximity_defense_random_pitch_max )
        {
            float tmpPitch = bacto->_proximity_defense_random_pitch_min;
            bacto->_proximity_defense_random_pitch_min = bacto->_proximity_defense_random_pitch_max;
            bacto->_proximity_defense_random_pitch_max = tmpPitch;
        }
        bacto->_proximity_defense_sequence_active = false;
        bacto->_proximity_defense_sequence_shots_fired = 0;
        bacto->_proximity_defense_next_shot_time = 0;
        bacto->_proximity_defense_next_activation_time = 0;
        bacto->_proximity_defense_at_death_done = false;
        bacto->_seek_and_explode = vhcl.seek_and_explode ? 1 : 0;
        bacto->_seek_and_explode_weapon = vhcl.seek_and_explode_weapon > 0 ? vhcl.seek_and_explode_weapon : 0;
        bacto->_seek_and_explode_trigger_radius = vhcl.seek_and_explode_trigger_radius > 0.0 ? vhcl.seek_and_explode_trigger_radius : 0.0;
        bacto->_seek_and_explode_triggered = false;
        bacto->SetUnitGuns(vhcl_id->skip_unit_guns ? std::vector<World::TRoboGun>() : vhcl.unit_guns);
        bacto->SetUnitDummies(vhcl_id->skip_unit_guns ? std::vector<World::TUnitDummy>() : vhcl.unit_dummies);

        bacto->_destroyFX = vhcl.dest_fx;
        bacto->_extDestroyFX = vhcl.ExtDestroyFX;
        bacto->_chainFX = vhcl.chain_fx;

        for (NC_STACK_base *& vp_fx : bacto->_vp_fx_models)
            vp_fx = NULL;

        bacto->_scale_start = vhcl.scale_fx_p0;
        bacto->_scale_speed = vhcl.scale_fx_p1;
        bacto->_scale_accel = vhcl.scale_fx_p2;
        bacto->_scale_duration = vhcl.scale_fx_p3;
        bacto->_scale_pos = 0;

        bacto->_hidden = vhcl.hidden;
        bacto->_unhideRadar = vhcl.unhideRadar;

        // OpenUA custom: seed the per-instance invisible/stealth state from the proto.
        // A unit with `invisible = 1` spawns cloaked and stays so until its first attack.
        bacto->_invisibleUnrevealed = vhcl.invisible;
        bacto->_invisible_reveal_vp = vhcl.invisible_reveal_vp;

        for (int i = 0; vhcl.scale_fx_pXX[ i ]; i++ )
        {
            bacto->_vp_fx_models[i] = _vhclModels.at( vhcl.scale_fx_pXX[ i ] );

            bacto->_status_flg |= BACT_STFLAG_SEFFECT;
        }

        bacto->_soundcarrier.Resize(vhcl.sndFX.size());

        for (World::TVhclSound &sfx : vhcl.sndFX)
            sfx.LoadSamples();

        for (size_t i = 0; i < vhcl.sndFX.size(); i++)
        {
            TSoundSource *smpl_inf = &bacto->_soundcarrier.Sounds[ i ];

            smpl_inf->Volume = vhcl.sndFX[i].volume;
            smpl_inf->Pitch = vhcl.sndFX[i].pitch;
            smpl_inf->PriorityBias = (i == World::TVhclProto::SND_COCKPIT) ? 4096 : 0;

            if ( World::TVhclProto::IsLoopingSnd(i) )
                smpl_inf->SetLoop(true);

            if ( vhcl.sndFX[i].MainSample.Sample )
                smpl_inf->PSample = vhcl.sndFX[i].MainSample.Sample->GetSampleData();
            else
                smpl_inf->PSample = 0;

            smpl_inf->SampleVariants.clear();
            if ( smpl_inf->PSample )
                smpl_inf->SampleVariants.push_back(smpl_inf->PSample);

            for (const World::TVhclSound::TSndSample &sample : vhcl.sndFX[i].MainSampleVariants)
            {
                if ( sample.Sample )
                    smpl_inf->SampleVariants.push_back(sample.Sample->GetSampleData());
            }

            if ( vhcl.sndFX[i].sndPrm.slot )
            {
                smpl_inf->PPFx = &vhcl.sndFX[i].sndPrm;
                smpl_inf->SetPFx(true);
            }
            else
            {
                smpl_inf->SetPFx(false);
            }

            if ( vhcl.sndFX[i].sndPrm_shk.slot )
            {
                smpl_inf->PShkFx = &vhcl.sndFX[i].sndPrm_shk;
                smpl_inf->SetShk(true);
            }
            else
            {
                smpl_inf->SetShk(false);
            }

            if ( !vhcl.sndFX[i].extS.empty() )
            {
                smpl_inf->PFragments = &vhcl.sndFX[i].extS; //CHECK IT
                smpl_inf->SetFragmented(true);
            }
            else
            {
                smpl_inf->SetFragmented(false);
            }
        }

        bacto->_pitch = bacto->_soundcarrier.Sounds[0].Pitch;
        bacto->_volume = bacto->_soundcarrier.Sounds[0].Volume;
        bacto->_base_snd_normal_pitch = bacto->_soundcarrier.Sounds[World::TVhclProto::SND_NORMAL].Pitch;
        bacto->_base_snd_fire_pitch = bacto->_soundcarrier.Sounds[World::TVhclProto::SND_FIRE].Pitch;
        bacto->_base_snd_wait_pitch = bacto->_soundcarrier.Sounds[World::TVhclProto::SND_WAIT].Pitch;

        bacto->_mimic_soundcarrier.Clear();
        if ( requestedVhcl.is_mimic )
        {
            requestedVhcl.snd_mimic.LoadSamples();
            if ( requestedVhcl.snd_mimic.MainSample.Sample )
            {
                bacto->_mimic_soundcarrier.Resize(1);

                TSoundSource &snd = bacto->_mimic_soundcarrier.Sounds[0];
                snd.PSample = requestedVhcl.snd_mimic.MainSample.Sample->GetSampleData();
                snd.SampleVariants.clear();
                if ( snd.PSample )
                    snd.SampleVariants.push_back(snd.PSample);
                snd.Volume = requestedVhcl.snd_mimic.volume;
                snd.Pitch = requestedVhcl.snd_mimic.pitch;
                snd.PriorityBias = 0;
                snd.SetLoop(true);
                snd.SetPFx(false);
                snd.SetShk(false);
                snd.SetFragmented(false);
            }
        }

        bacto->SetParameters(vhcl.initParams);

        bact_arg80 arg80;
        arg80.pos = vhcl_id->pos;
        arg80.field_C = 0;
        bacto->SetPosition(&arg80);

        setState_msg arg119;
        arg119.newStatus = BACT_STATUS_NORMAL;
        arg119.setFlags = 0;
        arg119.unsetFlags = 0;
        bacto->SetStateInternal(&arg119);
    }

    return bacto;
}


NC_STACK_ypamissile * NC_STACK_ypaworld::ypaworld_func147(ypaworld_arg146 *arg)
{
    if ( arg->vehicle_id > _weaponProtos.size() )
        return NULL;

    World::TWeapProto &wproto = _weaponProtos.at(arg->vehicle_id);

    if ( !(wproto._weaponFlags & 1) )
        return NULL;

    NC_STACK_ypamissile *wobj = dynamic_cast<NC_STACK_ypamissile *>( yw_createUnit(wproto.unitID) );

    if ( !wobj )
        return NULL;

    wobj->_energy = wproto.energy;
    wobj->_energy_max = wproto.energy;
    wobj->_shield = 0;
    wobj->_mass = wproto.mass;
    wobj->_force = wproto.force;
    wobj->_base_force = wproto.force;    // OpenUA fix: ypabact_ApplyDamagedRuntime() recomputes
    wobj->_maxrot = wproto.maxrot;       // _force = _base_force * mult every frame, so missiles must seed
    wobj->_base_maxrot = wproto.maxrot;  // _base_force/_base_maxrot too, or they crawl at the 5000/0.5 defaults.
    wobj->_height = wproto.heightStd;
    wobj->_radius = wproto.radius;
    wobj->_viewer_radius = wproto.vwr_radius;
    wobj->_overeof = wproto.overeof;
    wobj->_viewer_overeof = wproto.vwr_overeof;
    wobj->_airconst = wproto.airconst;
    wobj->_airconst_static = wproto.airconst;
    wobj->_adist_sector = wproto.adistSector;
    wobj->_adist_bact = wproto.adistBact;
    wobj->_vehicleID = arg->vehicle_id;
    wobj->_weapon = 0;

    wobj->_vp_normal =   _vhclModels.at(wproto.vp_normal);
    wobj->_vp_fire =     _vhclModels.at(wproto.vp_fire);
    wobj->_vp_dead =     _vhclModels.at(wproto.vp_dead);
    wobj->_vp_wait =     _vhclModels.at(wproto.vp_wait);
    wobj->_vp_megadeth = _vhclModels.at(wproto.vp_megadeth);
    wobj->_vp_genesis =  _vhclModels.at(wproto.vp_genesis);
    wobj->_vp_scale = wproto.vp_scale;
    wobj->_vp_tint = wproto.vp_tint;
    wobj->_vp_orientation = wproto.vp_orientation;
    wobj->_vp_spin_strength = wproto.vp_spin;
    wobj->_vp_trail_scale = wproto.vp_trail_scale;
    wobj->_vp_trail_tint = wproto.vp_trail_tint;
    wobj->_vp_trail_spin_strength = wproto.vp_trail_spin;

    // Physical projectiles use the same cached VP compound generation as
    // vehicles when radius is absent or auto_collision is explicit. Beam
    // weapons keep radius as hitscan thickness and never enter this path.
    bool useAutoCollisionSpheres = (wproto.auto_collision || !wproto.radius_defined) &&
                                   !wproto.IsLaser() && !wproto.IsVerticalLaser();
    World::rbcolls spawnColl = wproto.coll;
    if ( useAutoCollisionSpheres && spawnColl.roboColls.empty() )
    {
        auto getModel = [this](int16_t id) -> NC_STACK_base *
        {
            if ( id < 0 || (size_t)id >= _vhclModels.size() )
                return NULL;
            return _vhclModels.at(id);
        };

        NC_STACK_base *autoCollVP = getModel(wproto.vp_normal);
        if ( !autoCollVP )
            autoCollVP = getModel(wproto.vp_wait);
        if ( !autoCollVP )
            autoCollVP = getModel(wproto.vp_fire);
        if ( !autoCollVP )
            autoCollVP = getModel(wproto.vp_genesis);

        spawnColl = yw_GenerateAutoCollSpheresFromVP(autoCollVP, wproto.vp_scale,
                                                     wproto.vp_orientation, false, false, true);
        yw_SimplifyAutoWeaponCollSpheres(&spawnColl);
        if ( !spawnColl.roboColls.empty() )
            wproto.coll = spawnColl;
    }

    // Keep auto_collision operational even when a projectile VP has no usable
    // polygon geometry. The parser default (or an explicit radius) provides a
    // single safe compatibility sphere instead of silently disabling it.
    if ( useAutoCollisionSpheres && spawnColl.roboColls.empty() && wproto.radius > 0.01f )
    {
        World::TRoboColl fallback;
        fallback.robo_coll_radius = wproto.radius;
        spawnColl.roboColls.push_back(fallback);
        wproto.coll = spawnColl;
    }

    wobj->_collNodes = spawnColl;
    wobj->_autoCollisionSpheres = useAutoCollisionSpheres && !spawnColl.roboColls.empty();
    if ( wobj->_autoCollisionSpheres )
    {
        // Keep the cached prototype spheres canonical. Scale only this spawn's
        // runtime copy so repeated spawns can never compound the multiplier.
        float autoCollisionScale = yw_GetWeaponAutoCollisionScale();
        float automaticRadius = 0.0f;
        for (World::TRoboColl &sphere : wobj->_collNodes.roboColls)
        {
            sphere.robo_coll_radius *= autoCollisionScale;
            if ( sphere.robo_coll_radius <= 0.01 )
                continue;

            float extent = sphere.coll_pos.length() + sphere.robo_coll_radius;
            if ( extent > automaticRadius )
                automaticRadius = extent;
        }

        if ( !wproto.radius_defined )
            wobj->_radius = automaticRadius;
    }

    wobj->_destroyFX = wproto.dfx;
    wobj->_extDestroyFX = wproto.ExtDestroyFX;
    wobj->_chainFX = wproto.chain_fx;
    wobj->_decoration_fx = wproto.decoration_fx;
    wobj->_decoration_fx_next_time = 0;

    int missileType;

    switch(wproto._weaponFlags)
    {
    case World::TWeapProto::WEAPON_FLAGS_BOMB:
    case World::TWeapProto::WEAPON_FLAGS_HOMING_BOMB:
        missileType = NC_STACK_ypamissile::MISL_BOMB;
        break;

    case World::TWeapProto::WEAPON_FLAGS_MISSILE:
        missileType = NC_STACK_ypamissile::MISL_TARGETED;
        break;

    case World::TWeapProto::WEAPON_FLAGS_OBSAVOID:
        missileType = NC_STACK_ypamissile::MISL_OBSAVOID;
        break;

    case World::TWeapProto::WEAPON_FLAGS_GRENADE:
        missileType = NC_STACK_ypamissile::MISL_GRENADE;
        break;

    default:
        missileType = NC_STACK_ypamissile::MISL_DIRECT;
        break;
    }

    wobj->SetLifeTime(wproto.life_time);
    wobj->SetDelay(wproto.delay_time);
    wobj->SetDriveTime(wproto.drive_time);
    wobj->SetMissileType(missileType);
    wobj->SetPowerHeli(wproto.energy_heli * 1000.0);
    wobj->SetPowerTank(wproto.energy_tank * 1000.0);
    wobj->SetPowerFlyer(wproto.energy_flyer * 1000.0);
    wobj->SetPowerRobo(wproto.energy_robo * 1000.0);
    wobj->SetAreaDamage(wproto.aoe_unit_radius, wproto.aoe_unit_energy,
                         wproto.aoe_building_radius, wproto.aoe_building_energy,
                         wproto.aoe_sector_radius, wproto.aoe_sector_energy,
                         wproto.aoe_falloff);
    wobj->SetAoeUnitPush(wproto.aoe_unit_push);
    wobj->SetDirectPush(wproto.push);
    wobj->SetArmorPenetrationTargets(wproto.armor_penetration_targets);

    wobj->_soundcarrier.Resize(wproto.sndFXes.size());

    for (World::TVhclSound &sfx : wproto.sndFXes)
        sfx.LoadSamples();

    if ( wproto.debuff.allow )
        wproto.debuff.tick_snd.LoadSamples();

    for (size_t i = 0; i < wproto.sndFXes.size(); i++)
    {
        TSoundSource *v25 = &wobj->_soundcarrier.Sounds[i];

        v25->Volume = wproto.sndFXes[i].volume;
        v25->Pitch = wproto.sndFXes[i].pitch;

        if ( i == 0 )
            v25->SetLoop(true);

        if ( wproto.sndFXes[i].MainSample.Sample )
            v25->PSample = wproto.sndFXes[i].MainSample.Sample->GetSampleData();
        else
            v25->PSample = 0;

        v25->SampleVariants.clear();
        if ( v25->PSample )
            v25->SampleVariants.push_back(v25->PSample);

        for (const World::TVhclSound::TSndSample &sample : wproto.sndFXes[i].MainSampleVariants)
        {
            if ( sample.Sample )
                v25->SampleVariants.push_back(sample.Sample->GetSampleData());
        }

        if ( wproto.sndFXes[i].sndPrm.slot )
        {
            v25->SetPFx(true);
            v25->PPFx = &wproto.sndFXes[i].sndPrm;
        }
        else
        {
            v25->SetPFx(false);
        }

        if ( wproto.sndFXes[i].sndPrm_shk.slot )
        {
            v25->SetShk(true);
            v25->PShkFx = &wproto.sndFXes[i].sndPrm_shk;
        }
        else
        {
            v25->SetShk(false);
        }

        if ( !wproto.sndFXes[i].extS.empty() )
        {
            v25->SetFragmented(true);
            v25->PFragments = &wproto.sndFXes[i].extS;
        }
        else
        {
            v25->SetFragmented(false);
        }
    }

    wobj->SetParameters(wproto.initParams);

    bact_arg80 arg80;

    arg80.pos = arg->pos;
    arg80.field_C = 1;

    wobj->SetPosition(&arg80);

    setState_msg arg119;

    arg119.setFlags = 0;
    arg119.unsetFlags = 0;
    arg119.newStatus = BACT_STATUS_NORMAL;

    wobj->SetStateInternal(&arg119);

    return wobj;
}


size_t NC_STACK_ypaworld::ypaworld_func148(ypaworld_arg148 *arg)
{
    if (  !arg->field_C
       && !_allowMultiBuildLevel
       && IsAnyBuildingProcess(arg->owner))
            return false;

    cellArea &cell = _cells(arg->CellId);

    bool UserInSec = false;

    for ( NC_STACK_ypabact* &node : cell.unitsList )
    {

        if ( _userUnit == node || node->_bact_type == BACT_TYPES_ROBO)
        {
            UserInSec = true;
            break;
        }
    }

    if ( _userUnit  &&  &cell == _userUnit->_pSector )
        UserInSec = true;

    if (cell.IsBorder())
        return 0;

    if ( cell.PurposeType == cellArea::PT_CONSTRUCTING )
        return 0;
    else if ( UserInSec  && !arg->field_C )
        return 0;
    else if ( cell.PurposeType == cellArea::PT_POWERSTATION )
        PowerStationErase(&cell);
    else if ( (cell.PurposeType == cellArea::PT_TECHUPGRADE || cell.PurposeType == cellArea::PT_GATECLOSED || cell.PurposeType == cellArea::PT_GATEOPENED) && !arg->field_C )
        return 0;
    else if ( cell.PurposeType == cellArea::PT_TECHDEACTIVE && _isNetGame )
        return 0;

    if ( arg->field_C )
    {
        sb_0x456384(arg->CellId, arg->owner, arg->blg_ID, arg->field_18 & 1);
    }
    else
    {
        DestroyAllGunsInSector(&cell);

        if ( !BuildingConstructBegin(&cell, arg->blg_ID, arg->owner, World::CVBuildConstructTime) )
            return 0;
    }

    return 1;
}


void NC_STACK_ypaworld::ypaworld_func149(ypaworld_arg136 *arg)
{
    arg->tVal = 2.0;
    arg->isect = 0;

    vec3d stpos = arg->stPos;

    int v33 = (stpos.x + 150.0) / 75.0 * 16384.0;
    int v34 = (stpos.z - 150.0) / 75.0 * 16384.0;

    float v36 = fabs(arg->vect.x);
    float v39 = fabs(arg->vect.z);

    int v31;
    int v32;
    float v37;

    if ( v36 != 0.0 || v39 != 0.0 )
    {
        if ( v36 <= v39 )
        {
            v32 = (v36 / v39 * 16384.0);

            if ( arg->vect.x < 0.0 )
                v32 = -v32;

            if ( arg->vect.z >= 0.0 )
                v31 = 16384;
            else
                v31 = -16384;

            v37 = v39;
        }
        else
        {
            if ( arg->vect.x >= 0.0 )
                v32 = 16384;
            else
                v32 = -16384;

            v31 = (v39 / v36 * 16384.0);

            if ( arg->vect.z < 0.0 )
                v31 = -v31;

            v37 = v36;
        }
    }
    else
    {
        v31 = 0;
        v37 = 0.0;
        v32 = 0;
    }

    int a2a = v33 >> 16;
    int a3a = -v34 >> 16;

    TSectorCollision a6;
    int v10, v11;

    do // Don't like this :E
    {
        v10 = v33 >> 16;
        v11 = -v34 >> 16;

        a6 = sub_44DBF8(a2a, a3a, v10, v11, arg->flags);

        if ( a6.CollisionType )
        {
            if ( a6.CollisionType != 1 )
                sub_44E07C(a6);

            arg->stPos = stpos - a6.pos;

            sub_44D8B8(arg, a6);

            if ( arg->isect )
                return;
        }

        do // Don't like this :E
        {
            v37 -= 75.0;
            v33 += v32;
            v34 += v31;
        }
        while ( v33 >> 16 == v10 && -v34 >> 16 == v11 && v37 > 0.0 );

    }
    while( v37 > 0.0 );

    int v24 = ((int)((arg->vect.x + stpos.x + 150.0) / 75.0 * 16384.0)) >> 16;
    int v27 = ((int)((arg->vect.z + stpos.z - 150.0) / 75.0 * 16384.0)) >> 16;

    if ( v24 != v10 || -v27 != v11 )
    {
        a6 = sub_44DBF8(a2a, a3a, v24, -v27, arg->flags);

        if ( a6.CollisionType )
        {
            if ( a6.CollisionType != 1 )
                sub_44E07C(a6);

            arg->stPos = stpos - a6.pos;

            sub_44D8B8(arg, a6);
        }
    }
}


void NC_STACK_ypaworld::ypaworld_func150(yw_arg150 *arg)
{
    arg->field_24 = NULL;

    int v6 = arg->pos.x / 300.0 * 16384.0;
    int v7 = arg->pos.z / 300.0 * 16384.0;

    float v47 = fabs(arg->field_18.x);
    float v46 = fabs(arg->field_18.z);

    arg->field_28 = arg->field_18.length();

    int v27, v28, v35;

    if ( v47 != 0.0 || v46 != 0.0 )
    {
        if ( v47 <= v46 )
        {
            v27 = (v47 * 16384.0 / v46);

            if ( arg->field_18.x < 0.0 )
                v27 = -v27;

            if ( arg->field_18.z >= 0.0 )
                v28 = 16384;
            else
                v28 = -16384;

            v35 = v46;
        }
        else
        {
            if ( arg->field_18.x >= 0.0 )
                v27 = 16384;
            else
                v27 = -16384;

            v28 = (v46 * 16384.0 / v47);

            if ( arg->field_18.z < 0.0 )
                v28 = -v28;

            v35 = v47;
        }
    }
    else
    {
        v28 = 0;
        v35 = 0;
        v27 = 0;
    }

    vec3d v41 = arg->field_18 / arg->field_28;

    while ( v35 >= 0 )
    {
        int v12 = -v7 >> 16;
        int v29 = v6 >> 16;

        if ( IsSector( {v29, v12} ) )
        {
            for ( NC_STACK_ypabact* &sect_bacts : _cells(v29, v12).unitsList )
            {
                if ( sect_bacts != arg->unit && sect_bacts->_status != BACT_STATUS_DEAD && !sect_bacts->_isDummy )
                {
                    if ( !(arg->unit == _userUnit && _playerInHSGun) || sect_bacts != _userRobo )
                    {
                        vec3d v36 = sect_bacts->_position - arg->pos;
                        vec3d v16 = v41 * v36;

                        if ( v16.length() < sect_bacts->_radius )
                        {
                            float v30 = v36.length();

                            if ( v41.dot(v36) / v30 > 0.0 )
                            {
                                if ( arg->field_28 > v30 - sect_bacts->_radius )
                                {
                                    arg->field_24 = sect_bacts;
                                    arg->field_28 = v30 - sect_bacts->_radius;
                                }
                            }
                        }
                    }
                }
            }
        }

        if ( arg->field_24 )
            break;

        do
        {
            v6 += v27;
            v7 += v28;
            v35 -= 300;
        }
        while ( (v6 >> 16) == v29 && (-v7 >> 16) == v12 && v35 >= 0 );
    }
}


void NC_STACK_ypaworld::DeleteLevel()
{
    EnableLevelPasses();

    if ( _levelInfo.State == TLevelInfo::STATE_COMPLETED )
    {
        _prepareDebrief = true;
        if ( _firstContactFaction )
            _levelInfo.JodieFoster[ _firstContactFaction ] = 1;
    }
    else
    {
        _prepareDebrief = false;
    }

    if ( _levelInfo.State == TLevelInfo::STATE_COMPLETED )
    {
        if ( _GameShell )
        {
            FSMgr::FileHandle *fil = uaOpenFileAlloc(fmt::sprintf("save:%s/sgisold.txt", _GameShell->UserName), "w");

            if ( fil )
                delete fil;

            SaveSettings(_GameShell, fmt::sprintf("%s/user.txt", _GameShell->UserName), World::SDF_ALL);

            fil = uaOpenFileAlloc("env:user.def", "w");

            if ( fil )
            {
                fil->printf(_GameShell->UserName);
                delete fil;
            }
        }
    }

    if ( _isNetGame )
    {
        if ( !_GameShell->sentAQ )
            NetSendExitMsg(0);

        _GameShell->ypaworld_func151__sub7();
        _GameShell->yw_netcleanup();

        _prepareDebrief = true;
        _gameWasNetGame = true;

        if ( _userUnit )
            _userOwnerIdWasInNetGame = _userUnit->_owner;
    }
    else
    {
        _userOwnerIdWasInNetGame = 0;
        _gameWasNetGame = false;
    }

    if ( _replayRecorder->do_record )
        recorder_stoprec(this);

    _screenShotSeq = false;
    _replayRecorder->do_record = 0;

    NC_STACK_bitmap *disk = loadDisk_screen(this);

    if ( disk )
    {
        draw_splashScreen(this, disk);
        disk->Delete();
    }

    //ypaworld_func151__sub5(this); Free map events
    _voiceMessage.Reset();

    SFXEngine::SFXe.setMasterVolume(audio_volume);

    GUI_Close();

    if ( _skyObject )
    {
        _skyObject->Delete();
        _skyObject = NULL;
    }

    int plowner;
    if ( _userRobo )
        plowner = _userRobo->_owner;
    else
        plowner = 0;

    BeginLevelTeardown();

    while ( !_deadCacheList.empty() )
    {
        NC_STACK_ypabact *bct = _deadCacheList.front();

        if ( _gameWasNetGame )
        {
            if ( plowner != bct->_owner && bct->_bact_type == BACT_TYPES_ROBO )
                NetRemove(bct);
        }

        bct->Delete();
    }

    while ( !_unitsList.empty() )
    {
        NC_STACK_ypabact *bct = _unitsList.front();

        if ( _gameWasNetGame )
        {
            if ( plowner != bct->_owner && bct->_bact_type == BACT_TYPES_ROBO )
                NetRemove(bct);
        }

        bct->Delete();
    }

    // NetRemove can fill deadcache, so clean it again
    while ( !_deadCacheList.empty() )
        _deadCacheList.front()->Delete();

    _transientVPs.clear();
    _nextTransientVPId = 1;
    _damageHoverTargets.clear();
    ProtosFreeSounds();

    sb_0x44ac24(this);

    _powerStations.clear();
    _energyAccumMap.Clear();

    _lvlTypeMap.Clear();
    _lvlOwnMap.Clear();
    _lvlBuildingsMap.Clear();
    _lvlHeightMap.Clear();

    if ( _GameShell )
        _GameShell->samples1_info.Position = vec3d();

    if ( _gameWasNetGame )
    {
        LoadProtosScript(_initScriptFilePath);
    }

    EndLevelTeardown();
}

void NC_STACK_ypaworld::BeginLevelTeardown()
{
    _levelTeardownInProgress = true;

    // NC_STACK_ypaworld is reused by restart/load/menu transitions. Invalidate
    // every mission-owned reference before the first BACT is destroyed so UI,
    // debug and delayed runtime state cannot observe the outgoing hierarchy.
    _particles.Clear();
    Common::DeleteAndNull(&_script);
    _transientVPs.clear();
    _nextTransientVPId = 1;
    _damageHoverTargets.clear();
    _inBuildProcess.clear();
    _debugAoeRings.clear();
    ClearMortarMarkers();
    _mortarManualGid = 0;
    _mortarManualRadius = 0.0f;

    _hudMissileMultiLockTargets.clear();
    _cmdrsRemap.clear();
    _spectatorFollowTarget = NULL;
    _lastMsgSender = NULL;
    _viewerBact = NULL;
    _userRobo = NULL;
    _userUnit = NULL;
    _bactOnMouse = NULL;
    _bactPrevClicked = NULL;
    _cellOnMouse = NULL;
    _guiDragElement = NULL;
    _guiDragging = false;
    _guiDragDefaultMouse = false;
    _mouseGrabbed = false;
    _guiActFlags = 0;
    _doAction = World::DOACTION_0;
    _guiVisor = bact_hudi();
    _updateMessage = update_msg();

    _vehicleTakenControlTimestamp = 0;
    _vehicleTakenCommandId = 0;
    _activeCmdrID = 0;
    _activeCmdrRemapIndex = 0;
    _activeCmdrKidsCount = 0;
    _cmdrIdToSelect = -1;
    _makingWaypointsMode = false;
    _waypointCount = 0;
    _playerInHSGun = false;
    _upgradeId = 0;
    _upgradeTimeStamp = 0;
    _upgradeVehicleId = 0;
    _upgradeWeaponId = 0;
    _upgradeBuildId = 0;
    ClearGemNotificationCapture();
    _gemUnlockSoundAttemptedPath.clear();
}

void NC_STACK_ypaworld::EndLevelTeardown()
{
    _levelTeardownInProgress = false;
}

bool NC_STACK_ypaworld::IsLevelTeardownInProgress() const
{
    return _levelTeardownInProgress;
}


void NC_STACK_ypaworld::ypaworld_func153(bact_hudi *arg)
{
    if ( !arg->field_18 )
        _hudMissileMultiLockTargets.clear();

    _guiVisor = *arg;
}

void NC_STACK_ypaworld::NoteUserDamageHover(NC_STACK_ypabact *attacker, NC_STACK_ypabact *target)
{
    if ( !attacker || !target || attacker != _userUnit || attacker != _viewerBact || !attacker->getBACT_inputting() )
        return;

    if ( target == attacker ||
         target->_bact_type == BACT_TYPES_MISSLE ||
         target->_status == BACT_STATUS_DEAD ||
         target->_status == BACT_STATUS_CREATE ||
         target->_status == BACT_STATUS_BEAM ||
         target->ShouldHideFromStrategicUI() )
        return;

    int32_t hoverUntil = _timeStamp + 800;
    for (TDamageHoverTarget &hover : _damageHoverTargets)
    {
        if ( hover.target == target )
        {
            hover.until = hoverUntil;
            return;
        }
    }

    TDamageHoverTarget hover;
    hover.target = target;
    hover.until = hoverUntil;
    _damageHoverTargets.push_back(hover);
}

std::vector<NC_STACK_ypabact *> NC_STACK_ypaworld::GetUserDamageHoverTargets()
{
    std::vector<NC_STACK_ypabact *> targets;

    for (auto it = _damageHoverTargets.begin(); it != _damageHoverTargets.end(); )
    {
        NC_STACK_ypabact *target = it->target;
        if ( !target ||
             _timeStamp > it->until ||
             target->_status == BACT_STATUS_DEAD ||
             target->_status == BACT_STATUS_CREATE ||
             target->_status == BACT_STATUS_BEAM ||
             target->ShouldHideFromStrategicUI() )
        {
            it = _damageHoverTargets.erase(it);
            continue;
        }

        targets.push_back(target);
        ++it;
    }

    return targets;
}

void NC_STACK_ypaworld::ClearUserDamageHoverTarget(NC_STACK_ypabact *target)
{
    if ( !target )
        return;

    for (auto it = _damageHoverTargets.begin(); it != _damageHoverTargets.end(); )
    {
        if ( it->target == target )
            it = _damageHoverTargets.erase(it);
        else
            ++it;
    }
}

void UserData::sub_46D2B4()
{
    int v10 = inpListActiveElement;

    for (int i = 0; i <= 48; i++)
        Input::Engine.SetHotKey(i, "nop");

    for (int i = 1; i < World::INPUT_BIND_MAX; i++)
    {
        inpListActiveElement = i;
        p_YW->ReloadInput(i);
    }

    inpListActiveElement = v10;
}


bool NC_STACK_ypaworld::InitGameShell(UserData *usr)
{
    _GameShell = usr;
    usr->p_YW = this;

    _levelInfo.State = TLevelInfo::STATE_MENU;
    usr->EnvMode = ENVMODE_TITLE;

    System::IniConf::ReadFromNucleusIni();
    usr->RefreshPaletteThemes();
    usr->RefreshMenuFonts();

    _netExclusiveGem = System::IniConf::NetGameExclusiveGem.Get<bool>();

    usr->profiles.clear();
    usr->lang_dlls.clear();

    LoadKeyNames();

    listSaveDir("save:");
    listLocaleDir(usr, "locale:");


    usr->userNameDir = "NEWUSER";
    usr->userNameDirCursor = usr->userNameDir.size();

    usr->IgnoreScoreSaving = true;
    usr->diskListActiveElement = 0;
    usr->inpListActiveElement = 1;

    usr->samples1_info.Clear();
    usr->samples1_info.Resize(World::SOUND_ID_MAX);

//    for (TSoundSource &snd : usr->samples1_info.Sounds)
//    {
//        snd.Volume = 127;
//        snd.Pitch = 0;
//    }

    if ( !usr->ShellSoundsLoad() )
    {
        ypa_log_out("Error: Unable to load from Shell.ini\n");
        return false;
    }

    usr->_gfxMode = _gfxMode;
    usr->_gfxModeIndex = GFX::GFXEngine::Instance.GetGfxModeIndex(_gfxMode);

    if (usr->_gfxModeIndex < 0)
        usr->_gfxModeIndex = 0;

    usr->InputConfig[World::INPUT_BIND_DRIVE_DIR]   = UserData::TInputConf(World::INPUT_BIND_TYPE_SLIDER, 3,  Input::KC_RIGHT, Input::KC_LEFT);
    usr->InputConfig[World::INPUT_BIND_DRIVE_SPEED] = UserData::TInputConf(World::INPUT_BIND_TYPE_SLIDER, 4,  Input::KC_UP, Input::KC_DOWN);
    usr->InputConfig[World::INPUT_BIND_FLY_DIR]     = UserData::TInputConf(World::INPUT_BIND_TYPE_SLIDER, 0,  Input::KC_RIGHT, Input::KC_LEFT);
    usr->InputConfig[World::INPUT_BIND_FLY_HEIGHT]  = UserData::TInputConf(World::INPUT_BIND_TYPE_SLIDER, 1,  Input::KC_UP, Input::KC_DOWN);
    usr->InputConfig[World::INPUT_BIND_FLY_SPEED]   = UserData::TInputConf(World::INPUT_BIND_TYPE_SLIDER, 2,  Input::KC_CTRL, Input::KC_SHIFT);
    usr->InputConfig[World::INPUT_BIND_GUN_HEIGHT]  = UserData::TInputConf(World::INPUT_BIND_TYPE_SLIDER, 5,  Input::KC_A, Input::KC_Y);
    usr->InputConfig[World::INPUT_BIND_FIRE]        = UserData::TInputConf(World::INPUT_BIND_TYPE_BUTTON, 0,  Input::KC_SPACE);
    usr->InputConfig[World::INPUT_BIND_CAMFIRE]     = UserData::TInputConf(World::INPUT_BIND_TYPE_BUTTON, 1,  Input::KC_TAB);
    usr->InputConfig[World::INPUT_BIND_GUN]         = UserData::TInputConf(World::INPUT_BIND_TYPE_BUTTON, 2,  Input::KC_RETURN);
    usr->InputConfig[World::INPUT_BIND_BRAKE]       = UserData::TInputConf(World::INPUT_BIND_TYPE_BUTTON, 3,  Input::KC_NUM0);
    usr->InputConfig[World::INPUT_BIND_HUD]         = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 25, Input::KC_V);
    usr->InputConfig[World::INPUT_BIND_NEW]         = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 2,  Input::KC_N);
    usr->InputConfig[World::INPUT_BIND_ADD]         = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 3,  Input::KC_A);
    usr->InputConfig[World::INPUT_BIND_ORDER]       = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 0,  Input::KC_O);
    usr->InputConfig[World::INPUT_BIND_ATTACK]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 1,  Input::KC_SPACE);
    usr->InputConfig[World::INPUT_BIND_CONTROL]     = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 4,  Input::KC_C);
    usr->InputConfig[World::INPUT_BIND_AUTOPILOT]   = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 7,  Input::KC_G);
    usr->InputConfig[World::INPUT_BIND_MAP]         = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 8,  Input::KC_M);
    usr->InputConfig[World::INPUT_BIND_SQ_MANAGE]   = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 9,  Input::KC_F);
    usr->InputConfig[World::INPUT_BIND_LANDLAYER]   = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 10, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_OWNER]       = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 11, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_HEIGHT]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 12, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_LOCKVIEW]    = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 14, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_ZOOMIN]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 16, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_ZOOMOUT]     = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 17, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_MINIMAP]     = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 18, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_NEXT_COMM]   = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 20, Input::KC_F1);
    usr->InputConfig[World::INPUT_BIND_TO_HOST]     = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 21, Input::KC_F2);
    usr->InputConfig[World::INPUT_BIND_NEXT_UNIT]   = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 22, Input::KC_F3);
    usr->InputConfig[World::INPUT_BIND_TO_COMM]     = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 23, Input::KC_F4);
    usr->InputConfig[World::INPUT_BIND_QUIT]        = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 24, Input::KC_ESCAPE);
    usr->InputConfig[World::INPUT_BIND_LOG_WND]     = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 27, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_LAST_MSG]    = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 31, Input::KC_BACKSPACE);
    usr->InputConfig[World::INPUT_BIND_PAUSE]       = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 32, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_TO_ALL]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 37, Input::KC_NUM5);
    usr->InputConfig[World::INPUT_BIND_AGGR_1]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 38, Input::KC_1);
    usr->InputConfig[World::INPUT_BIND_AGGR_2]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 39, Input::KC_2);
    usr->InputConfig[World::INPUT_BIND_AGGR_3]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 40, Input::KC_3);
    usr->InputConfig[World::INPUT_BIND_AGGR_4]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 41, Input::KC_4);
    usr->InputConfig[World::INPUT_BIND_AGGR_5]      = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 42, Input::KC_5);
    usr->InputConfig[World::INPUT_BIND_WAPOINT]     = UserData::TInputConf(World::INPUT_BIND_TYPE_BUTTON, 4,  Input::KC_SHIFT);
    usr->InputConfig[World::INPUT_BIND_HELP]        = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 43, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_LAST_SEAT]   = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 44, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_SET_COMM]    = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 45, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_ANALYZER]    = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 46, Input::KC_NONE);
    usr->InputConfig[World::INPUT_BIND_COCKPIT_CAMERA] = UserData::TInputConf(World::INPUT_BIND_TYPE_HOTKEY, 47, Input::KC_K);

    usr->sub_46D2B4();

    windp_arg87 v67;

    if (!_netDriver->GetRemoteStart(&v67) )
    {
        ypa_log_out("Error while remote start check\n");
        return  false;
    }

    if ( v67.isClient )
    {
        _GameShell->netPlayerName = v67.callSIGN;

        if ( v67.isHoster )
            _GameShell->isHost = true;
        else
            _GameShell->isHost = false;

        _GameShell->remoteMode = true;

        usr->netLevelID = 0;
        usr->EnvMode = ENVMODE_NETPLAY;

        if ( usr->isHost )
        {
            usr->lobbyPlayers[ _netDriver->GetMyIndex() ].Ready = true;
            usr->rdyStart = true;
            usr->netSelMode = UserData::NETSCREEN_CHOOSE_MAP;
        }
        else
        {
            usr->netSelMode = UserData::NETSCREEN_INSESSION;
        }

        for(uint32_t i = 0; i < _netDriver->GetPlayerCount(); i++)
            _GameShell->lobbyPlayers[i].Name = _netDriver->GetPlayerName(i);

        usr->update_time_norm = 400;
        usr->flush_time_norm = 400;
    }
    else
    {
        _GameShell->remoteMode = false;
    }

    return true;
}


void NC_STACK_ypaworld::DeinitGameShell()
{
    _GameShell->yw_netcleanup();

    _GameShell->profiles.clear();

    _GameShell->lang_dlls.clear();

    SFXEngine::SFXe.StopPlayingSounds();

    for (NC_STACK_sample * &smpl : _GameShell->samples1)
    {
        if (smpl)
        {
            smpl->Delete();
            smpl = NULL;
        }
    }

    SFXEngine::SFXe.StopCarrier(&_GameShell->samples1_info);
}


void TMapRegionsNet::UnloadImages()
{
    if ( MenuImage )
    {
        MenuImage->Delete();
        MenuImage = NULL;
    }
    if ( RolloverImage )
    {
        RolloverImage->Delete();
        RolloverImage = NULL;
    }
    if ( FinishedImage )
    {
        FinishedImage->Delete();
        FinishedImage = NULL;
    }
    if ( EnabledImage )
    {
        EnabledImage->Delete();
        EnabledImage = NULL;
    }
    if ( MaskImage )
    {
        MaskImage->Delete();
        MaskImage = NULL;
    }
}


void sb_0x4e75e8__sub1(NC_STACK_ypaworld *yw, int mode)
{
    int v37 = 1;

    if ( yw->_globalMapRegions.NumSets )
    {
        std::string oldRsrc = Common::Env.SetPrefix("rsrc", "levels:");

        int v38 = 0;
        int v39 = 65535;
        for (int i = 0; i < yw->_globalMapRegions.NumSets; i++)
        {

            int xx = (yw->_globalMapRegions.background_map[i].Size.x - yw->_screenSize.x);
            int yy = (yw->_globalMapRegions.background_map[i].Size.y - yw->_screenSize.y);

            int sq = sqrt(xx * xx + yy * yy);

            if (sq < v39)
            {
                v38 = i;
                v39 = sq;
            }
        }

        std::string menu_map;
        std::string rollover_map;
        std::string mask_map;
        std::string finished_map;
        std::string enabled_map;

        NC_STACK_bitmap *ilbm_menu_map  = NULL;
        NC_STACK_bitmap *ilbm_rollover_map = NULL;
        NC_STACK_bitmap *ilbm_mask_map = NULL;
        NC_STACK_bitmap *ilbm_finished_map = NULL;
        NC_STACK_bitmap *ilbm_enabled_map = NULL;

        switch ( mode )
        {
        case ENVMODE_TITLE:
        case ENVMODE_INPUT:
        case ENVMODE_SETTINGS:
        case ENVMODE_NETPLAY:
        case ENVMODE_SELLOCALE:
        case ENVMODE_ABOUT:
        case ENVMODE_SELPLAYER:
        case ENVMODE_HELP:
        case ENVMODE_DATABASE:
            menu_map  = yw->_globalMapRegions.menu_map[v38].PicName;
            rollover_map = yw->_globalMapRegions.settings_map[v38].PicName;
            break;
        case ENVMODE_TUTORIAL:
            menu_map  = yw->_globalMapRegions.tut_background_map[v38].PicName;
            mask_map = yw->_globalMapRegions.tut_mask_map[v38].PicName;
            rollover_map = yw->_globalMapRegions.tut_rollover_map[v38].PicName;
            break;
        case ENVMODE_SINGLEPLAY:
            menu_map  = yw->_globalMapRegions.background_map[v38].PicName;
            rollover_map = yw->_globalMapRegions.rollover_map[v38].PicName;
            finished_map = yw->_globalMapRegions.finished_map[v38].PicName;
            mask_map = yw->_globalMapRegions.mask_map[v38].PicName;
            enabled_map = yw->_globalMapRegions.enabled_map[v38].PicName;
            break;
        default:
            break;
        }

        if ( !menu_map.empty() )
        {
            ilbm_menu_map = Utils::ProxyLoadImage({
                {NC_STACK_rsrc::RSRC_ATT_NAME, menu_map},
                {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1} } );
            if ( !ilbm_menu_map )
            {
                ypa_log_out("world.ini: Could not load %s\n", menu_map.c_str());
                v37 = 0;
            }
        }

        if ( !rollover_map.empty() )
        {
            ilbm_rollover_map = Utils::ProxyLoadImage({
                {NC_STACK_rsrc::RSRC_ATT_NAME, rollover_map},
                {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1} });
            if ( !ilbm_rollover_map )
            {
                ypa_log_out("world.ini: Could not load %s\n", rollover_map.c_str());
                v37 = 0;
            }
        }

        if ( !finished_map.empty() )
        {
            ilbm_finished_map = Utils::ProxyLoadImage({
                {NC_STACK_rsrc::RSRC_ATT_NAME, finished_map},
                {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1} });
            if ( !ilbm_finished_map )
            {
                ypa_log_out("world.ini: Could not load %s\n", finished_map.c_str());
                v37 = 0;
            }
        }

        if ( !enabled_map.empty() )
        {
            ilbm_enabled_map = Utils::ProxyLoadImage({
                {NC_STACK_rsrc::RSRC_ATT_NAME, enabled_map},
                {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1} });
            if ( !ilbm_enabled_map )
            {
                ypa_log_out("world.ini: Could not load %s\n", enabled_map.c_str());
                v37 = 0;
            }
        }
        if ( !mask_map.empty() )
        {
            ilbm_mask_map = Utils::ProxyLoadImage({{NC_STACK_rsrc::RSRC_ATT_NAME, mask_map}});
            if ( !ilbm_mask_map )
            {
                ypa_log_out("world.ini: Could not load %s\n", mask_map.c_str());
                v37 = 0;
            }
        }

        Common::Env.SetPrefix("rsrc", oldRsrc);

        if ( !v37 )
        {
            if ( ilbm_menu_map )
            {
                ilbm_menu_map->Delete();
                ilbm_menu_map = NULL;
            }
            if ( ilbm_rollover_map )
            {
                ilbm_rollover_map->Delete();
                ilbm_rollover_map = NULL;
            }
            if ( ilbm_finished_map )
            {
                ilbm_finished_map->Delete();
                ilbm_finished_map = NULL;
            }
            if ( ilbm_enabled_map )
            {
                ilbm_enabled_map->Delete();
                ilbm_enabled_map = NULL;
            }
            if ( ilbm_mask_map )
            {
                ilbm_mask_map->Delete();
                ilbm_mask_map = NULL;
            }
        }
        yw->_globalMapRegions.UnloadImages();
        yw->_globalMapRegions.MenuImage = ilbm_menu_map;
        yw->_globalMapRegions.MaskImage = ilbm_mask_map;
        yw->_globalMapRegions.RolloverImage = ilbm_rollover_map;
        yw->_globalMapRegions.FinishedImage = ilbm_finished_map;
        yw->_globalMapRegions.EnabledImage = ilbm_enabled_map;
    }
}

void sb_0x4e75e8__sub0(NC_STACK_ypaworld *yw)
{
    std::array<Common::Rect, 256> regions;

    if ( yw->_globalMapRegions.MaskImage )
    {
        for (int i = 0; i < 256; i++)
        {
            regions[i].left = 10000;
            regions[i].top = 10000;
            regions[i].right = -10000;
            regions[i].bottom = -10000;
        }

        ResBitmap *bitm = yw->_globalMapRegions.MaskImage->GetBitmap();

        SDL_LockSurface(bitm->swTex);
        for (int y = 0; y < bitm->height; y++ )
        {
            uint8_t *ln = ((uint8_t *)bitm->swTex->pixels + y * bitm->swTex->pitch);

            for (int x = 0; x < bitm->width; x++)
            {
                Common::Rect &rgn = regions.at( ln[x] );

                if ( x < rgn.left )
                    rgn.left = x;

                if ( x > rgn.right )
                    rgn.right = x;

                if ( y < rgn.top )
                    rgn.top = y;

                if ( y > rgn.bottom )
                    rgn.bottom = y;
            }
        }

        for (int i = 0; i < 256; i++)
        {
            TMapRegionInfo &minf = yw->_globalMapRegions.MapRegions[i];

            if ( minf.Status != TMapRegionInfo::STATUS_NONE && minf.Status != TMapRegionInfo::STATUS_NETWORK && regions.at(i).IsValid() )
            {
                minf.Rect.left = 2.0 * ((float)(regions[i].left) / (float)bitm->width) + -1.0;
                minf.Rect.right = 2.0 * ((float)(regions[i].right) / (float)bitm->width) + -1.0;
                minf.Rect.top = 2.0 * ((float)(regions[i].top) / (float)bitm->height) + -1.0;
                minf.Rect.bottom = 2.0 * ((float)(regions[i].bottom) / (float)bitm->height) + -1.0;
            }
            else
                minf.Rect = Common::FRect();
        }

        SDL_UnlockSurface(bitm->swTex);
    }
}

void NC_STACK_ypaworld::GameShellInitBkgMode(int mode)
{
    sb_0x4e75e8__sub1(this, mode);
    if ( mode == ENVMODE_TUTORIAL || mode == ENVMODE_SINGLEPLAY )
    {
        _firstContactFaction = 0;
        _briefScreen.Stage = TBriefengScreen::STAGE_NONE;
        _globalMapRegions.SelectedRegion = 0;

        sb_0x4e75e8__sub0(this);

        _tipOfDayId = loadTOD(this, "tod.def");

        int v6 = _tipOfDayId + 1;

        if ( (v6 + 2490) > 2512 )
            v6 = 0;
        writeTOD(this, "tod.def", v6);
    }
}

bool NC_STACK_ypaworld::GameShellInitBkg()
{
    GFX::Engine.raster_func211(Common::Rect (-(_screenSize.x / 2), -(_screenSize.y / 2), _screenSize.x / 2, _screenSize.y / 2) );
    GameShellInitBkgMode(_GameShell->EnvMode);
    return true;
}

//Controls creation methods
bool NC_STACK_ypaworld::CreateTitleControls(){
    _GameShell->titel_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)0},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)0},
        {NC_STACK_button::BTN_ATT_W, (int32_t)_screenSize.x},
        {NC_STACK_button::BTN_ATT_H, (int32_t)_screenSize.y} } );
    if ( !_GameShell->titel_button )
    {
        ypa_log_out("Unable to create Titel-Button-Object\n");
        return false;
    }

    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;

    btn_64arg.tileset_down = 19;
    btn_64arg.tileset_up = 18;
    btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
    btn_64arg.field_3A = 30;
    btn_64arg.xpos = _screenSize.x * 0.3328125;
    btn_64arg.ypos = _screenSize.y * 0.2291666666666666;
    btn_64arg.width = _screenSize.x / 3;
    btn_64arg.caption = Locale::Text::Title(Locale::TITLE_GAME);
    btn_64arg.caption2.clear();
    btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
    btn_64arg.pressedCode = 0;
    btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_SINGLE_PLAYER;
    btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_SINGLE_PLAYER_UP;
    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->titel_button->Add(&btn_64arg) )
    {
        btn_64arg.ypos = _screenSize.y * 0.3083333333333334;
        btn_64arg.caption = Locale::Text::Title(Locale::TITLE_NETWORK);
        btn_64arg.caption2.clear();
        btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_MULTIPLAYER_UP;
        btn_64arg.pressedCode = 0;
        btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
        btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_MULTIPLAYER;

        if ( _GameShell->titel_button->Add(&btn_64arg) )
        {
            btn_64arg.xpos = _screenSize.x * 0.3328125;
            btn_64arg.ypos = _screenSize.y * 0.4333333333333334;
            btn_64arg.width = _screenSize.x / 3;
            btn_64arg.caption = Locale::Text::Title(Locale::TITLE_INPUT);
            btn_64arg.caption2.clear();
            btn_64arg.pressedCode = 0;
            btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
            btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_INPUT_SETTINGS;
            btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_INPUT_SETTINGS_UP;

            if ( _GameShell->titel_button->Add(&btn_64arg) )
            {
                btn_64arg.ypos = _screenSize.y * 0.5125;
                btn_64arg.caption = Locale::Text::Title(Locale::TITLE_SETTINGS);
                btn_64arg.caption2.clear();
                btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_OPTIONS_UP;
                btn_64arg.pressedCode = 0;
                btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
                btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_OPTIONS;

                if ( _GameShell->titel_button->Add(&btn_64arg) )
                {
                    btn_64arg.ypos = _screenSize.y * 0.5916666666666667;
                    btn_64arg.caption = Locale::Text::Title(Locale::TITLE_PROFILE);
                    btn_64arg.caption2.clear();
                    btn_64arg.pressedCode = 0;
                    btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
                    btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_SAVE_LOAD_UP;
                    btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_SAVE_LOAD;

                    if ( _GameShell->titel_button->Add(&btn_64arg) )
                    {
                        btn_64arg.xpos = _screenSize.x * 0.890625;
                        btn_64arg.ypos = _screenSize.y * 0.9583333333333334;
                        btn_64arg.width = _screenSize.x * 0.1;
                        btn_64arg.caption = Locale::Text::Title(Locale::TITLE_LOCALE);
                        btn_64arg.caption2.clear();
                        btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_LANGUAGE_UP;
                        btn_64arg.pressedCode = 0;
                        btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
                        btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_LANGUAGE;

                        if ( _GameShell->titel_button->Add(&btn_64arg) )
                        {
                            btn_64arg.xpos = _screenSize.x * 0.3328125;
                            btn_64arg.ypos = _screenSize.y * 0.7166666666666667;
                            btn_64arg.width = _screenSize.x / 3;
                            btn_64arg.caption = Locale::Text::Title(Locale::TITLE_DATABASE);
                            btn_64arg.caption2.clear();
                            btn_64arg.pressedCode = 0;
                            btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
                            btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_HELP;
                            btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_HELP_UP;

                            if ( _GameShell->titel_button->Add(&btn_64arg) )
                            {
                                btn_64arg.ypos = _screenSize.y * 0.7958333333333333;
                                btn_64arg.caption = Locale::Text::Title(Locale::TITLE_QUIT);
                                btn_64arg.caption2.clear();
                                btn_64arg.upCode = UIWidgets::MAIN_MENU_EVENT_IDS::BTN_QUIT_UP;
                                btn_64arg.pressedCode = 0;
                                btn_64arg.downCode = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
                                btn_64arg.button_id = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_QUIT;

                                if ( _GameShell->titel_button->Add(&btn_64arg) )
                                    v70 = 1;
                            }
                        }
                    }
                }
            }
        }
    }

    if ( !v70 )
    {
        ypa_log_out("Unable to add button to Titel\n");
        return false;
    }

    NC_STACK_button::button_66arg v228;

    if ( _GameShell->lang_dlls.size() <= 1 )
    {
        v228.field_4 = 0;
        v228.butID = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_LANGUAGE;
        _GameShell->titel_button->Disable(&v228);
    }

    _GameShell->titel_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::CreateSubBarControls(){
    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;
    dword_5A50B6_h = _screenSize.x / 4 - 20;

    _GameShell->sub_bar_button = Nucleus::CInit<NC_STACK_button>({
        {NC_STACK_button::BTN_ATT_X, (int32_t)0},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)(_screenSize.y - _fontH)},
        {NC_STACK_button::BTN_ATT_W, (int32_t)_screenSize.x},
        {NC_STACK_button::BTN_ATT_H, (int32_t)_fontH}});

    if ( !_GameShell->sub_bar_button )
    {
        ypa_log_out("Unable to create Button-Object\n");
        return false;
    }

    v70 = 0;

    btn_64arg.tileset_down = 19;
    btn_64arg.field_3A = 30;
    btn_64arg.ypos = 0;
    btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
    btn_64arg.tileset_up = 18;
    btn_64arg.xpos = dword_5A50B6_h + buttonsSpace;
    btn_64arg.width = dword_5A50B6_h;
    btn_64arg.caption = Locale::Text::GlobMap(Locale::GLOBMAP_REWIND);
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 1251;
    btn_64arg.pressedCode = 0;
    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn_64arg.button_id = 1011;
    btn_64arg.upCode = 1016;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
    {
        btn_64arg.xpos = 2 * (buttonsSpace + dword_5A50B6_h);
        btn_64arg.caption = Locale::Text::GlobMap(Locale::GLOBMAP_STEPFWD);
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 1020;
        btn_64arg.pressedCode = 1018;
        btn_64arg.button_id = 1013;

        if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
        {
            btn_64arg.xpos = 0;
            btn_64arg.caption = Locale::Text::GlobMap(Locale::GLOBMAP_START);
            btn_64arg.caption2.clear();
            btn_64arg.upCode = 1019;
            btn_64arg.pressedCode = 0;
            btn_64arg.downCode = 1251;
            btn_64arg.button_id = 1014;

            if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
            {
                btn_64arg.xpos = (_screenSize.x - 3 * dword_5A50B6_h - 2 * buttonsSpace);
                btn_64arg.caption = Locale::Text::Advanced(Locale::ADV_GOTOLDSV);
                btn_64arg.caption2.clear();
                btn_64arg.pressedCode = 0;
                btn_64arg.downCode = 1251;
                btn_64arg.button_id = 1020;
                btn_64arg.upCode = 1026;

                if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
                {
                    btn_64arg.xpos = (_screenSize.x - 2 * dword_5A50B6_h - buttonsSpace);
                    btn_64arg.caption = Locale::Text::GlobMap(Locale::GLOBMAP_LOAD);
                    btn_64arg.caption2.clear();
                    btn_64arg.upCode = 1021;
                    btn_64arg.pressedCode = 0;
                    btn_64arg.downCode = 1251;
                    btn_64arg.button_id = 1015;

                    if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
                    {
                        btn_64arg.xpos = _screenSize.x - dword_5A50B6_h;
                        btn_64arg.caption = Locale::Text::GlobMap(Locale::GLOBMAP_GOBACK);
                        btn_64arg.caption2.clear();
                        btn_64arg.pressedCode = 0;
                        btn_64arg.downCode = 1251;
                        btn_64arg.button_id = 1019;
                        btn_64arg.upCode = 1013;

                        if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
                        {
                            int playAsWidth = _screenSize.x * 0.36;

                            btn_64arg.xpos = (_screenSize.x - playAsWidth) / 2;
                            btn_64arg.width = playAsWidth;
                            btn_64arg.caption = "Play As";
                            btn_64arg.caption2.clear();
                            btn_64arg.pressedCode = 0;
                            btn_64arg.downCode = 1251;
                            btn_64arg.button_id = 1027;
                            btn_64arg.upCode = 1027;

                            if ( _GameShell->sub_bar_button->Add(&btn_64arg) )
                                v70 = 1;
                        }
                    }
                }
            }
        }
    }
    if ( !v70 )
    {
        ypa_log_out("Unable to add button to sub-bar\n");
        return false;
    }
    NC_STACK_button::button_66arg v228;
    if ( _GameShell->sgmSaveExist != 1 )
    {
        v228.butID = 1015;
        v228.field_4 = 0;
        _GameShell->sub_bar_button->Disable(&v228);
    }

    v228.field_4 = 0;
    v228.butID = 1014;
    _GameShell->sub_bar_button->Disable(&v228);

    v228.butID = 1013;
    _GameShell->sub_bar_button->Disable(&v228);

    v228.butID = 1011;
    _GameShell->sub_bar_button->Disable(&v228);

    v228.butID = 1027;
    _GameShell->sub_bar_button->Disable(&v228);

    _GameShell->sub_bar_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::CreateConfirmControls()
{

    _GameShell->confirm_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)0},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)0},
        {NC_STACK_button::BTN_ATT_W, (int32_t)_screenSize.x},
        {NC_STACK_button::BTN_ATT_H, (int32_t)_screenSize.y}} );
    if ( !_GameShell->confirm_button )
    {
        ypa_log_out("Unable to create Confirm-Button-Object\n");
        return false;
    }

    NC_STACK_button::button_64_arg btn_64arg;
    btn_64arg.tileset_up = 18;
    btn_64arg.tileset_down = 19;
    btn_64arg.field_3A = 30;
    btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
    btn_64arg.xpos = _screenSize.x * 0.25;
    btn_64arg.ypos = _screenSize.y * 0.53125;
    btn_64arg.width = _screenSize.x * 0.125;
    btn_64arg.caption = Locale::Text::Common(Locale::CMN_OK);
    btn_64arg.caption2.clear();
    btn_64arg.pressedCode = 0;
    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn_64arg.upCode = 1350;
    btn_64arg.downCode = 1251;
    btn_64arg.button_id = 1300;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->confirm_button->Add(&btn_64arg) )
    {
        btn_64arg.xpos = _screenSize.x * 0.625;
        btn_64arg.caption = Locale::Text::Common(Locale::CMN_CANCEL);
        btn_64arg.upCode = 1351;
        btn_64arg.caption2.clear();
        btn_64arg.button_id = 1301;
        btn_64arg.downCode = 1251;
        btn_64arg.pressedCode = 0;

        if ( _GameShell->confirm_button->Add(&btn_64arg) )
        {
            btn_64arg.tileset_down = 16;
            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
            btn_64arg.tileset_up = 16;
            btn_64arg.field_3A = 16;
            btn_64arg.xpos = _screenSize.x * 0.25;
            btn_64arg.ypos = _screenSize.y * 0.4375;
            btn_64arg.caption = " ";
            btn_64arg.caption2.clear();
            btn_64arg.downCode = 0;
            btn_64arg.upCode = 0;
            btn_64arg.pressedCode = 0;
            btn_64arg.flags = NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
            btn_64arg.button_id = 1302;
            btn_64arg.width = _screenSize.x * 0.5;
            btn_64arg.txt_r = _iniColors[60].r;
            btn_64arg.txt_g = _iniColors[60].g;
            btn_64arg.txt_b = _iniColors[60].b;

            if ( _GameShell->confirm_button->Add(&btn_64arg) )
            {
                btn_64arg.button_id = 1303;
                btn_64arg.ypos = _screenSize.y * 0.46875;
                btn_64arg.caption = " ";
                btn_64arg.flags = NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                btn_64arg.caption2.clear();

                _GameShell->confirm_button->Add(&btn_64arg);
            }
        }
    }
    NC_STACK_button::button_66arg v228;
    v228.field_4 = 0;
    v228.butID = 1300;
    _GameShell->confirm_button->Disable(&v228);

    v228.butID = 1301;
    _GameShell->confirm_button->Disable(&v228);

    _GameShell->confirm_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::CreateInputControls()
{
    int menuWidth = _screenSize.x * 0.7;
    int posLeftPaddingX = (_screenSize.x - menuWidth) / 2;

    GuiList::tInit args;
    args.resizeable = false;
    args.numEntries = World::INPUT_BIND_MAX - 1;
    args.shownEntries = 8;
    args.firstShownEntry = 0;
    args.selectedEntry = 0;
    args.maxShownEntries = 8;
    args.withIcon = false;
    args.entryHeight = _fontH;
    args.entryWidth = dword_5A50B2_h;
    args.enabled = true;
    args.vborder = _fontBorderH;
    args.instantInput = false;
    args.keyboardInput = true;

    if ( !_GameShell->input_listview.Init(this, args) )
    {
        ypa_log_out("Unable to create Input-ListView\n");
        return false;
    }

    _GameShell->input_listview.x = posLeftPaddingX;
    _GameShell->input_listview.y = scaledFontHeight + (vertMenuSpace + _fontH) * 4;

    _GameShell->button_input_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)posLeftPaddingX},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)scaledFontHeight},
        {NC_STACK_button::BTN_ATT_W, (int32_t)(_screenSize.x - posLeftPaddingX)},
        {NC_STACK_button::BTN_ATT_H, (int32_t)(_screenSize.y - scaledFontHeight)}});
    if ( !_GameShell->button_input_button )
    {
        ypa_log_out("Unable to create Input-Button\n");
        return false;
    }
    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;

    v70 = 0;
    btn_64arg.tileset_down = 16;
    btn_64arg.tileset_up = 16;
    btn_64arg.field_3A = 16;
    btn_64arg.xpos = 0;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
    btn_64arg.ypos = 0;
    btn_64arg.width = menuWidth;
    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_TITLE);
    btn_64arg.downCode = 0;
    btn_64arg.caption2.clear();
    btn_64arg.upCode = 0;
    btn_64arg.pressedCode = 0;
    btn_64arg.button_id = 1057;
    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->button_input_button->Add(&btn_64arg) )
    {
        btn_64arg.xpos = 0;
        btn_64arg.ypos = vertMenuSpace + _fontH;
        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_TXT2);
        btn_64arg.caption2.clear();
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1058;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( _GameShell->button_input_button->Add(&btn_64arg) )
        {
            btn_64arg.xpos = 0;
            btn_64arg.ypos = 2 * (_fontH + vertMenuSpace);
            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_TXT3);
            btn_64arg.caption2.clear();
            btn_64arg.pressedCode = 0;
            btn_64arg.button_id = 1059;

            if ( _GameShell->button_input_button->Add(&btn_64arg) )
            {
                btn_64arg.xpos = 0;
                btn_64arg.ypos = 3 * (vertMenuSpace + _fontH);
                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_TXT4);
                btn_64arg.caption2.clear();
                btn_64arg.pressedCode = 0;
                btn_64arg.button_id = 1060;

                if ( _GameShell->button_input_button->Add(&btn_64arg) )
                {
                    btn_64arg.tileset_down = 19;
                    btn_64arg.field_3A = 30;
                    btn_64arg.tileset_up = 18;
                    btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                    btn_64arg.xpos = menuWidth / 6;
                    btn_64arg.caption = "g";
                    btn_64arg.caption2 = "g";
                    btn_64arg.upCode = 1051;
                    btn_64arg.pressedCode = 0;
                    btn_64arg.flags = 0;
                    btn_64arg.ypos = 6 * vertMenuSpace + 14 * _fontH;
                    btn_64arg.width = checkBoxWidth;
                    btn_64arg.downCode = 1050;
                    btn_64arg.button_id = 1050;

                    if ( _GameShell->button_input_button->Add(&btn_64arg) )
                    {
                        btn_64arg.tileset_down = 16;
                        btn_64arg.tileset_up = 16;
                        btn_64arg.field_3A = 16;
                        btn_64arg.xpos = (checkBoxWidth + buttonsSpace + menuWidth / 6);
                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                        btn_64arg.width = (menuWidth / 2 - buttonsSpace);
                        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_JOYSTICK);
                        btn_64arg.button_id = 2;
                        btn_64arg.caption2.clear();
                        btn_64arg.downCode = 0;
                        btn_64arg.upCode = 0;
                        btn_64arg.pressedCode = 0;
                        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                        btn_64arg.txt_r = _iniColors[60].r;
                        btn_64arg.txt_g = _iniColors[60].g;
                        btn_64arg.txt_b = _iniColors[60].b;

                        if ( _GameShell->button_input_button->Add(&btn_64arg) )
                        {
                            btn_64arg.tileset_down = 19;
                            btn_64arg.field_3A = 30;
                            btn_64arg.tileset_up = 18;
                            btn_64arg.caption = "g";
                            btn_64arg.caption2 = "g";
                            btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                            btn_64arg.xpos = buttonsSpace + (menuWidth / 2);
                            btn_64arg.width = checkBoxWidth;
                            btn_64arg.downCode = 1058;
                            btn_64arg.pressedCode = 0;
                            btn_64arg.button_id = 1061;
                            btn_64arg.upCode = 1059;
                            btn_64arg.flags = 0;

                            if ( _GameShell->button_input_button->Add(&btn_64arg) )
                            {
                                btn_64arg.tileset_down = 16;
                                btn_64arg.tileset_up = 16;
                                btn_64arg.field_3A = 16;
                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                btn_64arg.xpos = (checkBoxWidth + (menuWidth / 2) + 2 * buttonsSpace);
                                btn_64arg.width = ((menuWidth / 2) - buttonsSpace);
                                btn_64arg.caption = Locale::Text::Advanced(Locale::ADV_ALTJOYMODEL);
                                btn_64arg.caption2.clear();
                                btn_64arg.downCode = 0;
                                btn_64arg.upCode = 0;
                                btn_64arg.pressedCode = 0;
                                btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                btn_64arg.button_id = 2;
                                btn_64arg.txt_r = _iniColors[60].r;
                                btn_64arg.txt_g = _iniColors[60].g;
                                btn_64arg.txt_b = _iniColors[60].b;

                                if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                {
                                    btn_64arg.tileset_down = 19;
                                    btn_64arg.field_3A = 30;
                                    btn_64arg.tileset_up = 18;
                                    btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                    btn_64arg.xpos = menuWidth / 3;
                                    btn_64arg.caption = "g";
                                    btn_64arg.caption2 = "g";
                                    btn_64arg.upCode = 1055;
                                    btn_64arg.button_id = 1055;
                                    btn_64arg.ypos = 7 * vertMenuSpace + (15 * _fontH);
                                    btn_64arg.pressedCode = 0;
                                    btn_64arg.width = checkBoxWidth;
                                    btn_64arg.flags = 0;
                                    btn_64arg.downCode = 1056;

                                    if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                    {
                                        btn_64arg.tileset_down = 16;
                                        btn_64arg.tileset_up = 16;
                                        btn_64arg.field_3A = 16;
                                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                        btn_64arg.xpos = (checkBoxWidth + (menuWidth / 3) + buttonsSpace);
                                        btn_64arg.width = menuWidth / 2;
                                        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_FF);
                                        btn_64arg.button_id = 2;
                                        btn_64arg.caption2.clear();
                                        btn_64arg.downCode = 0;
                                        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                        btn_64arg.upCode = 0;
                                        btn_64arg.pressedCode = 0;

                                        if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                        {
                                            btn_64arg.tileset_down = 19;
                                            btn_64arg.tileset_up = 18;
                                            btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                                            btn_64arg.field_3A = 30;
                                            btn_64arg.xpos = menuWidth / 6;
                                            btn_64arg.ypos = 5 * vertMenuSpace + 13 * _fontH;
                                            btn_64arg.width = (menuWidth / 3 - buttonsSpace);
                                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_I_DELETE);
                                            btn_64arg.downCode = 1251;
                                            btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                                            btn_64arg.caption2.clear();
                                            btn_64arg.pressedCode = 0;
                                            btn_64arg.upCode = 1057;
                                            btn_64arg.button_id = 1056;
                                            btn_64arg.txt_r = _iniColors[68].r;
                                            btn_64arg.txt_g = _iniColors[68].g;
                                            btn_64arg.txt_b = _iniColors[68].b;

                                            if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                            {
                                                btn_64arg.xpos = buttonsSpace + menuWidth / 2;
                                                btn_64arg.caption = Locale::Text::Common(Locale::CMN_RESETDEF);
                                                btn_64arg.caption2.clear();
                                                btn_64arg.pressedCode = 0;
                                                btn_64arg.upCode = 1053;
                                                btn_64arg.button_id = 1053;

                                                if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                                {
                                                    btn_64arg.xpos = 0;
                                                    btn_64arg.ypos = bottomButtonsY;
                                                    btn_64arg.width = button1LineWidth;
                                                    btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                                                    btn_64arg.caption = Locale::Text::Common(Locale::CMN_OK);
                                                    btn_64arg.caption2.clear();
                                                    btn_64arg.pressedCode = 0;
                                                    btn_64arg.button_id = 1051;
                                                    btn_64arg.upCode = 1052;
                                                    btn_64arg.downCode = 1251;

                                                    if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                                    {
                                                        btn_64arg.xpos = bottomThirdBtnPosX;
                                                        btn_64arg.ypos = bottomButtonsY;
                                                        btn_64arg.width = button1LineWidth;
                                                        btn_64arg.caption = Locale::Text::Common(Locale::CMN_HELP);
                                                        btn_64arg.upCode = 1250;
                                                        btn_64arg.caption2.clear();
                                                        btn_64arg.button_id = 1052;
                                                        btn_64arg.pressedCode = 0;

                                                        if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                                        {
                                                            btn_64arg.xpos = bottomSecondBtnPosX;
                                                            btn_64arg.ypos = bottomButtonsY;
                                                            btn_64arg.width = button1LineWidth;
                                                            btn_64arg.caption = Locale::Text::Common(Locale::CMN_CANCEL);
                                                            btn_64arg.upCode = 1054;
                                                            btn_64arg.button_id = 1054;
                                                            btn_64arg.caption2.clear();
                                                            btn_64arg.pressedCode = 0;

                                                            if ( _GameShell->button_input_button->Add(&btn_64arg) )
                                                            {
                                                                v70 = 1;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if ( !v70 )
    {
        ypa_log_out("Unable to add input-button\n");
        return false;
    }

        // OpenUA: remove deprecated online-help button from Input Settings.
    _GameShell->button_input_button->Remove(1052);

_GameShell->button_input_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::CreateVideoControls()
{
    int menuWidth = _screenSize.x * 0.7;
    int posLeftPaddingX = (_screenSize.x - menuWidth) / 2;

    int v261 = 0;
    int v3 = 0;

    const std::vector<GFX::TGFXDeviceInfo> &devices = GFX::Engine.GetDevices();

    for ( const GFX::TGFXDeviceInfo &dev : devices )
    {
        if ( dev.isCurrent )
        {
            _GameShell->win3d_guid = dev.guid ;

            if ( !StriCmp(dev.name, "software") )
                _GameShell->win3d_name = Locale::Text::Advanced(Locale::ADV_SOFTWARE);
            else
                _GameShell->win3d_name = dev.name;

            v3 = v261;
            break;
        }
        v261++;
    }

    int v294 = menuWidth - 3 * buttonsSpace - _fontVBScrollW;
    int v94 = (menuWidth - 3 * buttonsSpace - _fontVBScrollW) * 0.6;

    GuiList::tInit args;
    args = GuiList::tInit();
    args.resizeable = false;
    args.numEntries = GFX::GFXEngine::Instance.GetAvailableModes().size();
    args.shownEntries = 4;
    args.firstShownEntry = 0;
    args.selectedEntry = 0;
    args.maxShownEntries = 4;
    args.withIcon = false;
    args.entryHeight = _fontH;
    args.entryWidth = v94;
    args.enabled = true;
    args.vborder = _fontBorderH;
    args.instantInput = true;
    args.keyboardInput = true;

    if ( !_GameShell->video_listvw.Init(this, args) )
    {
        ypa_log_out("Unable to create Game-Video-Menu\n");
        return false;
    }

    args = GuiList::tInit();
    args.resizeable = false;
    args.numEntries = devices.size();
    args.shownEntries = 4;
    args.firstShownEntry = 0;
    args.selectedEntry = v3;
    args.maxShownEntries = 4;
    args.withIcon = false;
    args.entryHeight = _fontH;
    args.entryWidth = v94;
    args.enabled = true;
    args.vborder = _fontBorderH;
    args.instantInput = true;
    args.keyboardInput = true;

    if ( !_GameShell->d3d_listvw.Init(this, args) )
    {
        ypa_log_out("Unable to create D3D-Menu\n");
        return false;
    }

    _GameShell->video_button = Nucleus::CInit<NC_STACK_button>({
        {NC_STACK_button::BTN_ATT_X, (int32_t)posLeftPaddingX},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)scaledFontHeight},
        {NC_STACK_button::BTN_ATT_W, (int32_t)(_screenSize.x - posLeftPaddingX)},
        {NC_STACK_button::BTN_ATT_H, (int32_t)(_screenSize.y - scaledFontHeight)}});

    if ( !_GameShell->video_button )
    {
        ypa_log_out("Unable to create Video-Button\n");
        return false;
    }

    int v98 = v294 * 0.4;
    int v99 = posLeftPaddingX + buttonsSpace + v98;

    _GameShell->video_listvw.x = v99;
    _GameShell->video_listvw.y = 4 * vertMenuSpace + 4 * _fontH + scaledFontHeight; // OpenUA: opens just below Display Resolution (row 3)

    _GameShell->d3d_listvw.x = v99;
    _GameShell->d3d_listvw.y = 7 * vertMenuSpace + 7 * _fontH + scaledFontHeight;

    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;

    v70 = 0;

    btn_64arg.tileset_down = 16;
    btn_64arg.tileset_up = 16;
    btn_64arg.field_3A = 16;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
    btn_64arg.xpos = 0;
    btn_64arg.ypos = 0;
    btn_64arg.width = menuWidth;
    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_TITLE);
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 0;
    btn_64arg.upCode = 0;
    btn_64arg.pressedCode = 0;
    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
    btn_64arg.button_id = 1168;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( true ) // OpenUA: legacy Options title hidden; the screen content is self-explanatory.
    {
        btn_64arg.xpos = 0;
        btn_64arg.ypos = vertMenuSpace + _fontH;
        btn_64arg.width = menuWidth;
        btn_64arg.caption.clear();
        btn_64arg.caption2.clear();
        btn_64arg.button_id = 1169;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( true ) // OpenUA: legacy Options helper line hidden; Help button is removed.
        {
            btn_64arg.xpos = 0;
            btn_64arg.ypos = 2 * (_fontH + vertMenuSpace);
            btn_64arg.width = menuWidth;
            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_TXT3);
            btn_64arg.button_id = 1170;
            btn_64arg.caption2.clear();

            if ( true ) // OpenUA: legacy instruction line 2 hidden (mentioned the removed Direct3D selector)
            {
                btn_64arg.xpos = 0;
                btn_64arg.width = menuWidth;
                btn_64arg.ypos = 3 * (_fontH + vertMenuSpace);
                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_TXT4);
                btn_64arg.caption2.clear();
                btn_64arg.button_id = 1171;

                if ( true ) // OpenUA: legacy instruction line 3 hidden
                {
                    btn_64arg.tileset_down = 16;
                    btn_64arg.tileset_up = 16;
                    btn_64arg.field_3A = 16;
                    btn_64arg.xpos = 0;
                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                    btn_64arg.ypos = 3 * (_fontH + vertMenuSpace); // OpenUA: Display Resolution moved above Atmosphere
                    btn_64arg.width = v98;
                    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_RES);
                    btn_64arg.caption2.clear();
                    btn_64arg.downCode = 0;
                    btn_64arg.upCode = 0;
                    btn_64arg.pressedCode = 0;
                    btn_64arg.button_id = 2;
                    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_TEXT;
                    btn_64arg.txt_r = _iniColors[60].r;
                    btn_64arg.txt_g = _iniColors[60].g;
                    btn_64arg.txt_b = _iniColors[60].b;

                    if ( _GameShell->video_button->Add(&btn_64arg) )
                    {
                        btn_64arg.tileset_down = 19;
                        btn_64arg.field_3A = 30;
                        btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                        btn_64arg.caption = _gfxMode.name;
                        btn_64arg.caption2.clear();
                        btn_64arg.pressedCode = 0;
                        btn_64arg.tileset_up = 18;
                        btn_64arg.downCode = 1100;
                        btn_64arg.button_id = 1156;
                        btn_64arg.xpos = buttonsSpace + v294 * 0.4;
                        btn_64arg.upCode = 1101;
                        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                        btn_64arg.width = v294 * 0.6;
                        btn_64arg.txt_r = _iniColors[68].r;
                        btn_64arg.txt_g = _iniColors[68].g;
                        btn_64arg.txt_b = _iniColors[68].b;

                        if ( _GameShell->video_button->Add(&btn_64arg) )
                        {
                            btn_64arg.tileset_down = 16;
                            btn_64arg.tileset_up = 16;
                            btn_64arg.field_3A = 16;
                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                            btn_64arg.xpos = 0;
                            btn_64arg.ypos = 2 * (3 * (vertMenuSpace + _fontH));
                            btn_64arg.width = v294 * 0.4;
                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_SEL3D);
                            btn_64arg.caption2.clear();
                            btn_64arg.downCode = 0;
                            btn_64arg.upCode = 0;
                            btn_64arg.pressedCode = 0;
                            btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_TEXT;
                            btn_64arg.button_id = 2;
                            btn_64arg.txt_r = _iniColors[60].r;
                            btn_64arg.txt_g = _iniColors[60].g;
                            btn_64arg.txt_b = _iniColors[60].b;

                            if ( true ) // OpenUA: 3D device label hidden from Options UI (device still chosen from NUCLEUS.INI / auto)
                            {
                                btn_64arg.width = v294 * 0.6;
                                btn_64arg.tileset_down = 19;
                                btn_64arg.field_3A = 30;
                                btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                btn_64arg.downCode = 1134;
                                btn_64arg.upCode = 1135;
                                btn_64arg.tileset_up = 18;
                                btn_64arg.caption2.clear();
                                btn_64arg.pressedCode = 0;
                                btn_64arg.xpos = buttonsSpace + v294 * 0.4;
                                btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                                btn_64arg.caption = _GameShell->win3d_name;
                                btn_64arg.button_id = 1172;
                                btn_64arg.txt_r = _iniColors[68].r;
                                btn_64arg.txt_g = _iniColors[68].g;
                                btn_64arg.txt_b = _iniColors[68].b;

                                if ( true ) // OpenUA: 3D device selector hidden from Options UI (renderer = current OpenUA/OpenGL device)
                                {
                                    int v117 = dword_5A50B2 - 6 * buttonsSpace - 2 * checkBoxWidth;

                                    btn_64arg.tileset_down = 19;
                                    btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                    btn_64arg.tileset_up = 18;
                                    btn_64arg.field_3A = 30;
                                    btn_64arg.xpos = 0;
                                    btn_64arg.caption = "g";
                                    btn_64arg.caption2 = "g";
                                    btn_64arg.downCode = 1102;
                                    btn_64arg.width = checkBoxWidth;
                                    btn_64arg.upCode = 1103;
                                    btn_64arg.ypos = 7 * (vertMenuSpace + _fontH);
                                    btn_64arg.pressedCode = 0;
                                    btn_64arg.flags = 0;
                                    btn_64arg.button_id = 1157;

                                    if ( true ) // OpenUA: Horizon Depth checkbox hidden (gfx FARVIEW still read from NUCLEUS.INI)
                                    {
                                        int v120 = v117 / 2;

                                        btn_64arg.tileset_down = 16;
                                        btn_64arg.tileset_up = 16;
                                        btn_64arg.field_3A = 16;
                                        btn_64arg.xpos = checkBoxWidth + buttonsSpace;
                                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                        btn_64arg.width = v120;
                                        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_FARVIEW);
                                        btn_64arg.caption2.clear();
                                        btn_64arg.downCode = 0;
                                        btn_64arg.upCode = 0;
                                        btn_64arg.pressedCode = 0;
                                        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                        btn_64arg.button_id = 2;
                                        btn_64arg.txt_r = _iniColors[60].r;
                                        btn_64arg.txt_g = _iniColors[60].g;
                                        btn_64arg.txt_b = _iniColors[60].b;

                                        if ( true ) // OpenUA: Horizon Depth label hidden
                                        {
                                            btn_64arg.tileset_down = 19;
                                            btn_64arg.tileset_up = 18;
                                            btn_64arg.width = checkBoxWidth;
                                            btn_64arg.caption = "g";
                                            btn_64arg.caption2 = "g";
                                            btn_64arg.field_3A = 30;
                                            btn_64arg.upCode = 1107;
                                            btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                            btn_64arg.downCode = 1106;
                                            btn_64arg.xpos = 0; // OpenUA repack: Sky -> left column, row 10
                                            btn_64arg.ypos = 10 * (vertMenuSpace + _fontH);
                                            btn_64arg.pressedCode = 0;
                                            btn_64arg.flags = 0;
                                            btn_64arg.button_id = 1160;

                                            if ( _GameShell->video_button->Add(&btn_64arg) )
                                            {
                                                btn_64arg.tileset_down = 16;
                                                btn_64arg.tileset_up = 16;
                                                btn_64arg.field_3A = 16;
                                                btn_64arg.width = v120;
                                                btn_64arg.xpos = checkBoxWidth + buttonsSpace; // OpenUA repack: Sky label -> left column
                                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_SKY);
                                                btn_64arg.caption2.clear();
                                                btn_64arg.downCode = 0;
                                                btn_64arg.upCode = 0;
                                                btn_64arg.pressedCode = 0;
                                                btn_64arg.button_id = 2;
                                                btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                if ( _GameShell->video_button->Add(&btn_64arg) )
                                                {
                                                    btn_64arg.tileset_down = 19;
                                                    btn_64arg.tileset_up = 18;
                                                    btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                    btn_64arg.xpos = 0;
                                                    btn_64arg.field_3A = 30;
                                                    btn_64arg.width = checkBoxWidth;
                                                    btn_64arg.caption = "g";
                                                    btn_64arg.caption2 = "g";
                                                    btn_64arg.pressedCode = 0;
                                                    btn_64arg.ypos = 8 * (_fontH + vertMenuSpace);
                                                    btn_64arg.downCode = 1132;
                                                    btn_64arg.upCode = 1133;
                                                    btn_64arg.button_id = 1165;
                                                    btn_64arg.flags = 0;

                                                    if ( true ) // OpenUA: Software Cursor checkbox hidden (gfx SOFTMOUSE still read from NUCLEUS.INI)
                                                    {
                                                        btn_64arg.tileset_down = 16;
                                                        btn_64arg.tileset_up = 16;
                                                        btn_64arg.field_3A = 16;
                                                        btn_64arg.width = v120;
                                                        btn_64arg.xpos = checkBoxWidth + buttonsSpace;
                                                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_SWMOUSE);
                                                        btn_64arg.caption2.clear();
                                                        btn_64arg.downCode = 0;
                                                        btn_64arg.upCode = 0;
                                                        btn_64arg.pressedCode = 0;
                                                        btn_64arg.button_id = 2;
                                                        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                        if ( true ) // OpenUA: Software Cursor label hidden
                                                        {
                                                            btn_64arg.width = checkBoxWidth;
                                                            btn_64arg.tileset_down = 19;
                                                            btn_64arg.tileset_up = 18;
                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                            btn_64arg.field_3A = 30;
                                                            btn_64arg.downCode = 1130;
                                                            btn_64arg.pressedCode = 0;
                                                            btn_64arg.flags = 0;
                                                            btn_64arg.caption = "g";
                                                            btn_64arg.caption2 = "g";
                                                            btn_64arg.upCode = 1131;
                                                            btn_64arg.button_id = 1166;
                                                            btn_64arg.xpos = 3 * buttonsSpace + checkBoxWidth + v120;
                                                            btn_64arg.ypos = 9 * (vertMenuSpace + _fontH); // OpenUA repack: Windowed -> right column, row 9

                                                            if ( _GameShell->video_button->Add(&btn_64arg) )
                                                            {
                                                                btn_64arg.tileset_down = 16;
                                                                btn_64arg.tileset_up = 16;
                                                                btn_64arg.field_3A = 16;
                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                btn_64arg.xpos = 4 * buttonsSpace + v120 + 2 * checkBoxWidth;
                                                                btn_64arg.width = v120;
                                                                btn_64arg.caption = Locale::Text::Advanced(Locale::ADV_WINDOWEDMODE);
                                                                btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                btn_64arg.caption2.clear();
                                                                btn_64arg.downCode = 0;
                                                                btn_64arg.upCode = 0;
                                                                btn_64arg.pressedCode = 0;
                                                                btn_64arg.button_id = 2;

                                                                if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                {
                                                                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                    btn_64arg.xpos = checkBoxWidth + buttonsSpace;
                                                                    btn_64arg.ypos = 8 * (vertMenuSpace + _fontH);
                                                                    btn_64arg.width = v120;
                                                                    btn_64arg.caption = Locale::Text::Advanced(Locale::ADV_USE16BIT);
                                                                    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                    btn_64arg.caption2.clear();
                                                                    btn_64arg.downCode = 0;
                                                                    btn_64arg.upCode = 0;
                                                                    btn_64arg.pressedCode = 0;
                                                                    btn_64arg.button_id = 0;

                                                                    if ( true ) // OpenUA: 16-Bit Textures label hidden (gfx 16BITTEXTURE still read from NUCLEUS.INI)
                                                                    {
                                                                        btn_64arg.width = checkBoxWidth;
                                                                        btn_64arg.tileset_down = 19;
                                                                        btn_64arg.tileset_up = 18;
                                                                        btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                        btn_64arg.pressedCode = 0;
                                                                        btn_64arg.flags = 0;
                                                                        btn_64arg.field_3A = 30;
                                                                        btn_64arg.xpos = 0;
                                                                        btn_64arg.button_id = 1150;
                                                                        btn_64arg.caption = "g";
                                                                        btn_64arg.caption2 = "g";
                                                                        btn_64arg.downCode = 1113;
                                                                        btn_64arg.upCode = 1114;

                                                                        if ( true ) // OpenUA: 16-Bit Textures checkbox hidden
                                                                        {
                                                                            btn_64arg.tileset_down = 16;
                                                                            btn_64arg.tileset_up = 16;
                                                                            btn_64arg.field_3A = 16;
                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                            btn_64arg.xpos = checkBoxWidth + buttonsSpace; // OpenUA repack: Music label -> left column, row 11
                                                                            btn_64arg.ypos = 11 * (vertMenuSpace + _fontH);
                                                                            btn_64arg.width = v120;
                                                                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_ENCDAUD);
                                                                            btn_64arg.caption2.clear();
                                                                            btn_64arg.downCode = 0;
                                                                            btn_64arg.upCode = 0;
                                                                            btn_64arg.pressedCode = 0;
                                                                            btn_64arg.button_id = 0;
                                                                            btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                            if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                            {
                                                                                btn_64arg.width = checkBoxWidth;
                                                                                btn_64arg.tileset_down = 19;
                                                                                btn_64arg.tileset_up = 18;
                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                                btn_64arg.field_3A = 30;
                                                                                btn_64arg.downCode = 1128;
                                                                                btn_64arg.pressedCode = 0;
                                                                                btn_64arg.flags = 0;
                                                                                btn_64arg.caption = "g";
                                                                                btn_64arg.caption2 = "g";
                                                                                btn_64arg.upCode = 1129;
                                                                                btn_64arg.button_id = 1164;
                                                                                btn_64arg.xpos = 0; // OpenUA repack: Music checkbox -> left column, row 11

                                                                                if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                {
                                                                                    btn_64arg.tileset_down = 19;
                                                                                    btn_64arg.field_3A = 30;
                                                                                    btn_64arg.tileset_up = 18;
                                                                                    btn_64arg.xpos = 0;
                                                                                    btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                                    btn_64arg.width = checkBoxWidth;
                                                                                    btn_64arg.caption = "g";
                                                                                    btn_64arg.caption2 = "g";
                                                                                    btn_64arg.ypos = 9 * (vertMenuSpace + _fontH);
                                                                                    btn_64arg.downCode = 1126;
                                                                                    btn_64arg.pressedCode = 0;
                                                                                    btn_64arg.button_id = 1163;
                                                                                    btn_64arg.upCode = 1127;
                                                                                    btn_64arg.flags = 0;

                                                                                    if ( true ) // OpenUA: Enemy Indicators checkbox hidden (enemyIndicator still read from NUCLEUS.INI)
                                                                                    {
                                                                                        btn_64arg.tileset_down = 16;
                                                                                        btn_64arg.tileset_up = 16;
                                                                                        btn_64arg.field_3A = 16;
                                                                                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                        btn_64arg.xpos = checkBoxWidth + buttonsSpace;
                                                                                        btn_64arg.width = v120 - checkBoxWidth;
                                                                                        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_ENMYINDIC);
                                                                                        btn_64arg.caption2.clear();
                                                                                        btn_64arg.downCode = 0;
                                                                                        btn_64arg.upCode = 0;
                                                                                        btn_64arg.pressedCode = 0;
                                                                                        btn_64arg.button_id = 0;
                                                                                        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                                        if ( true ) // OpenUA: Enemy Indicators label hidden
                                                                                        {
                                                                                            btn_64arg.width = checkBoxWidth;
                                                                                            btn_64arg.tileset_down = 19;
                                                                                            btn_64arg.tileset_up = 18;
                                                                                            btn_64arg.field_3A = 30;
                                                                                            btn_64arg.xpos = 3 * buttonsSpace + checkBoxWidth + v120; // OpenUA repack: Host Station AI -> right column, row 11
                                                                                            btn_64arg.ypos = 11 * (vertMenuSpace + _fontH);
                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                                            btn_64arg.pressedCode = 0;
                                                                                            btn_64arg.flags = 0;
                                                                                            btn_64arg.caption = "g";
                                                                                            btn_64arg.caption2 = "g";
                                                                                            btn_64arg.downCode = 1137;
                                                                                            btn_64arg.upCode = 1138;
                                                                                            btn_64arg.button_id = 1174;

                                                                                            if ( !_GameShell->video_button->Add(&btn_64arg) )
                                                                                            {
                                                                                                ypa_log_out("Unable to add player Host Station AI behavior checkbox\n");
                                                                                                return false;
                                                                                            }

                                                                                            btn_64arg.tileset_down = 16;
                                                                                            btn_64arg.tileset_up = 16;
                                                                                            btn_64arg.field_3A = 16;
                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                            btn_64arg.xpos = 4 * buttonsSpace + v120 + 2 * checkBoxWidth; // OpenUA repack: Host Station AI label -> right column
                                                                                            btn_64arg.width = v120;
                                                                                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_HOSTSTATIONAI);
                                                                                            btn_64arg.caption2.clear();
                                                                                            btn_64arg.downCode = 0;
                                                                                            btn_64arg.upCode = 0;
                                                                                            btn_64arg.pressedCode = 0;
                                                                                            btn_64arg.button_id = 0;
                                                                                            btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                                            if ( !_GameShell->video_button->Add(&btn_64arg) )
                                                                                            {
                                                                                                ypa_log_out("Unable to add player Host Station AI behavior label\n");
                                                                                                return false;
                                                                                            }

                                                                                            btn_64arg.width = checkBoxWidth;
                                                                                            btn_64arg.tileset_down = 19;
                                                                                            btn_64arg.tileset_up = 18;
                                                                                            btn_64arg.field_3A = 30;
                                                                                            btn_64arg.xpos = 0; // OpenUA repack: Spectator Mode -> left column, row 12
                                                                                            btn_64arg.ypos = 12 * (vertMenuSpace + _fontH);
                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                                            btn_64arg.pressedCode = 0;
                                                                                            btn_64arg.flags = 0;
                                                                                            btn_64arg.caption = "g";
                                                                                            btn_64arg.caption2 = "g";
                                                                                            btn_64arg.downCode = 1139;
                                                                                            btn_64arg.upCode = 1140;
                                                                                            btn_64arg.button_id = 1175;

                                                                                            if ( !_GameShell->video_button->Add(&btn_64arg) )
                                                                                            {
                                                                                                ypa_log_out("Unable to add spectator mode checkbox\n");
                                                                                                return false;
                                                                                            }

                                                                                            btn_64arg.tileset_down = 16;
                                                                                            btn_64arg.tileset_up = 16;
                                                                                            btn_64arg.field_3A = 16;
                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                            btn_64arg.xpos = checkBoxWidth + buttonsSpace; // OpenUA repack: Spectator Mode label -> left column
                                                                                            btn_64arg.width = v120;
                                                                                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_SPECTATORMODE);
                                                                                            btn_64arg.caption2.clear();
                                                                                            btn_64arg.downCode = 0;
                                                                                            btn_64arg.upCode = 0;
                                                                                            btn_64arg.pressedCode = 0;
                                                                                            btn_64arg.button_id = 1175;
                                                                                            btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                                            if ( !_GameShell->video_button->Add(&btn_64arg) )
                                                                                            {
                                                                                                ypa_log_out("Unable to add spectator mode label\n");
                                                                                                return false;
                                                                                            }

                                                                                            btn_64arg.ypos = 9 * (_fontH + vertMenuSpace);
                                                                                            btn_64arg.tileset_down = 16;
                                                                                            btn_64arg.tileset_up = 16;
                                                                                            btn_64arg.field_3A = 16;
                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                            btn_64arg.xpos = v120 + 2 * checkBoxWidth + 4 * buttonsSpace;
                                                                                            btn_64arg.width = v120;
                                                                                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_INVERT);
                                                                                            btn_64arg.caption2.clear();
                                                                                            btn_64arg.downCode = 0;
                                                                                            btn_64arg.upCode = 0;
                                                                                            btn_64arg.pressedCode = 0;
                                                                                            btn_64arg.button_id = 0;
                                                                                            btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                                            if ( true ) // OpenUA: Stereo Reverse label hidden (SF_INVERTLR still read from NUCLEUS.INI)
                                                                                            {
                                                                                                btn_64arg.width = checkBoxWidth;
                                                                                                btn_64arg.tileset_down = 19;
                                                                                                btn_64arg.tileset_up = 18;
                                                                                                btn_64arg.pressedCode = 0;
                                                                                                btn_64arg.flags = 0;
                                                                                                btn_64arg.caption = "g";
                                                                                                btn_64arg.caption2 = "g";
                                                                                                btn_64arg.field_3A = 30;
                                                                                                btn_64arg.button_id = 1151;
                                                                                                btn_64arg.xpos = 3 * buttonsSpace + checkBoxWidth + v120;
                                                                                                btn_64arg.downCode = 1112;
                                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                                                btn_64arg.upCode = 1111;

                                                                                                if ( true ) // OpenUA: Stereo Reverse checkbox hidden
                                                                                                {
                                                                                                    btn_64arg.tileset_down = 16;
                                                                                                    btn_64arg.tileset_up = 16;
                                                                                                    btn_64arg.field_3A = 16;
                                                                                                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                    btn_64arg.xpos = 0;
                                                                                                    btn_64arg.ypos = 13 * (vertMenuSpace + _fontH); // OpenUA repack: Explosion Effects row 13
                                                                                                    btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.3;
                                                                                                    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_DESTRFX);
                                                                                                    btn_64arg.caption2.clear();
                                                                                                    btn_64arg.downCode = 0;
                                                                                                    btn_64arg.upCode = 0;
                                                                                                    btn_64arg.pressedCode = 0;
                                                                                                    btn_64arg.button_id = 2;
                                                                                                    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                                                    if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                    {
                                                                                                        NC_STACK_button::Slider v225;

                                                                                                        v225.value = 8;
                                                                                                        v225.max = 16;
                                                                                                        v225.min = 0;

                                                                                                        btn_64arg.caption2.clear();
                                                                                                        btn_64arg.tileset_down = 18;
                                                                                                        btn_64arg.tileset_up = 18;
                                                                                                        btn_64arg.field_3A = 30;
                                                                                                        btn_64arg.button_type = NC_STACK_button::TYPE_SLIDER;
                                                                                                        btn_64arg.pressedCode = 1110;
                                                                                                        btn_64arg.button_id = 1159;
                                                                                                        btn_64arg.xpos = buttonsSpace + (dword_5A50B2 - 5 * buttonsSpace) * 0.3;
                                                                                                        btn_64arg.caption = " ";
                                                                                                        btn_64arg.downCode = 1108;
                                                                                                        btn_64arg.flags = 0;
                                                                                                        btn_64arg.field_34 = &v225;
                                                                                                        btn_64arg.upCode = 1109;
                                                                                                        btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.55;

                                                                                                        if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                        {
                                                                                                            btn_64arg.tileset_down = 16;
                                                                                                            btn_64arg.tileset_up = 16;
                                                                                                            btn_64arg.field_3A = 16;
                                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                            btn_64arg.caption2.clear();
                                                                                                            btn_64arg.xpos = buttonsSpace + (dword_5A50B2 - 5 * buttonsSpace) * 0.85;
                                                                                                            btn_64arg.downCode = 0;
                                                                                                            btn_64arg.upCode = 0;
                                                                                                            btn_64arg.pressedCode = 0;
                                                                                                            btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.15;
                                                                                                            btn_64arg.button_id = 1158;
                                                                                                            btn_64arg.caption = " 4";
                                                                                                            btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;

                                                                                                            if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                            {
                                                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                                btn_64arg.xpos = 0;
                                                                                                                btn_64arg.ypos = 14 * (vertMenuSpace + _fontH); // OpenUA repack: Sound Volume row 14
                                                                                                                btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.3;
                                                                                                                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_FXVOL);
                                                                                                                btn_64arg.caption2.clear();
                                                                                                                btn_64arg.downCode = 0;
                                                                                                                btn_64arg.upCode = 0;
                                                                                                                btn_64arg.pressedCode = 0;
                                                                                                                btn_64arg.button_id = 2;
                                                                                                                btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                                                                                                if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                {
                                                                                                                    btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.55;

                                                                                                                    v225.min = 1;
                                                                                                                    v225.max = 127;
                                                                                                                    v225.value = 100;

                                                                                                                    btn_64arg.field_3A = 30;
                                                                                                                    btn_64arg.tileset_down = 18;
                                                                                                                    btn_64arg.tileset_up = 18;
                                                                                                                    btn_64arg.button_type = NC_STACK_button::TYPE_SLIDER;
                                                                                                                    btn_64arg.caption2.clear();
                                                                                                                    btn_64arg.button_id = 1152;
                                                                                                                    btn_64arg.xpos = buttonsSpace + (dword_5A50B2 - 5 * buttonsSpace) * 0.3;
                                                                                                                    btn_64arg.caption = " ";
                                                                                                                    btn_64arg.downCode = 1115;
                                                                                                                    btn_64arg.upCode = 1117;
                                                                                                                    btn_64arg.field_34 = &v225;
                                                                                                                    btn_64arg.pressedCode = 1116;
                                                                                                                    btn_64arg.flags = 0;

                                                                                                                    if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                    {
                                                                                                                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                                        btn_64arg.tileset_down = 16;
                                                                                                                        btn_64arg.tileset_up = 16;
                                                                                                                        btn_64arg.field_3A = 16;
                                                                                                                        btn_64arg.caption = "4";
                                                                                                                        btn_64arg.button_id = 1153;
                                                                                                                        btn_64arg.caption2.clear();
                                                                                                                        btn_64arg.xpos = (2 * buttonsSpace) + (dword_5A50B2 - 5 * buttonsSpace) * 0.85;
                                                                                                                        btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.15;
                                                                                                                        btn_64arg.downCode = 0;
                                                                                                                        btn_64arg.flags = NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                                                                                                                        btn_64arg.upCode = 0;
                                                                                                                        btn_64arg.pressedCode = 0;

                                                                                                                        if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                        {
                                                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                                            btn_64arg.xpos = 0;
                                                                                                                            btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.3;
                                                                                                                            btn_64arg.ypos = 15 * (vertMenuSpace + _fontH); // OpenUA repack: Music Volume row 15
                                                                                                                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_S_CDVOL);
                                                                                                                            btn_64arg.caption2.clear();
                                                                                                                            btn_64arg.downCode = 0;
                                                                                                                            btn_64arg.upCode = 0;
                                                                                                                            btn_64arg.pressedCode = 0;
                                                                                                                            btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                                                                            btn_64arg.button_id = 2;

                                                                                                                            if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                            {
                                                                                                                                btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.55;
                                                                                                                                v225.min = 1;
                                                                                                                                v225.max = 127;
                                                                                                                                v225.value = 100;

                                                                                                                                btn_64arg.tileset_down = 18;
                                                                                                                                btn_64arg.tileset_up = 18;
                                                                                                                                btn_64arg.upCode = 1120;
                                                                                                                                btn_64arg.field_3A = 30;
                                                                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_SLIDER;
                                                                                                                                btn_64arg.caption2.clear();
                                                                                                                                btn_64arg.downCode = 1118;
                                                                                                                                btn_64arg.xpos = buttonsSpace + (dword_5A50B2 - 5 * buttonsSpace) * 0.3;
                                                                                                                                btn_64arg.caption = " ";
                                                                                                                                btn_64arg.pressedCode = 1119;
                                                                                                                                btn_64arg.field_34 = &v225;
                                                                                                                                btn_64arg.flags = 0;
                                                                                                                                btn_64arg.button_id = 1154;

                                                                                                                                if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                                {
                                                                                                                                    btn_64arg.tileset_down = 16;
                                                                                                                                    btn_64arg.tileset_up = 16;
                                                                                                                                    btn_64arg.field_3A = 16;
                                                                                                                                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                                                    btn_64arg.caption = "4";
                                                                                                                                    btn_64arg.caption2.clear();
                                                                                                                                    btn_64arg.downCode = 0;
                                                                                                                                    btn_64arg.upCode = 0;
                                                                                                                                    btn_64arg.pressedCode = 0;
                                                                                                                                    btn_64arg.xpos = (2 * buttonsSpace) + (dword_5A50B2 - 5 * buttonsSpace) * 0.85;
                                                                                                                                    btn_64arg.width = (dword_5A50B2 - 5 * buttonsSpace) * 0.15;
                                                                                                                                    btn_64arg.button_id = 1155;
                                                                                                                                    btn_64arg.flags = NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;

                                                                                                                                    if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                                    {
                                                                                                                                        btn_64arg.tileset_up = 18;
                                                                                                                                        btn_64arg.field_3A = 30;
                                                                                                                                        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                                                                                                                                        btn_64arg.xpos = 0;
                                                                                                                                        btn_64arg.ypos = bottomButtonsY;
                                                                                                                                        btn_64arg.width = button1LineWidth;
                                                                                                                                        btn_64arg.tileset_down = 19;
                                                                                                                                        btn_64arg.caption = Locale::Text::Common(Locale::CMN_OK);
                                                                                                                                        btn_64arg.upCode = 1124;
                                                                                                                                        btn_64arg.caption2.clear();
                                                                                                                                        btn_64arg.downCode = 0;
                                                                                                                                        btn_64arg.pressedCode = 0;
                                                                                                                                        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                                                                                                                                        btn_64arg.button_id = 1161;
                                                                                                                                        btn_64arg.txt_r = _iniColors[68].r;
                                                                                                                                        btn_64arg.txt_g = _iniColors[68].g;
                                                                                                                                        btn_64arg.txt_b = _iniColors[68].b;

                                                                                                                                        if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                                        {
                                                                                                                                            btn_64arg.xpos = bottomThirdBtnPosX;
                                                                                                                                            btn_64arg.ypos = bottomButtonsY;
                                                                                                                                            btn_64arg.width = button1LineWidth;
                                                                                                                                            btn_64arg.caption = Locale::Text::Common(Locale::CMN_HELP);
                                                                                                                                            btn_64arg.upCode = 1250;
                                                                                                                                            btn_64arg.caption2.clear();
                                                                                                                                            btn_64arg.downCode = 0;
                                                                                                                                            btn_64arg.pressedCode = 0;
                                                                                                                                            btn_64arg.button_id = 1167;

                                                                                                                                            if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                                            {
                                                                                                                                                btn_64arg.xpos = bottomSecondBtnPosX;
                                                                                                                                                btn_64arg.ypos = bottomButtonsY;
                                                                                                                                                btn_64arg.width = button1LineWidth;
                                                                                                                                                btn_64arg.caption = Locale::Text::Common(Locale::CMN_CANCEL);
                                                                                                                                                btn_64arg.upCode = 1125;
                                                                                                                                                btn_64arg.caption2.clear();
                                                                                                                                                btn_64arg.downCode = 0;
                                                                                                                                                btn_64arg.pressedCode = 0;
                                                                                                                                                btn_64arg.button_id = 1162;

                                                                                                                                                if ( _GameShell->video_button->Add(&btn_64arg) )
                                                                                                                                                {
                                                                                                                                                    v70 = 1;
                                                                                                                                                }
                                                                                                                                            }
                                                                                                                                        }
                                                                                                                                    }
                                                                                                                                }
                                                                                                                            }
                                                                                                                        }
                                                                                                                    }
                                                                                                                }
                                                                                                            }
                                                                                                        }
                                                                                                    }
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if ( !v70 )
    {
        ypa_log_out("Unable to add video-button\n");
        return false;
    }

    btn_64arg.tileset_down = 16;
    btn_64arg.tileset_up = 16;
    btn_64arg.field_3A = 16;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
    btn_64arg.xpos = 0;
    btn_64arg.ypos = 4 * (_fontH + vertMenuSpace); // OpenUA: Atmosphere now sits below Display Resolution
    btn_64arg.width = v98;
    btn_64arg.caption = "Atmosphere";
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 0;
    btn_64arg.upCode = 0;
    btn_64arg.pressedCode = 0;
    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_TEXT;
    btn_64arg.button_id = 2;
    btn_64arg.txt_r = _iniColors[60].r;
    btn_64arg.txt_g = _iniColors[60].g;
    btn_64arg.txt_b = _iniColors[60].b;

    if ( !_GameShell->video_button->Add(&btn_64arg) )
    {
        ypa_log_out("Unable to add palette theme label\n");
        return false;
    }

    btn_64arg.tileset_down = 19;
    btn_64arg.tileset_up = 18;
    btn_64arg.field_3A = 30;
    btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
    btn_64arg.xpos = buttonsSpace + v294 * 0.4;
    btn_64arg.width = v294 * 0.6;
    btn_64arg.caption = "Standard"; // OpenUA: Atmosphere now selects a fullscreen visual filter
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 0;
    btn_64arg.upCode = 1136;
    btn_64arg.pressedCode = 0;
    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn_64arg.button_id = 1173;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( !_GameShell->video_button->Add(&btn_64arg) )
    {
        ypa_log_out("Unable to add palette theme button\n");
        return false;
    }

    // ===== OpenUA: modern graphics options =====================================
    // Blending (cycle-button) and VHS Filter. Palette strength is INI-only.
    // Hardcoded captions, like "Atmosphere".
    {
        // --- Blending label + cycle-button (row 5, left column) ---
        btn_64arg.tileset_down = 16;
        btn_64arg.tileset_up = 16;
        btn_64arg.field_3A = 16;
        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
        btn_64arg.xpos = 0;
        btn_64arg.ypos = 5 * (_fontH + vertMenuSpace);
        btn_64arg.width = v98;
        btn_64arg.caption = "Blending";
        btn_64arg.caption2.clear();
        btn_64arg.button_id = 2;
        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Blending label\n");
            return false;
        }

        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.field_3A = 30;
        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
        btn_64arg.xpos = buttonsSpace + v294 * 0.4;
        btn_64arg.width = v294 * 0.6;
        btn_64arg.caption = "Default";
        btn_64arg.caption2.clear();
        btn_64arg.upCode = 1306;
        btn_64arg.button_id = 1183;
        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[68].r;
        btn_64arg.txt_g = _iniColors[68].g;
        btn_64arg.txt_b = _iniColors[68].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Blending button\n");
            return false;
        }

        // --- Menu Font cycle-button (row 6, left column) ---
        btn_64arg.tileset_down = 16;
        btn_64arg.tileset_up = 16;
        btn_64arg.field_3A = 16;
        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
        btn_64arg.xpos = 0;
        btn_64arg.ypos = 6 * (_fontH + vertMenuSpace);
        btn_64arg.width = v98;
        btn_64arg.caption = "Menu Font";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 0;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 2;
        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Menu Font label\n");
            return false;
        }

        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.field_3A = 30;
        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
        btn_64arg.xpos = buttonsSpace + v294 * 0.4;
        btn_64arg.ypos = 6 * (_fontH + vertMenuSpace);
        btn_64arg.width = v294 * 0.6;
        btn_64arg.caption = "Default";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 1311;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1186;
        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[68].r;
        btn_64arg.txt_g = _iniColors[68].g;
        btn_64arg.txt_b = _iniColors[68].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Menu Font button\n");
            return false;
        }

        int gv117 = dword_5A50B2 - 6 * buttonsSpace - 2 * checkBoxWidth;
        int gv120 = gv117 / 2;

        // --- Default View cycle-button (row 8, full-width bar below FPS Limit) ---
        btn_64arg.tileset_down = 16;
        btn_64arg.tileset_up = 16;
        btn_64arg.field_3A = 16;
        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
        btn_64arg.xpos = 0;
        btn_64arg.ypos = 8 * (_fontH + vertMenuSpace);
        btn_64arg.width = v98;
        btn_64arg.caption = "Default View";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 0;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 2;
        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Default View label\n");
            return false;
        }

        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.field_3A = 30;
        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
        btn_64arg.xpos = buttonsSpace + v294 * 0.4;
        btn_64arg.ypos = 8 * (_fontH + vertMenuSpace);
        btn_64arg.width = v294 * 0.6;
        btn_64arg.caption = "Cockpit";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 1313;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1188;
        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[68].r;
        btn_64arg.txt_g = _iniColors[68].g;
        btn_64arg.txt_b = _iniColors[68].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Default View button\n");
            return false;
        }

        // --- VHS Filter checkbox (row 9, left column) ---
        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.field_3A = 30;
        btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
        btn_64arg.xpos = 0;
        btn_64arg.ypos = 9 * (_fontH + vertMenuSpace);
        btn_64arg.width = checkBoxWidth;
        btn_64arg.caption = "g";
        btn_64arg.caption2 = "g";
        btn_64arg.downCode = 1309;
        btn_64arg.upCode = 1310;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1185;
        btn_64arg.flags = 0;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add VHS Filter checkbox\n");
            return false;
        }

        btn_64arg.tileset_down = 16;
        btn_64arg.tileset_up = 16;
        btn_64arg.field_3A = 16;
        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
        btn_64arg.xpos = checkBoxWidth + buttonsSpace;
        btn_64arg.width = gv120;
        btn_64arg.caption = "VHS Filter";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 0;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 2;
        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add VHS Filter label\n");
            return false;
        }
    }

    // --- Intro Movies checkbox (row 10, right column) ---
    {
        int gv117 = dword_5A50B2 - 6 * buttonsSpace - 2 * checkBoxWidth;
        int gv120 = gv117 / 2;

        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.field_3A = 30;
        btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
        btn_64arg.xpos = 3 * buttonsSpace + checkBoxWidth + gv120;
        btn_64arg.ypos = 10 * (_fontH + vertMenuSpace);
        btn_64arg.width = checkBoxWidth;
        btn_64arg.caption = "g";
        btn_64arg.caption2 = "g";
        btn_64arg.downCode = 1307;
        btn_64arg.upCode = 1308;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1184;
        btn_64arg.flags = 0;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Intro Movies checkbox\n");
            return false;
        }

        btn_64arg.tileset_down = 16;
        btn_64arg.tileset_up = 16;
        btn_64arg.field_3A = 16;
        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
        btn_64arg.xpos = 4 * buttonsSpace + gv120 + 2 * checkBoxWidth;
        btn_64arg.width = gv120;
        btn_64arg.caption = "Intro Movies";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 0;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 2;
        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add Intro Movies label\n");
            return false;
        }
    }

    // --- FPS Limit cycle-button (row 7, below Menu Font) ---
    {
        btn_64arg.tileset_down = 16;
        btn_64arg.tileset_up = 16;
        btn_64arg.field_3A = 16;
        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
        btn_64arg.xpos = 0;
        btn_64arg.ypos = 7 * (_fontH + vertMenuSpace);
        btn_64arg.width = v98;
        btn_64arg.caption = "FPS Limit";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 0;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 2;
        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add FPS Limit label\n");
            return false;
        }

        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.field_3A = 30;
        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
        btn_64arg.xpos = buttonsSpace + v294 * 0.4;
        btn_64arg.ypos = 7 * (_fontH + vertMenuSpace);
        btn_64arg.width = v294 * 0.6;
        btn_64arg.caption = "60";
        btn_64arg.caption2.clear();
        btn_64arg.downCode = 0;
        btn_64arg.upCode = 1312;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1187;
        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[68].r;
        btn_64arg.txt_g = _iniColors[68].g;
        btn_64arg.txt_b = _iniColors[68].b;

        if ( !_GameShell->video_button->Add(&btn_64arg) )
        {
            ypa_log_out("Unable to add FPS Limit button\n");
            return false;
        }
    }
    // ===== end OpenUA modern graphics options ==================================

    _GameShell->UpdatePaletteThemeText();
    _GameShell->UpdateGfxOptionTexts();
    _GameShell->UpdateMenuFontText();

    NC_STACK_button::button_66arg v229;
    v229.butID = 1151;
    v229.field_4 = ((_GameShell->soundFlags & World::SF_INVERTLR) == 0) + 1;

    _GameShell->video_button->SetState(&v229);


        // OpenUA: remove deprecated online-help button from Options.
    _GameShell->video_button->Remove(1167);

_GameShell->video_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::CreateDiskControls()
{
    int menuWidth = _screenSize.x * 0.7;
    int posLeftPaddingX = (_screenSize.x - menuWidth) / 2;

    GuiList::tInit args;
    args = GuiList::tInit();
    args.resizeable = false;
    args.numEntries = _GameShell->profiles.size();
    args.shownEntries = 10;
    args.firstShownEntry = 0;
    args.selectedEntry = 0;
    args.maxShownEntries = 10;
    args.withIcon = false;
    args.entryHeight = _fontH;
    args.entryWidth = menuWidth;
    args.enabled = true;
    args.vborder = _fontBorderH;
    args.instantInput = false;
    args.keyboardInput = true;

    if ( !_GameShell->disk_listvw.Init(this, args) )
    {
        ypa_log_out("Unable to create disk-listview\n");
        return false;
    }

    _GameShell->disk_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)posLeftPaddingX},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)scaledFontHeight},
        {NC_STACK_button::BTN_ATT_W, (int32_t)(_screenSize.x - posLeftPaddingX)},
        {NC_STACK_button::BTN_ATT_H, (int32_t)(_screenSize.y - scaledFontHeight)}} );

    if ( !_GameShell->disk_button )
    {
        ypa_log_out("Unable to create disk-buttonobject\n");
        return false;
    }

    _GameShell->disk_listvw.x = posLeftPaddingX;
    _GameShell->disk_listvw.y = 4 * (vertMenuSpace + _fontH) + scaledFontHeight;

    _GameShell->userNameDir = _GameShell->UserName;

    _GameShell->userNameDirCursor = _GameShell->userNameDir.size();

    std::string v223 = _GameShell->userNameDir;

    if ( _GameShell->diskScreenMode )
        v223 += "h";

    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;

    v70 = 0;

    btn_64arg.tileset_down = 16;
    btn_64arg.tileset_up = 16;
    btn_64arg.field_3A = 16;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;

    btn_64arg.xpos = 0;
    btn_64arg.ypos = 0;
    btn_64arg.width = menuWidth;
    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_TITLE);
    btn_64arg.caption2.clear();
    btn_64arg.button_id = 1108;
    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->disk_button->Add(&btn_64arg) )
    {
        btn_64arg.xpos = 0;
        btn_64arg.ypos = buttonsSpace + _fontH;
        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_TXT2);
        btn_64arg.button_id = 1109;
        btn_64arg.caption2.clear();
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( _GameShell->disk_button->Add(&btn_64arg))
        {
            btn_64arg.xpos = 0;
            btn_64arg.ypos = 2 * (_fontH + buttonsSpace);
            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_TXT3);
            btn_64arg.caption2.clear();
            btn_64arg.button_id = 1110;

            if ( _GameShell->disk_button->Add(&btn_64arg) )
            {
                btn_64arg.xpos = 0;
                btn_64arg.ypos = 3 * (buttonsSpace + _fontH);
                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_TXT4);
                btn_64arg.caption2.clear();
                btn_64arg.button_id = 1111;

                if ( _GameShell->disk_button->Add(&btn_64arg) )
                {
                    btn_64arg.tileset_down = 17;
                    btn_64arg.tileset_up = 17;
                    btn_64arg.field_3A = 17;
                    btn_64arg.xpos = 0;
                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                    btn_64arg.width = menuWidth;
                    btn_64arg.caption2.clear();
                    btn_64arg.downCode = 0;
                    btn_64arg.upCode = 0;
                    btn_64arg.pressedCode = 0;
                    btn_64arg.caption = v223.c_str();
                    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                    btn_64arg.button_id = 1100;
                    btn_64arg.ypos = 6 * buttonsSpace + 14 * _fontH;

                    if ( _GameShell->disk_button->Add(&btn_64arg) )
                    {
                        btn_64arg.tileset_down = 19;
                        btn_64arg.tileset_up = 18;
                        btn_64arg.field_3A = 30;
                        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                        btn_64arg.xpos = buttonsSpace + (menuWidth - 3 * buttonsSpace) * 0.25;
                        btn_64arg.ypos = 7 * buttonsSpace + 15 * _fontH;
                        btn_64arg.width = (menuWidth - 3 * buttonsSpace) * 0.25;
                        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_LOAD);
                        btn_64arg.downCode = 1251;
                        btn_64arg.upCode = 1160;
                        btn_64arg.caption2.clear();
                        btn_64arg.pressedCode = 0;
                        btn_64arg.button_id = 1101;
                        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                        btn_64arg.txt_r = _iniColors[68].r;
                        btn_64arg.txt_g = _iniColors[68].g;
                        btn_64arg.txt_b = _iniColors[68].b;

                        if ( _GameShell->disk_button->Add(&btn_64arg) )
                        {
                            btn_64arg.xpos = (3 * buttonsSpace) + (menuWidth - 3 * buttonsSpace) * 0.75;
                            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_DELETE);;
                            btn_64arg.caption2.clear();
                            btn_64arg.upCode = 1161;
                            btn_64arg.button_id = 1102;

                            if ( _GameShell->disk_button->Add(&btn_64arg) )
                            {
                                btn_64arg.xpos = 0;
                                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_NEW);
                                btn_64arg.button_id = 1103;
                                btn_64arg.caption2.clear();
                                btn_64arg.upCode = 1162;

                                if ( _GameShell->disk_button->Add(&btn_64arg) )
                                {
                                    btn_64arg.xpos = (2 * buttonsSpace) + (menuWidth - 3 * buttonsSpace) * 0.5;
                                    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_P_SAVE);
                                    btn_64arg.button_id = 1104;
                                    btn_64arg.caption2.clear();
                                    btn_64arg.upCode = 1163;

                                    if ( _GameShell->disk_button->Add(&btn_64arg) )
                                    {
                                        btn_64arg.xpos = 0;
                                        btn_64arg.ypos = bottomButtonsY;
                                        btn_64arg.width = button1LineWidth;
                                        btn_64arg.caption = Locale::Text::Common(Locale::CMN_OK);
                                        btn_64arg.caption2.clear();
                                        btn_64arg.downCode = 1251;
                                        btn_64arg.button_id = 1105;
                                        btn_64arg.pressedCode = 0;
                                        btn_64arg.upCode = 1164;

                                        if ( _GameShell->disk_button->Add(&btn_64arg) )
                                        {
                                            btn_64arg.ypos = bottomButtonsY;
                                            btn_64arg.width = button1LineWidth;
                                            btn_64arg.xpos = bottomThirdBtnPosX;
                                            btn_64arg.caption = Locale::Text::Common(Locale::CMN_HELP);
                                            btn_64arg.button_id = 1107;
                                            btn_64arg.caption2.clear();
                                            btn_64arg.upCode = 1250;

                                            if ( _GameShell->disk_button->Add(&btn_64arg) )
                                            {
                                                btn_64arg.ypos = bottomButtonsY;
                                                btn_64arg.width = button1LineWidth;
                                                btn_64arg.xpos = bottomSecondBtnPosX;
                                                btn_64arg.caption = Locale::Text::Common(Locale::CMN_CANCEL);
                                                btn_64arg.button_id = 1106;
                                                btn_64arg.caption2.clear();
                                                btn_64arg.upCode = 1165;

                                                if ( _GameShell->disk_button->Add(&btn_64arg) )
                                                {
                                                    v70 = 1;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if ( !v70 )
    {
        ypa_log_out("Unable to add button to disk-buttonobject\n");
        return false;
    }


        // OpenUA: remove deprecated online-help button from Save/Load.
    _GameShell->disk_button->Remove(1107);

_GameShell->disk_button->HideScreen();
    NC_STACK_button::button_66arg v228;
    v228.field_4 = 0;
    v228.butID = 1105;
    _GameShell->disk_button->Disable(&v228);
    return true;
}
bool NC_STACK_ypaworld::CreateLocaleControls()
{
    int menuWidth = _screenSize.x * 0.7;
    int posLeftPaddingX = (_screenSize.x - menuWidth) / 2;

    GuiList::tInit args;
    args = GuiList::tInit();
    args.resizeable = false;
    args.numEntries = 10;
    args.shownEntries = 10;
    args.firstShownEntry = 0;
    args.selectedEntry = 0;
    args.maxShownEntries = 10;
    args.withIcon = false;
    args.entryHeight = _fontH;
    args.entryWidth = menuWidth - _fontVBScrollW;
    args.enabled = true;
    args.vborder = _fontBorderH;
    args.instantInput = false;
    args.keyboardInput = true;


    if ( !_GameShell->local_listvw.Init(this, args) )
    {
        ypa_log_out("Unable to create local-listview\n");
        return false;
    }

    _GameShell->locale_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)posLeftPaddingX},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)scaledFontHeight},
        {NC_STACK_button::BTN_ATT_W, (int32_t)(_screenSize.x - posLeftPaddingX)},
        {NC_STACK_button::BTN_ATT_H, (int32_t)(_screenSize.y - scaledFontHeight)}} );

    if ( !_GameShell->locale_button )
    {
        ypa_log_out("Unable to create locale-buttonobject\n");
        return false;
    }

    _GameShell->local_listvw.x = posLeftPaddingX;
    _GameShell->local_listvw.y = 4 * (vertMenuSpace + _fontH) + scaledFontHeight;

    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;

    v70 = 0;
    btn_64arg.tileset_down = 16;
    btn_64arg.tileset_up = 16;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
    btn_64arg.xpos = 0;
    btn_64arg.width = menuWidth;
    btn_64arg.field_3A = 30;
    btn_64arg.ypos = 0;
    btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_L_TITLE);
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 0;
    btn_64arg.upCode = 0;
    btn_64arg.pressedCode = 0;
    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
    btn_64arg.button_id = 1253;
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->locale_button->Add(&btn_64arg) )
    {
        btn_64arg.xpos = 0;
        btn_64arg.ypos = vertMenuSpace + _fontH;
        btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_L_TXT2);
        btn_64arg.caption2.clear();
        btn_64arg.button_id = 1254;
        btn_64arg.txt_r = _iniColors[60].r;
        btn_64arg.txt_g = _iniColors[60].g;
        btn_64arg.txt_b = _iniColors[60].b;

        if ( _GameShell->locale_button->Add(&btn_64arg) )
        {
            btn_64arg.xpos = 0;
            btn_64arg.ypos = 2 * (_fontH + vertMenuSpace);
            btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_L_TXT3);
            btn_64arg.caption2.clear();
            btn_64arg.button_id = 1255;

            if ( _GameShell->locale_button->Add(&btn_64arg) )
            {
                btn_64arg.xpos = 0;
                btn_64arg.ypos = 3 * (vertMenuSpace + _fontH);
                btn_64arg.caption = Locale::Text::Dialogs(Locale::DLG_L_TXT4);
                btn_64arg.caption2.clear();
                btn_64arg.button_id = 1256;

                if ( _GameShell->locale_button->Add(&btn_64arg) )
                {
                    btn_64arg.tileset_down = 19;
                    btn_64arg.tileset_up = 18;
                    btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                    btn_64arg.field_3A = 30;
                    btn_64arg.xpos = 0;
                    btn_64arg.ypos = bottomButtonsY;
                    btn_64arg.width = button1LineWidth;
                    btn_64arg.caption = Locale::Text::Common(Locale::CMN_OK);
                    btn_64arg.caption2.clear();
                    btn_64arg.downCode = 0;
                    btn_64arg.pressedCode = 0;
                    btn_64arg.upCode = 1300;
                    btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                    btn_64arg.button_id = 1250;
                    btn_64arg.txt_r = _iniColors[68].r;
                    btn_64arg.txt_g = _iniColors[68].g;
                    btn_64arg.txt_b = _iniColors[68].b;

                    if ( _GameShell->locale_button->Add(&btn_64arg) )
                    {
                        btn_64arg.xpos = bottomThirdBtnPosX;
                        btn_64arg.ypos = bottomButtonsY;
                        btn_64arg.width = button1LineWidth;
                        btn_64arg.caption = Locale::Text::Common(Locale::CMN_HELP);
                        btn_64arg.button_id = 1252;
                        btn_64arg.caption2.clear();
                        btn_64arg.downCode = 0;
                        btn_64arg.upCode = 1250;
                        btn_64arg.pressedCode = 0;

                        if ( _GameShell->locale_button->Add(&btn_64arg) )
                        {
                            btn_64arg.xpos = bottomSecondBtnPosX;
                            btn_64arg.ypos = bottomButtonsY;
                            btn_64arg.width = button1LineWidth;
                            btn_64arg.caption = Locale::Text::Common(Locale::CMN_CANCEL);
                            btn_64arg.caption2.clear();
                            btn_64arg.downCode = 0;
                            btn_64arg.pressedCode = 0;
                            btn_64arg.upCode = 1301;
                            btn_64arg.button_id = 1251;

                            if ( _GameShell->locale_button->Add(&btn_64arg) )
                                v70 = 1;
                        }
                    }
                }
            }
        }
    }
    if ( !v70 )
    {
        ypa_log_out("Unable to add locale-button\n");
        return false;
    }

        // OpenUA: remove deprecated online-help button from Language.
    _GameShell->locale_button->Remove(1252);

_GameShell->locale_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::CreateAboutControls(){
    _GameShell->about_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)0},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)scaledFontHeight},
        {NC_STACK_button::BTN_ATT_W, (int32_t)(_screenSize.x - 0)},
        {NC_STACK_button::BTN_ATT_H, (int32_t)(_screenSize.y - scaledFontHeight)}} );

    if ( !_GameShell->about_button )
    {
        ypa_log_out("Unable to create sound-buttonobject\n");
        return false;
    }
    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;
    v70 = 0;
    btn_64arg.tileset_down = 16;
    btn_64arg.tileset_up = 16;
    btn_64arg.field_3A = 16;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
    btn_64arg.xpos = 0;
    btn_64arg.width = (_screenSize.x - 4 * buttonsSpace);
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 0;
    btn_64arg.upCode = 0;
    btn_64arg.pressedCode = 0;
    btn_64arg.button_id = 2;
    btn_64arg.ypos = vertMenuSpace + _fontH;
    btn_64arg.flags = NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn_64arg.caption = "Fuer den Kauf dieses erzgebirgischen Qualitaetsspielzeuges bedanken sich";
    btn_64arg.txt_r = _iniColors[68].r;
    btn_64arg.txt_g = _iniColors[68].g;
    btn_64arg.txt_b = _iniColors[68].b;

    if ( _GameShell->about_button->Add(&btn_64arg) )
    {
        btn_64arg.ypos = 2 * (_fontH + vertMenuSpace);
        btn_64arg.caption = "Bernd Beyreuther,";

        if ( _GameShell->about_button->Add(&btn_64arg) )
        {
            btn_64arg.ypos = 3 * (vertMenuSpace + _fontH);
            btn_64arg.caption = "Andre 'Floh' Weissflog, Andreas Flemming,";

            if ( _GameShell->about_button->Add(&btn_64arg) )
            {
                btn_64arg.ypos = 4 * (_fontH + vertMenuSpace);
                btn_64arg.caption = "Stefan 'Metzel Hetzel' Karau, Sylvius Lack,";

                if ( _GameShell->about_button->Add(&btn_64arg) )
                {
                    btn_64arg.ypos = 5 * (vertMenuSpace + _fontH);
                    btn_64arg.caption = "Dietmar 'Didi' Koebelin, Nico Nitsch, Steffen Priebus, ";

                    if ( _GameShell->about_button->Add(&btn_64arg) )
                    {
                        btn_64arg.ypos = 6 * (_fontH + vertMenuSpace);
                        btn_64arg.caption = "Stefan Warias, Henrik Volkening und";

                        if ( _GameShell->about_button->Add(&btn_64arg) )
                        {
                            btn_64arg.ypos = 7 * (vertMenuSpace + _fontH);
                            btn_64arg.caption = "Uta Kapp";

                            if ( _GameShell->about_button->Add(&btn_64arg) )
                            {
                                btn_64arg.ypos = 8 * (_fontH + vertMenuSpace);
                                btn_64arg.caption = " ";

                                if ( _GameShell->about_button->Add(&btn_64arg) )
                                {
                                    btn_64arg.ypos = 9 * (_fontH + vertMenuSpace);
                                    btn_64arg.caption = "Unser Dank gilt:";

                                    if ( _GameShell->about_button->Add(&btn_64arg) )
                                    {
                                        btn_64arg.ypos = 10 * (_fontH + vertMenuSpace);
                                        btn_64arg.caption = "dem gesamten Microsoft Team, besonders";

                                        if ( _GameShell->about_button->Add(&btn_64arg) )
                                        {
                                            btn_64arg.ypos = 11 * (vertMenuSpace + _fontH);
                                            btn_64arg.caption = "Michael Lyons, Jonathan Sposato und Earnest Yuen";

                                            if ( _GameShell->about_button->Add(&btn_64arg) )
                                            {
                                                btn_64arg.ypos = 12 * (_fontH + vertMenuSpace);
                                                btn_64arg.caption = "weiterhin";

                                                if ( _GameShell->about_button->Add(&btn_64arg) )
                                                {
                                                    btn_64arg.ypos = 13 * (_fontH + vertMenuSpace);
                                                    btn_64arg.caption = "Robert Birker, Andre 'Goetz' Blechschmidt, Jan Blechschmidt, Stephan Bludau,";

                                                    if ( _GameShell->about_button->Add(&btn_64arg) )
                                                    {
                                                        btn_64arg.ypos = 14 * (_fontH + vertMenuSpace);
                                                        btn_64arg.caption = "Andre Kunth, Markus Lorenz, Dirk Mansbart";

                                                        if ( _GameShell->about_button->Add(&btn_64arg) )
                                                        {
                                                            btn_64arg.ypos = 15 * (vertMenuSpace + _fontH);
                                                            btn_64arg.caption = "und natuerlich";

                                                            if ( _GameShell->about_button->Add(&btn_64arg) )
                                                            {
                                                                btn_64arg.ypos = 16 * (_fontH + vertMenuSpace);
                                                                btn_64arg.caption = "        GoldEd - dPaint - SAS/C";

                                                                if ( _GameShell->about_button->Add(&btn_64arg) )
                                                                {
                                                                    v70 = 1;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if ( !v70 )
    {
        ypa_log_out("Unable to add about-button\n");
        return false;
    }

    _GameShell->about_button->HideScreen();
    return true;
}

bool NC_STACK_ypaworld::CreateDatabaseControls()
{
    _GameShell->database_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)0},
        // OpenUA Database: use the full shell height. The previous panel started at
        // scaledFontHeight, leaving a large empty strip above the tabs and wasting
        // vertical space.
        {NC_STACK_button::BTN_ATT_Y, (int32_t)0},
        {NC_STACK_button::BTN_ATT_W, (int32_t)_screenSize.x},
        {NC_STACK_button::BTN_ATT_H, (int32_t)_screenSize.y} });

    if ( !_GameShell->database_button )
    {
        ypa_log_out("Unable to create database-button\n");
        return false;
    }

    // Database V2: list rows now show only ID + name, so the left bars can be
    // shorter and the lore/image pane can claim more of the screen.
    const int lw   = (_screenSize.x * 34) / 100;   // list panel width
    const int rx   = (_screenSize.x * 36) / 100;   // right panel x origin
    const int rw   = _screenSize.x - rx;            // right panel width
    const int lh   = _fontH + vertMenuSpace;        // line height
    const int DB_VISIBLE_LINES = 22;                // fill the vertical space above the bottom buttons without overlapping navigation
    const int DB_DETAIL_LINES = 14;
    const int panelH = _screenSize.y;
    // OpenUA Database: anchor navigation buttons to the lowest safe row of the
    // full-screen database panel. The previous formula subtracted an extra
    // vertMenuSpace, leaving a visible gap under Prev/Back/Next.
    // Button height is roughly _fontH; subtracting the full line height (lh)
    // leaves one vertMenuSpace gap at the bottom. Subtract only _fontH to
    // place Prev/Back/Next on the lowest safe row without clipping.
    int nav_y = panelH - _fontH;
    if (nav_y < 10 * lh) nav_y = 10 * lh;   // safety floor on tiny resolutions

    int imgW = std::max(120, (rw * 70) / 100);
    const int textW = imgW;
    const int textX = rx;
    const int statsX = rx + (rw * 52) / 100;
    int statsW = _screenSize.x - statsX - vertMenuSpace;
    if ( statsW < 80 ) statsW = 80;
    const int imgX = rx + (rw - imgW) / 2;
    const int imgY = (DB_DETAIL_LINES + 3) * lh;
    int imgH = nav_y - imgY - vertMenuSpace;
    if (imgH < lh * 3) imgH = lh * 3;
    if (imgX + imgW > _screenSize.x) imgW = _screenSize.x - imgX;
    if (imgH < 1) imgH = 1;

    bool ok = true;
    NC_STACK_button::button_64_arg btn;
    btn.caption2.clear();
    btn.pressedCode = 0;

    // --- TAB BUTTONS (full width, top row) ---
    btn.tileset_down = 19;
    btn.tileset_up   = 18;
    btn.field_3A     = 30;
    btn.button_type  = NC_STACK_button::TYPE_BUTTON;
    btn.downCode     = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
    btn.flags        = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn.txt_r        = _iniColors[68].r;
    btn.txt_g        = _iniColors[68].g;
    btn.txt_b        = _iniColors[68].b;
    btn.ypos         = 0;
    btn.width        = _screenSize.x / 3;

    btn.xpos = 0;                        btn.caption = "Units";     btn.button_id = UIWidgets::DB_BTN_UNITS;     btn.upCode = UIWidgets::DB_UP_UNITS;
    if (!_GameShell->database_button->Add(&btn)) ok = false;
    btn.xpos = _screenSize.x / 3;       btn.caption = "Weapons";   btn.button_id = UIWidgets::DB_BTN_WEAPONS;   btn.upCode = UIWidgets::DB_UP_WEAPONS;
    if (!_GameShell->database_button->Add(&btn)) ok = false;
    btn.xpos = (2 * _screenSize.x) / 3; btn.caption = "Buildings"; btn.button_id = UIWidgets::DB_BTN_BUILDINGS; btn.upCode = UIWidgets::DB_UP_BUILDINGS;
    if (!_GameShell->database_button->Add(&btn)) ok = false;

    // --- LEFT PANEL: page label (TYPE_CAPTION, left-aligned) ---
    btn.tileset_down = 16;
    btn.tileset_up   = 16;
    btn.field_3A     = 16;
    btn.button_type  = NC_STACK_button::TYPE_CAPTION;
    btn.downCode     = 0;
    btn.upCode       = 0;
    btn.xpos         = 0;
    btn.width        = lw;
    btn.flags        = NC_STACK_button::FLAG_TEXT;
    btn.ypos         = lh;
    btn.caption      = " ";
    btn.button_id    = UIWidgets::DB_LABEL_PAGE;
    if (!_GameShell->database_button->Add(&btn)) ok = false;

    // --- LEFT PANEL: compact list rows (TYPE_BUTTON, clickable, no border) ---
    btn.tileset_down = 19;
    btn.tileset_up   = 18;
    btn.field_3A     = 30;
    btn.button_type  = NC_STACK_button::TYPE_BUTTON;
    btn.downCode     = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
    btn.flags        = NC_STACK_button::FLAG_TEXT;   // no border, just text+click
    btn.xpos         = 0;
    btn.width        = lw;
    for (int k = 0; k < DB_VISIBLE_LINES; k++)
    {
        btn.ypos      = (k + 2) * lh;
        btn.caption   = " ";
        btn.button_id = UIWidgets::DB_LINE_0 + k;
        btn.upCode    = UIWidgets::DB_UP_LINE_BASE + k;
        if (!_GameShell->database_button->Add(&btn)) ok = false;
    }

    // --- RIGHT PANEL: detail header (TYPE_CAPTION) ---
    btn.tileset_down = 16;
    btn.tileset_up   = 16;
    btn.field_3A     = 16;
    btn.button_type  = NC_STACK_button::TYPE_CAPTION;
    btn.downCode     = 0;
    btn.upCode       = 0;
    btn.xpos         = textX;
    btn.width        = textW;
    btn.flags        = NC_STACK_button::FLAG_TEXT;
    btn.ypos         = lh;
    btn.caption      = "-- Details --";
    btn.button_id    = UIWidgets::DB_DETAIL_HEADER;
    if (!_GameShell->database_button->Add(&btn)) ok = false;

    // --- RIGHT PANEL: player-facing detail/description lines (TYPE_CAPTION, left-aligned) ---
    btn.flags = NC_STACK_button::FLAG_TEXT;
    btn.xpos = textX;
    btn.width = textW;
    for (int k = 0; k < DB_DETAIL_LINES; k++)
    {
        btn.ypos      = (k + 2) * lh;
        btn.caption   = " ";
        btn.button_id = UIWidgets::DB_DETAIL_0 + k;
        if (!_GameShell->database_button->Add(&btn)) ok = false;
    }

    // --- RIGHT PANEL: compact runtime stats block, using the empty upper-right space. ---
    btn.flags     = NC_STACK_button::FLAG_TEXT;
    btn.xpos      = statsX;
    btn.width     = statsW;
    btn.ypos      = 2 * lh;
    btn.caption   = " ";
    btn.button_id = UIWidgets::DB_STATS_HEADER;
    if (!_GameShell->database_button->Add(&btn)) ok = false;

    for (int k = 0; k < 12; k++)
    {
        btn.ypos      = (k + 3) * lh;
        btn.caption   = " ";
        btn.button_id = UIWidgets::DB_STATS_0 + k;
        if (!_GameShell->database_button->Add(&btn)) ok = false;
    }

    // --- RIGHT PANEL: image placeholder text, centered inside the custom preview box ---
    btn.xpos      = imgX;
    btn.width     = imgW;
    btn.ypos      = imgY + (imgH / 2) - (lh / 2);
    btn.flags     = NC_STACK_button::FLAG_TEXT | NC_STACK_button::FLAG_CENTER;
    btn.caption   = " ";
    btn.button_id = UIWidgets::DB_IMAGE_TEXT;
    if (!_GameShell->database_button->Add(&btn)) ok = false;

    // --- NAV BUTTONS (full width, anchored near the bottom of the panel) ---
    // The database panel starts at screen top, so child Y is relative to the full screen.
    btn.tileset_down = 19;
    btn.tileset_up   = 18;
    btn.field_3A     = 30;
    btn.button_type  = NC_STACK_button::TYPE_BUTTON;
    btn.downCode     = UIWidgets::MAIN_MENU_EVENT_IDS::ALL_DOWN;
    btn.flags        = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
    btn.ypos         = nav_y;
    btn.width        = _screenSize.x / 3;

    btn.xpos = 0;                        btn.caption = "< Prev"; btn.button_id = UIWidgets::DB_BTN_PREV; btn.upCode = UIWidgets::DB_UP_PREV;
    if (!_GameShell->database_button->Add(&btn)) ok = false;
    btn.xpos = _screenSize.x / 3;       btn.caption = "Back";   btn.button_id = UIWidgets::DB_BTN_BACK; btn.upCode = UIWidgets::DB_UP_BACK;
    if (!_GameShell->database_button->Add(&btn)) ok = false;
    btn.xpos = (2 * _screenSize.x) / 3; btn.caption = "Next >"; btn.button_id = UIWidgets::DB_BTN_NEXT; btn.upCode = UIWidgets::DB_UP_NEXT;
    if (!_GameShell->database_button->Add(&btn)) ok = false;

    if ( !ok )
    {
        ypa_log_out("Unable to add buttons to database panel\n");
        return false;
    }

    _GameShell->database_button->HideScreen();
    return true;
}

bool NC_STACK_ypaworld::CreateNetworkControls()
{
    int posLeftPaddingX = (_screenSize.x * 0.3) / 2;

    GuiList::tInit args;
    args = GuiList::tInit();
    args.resizeable = false;
    args.numEntries = 12;
    args.shownEntries = 12;
    args.firstShownEntry = 0;
    args.selectedEntry = 0;
    args.maxShownEntries = 12;
    args.withIcon = false;
    args.entryHeight = _fontH;
    args.entryWidth = dword_5A50B2_h;
    args.enabled = true;
    args.vborder = _fontBorderH;
    args.instantInput = false;
    args.keyboardInput = true;

    if ( !_GameShell->network_listvw.Init(this, args) )
    {
        ypa_log_out("Unable to create network-listview\n");
        return false;
    }

    int nypos = scaledFontHeight - _fontH;

    _GameShell->network_button = Nucleus::CInit<NC_STACK_button>( {
        {NC_STACK_button::BTN_ATT_X, (int32_t)posLeftPaddingX},
        {NC_STACK_button::BTN_ATT_Y, (int32_t)nypos},
        {NC_STACK_button::BTN_ATT_W, (int32_t)(_screenSize.x - posLeftPaddingX)},
        {NC_STACK_button::BTN_ATT_H, (int32_t)(_screenSize.y - nypos)}});
    if ( !_GameShell->network_button )
    {
        ypa_log_out("Unable to create network-buttonobject\n");
        return false;
    }

    _GameShell->netListY = 3 * (vertMenuSpace + _fontH) + nypos;

    _GameShell->network_listvw.x = posLeftPaddingX;
    _GameShell->network_listvw.y = _GameShell->netListY;

    int v70 = 0;
    NC_STACK_button::button_64_arg btn_64arg;

    btn_64arg.tileset_down = 17;
    btn_64arg.tileset_up = 17;
    btn_64arg.field_3A = 17;
    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
    btn_64arg.xpos = 0;
    btn_64arg.caption = "???";
    btn_64arg.caption2.clear();
    btn_64arg.downCode = 0;
    btn_64arg.upCode = 0;
    btn_64arg.pressedCode = 0;
    btn_64arg.ypos = 14 * (vertMenuSpace + _fontH);
    btn_64arg.button_id = UIWidgets::NETWORK_MENU_WIDGET_IDS::TXTBOX;
    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
    btn_64arg.width = dword_5A50B6 * 0.8;
    btn_64arg.txt_r = _iniColors[60].r;
    btn_64arg.txt_g = _iniColors[60].g;
    btn_64arg.txt_b = _iniColors[60].b;


    v70 = 0;

    if ( _GameShell->network_button->Add(&btn_64arg) )
    {
        btn_64arg.tileset_down = 19;
        btn_64arg.tileset_up = 18;
        btn_64arg.xpos = buttonsSpace + dword_5A50B6 * 0.8;
        btn_64arg.field_3A = 30;
        btn_64arg.width = dword_5A50B6 * 0.2 - buttonsSpace;
        btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
        btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_SEND);
        btn_64arg.caption2.clear();
        btn_64arg.upCode = 1210;
        btn_64arg.pressedCode = 0;
        btn_64arg.button_id = 1225;
        btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
        btn_64arg.txt_r = _iniColors[68].r;
        btn_64arg.txt_g = _iniColors[68].g;
        btn_64arg.txt_b = _iniColors[68].b;

        if ( _GameShell->network_button->Add(&btn_64arg) )
        {
            int v284 = ((dword_5A50B6 - 3 * buttonsSpace) * 0.25 - 3 * buttonsSpace) * 0.25;

            TileMap *v198 = GFX::Engine.GetTileset(8);

            btn_64arg.tileset_down = 16;
            btn_64arg.tileset_up = 16;
            btn_64arg.field_3A = 16;
            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
            btn_64arg.ypos = (15 * (vertMenuSpace + _fontH));
            btn_64arg.xpos = 0;
            btn_64arg.width = dword_5A50B6 * 0.4 - 2 * buttonsSpace;
            btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_SELRACE);
            btn_64arg.caption2.clear();
            btn_64arg.downCode = 0;
            btn_64arg.flags = NC_STACK_button::FLAG_TEXT | NC_STACK_button::FLAG_RALIGN;
            btn_64arg.button_id = 1220;
            btn_64arg.txt_r = _iniColors[60].r;
            btn_64arg.txt_g = _iniColors[60].g;
            btn_64arg.txt_b = _iniColors[60].b;

            if ( _GameShell->network_button->Add(&btn_64arg) )
            {
                btn_64arg.tileset_down = 8;
                btn_64arg.tileset_up = 8;
                btn_64arg.field_3A = 30;
                btn_64arg.button_type = 4;
                btn_64arg.xpos += btn_64arg.width + 2 * buttonsSpace;
                btn_64arg.width = v198->map[65].w;
                btn_64arg.caption = "A";
                btn_64arg.caption2 = "B";
                btn_64arg.button_id = 1206;
                btn_64arg.downCode = 1204;
                btn_64arg.flags = 0;

                if ( _GameShell->network_button->Add(&btn_64arg) )
                {
                    btn_64arg.caption = "C";
                    btn_64arg.caption2 = "D";
                    btn_64arg.downCode = 1205;
                    btn_64arg.button_id = 1207;
                    btn_64arg.xpos += v284 + buttonsSpace;

                    if ( _GameShell->network_button->Add(&btn_64arg) )
                    {
                        btn_64arg.caption = "E";
                        btn_64arg.caption2 = "F";
                        btn_64arg.downCode = 1206;
                        btn_64arg.button_id = 1208;
                        btn_64arg.xpos += v284 + buttonsSpace;

                        if ( _GameShell->network_button->Add(&btn_64arg) )
                        {
                            btn_64arg.caption = "G";
                            btn_64arg.caption2 = "H";
                            btn_64arg.downCode = 1207;
                            btn_64arg.button_id = 1209;
                            btn_64arg.xpos += v284 + buttonsSpace;

                            if ( _GameShell->network_button->Add(&btn_64arg) )
                            {
                                btn_64arg.tileset_down = 19;
                                btn_64arg.tileset_up = 18;
                                btn_64arg.field_3A = 30;
                                btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                                btn_64arg.xpos += v284 + 2 * buttonsSpace;
                                btn_64arg.width = dword_5A50B2_h - btn_64arg.xpos;
                                btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_BACK);
                                btn_64arg.caption2.clear();
                                btn_64arg.pressedCode = 0;
                                btn_64arg.button_id = 1205;
                                btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                                btn_64arg.upCode = 1203;
                                btn_64arg.downCode = 1251;
                                btn_64arg.txt_r = _iniColors[68].r;
                                btn_64arg.txt_g = _iniColors[68].g;
                                btn_64arg.txt_b = _iniColors[68].b;

                                if ( _GameShell->network_button->Add(&btn_64arg) )
                                {
                                    btn_64arg.tileset_down = 16;
                                    btn_64arg.xpos = 0;
                                    btn_64arg.ypos = 0;
                                    btn_64arg.tileset_up = 16;
                                    btn_64arg.field_3A = 16;
                                    btn_64arg.width = dword_5A50B2_h;
                                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                    btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_SELPROV);
                                    btn_64arg.caption2.clear();
                                    btn_64arg.downCode = 0;
                                    btn_64arg.upCode = 0;
                                    btn_64arg.button_id = UIWidgets::NETWORK_MENU_WIDGET_IDS::TXT_MENU_TITLE;
                                    btn_64arg.pressedCode = 0;
                                    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;

                                    if ( _GameShell->network_button->Add(&btn_64arg) )
                                    {
                                        btn_64arg.xpos = 0;
                                        btn_64arg.ypos = buttonsSpace + _fontH;
                                        btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_TXT2);
                                        btn_64arg.caption2.clear();
                                        btn_64arg.button_id = UIWidgets::NETWORK_MENU_WIDGET_IDS::TXT_MENU_DESCR_LINE1;
                                        btn_64arg.txt_r = _iniColors[60].r;
                                        btn_64arg.txt_g = _iniColors[60].g;
                                        btn_64arg.txt_b = _iniColors[60].b;

                                        if ( _GameShell->network_button->Add(&btn_64arg) )
                                        {
                                            btn_64arg.xpos = 0;
                                            btn_64arg.ypos = 2 * (_fontH + buttonsSpace);
                                            btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_TXT3);
                                            btn_64arg.caption2.clear();
                                            btn_64arg.button_id = UIWidgets::NETWORK_MENU_WIDGET_IDS::TXT_MENU_DESCR_LINE2;

                                            if ( _GameShell->network_button->Add(&btn_64arg) )
                                            {
                                                btn_64arg.tileset_down = 19;
                                                btn_64arg.tileset_up = 18;
                                                btn_64arg.button_type = NC_STACK_button::TYPE_BUTTON;
                                                btn_64arg.field_3A = 30;
                                                btn_64arg.xpos = dword_5A50B6 * 0.3;
                                                btn_64arg.ypos = (buttonsSpace + _fontH) * 15.2;
                                                btn_64arg.width = dword_5A50B6 * 0.4;
                                                btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_NEW);
                                                btn_64arg.button_id = UIWidgets::NETWORK_MENU_WIDGET_IDS::BTN_CREATE_SESSTION;
                                                btn_64arg.flags = NC_STACK_button::FLAG_BORDER | NC_STACK_button::FLAG_CENTER | NC_STACK_button::FLAG_TEXT;
                                                btn_64arg.downCode = 1251;
                                                btn_64arg.upCode = 1201;
                                                btn_64arg.caption2.clear();
                                                btn_64arg.pressedCode = 0;
                                                btn_64arg.txt_r = _iniColors[68].r;
                                                btn_64arg.txt_g = _iniColors[68].g;
                                                btn_64arg.txt_b = _iniColors[68].b;

                                                if ( _GameShell->network_button->Add(&btn_64arg) )
                                                {
                                                    btn_64arg.xpos = 0;
                                                    btn_64arg.ypos = bottomButtonsY + _fontH;
                                                    btn_64arg.width = button1LineWidth;
                                                    btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_NEXT);
                                                    btn_64arg.caption2.clear();
                                                    btn_64arg.pressedCode = 0;
                                                    btn_64arg.button_id = 1201;
                                                    btn_64arg.upCode = 1200;

                                                    if ( _GameShell->network_button->Add(&btn_64arg) )
                                                    {
                                                        btn_64arg.xpos = bottomThirdBtnPosX;
                                                        btn_64arg.ypos = bottomButtonsY + _fontH;
                                                        btn_64arg.width = button1LineWidth;
                                                        btn_64arg.caption = Locale::Text::Common(Locale::CMN_HELP);
                                                        btn_64arg.caption2.clear();
                                                        btn_64arg.upCode = 1250;
                                                        btn_64arg.pressedCode = 0;
                                                        btn_64arg.button_id = 1218;

                                                        if ( _GameShell->network_button->Add(&btn_64arg) )
                                                        {
                                                            btn_64arg.xpos = bottomSecondBtnPosX;
                                                            btn_64arg.ypos = bottomButtonsY + _fontH;
                                                            btn_64arg.width = button1LineWidth;
                                                            btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_CANCEL);
                                                            btn_64arg.caption2.clear();
                                                            btn_64arg.upCode = 1202;
                                                            btn_64arg.pressedCode = 0;
                                                            btn_64arg.button_id = 1203;

                                                            if ( _GameShell->network_button->Add(&btn_64arg) )
                                                            {
                                                                int v204;

                                                                if ( _screenSize.x < 512 )
                                                                    v204 = 6 * checkBoxWidth;
                                                                else
                                                                    v204 = 4 * checkBoxWidth;

                                                                btn_64arg.tileset_down = 16;
                                                                btn_64arg.tileset_up = 16;
                                                                btn_64arg.field_3A = 16;
                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                btn_64arg.xpos = v204 + checkBoxWidth;
                                                                btn_64arg.ypos = 4 * (_fontH + buttonsSpace);
                                                                btn_64arg.caption = " ";
                                                                btn_64arg.width = dword_5A50B2_h - v204 - checkBoxWidth;
                                                                btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                btn_64arg.caption2.clear();
                                                                btn_64arg.downCode = 0;
                                                                btn_64arg.upCode = 0;
                                                                btn_64arg.pressedCode = 0;
                                                                btn_64arg.button_id = 1210;
                                                                btn_64arg.txt_r = _iniColors[60].r;
                                                                btn_64arg.txt_g = _iniColors[60].g;
                                                                btn_64arg.txt_b = _iniColors[60].b;

                                                                if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                {
                                                                    btn_64arg.ypos = 5 * (buttonsSpace + _fontH);
                                                                    btn_64arg.button_id = 1211;

                                                                    if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                    {
                                                                        btn_64arg.ypos = 6 * (buttonsSpace + _fontH);
                                                                        btn_64arg.button_id = 1212;

                                                                        if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                        {
                                                                            btn_64arg.ypos = 7 * (buttonsSpace + _fontH);
                                                                            btn_64arg.button_id = 1213;

                                                                            if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                            {
                                                                                btn_64arg.tileset_down = 8;
                                                                                btn_64arg.tileset_up = 8;
                                                                                btn_64arg.field_3A = 8;
                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                btn_64arg.xpos = 0;
                                                                                btn_64arg.width = v204;
                                                                                btn_64arg.caption2.clear();
                                                                                btn_64arg.downCode = 0;
                                                                                btn_64arg.upCode = 0;
                                                                                btn_64arg.ypos = 4 * (_fontH + buttonsSpace);
                                                                                btn_64arg.pressedCode = 0;
                                                                                btn_64arg.caption = " ";
                                                                                btn_64arg.flags = 0;
                                                                                btn_64arg.button_id = 1214;
                                                                                btn_64arg.txt_r = _iniColors[60].r;
                                                                                btn_64arg.txt_g = _iniColors[60].g;
                                                                                btn_64arg.txt_b = _iniColors[60].b;

                                                                                if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                {
                                                                                    btn_64arg.ypos = 5 * (_fontH + buttonsSpace);
                                                                                    btn_64arg.button_id = 1215;

                                                                                    if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                    {
                                                                                        btn_64arg.ypos = 6 * (buttonsSpace + _fontH);
                                                                                        btn_64arg.button_id = 1216;

                                                                                        if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                        {
                                                                                            btn_64arg.ypos = 7 * (buttonsSpace + _fontH);
                                                                                            btn_64arg.button_id = 1217;

                                                                                            if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                            {
                                                                                                btn_64arg.tileset_down = 19;
                                                                                                btn_64arg.tileset_up = 18;
                                                                                                btn_64arg.xpos = 0;
                                                                                                btn_64arg.field_3A = 30;
                                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CHECKBX;
                                                                                                btn_64arg.width = checkBoxWidth;
                                                                                                btn_64arg.caption = "g";
                                                                                                btn_64arg.caption2 = "g";
                                                                                                btn_64arg.pressedCode = 0;
                                                                                                btn_64arg.button_id = 1219;
                                                                                                btn_64arg.ypos = bottomButtonsY + _fontH;
                                                                                                btn_64arg.downCode = 1208;
                                                                                                btn_64arg.flags = 0;
                                                                                                btn_64arg.upCode = 1209;

                                                                                                if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                                {
                                                                                                    btn_64arg.tileset_down = 16;
                                                                                                    btn_64arg.tileset_up = 16;
                                                                                                    btn_64arg.field_3A = 16;
                                                                                                    btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                    btn_64arg.xpos += buttonsSpace + checkBoxWidth;
                                                                                                    btn_64arg.width = button1LineWidth - checkBoxWidth - buttonsSpace;
                                                                                                    btn_64arg.caption = Locale::Text::Netdlg(Locale::NETDLG_READY);
                                                                                                    btn_64arg.caption2.clear();
                                                                                                    btn_64arg.downCode = 0;
                                                                                                    btn_64arg.upCode = 0;
                                                                                                    btn_64arg.pressedCode = 0;
                                                                                                    btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                                                    btn_64arg.button_id = 1221;

                                                                                                    if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                                    {
                                                                                                        btn_64arg.xpos = 0;
                                                                                                        btn_64arg.tileset_down = 16;
                                                                                                        btn_64arg.tileset_up = 16;
                                                                                                        btn_64arg.field_3A = 16;
                                                                                                        btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                        btn_64arg.ypos = 3 * (_fontH + buttonsSpace);
                                                                                                        btn_64arg.width = dword_5A50B6 * 0.3;
                                                                                                        btn_64arg.caption = Locale::Text::Get(Locale::LBL_YOUPLAY, Locale::DefaultStrings::YouPlay);
                                                                                                        btn_64arg.caption2.clear();
                                                                                                        btn_64arg.downCode = 0;
                                                                                                        btn_64arg.upCode = 0;
                                                                                                        btn_64arg.pressedCode = 0;
                                                                                                        btn_64arg.button_id = 1227;
                                                                                                        btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                                                        btn_64arg.txt_r = _iniColors[68].r;
                                                                                                        btn_64arg.txt_g = _iniColors[68].g;
                                                                                                        btn_64arg.txt_b = _iniColors[68].b;

                                                                                                        if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                                        {
                                                                                                            btn_64arg.xpos = dword_5A50B6 * 0.3;
                                                                                                            btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                            btn_64arg.width = dword_5A50B6 * 0.7;
                                                                                                            btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                                                            btn_64arg.button_id = 1226;
                                                                                                            btn_64arg.caption = "...";

                                                                                                            if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                                            {
                                                                                                                btn_64arg.tileset_down = 16;
                                                                                                                btn_64arg.tileset_up = 16;
                                                                                                                btn_64arg.field_3A = 16;
                                                                                                                btn_64arg.button_type = NC_STACK_button::TYPE_CAPTION;
                                                                                                                btn_64arg.xpos = 0;
                                                                                                                btn_64arg.ypos = (14 * (vertMenuSpace + _fontH));
                                                                                                                btn_64arg.width = dword_5A50B2_h;
                                                                                                                btn_64arg.caption = Locale::Text::Advanced(Locale::ADV_REFRESHSESS);
                                                                                                                btn_64arg.caption2.clear();
                                                                                                                btn_64arg.downCode = 0;
                                                                                                                btn_64arg.upCode = 0;
                                                                                                                btn_64arg.pressedCode = 0;
                                                                                                                btn_64arg.button_id = 1228;
                                                                                                                btn_64arg.flags = NC_STACK_button::FLAG_TEXT;
                                                                                                                btn_64arg.txt_r = _iniColors[60].r;
                                                                                                                btn_64arg.txt_g = _iniColors[60].g;
                                                                                                                btn_64arg.txt_b = _iniColors[60].b;

                                                                                                                if ( _GameShell->network_button->Add(&btn_64arg) )
                                                                                                                    v70 = 1;
                                                                                                            }
                                                                                                        }
                                                                                                    }
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if ( !v70 )
    {
        ypa_log_out("Unable to add network-button\n");
        return false;
    }
    NC_STACK_button::button_66arg v228;
    v228.butID = 1210;
    v228.field_4 = 0;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1211;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1212;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1213;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1214;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1215;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1216;
    _GameShell->network_button->Disable(&v228);

    v228.butID = 1217;
    _GameShell->network_button->Disable(&v228);

        // OpenUA: remove deprecated online-help button from Network.
    _GameShell->network_button->Remove(1218);

_GameShell->network_button->HideScreen();
    return true;
}
bool NC_STACK_ypaworld::OpenGameShell()
{
    printf("OpenGameShell\n");
    SetGameShellVideoMode( _GameShell->IsWindowedFlag() );

    if ( !yw_LoadSet(46) )
    {
        ypa_log_out("Unable to load set for shell\n");
        return false;
    }

    _GameShell->sgmSaveExist = 0;
    _GameShell->confFirstKey = true;
    _GameShell->lastInputEvent = 0;
    _GameShell->p_YW->_upScreenBorder = 0;
    _GameShell->keyCatchMode = false;
    _GameShell->p_YW->_helpURL.clear();
    _GameShell->blocked = false;

    if ( _GameShell->default_lang_dll )
    {
        if ( ! ypaworld_func166(*_GameShell->default_lang_dll) )
            ypa_log_out("Warning: Catalogue not found\n");
    }
    else
    {
        ypa_log_out("Warning: No Language selected, use default set\n");
    }

    ypaworld_func156__sub1(_GameShell);

    if ( !GameShellInitBkg() )
    {
        ypa_log_out("Could not init level select stuff!\n");
        return false;
    }

    GFX::displ_arg263 v233;

    if (_mousePointers[0])
        v233.bitm = _mousePointers[0]->GetBitmap();
    v233.pointer_id = 1;

    GFX::Engine.SetCursor(v233.pointer_id, 0);



    if ( _GameShell->GFXFlags & World::GFX_FLAG_SOFTMOUSE )
    {
        GFX::Engine.setWDD_cursor(1);
    }
    else
    {
        GFX::Engine.setWDD_cursor(0);
    }

    LoadKeyNames();


    _GameShell->InputConfigTitle[World::INPUT_BIND_PAUSE]       = Locale::Text::Inputs(Locale::INPUTS_PAUSE);
    _GameShell->InputConfigTitle[World::INPUT_BIND_QUIT]        = Locale::Text::Inputs(Locale::INPUTS_QUIT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_DRIVE_DIR]   = Locale::Text::Inputs(Locale::INPUTS_DRIVEDIR);
    _GameShell->InputConfigTitle[World::INPUT_BIND_DRIVE_SPEED] = Locale::Text::Inputs(Locale::INPUTS_DRIVESPD);
    _GameShell->InputConfigTitle[World::INPUT_BIND_GUN_HEIGHT]  = Locale::Text::Inputs(Locale::INPUTS_GUNHGHT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_FLY_HEIGHT]  = Locale::Text::Inputs(Locale::INPUTS_FLYHGHT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_FLY_SPEED]   = Locale::Text::Inputs(Locale::INPUTS_FLYSPD);
    _GameShell->InputConfigTitle[World::INPUT_BIND_FLY_DIR]     = Locale::Text::Inputs(Locale::INPUTS_FLYDIR);
    _GameShell->InputConfigTitle[World::INPUT_BIND_BRAKE]       = Locale::Text::Inputs(Locale::INPUTS_STOP);
    _GameShell->InputConfigTitle[World::INPUT_BIND_FIRE]        = Locale::Text::Inputs(Locale::INPUTS_FIRE);
    _GameShell->InputConfigTitle[World::INPUT_BIND_CAMFIRE]     = Locale::Text::Inputs(Locale::INPUTS_FIREVW);
    _GameShell->InputConfigTitle[World::INPUT_BIND_GUN]         = Locale::Text::Inputs(Locale::INPUTS_FIREGUN);
    _GameShell->InputConfigTitle[World::INPUT_BIND_SET_COMM]    = Locale::Text::Inputs(Locale::INPUTS_MAKECOMM);
    _GameShell->InputConfigTitle[World::INPUT_BIND_HUD]         = Locale::Text::Inputs(Locale::INPUTS_HEADUPDISP);
    _GameShell->InputConfigTitle[World::INPUT_BIND_AUTOPILOT]   = Locale::Text::Inputs(Locale::INPUTS_AUTOPILOT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_ORDER]       = Locale::Text::Inputs(Locale::INPUTS_ORDER);
    _GameShell->InputConfigTitle[World::INPUT_BIND_NEW]         = Locale::Text::Inputs(Locale::INPUTS_NEW);
    _GameShell->InputConfigTitle[World::INPUT_BIND_ADD]         = Locale::Text::Inputs(Locale::INPUTS_ADD);
    _GameShell->InputConfigTitle[World::INPUT_BIND_SQ_MANAGE]   = Locale::Text::Inputs(Locale::INPUTS_FINDER);
    _GameShell->InputConfigTitle[World::INPUT_BIND_AGGR_1]      = Locale::Text::Inputs(Locale::INPUTS_AGGR1);
    _GameShell->InputConfigTitle[World::INPUT_BIND_AGGR_2]      = Locale::Text::Inputs(Locale::INPUTS_AGGR2);
    _GameShell->InputConfigTitle[World::INPUT_BIND_AGGR_3]      = Locale::Text::Inputs(Locale::INPUTS_AGGR3);
    _GameShell->InputConfigTitle[World::INPUT_BIND_AGGR_4]      = Locale::Text::Inputs(Locale::INPUTS_AGGR4);
    _GameShell->InputConfigTitle[World::INPUT_BIND_AGGR_5]      = Locale::Text::Inputs(Locale::INPUTS_AGGR5);
    _GameShell->InputConfigTitle[World::INPUT_BIND_MAP]         = Locale::Text::Inputs(Locale::INPUTS_MAP);
    _GameShell->InputConfigTitle[World::INPUT_BIND_WAPOINT]     = Locale::Text::Inputs(Locale::INPUTS_SELWAYPT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_LANDLAYER]   = Locale::Text::Inputs(Locale::INPUTS_LANDSCAPE);
    _GameShell->InputConfigTitle[World::INPUT_BIND_OWNER]       = Locale::Text::Inputs(Locale::INPUTS_OWNER);
    _GameShell->InputConfigTitle[World::INPUT_BIND_HEIGHT]      = Locale::Text::Inputs(Locale::INPUTS_HEIGHT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_MINIMAP]     = Locale::Text::Inputs(Locale::INPUTS_MAPMINI);
    _GameShell->InputConfigTitle[World::INPUT_BIND_LOCKVIEW]    = Locale::Text::Inputs(Locale::INPUTS_LOCKVW);
    _GameShell->InputConfigTitle[World::INPUT_BIND_ZOOMIN]      = Locale::Text::Inputs(Locale::INPUTS_ZOOMIN);
    _GameShell->InputConfigTitle[World::INPUT_BIND_ZOOMOUT]     = Locale::Text::Inputs(Locale::INPUTS_ZOOMOUT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_LOG_WND]     = Locale::Text::Inputs(Locale::INPUTS_LOGWIN);
    _GameShell->InputConfigTitle[World::INPUT_BIND_CONTROL]     = Locale::Text::Inputs(Locale::INPUTS_CONTROL);
    _GameShell->InputConfigTitle[World::INPUT_BIND_LAST_SEAT]   = Locale::Text::Inputs(Locale::INPUTS_TOLASTOCCUP);
    _GameShell->InputConfigTitle[World::INPUT_BIND_ATTACK]      = Locale::Text::Inputs(Locale::INPUTS_FIGHT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_TO_HOST]     = Locale::Text::Inputs(Locale::INPUTS_TOROBO);
    _GameShell->InputConfigTitle[World::INPUT_BIND_TO_COMM]     = Locale::Text::Inputs(Locale::INPUTS_TOCOMM);
    _GameShell->InputConfigTitle[World::INPUT_BIND_NEXT_UNIT]   = Locale::Text::Inputs(Locale::INPUTS_NEXTUNIT);
    _GameShell->InputConfigTitle[World::INPUT_BIND_NEXT_COMM]   = Locale::Text::Inputs(Locale::INPUTS_NEXTCOM);
    _GameShell->InputConfigTitle[World::INPUT_BIND_LAST_MSG]    = Locale::Text::Inputs(Locale::INPUTS_JUMPLASTMSNG);
    _GameShell->InputConfigTitle[World::INPUT_BIND_TO_ALL]      = Locale::Text::Inputs(Locale::INPUTS_MSGTOALL);
    _GameShell->InputConfigTitle[World::INPUT_BIND_HELP]        = Locale::Text::Inputs(Locale::INPUTS_HELP);
    _GameShell->InputConfigTitle[World::INPUT_BIND_ANALYZER]    = Locale::Text::Inputs(Locale::INPUTS_ANALYZER);
    _GameShell->InputConfigTitle[World::INPUT_BIND_COCKPIT_CAMERA] = "Toggle Cockpit Camera";



    if ( _screenSize.x < 512 )
    {
        buttonsSpace = 2;
        vertMenuSpace = 2;
        checkBoxWidth = 8;
        dword_5A50B2 = 210;
        word_5A50AE = 200;
        word_5A50BC = 220;
        word_5A50BA = 300;
        word_5A50BE = 270;
    }
    else
    {
        buttonsSpace = 3;
        vertMenuSpace = 3;
        checkBoxWidth = 16;
        dword_5A50B2 = 380;
        word_5A50AE = 280;
        word_5A50BC = 390;
        word_5A50BA = 500;
        word_5A50BE = 480;
    }

    int menuWidth = _screenSize.x * 0.7;
    int menuHeight = _screenSize.y * 0.8;

    scaledFontHeight = _fontH;
    if ( _screenSize.x >= 512 )
        scaledFontHeight += (_screenSize.y - 384) / 2;



    if ( _screenSize.x < 512 )
        bottomButtonsY = menuHeight - _fontH;
    else
        bottomButtonsY = menuHeight - _fontH - (_screenSize.y - 384) / 2;

    button1LineWidth = ( menuWidth - 2 * buttonsSpace )/ 3;



    bottomSecondBtnPosX = buttonsSpace + button1LineWidth;
    bottomThirdBtnPosX = 2 * buttonsSpace + 2 * button1LineWidth;

    printf("Creating CreateTitleControls\n");
    if (!this->CreateTitleControls()) return false;
    printf("Creating CreateSubBarControls\n");
    if (!this->CreateSubBarControls()) return false;
    printf("Creating CreateConfirmControls\n");
    if (!this->CreateConfirmControls()) return false;


    dword_5A50B2_h = menuWidth - _fontVBScrollW;


    printf("Creating CreateInputControls\n");
    if (!this->CreateInputControls()) return false;
    printf("Creating CreateVideoControls\n");
    if (!this->CreateVideoControls()) return false;
    printf("Creating CreateDiskControls\n");
    if (!this->CreateDiskControls()) return false;
    printf("Creating CreateLocaleControls\n");
    if (!this->CreateLocaleControls()) return false;
    printf("Creating CreateAboutControls\n");
    if (!this->CreateAboutControls()) return false;
    printf("Creating CreateDatabaseControls\n");
    if (!this->CreateDatabaseControls()) return false;

    dword_5A50B6 = menuWidth - _fontVBScrollW;

    if (!this->CreateNetworkControls()) return false;

    switch (_GameShell->EnvMode)
    {
    default:
        _GameShell->titel_button->ShowScreen();
        break;
    case ENVMODE_TUTORIAL:
    case ENVMODE_SINGLEPLAY:
        _GameShell->sub_bar_button->ShowScreen();

        if ( _GameShell->GameIsOver )
        {
            NC_STACK_button::button_66arg v231;
            v231.field_4 = 0;
            v231.butID = 1014;

            _GameShell->sub_bar_button->Disable(&v231);

            v231.butID = 1013;
            _GameShell->sub_bar_button->Disable(&v231);
        }
        break;
    case ENVMODE_NETPLAY:
        _GameShell->network_button->ShowScreen();
        break;
    }


    UpdateGameShell();

    SFXEngine::SFXe.startSound(&_GameShell->samples1_info, 6);

    _GameShell->HasInited = true;

    if ( _GameShell->remoteMode )
    {
        _GameShell->GameShellUiOpenNetwork();
        _GameShell->p_YW->_isNetGame = true;
        _GameShell->FreeFraction = (World::OWNER_GHOR_BIT | World::OWNER_MYKO_BIT | World::OWNER_TAER_BIT);
        _GameShell->SelectedFraction = World::OWNER_RESIST_BIT;
    }
    else
    {
        _GameShell->yw_netcleanup();
        _GameShell->netSelMode = UserData::NETSCREEN_MODE_SELECT;
    }

    _GameShell->netSel = -1;

    if ( _GameShell->p_YW->_preferences & World::PREF_CDMUSICDISABLE )
    {
        SFXEngine::SFXe.StopMusicTrack();
        SFXEngine::SFXe.SetMusicTrack(_GameShell->shelltrack, _GameShell->shelltrack__adv.min_delay, _GameShell->shelltrack__adv.max_delay);
        SFXEngine::SFXe.PlayMusicTrack();
    }

    return true;
}


void NC_STACK_ypaworld::CloseGameShell()
{
    if ( _GameShell->HasInited )
    {
        if ( _GameShell->confirm_button )
        {
            _GameShell->confirm_button->HideScreen();
            _GameShell->confirm_button->Delete();
        }
        _GameShell->confirm_button = NULL;

        if ( _GameShell->sub_bar_button )
        {
            _GameShell->sub_bar_button->HideScreen();
            _GameShell->sub_bar_button->Delete();
        }
        _GameShell->sub_bar_button = NULL;

        if ( _GameShell->titel_button )
        {
            _GameShell->titel_button->HideScreen();
            _GameShell->titel_button->Delete();
        }
        _GameShell->titel_button = NULL;

        if ( _GameShell->button_input_button )
        {
            if ( _GameShell->input_listview.IsOpen() )
                _GameShell->p_YW->GuiWinClose( &_GameShell->input_listview );
            _GameShell->input_listview.Free();

            _GameShell->button_input_button->HideScreen();
            _GameShell->button_input_button->Delete();
            _GameShell->button_input_button = NULL;
        }

        if ( _GameShell->video_button )
        {
            if ( _GameShell->video_listvw.IsOpen() )
                _GameShell->p_YW->GuiWinClose( &_GameShell->video_listvw );
            _GameShell->video_listvw.Free();

            if ( _GameShell->d3d_listvw.IsOpen() )
                _GameShell->p_YW->GuiWinClose( &_GameShell->d3d_listvw );
            _GameShell->d3d_listvw.Free();

            _GameShell->video_button->HideScreen();
            _GameShell->video_button->Delete();
            _GameShell->video_button = NULL;
        }

        if ( _GameShell->disk_button )
        {
            if ( _GameShell->disk_listvw.IsOpen() )
                _GameShell->p_YW->GuiWinClose( &_GameShell->disk_listvw );
            _GameShell->disk_listvw.Free();

            _GameShell->disk_button->HideScreen();
            _GameShell->disk_button->Delete();
            _GameShell->disk_button = NULL;
        }

        if ( _GameShell->locale_button )
        {
            if ( _GameShell->local_listvw.IsOpen() )
                _GameShell->p_YW->GuiWinClose( &_GameShell->local_listvw );
            _GameShell->local_listvw.Free();

            _GameShell->locale_button->HideScreen();
            _GameShell->locale_button->Delete();
            _GameShell->locale_button = NULL;
        }

        if ( _GameShell->about_button )
        {
            _GameShell->about_button->HideScreen();
            _GameShell->about_button->Delete();
            _GameShell->about_button = NULL;
        }

        if ( _GameShell->network_button )
        {
            if ( _GameShell->network_listvw.IsOpen() )
                _GameShell->p_YW->GuiWinClose( &_GameShell->network_listvw );
            _GameShell->network_listvw.Free();

            _GameShell->network_button->HideScreen();
            _GameShell->network_button->Delete();
            _GameShell->network_button = NULL;
        }

        SFXEngine::SFXe.StopPlayingSounds();

        _globalMapRegions.UnloadImages();

        if ( _GameShell->EnvMode == ENVMODE_TUTORIAL || _GameShell->EnvMode == ENVMODE_SINGLEPLAY )
        {
            if ( _levelInfo.State == TLevelInfo::STATE_BRIEFING )
            {
                FreeBriefing();
            }
            else if ( _levelInfo.State == TLevelInfo::STATE_DEBRIEFING )
            {
                FreeDebrief();
            }
        }

        sb_0x44ac24(this);

        _GameShell->HasInited = false;
    }
}

//Draw bkg or briefing
void NC_STACK_ypaworld::GameShellBkgProcess()
{
    if ( _GameShell->EnvModeChanged )
        GameShellInitBkgMode(_GameShell->EnvMode);

    switch(_GameShell->EnvMode)
    {
    case ENVMODE_TUTORIAL:
    case ENVMODE_SINGLEPLAY:
        ypaworld_func158__sub4__sub1();
        break;

    case ENVMODE_TITLE:
        GameShellBlitBkg(_globalMapRegions.MenuImage);
        break;

    default:
        GameShellBlitBkg(_globalMapRegions.RolloverImage);
        break;
    }
}

void draw_tooltip(NC_STACK_ypaworld *yw)
{
    if ( yw->_mouseGrabbed || (yw->_toolTipId && !yw->_mouseCursorHidden) )
    {
        int v15 = -(yw->_fontH + yw->_downScreenBorder + yw->_fontH / 4);
        std::string v2;

        if ( yw->_toolTipHotKeyId != -1 )
        {
            int16_t keycode = Input::Engine.GetHotKey(yw->_toolTipHotKeyId);

            if ( keycode != Input::KC_NONE )
            {
                if ( yw->_GameShell && !Input::Engine.KeyTitle.at(keycode).empty())
                {
                    v2 = fmt::sprintf("[%s]", Input::Engine.KeyTitle.at(keycode));
                    v15 = -(yw->_downScreenBorder + 2 * yw->_fontH + yw->_fontH / 4);
                }
            }
        }

        CmdStream buf;
        buf.reserve(1024);

        FontUA::select_tileset(&buf, 15);
        FontUA::set_xpos(&buf, 0);
        FontUA::set_ypos(&buf, v15);

        if ( !v2.empty() )
        {
            FontUA::set_txtColor(&buf, yw->_iniColors[61].r, yw->_iniColors[61].g, yw->_iniColors[61].b);

            FontUA::FormateCenteredSkipableItem(yw->_guiTiles[15], &buf, v2.c_str(), yw->_screenSize.x);

            FontUA::next_line(&buf);
        }

        FontUA::set_txtColor(&buf, yw->_iniColors[63].r, yw->_iniColors[63].g, yw->_iniColors[63].b);

        FontUA::FormateCenteredSkipableItem(yw->_guiTiles[15], &buf,  yw->GetTooltipString() , yw->_screenSize.x);

        FontUA::set_end(&buf);

        GFX::Engine.ProcessDrawSeq(buf);
    }

    yw->_toolTipHotKeyId = -1;
    yw->_toolTipId = 0;
}

//Make screenshot
void sub_4476AC(NC_STACK_ypaworld *yw)
{
    GFX::Engine.SaveScreenshot(fmt::sprintf("env:snaps/f_%04d", yw->_screenShotCount));

    yw->_screenShotCount++;
}

//FIXME
int PrevMouseX = 0;
int PrevMouseY = 0;

bool NC_STACK_ypaworld::IsAnyInput(TInputState *struc)
{
    bool click = (struc->ClickInf.flag & ~TClickBoxInf::FLAG_OK);
    bool mousemove = struc->ClickInf.move.ScreenPos.x != PrevMouseX || PrevMouseY != struc->ClickInf.move.ScreenPos.y;

    PrevMouseX = struc->ClickInf.move.ScreenPos.x;
    PrevMouseY = struc->ClickInf.move.ScreenPos.y;

    return struc->KbdLastDown != Input::KC_NONE || struc->KbdLastHit != Input::KC_NONE || struc->HotKeyID >= 0 || click || mousemove;
}

void NC_STACK_ypaworld::ProcessGameShell()
{
    _GameShell->envAction.action = EnvAction::ACTION_NONE;

    SFXEngine::SFXe.sub_423EFC(_GameShell->DTime, vec3d(0.0), vec3d(0.0), mat3x3::Ident());

    GFX::Engine.BeginFrame();

    int oldMode = _GameShell->EnvMode;
    _GameShell->EnvModeChanged = false;

    _GameShell->GameShellUiHandleInput();

    if ( oldMode != _GameShell->EnvMode )
        _GameShell->EnvModeChanged = true;

    GameShellBkgProcess();

    draw_tooltip(this);

    ypaworld_func158__sub3(this, _GameShell);

    if ( _isNetGame )
    {
        _netFlushTimer -= _GameShell->DTime;
        if ( _netFlushTimer <= 0 )
        {
            _netDriver->FlushBroadcastBuffer();

            _netFlushTimer = 100;
        }
    }

    SFXEngine::SFXe.UpdateSoundCarrier(&_GameShell->samples1_info);

    SFXEngine::SFXe.sb_0x424c74();


    GFX::Engine.EndFrame();


    if ( sub_449678(_GameShell->Input, Input::KC_NUMMUL) )
        sub_4476AC(this);

    if ( _GameShell->netSelMode == UserData::NETSCREEN_INSESSION )
    {
        yw_CheckCRCs(this);
    }

    if ( IsAnyInput(_GameShell->Input) )
        _GameShell->lastInputEvent = _GameShell->GlobalTime;

    if ( (_GameShell->GlobalTime - _GameShell->lastInputEvent) > World::CVDemoWait && _GameShell->EnvMode == ENVMODE_TITLE )
        _GameShell->envAction.action = EnvAction::ACTION_REPLAY;

    _GameShell->GameIsOver = false;

    // OpenUA: legacy online help pages are obsolete/dead.
    // Clear pending help URLs instead of launching an external browser.
    if ( !_helpURL.empty() )
        _helpURL.clear();
}


void NC_STACK_ypaworld::ypaworld_func159(yw_arg159 *arg)
{
    if ( arg->MsgID )
        VoiceMessagePlayMsg(arg->unit, arg->Priority, arg->MsgID);

    if ( IsSpectatorControlled() && (arg->MsgID || arg->unit) )
        return;

    if ( arg->unit )
        info_log.field_255C = arg->unit->_gid;
    else
        info_log.field_255C = 0;

    info_log.field_2560 = _timeStamp;
    info_log.field_2564 = arg->MsgID;

    if ( !arg->txt.empty() )
    {
        inflog_msg *v6;

        if ( info_log.field_250 >= 5 )
        {
            info_log.msg_count++;

            if ( info_log.msg_count >= 64 )
                info_log.msg_count = 0;

            if ( info_log.field_254 == info_log.msg_count )
            {
                info_log.field_254++;

                if ( info_log.field_254 >= 64 )
                    info_log.field_254 = 0;
            }

            info_log.numEntries++;;

            if ( info_log.numEntries > 64 )
                info_log.numEntries = 64;

            v6 = &info_log.msgs[info_log.msg_count];

            info_log.field_24C = info_log.msg_count;
        }
        else
        {
            info_log.msg_count = info_log.field_24C;

            v6 = &info_log.msgs[info_log.field_24C];
        }

        info_log.field_256C = 5000;
        info_log.field_2568 = 1;
        info_log.field_250 = arg->Priority;

        if ( arg->unit )
            v6->id = arg->unit->_gid;
        else
            v6->id = 0;

        v6->field_8 = 7000;
        v6->field_4 = _timeStamp;

        const char *v5 = arg->txt.c_str();

        int v10 = 0;

        while ( *v5 )
        {
            if ( *v5 == '\n' )
            {
                v6->txt[v10] = 0;

                v10 = 0;

                info_log.msg_count++;

                if ( info_log.msg_count >= 64 )
                    info_log.msg_count = 0;

                if ( info_log.field_254 == info_log.msg_count )
                {
                    info_log.field_254++;

                    if ( info_log.field_254 >= 64 )
                        info_log.field_254 = 0;
                }

                info_log.numEntries++;

                if ( info_log.numEntries > 64 )
                    info_log.numEntries = 64;

                info_log.field_256C += 5000;
                info_log.field_2568++;

                v6 = &info_log.msgs[ info_log.msg_count ];

                if ( arg->unit )
                    v6->id = arg->unit->_gid;
                else
                    v6->id = 0;

                v6->field_8 = 7000;
                v6->field_4 = 0;
            }
            else if ( v10 < 125 )
            {
                v6->txt[v10] = *v5;
                v10++;
            }

            v5++;
        }

        v6->txt[v10] = 0;

        info_log.firstShownEntries = info_log.numEntries - info_log.shownEntries;

        if ( info_log.firstShownEntries < 0 )
            info_log.firstShownEntries = 0;
    }
}


void NC_STACK_ypaworld::ypaworld_func160(void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func160");
}

// Load Level
size_t NC_STACK_ypaworld::ypaworld_func161(yw_arg161 *arg)
{
    int ok = 0;
    TLevelDescription mapp;

    _particles.Clear();

    if ( LevelCommonLoader(&mapp, arg->lvlID, arg->field_4) )
    {
        if ( LoadTypeMap(mapp.TypStr) )
        {
            if ( LoadOwnerMap( mapp.OwnStr) )
            {
                if ( LoadHightMap(mapp.HgtStr) )
                {
                    std::vector<MapRobo> playAsRobos = BriefingReorderRobosForPlayAs(mapp.Robos);

                    if ( yw_createRobos(playAsRobos) )
                    {
                        if ( LoadBlgMap(mapp.BlgStr) )
                        {
                            if ( _levelInfo.Mode != 1 )
                            {
                                yw_InitSquads(mapp.Squads);
                                InitBuddies();

                                for (int yy = 0; yy < _mapSize.y; yy++)
                                {
                                    for (int xx = 0; xx < _mapSize.x; xx++)
                                    {
                                        CellCheckHealth(&_cells(xx, yy), World::OWNER_RECALC, NULL);
                                    }
                                }

                                yw_InitTechUpgradeBuildings();
                                InitGates();
                                InitSuperItems();
                                UpdatePowerEnergy();
                                TryActivateSpectatorMode();
                            }

                            PrepareAllFillers();

                            if ( sb_0x451034(this) )
                                ok = 1;
                        }
                    }
                }
            }
        }
    }

    if ( !ok )
    {
        printf("Load level not OK\n");
        DeleteLevel();
    }

    return ok;
}


size_t NC_STACK_ypaworld::ypaworld_func162(const std::string &fname)
{
    _replayPlayer = new TGameRecorder();

    if ( !_replayPlayer )
        return 0;

    TGameRecorder *repl = _replayPlayer;

    repl->filename = fname;
    _debugAoeRings.clear();
    _timeStamp = 0;

    if ( !recorder_open_replay(repl) )
        return 0;

    while ( repl->mfile.parse() != IFFile::IFF_ERR_EOC )
    {
        const IFFile::Context &v13 = repl->mfile.GetCurrentChunk();

        if ( v13.Is(TAG_SINF) )
        {
            repl->seqn = repl->mfile.readU16L();
            repl->level_id = repl->mfile.readU16L();
            repl->mfile.parse();
        }
        else if ( v13.Is(TAG_FORM, TAG_FRAM) )
        {
            repl->field_74++;
            repl->mfile.skipChunk();
        }
        else
        {
            repl->mfile.skipChunk();
        }
    }


    repl->mfile.close();

    yw_arg161 arg161;
    arg161.field_4 = 1;
    arg161.lvlID = repl->level_id;

    if ( !ypaworld_func161(&arg161) )
        return 0;

    if ( !recorder_create_camera() )
    {
        ypa_log_out("PLAYER ERROR: could not create virtual camera!\n");
        ypaworld_func164();

        return 0;
    }

    repl->field_44 = vec3d(0.0, 0.0, 0.0);
    repl->rotation_matrix = mat3x3::Ident();

    if ( !recorder_go_to_frame(repl, 0) )
    {
        ypa_log_out("PLAYER ERROR: could not position on 1st frame!\n");
        ypaworld_func164();
        return 0;
    }

    yw_arg165 arg165;

    arg165.field_0 = 2;
    arg165.frame = 0;
    ypaworld_func165(&arg165);

    arg165.field_0 = 20;
    arg165.frame = 0;
    ypaworld_func165(&arg165);

    return 1;
}


void NC_STACK_ypaworld::ypaworld_func163(base_64arg *arg)
{
    TGameRecorder *repl = _replayPlayer;
    uint32_t v33 = profiler_begin();

    _framesElapsed++;
    _updateMessage.user_action = World::DOACTION_0;
    _updateMessage.gTime = arg->TimeStamp;
    _updateMessage.frameTime = arg->DTime;
    _updateMessage.units_count = 0;
    _updateMessage.inpt = arg->field_8;
    _FPS = 1024 / arg->DTime;

    _profileVals[PFID_FPS] = _FPS;

    GFX::Engine.BeginFrame();

    /*_win3d->setRSTR_BGpen(0);

    _win3d->raster_func192(NULL);*/

    sub_4C40AC();

    _guiVisor.field_0 = 0;
    _guiVisor.field_4 = 0;

    if ( repl->field_7C != 1 )
        ypaworld_func163__sub1(repl, arg->DTime);

    CameraPrepareRender(repl, _userUnit, arg->field_8);

    vec3d a3a = _userUnit->_fly_dir * _userUnit->_fly_dir_length;

    SFXEngine::SFXe.sub_423EFC(arg->DTime, _userUnit->_position, a3a, _userUnit->_rotation);

    for ( NC_STACK_ypabact* &bct : _userUnit->_kidList )
    {
        bct->_tForm.Pos = bct->_position;

        bct->_tForm.SclRot = bct->_rotation.Transpose();

        bct->_soundcarrier.Position = bct->_position;

        bct->_soundcarrier.Vector = bct->_fly_dir * bct->_fly_dir_length;

        SFXEngine::SFXe.UpdateSoundCarrier(&bct->_soundcarrier);
    }

    const mat3x3 &v25 = SFXEngine::SFXe.sb_0x424c74();
    TF::TForm3D *v26 = TF::Engine.GetViewPoint();

    v26->SclRot = v25 * v26->SclRot;

    uint32_t v28 = profiler_begin();

    RenderGame(arg, 0);

    debug_info_draw(arg->field_8);

    GFX::Engine.EndFrame();

    _profileVals[PFID_UPDATETIME] = profiler_end(v28);

    sb_0x447720(this, arg->field_8);

    _profileVals[PFID_FRAMETIME] = profiler_end(v33);
    _profileVals[PFID_POLYGONS] = _polysDraw;

    ProfileCalcValues();
}



void NC_STACK_ypaworld::ypaworld_func164()
{
    if ( _replayPlayer )
    {
        _replayPlayer->mfile.close();

        DeleteLevel();

        delete _replayPlayer;
        _replayPlayer = NULL;
    }
}


void NC_STACK_ypaworld::ypaworld_func165(yw_arg165 *arg)
{
    TGameRecorder *repl = _replayPlayer;

    if ( (repl->field_80 == 18 || repl->field_80 == 19 || repl->field_80 == 20) && (arg->field_0 == 16 || arg->field_0 == 17) )
    {
        repl->field_44 = _userUnit->_position;
        repl->rotation_matrix = _userUnit->_rotation;
    }

    if ( arg->field_0 == 1 || arg->field_0 == 2 )
    {
        repl->field_7C = arg->field_0;
    }
    else if ( arg->field_0 == 3 )
    {
        recorder_go_to_frame(repl, arg->frame);
    }
    else if ( arg->field_0 == 4 )
    {
        recorder_go_to_frame(repl, repl->frame_id + 1);
    }
    else if ( arg->field_0 == 5 )
    {
        recorder_go_to_frame(repl, repl->frame_id - 1);
    }
    else if ( arg->field_0 == 7 )
    {
        recorder_go_to_frame(repl, repl->frame_id + arg->frame);
    }
    else if ( arg->field_0 == 16 || arg->field_0 == 17 )
    {
        repl->field_84 = 0;
        repl->field_80 = arg->field_0;
    }
    else if ( arg->field_0 == 18 || arg->field_0 == 19 || arg->field_0 == 20 )
    {
        repl->field_80 = arg->field_0;
        repl->field_84 = arg->frame;

        repl->field_44 = vec3d(0.0, 0.0, 0.0);
        repl->rotation_matrix = mat3x3::Ident();
    }
    else
    {
        repl->field_7C = 1;
    }
}

size_t NC_STACK_ypaworld::ypaworld_func166(const std::string &langname)
{
    Locale::Text::SetLangDefault();

    Locale::Text::SetLocaleName(langname);

    if ( !Locale::Text::LngFileLoad( fmt::sprintf("locale:%s.lng", langname) ) )
    {
        Locale::Text::SetLangDefault();
        return 0;
    }

    std::string fontStr;

    if ( _screenSize.x >= 512 )
        fontStr = Locale::Text::Font();
    else
        fontStr = Locale::Text::SmallFont();

    fontStr = System::ResolveMenuFontDescr(fontStr);
    GFX::Engine.LoadFontByDescr(fontStr);
    Gui::UA::LoadFont(fontStr);

    return 1;
}


// Update menu values
void NC_STACK_ypaworld::UpdateGameShell()
{
    if ( _GameShell->diskListActiveElement )
    {
        _GameShell->disk_listvw.PosOnSelected(_GameShell->diskListActiveElement - 1);

        NC_STACK_button::button_66arg v18;
        v18.field_4 = 1;
        v18.butID = 1101;
        _GameShell->disk_button->Enable(&v18);

        v18.butID = 1102;
        _GameShell->disk_button->Enable(&v18);

        v18.butID = 1103;
        _GameShell->disk_button->Enable(&v18);
    }
    else
    {
        NC_STACK_button::button_66arg v18;
        v18.field_4 = 1;
        v18.butID = 1101;
        _GameShell->disk_button->Disable(&v18);

        v18.butID = 1102;
        _GameShell->disk_button->Disable(&v18);

        v18.butID = 1103;
        _GameShell->disk_button->Disable(&v18);
    }

    NC_STACK_button::button_66arg v16;
    v16.butID = 1151;
    v16.field_4 = ((_GameShell->soundFlags & World::SF_INVERTLR) == 0) + 1;

    _GameShell->video_button->SetState(&v16);


    v16.butID = 1150;
    v16.field_4 = ((_GameShell->GFXFlags & World::GFX_FLAG_16BITTEXTURE) == 0) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.field_4 = ((_GameShell->soundFlags & World::SF_CDSOUND) == 0) + 1;
    v16.butID = 1164;
    _GameShell->video_button->SetState(&v16);

    NC_STACK_button::Slider *tmp = _GameShell->video_button->GetSliderData(1152);
    tmp->value = _GameShell->soundVolume;

    _GameShell->video_button->Refresh(1152);

    tmp = _GameShell->video_button->GetSliderData(1154);
    tmp->value = _GameShell->musicVolume;

    _GameShell->video_button->Refresh(1154);

    v16.butID = 1163;
    v16.field_4 = (_GameShell->enemyIndicator == 0) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.butID = 1174;
    v16.field_4 = (!System::IniConf::GameRoboPlayerAIBehavior.Get<bool>()) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.butID = 1175;
    v16.field_4 = (!System::IniConf::GameSpectatorMode.Get<bool>()) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.butID = 1157;
    v16.field_4 = ((_GameShell->GFXFlags & World::GFX_FLAG_FARVIEW) == 0) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.field_4 = ((_GameShell->GFXFlags & World::GFX_FLAG_SKYRENDER) == 0) + 1;
    v16.butID = 1160;
    _GameShell->video_button->SetState(&v16);

    v16.butID = 1165;
    v16.field_4 = ((_GameShell->GFXFlags & World::GFX_FLAG_SOFTMOUSE) == 0) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.butID = 1166;
    v16.field_4 = (!_GameShell->IsWindowedFlag()) + 1;
    _GameShell->video_button->SetState(&v16);

    // OpenUA: modern graphics options initial state (read from config)
    _GameShell->confBlending = System::IniConf::GfxBlending.Get<int32_t>();
    _GameShell->confMaxFps = System::IniConf::GfxMaxFps.Get<int32_t>();
    _GameShell->confMoviePlayer = System::IniConf::GfxMoviePlayer.Get<bool>();
    _GameShell->confVhsFilter = System::IniConf::GfxVhsFilter.Get<bool>();
    _GameShell->confMenuFont = _GameShell->menuFont;
    _GameShell->cockpitCameraRuntimeMode = _GameShell->defaultCockpitCamera;
    _GameShell->confDefaultCockpitCamera = _GameShell->defaultCockpitCamera;

    v16.butID = 1184; // Intro Movies checkbox
    v16.field_4 = (!_GameShell->confMoviePlayer) + 1;
    _GameShell->video_button->SetState(&v16);

    v16.butID = 1185; // VHS Filter checkbox
    v16.field_4 = (!_GameShell->confVhsFilter) + 1;
    _GameShell->video_button->SetState(&v16);

    _GameShell->video_button->SetText(1156, _GameShell->p_YW->_gfxMode.name);
    _GameShell->UpdatePaletteThemeText();
    _GameShell->UpdateGfxOptionTexts();
    _GameShell->UpdateMenuFontText();

    tmp = _GameShell->video_button->GetSliderData(1159);
    tmp->value = _GameShell->fxnumber;

    _GameShell->video_button->Refresh(1159);

    NC_STACK_button::button_66arg v9;
    v9.butID = 1050;
    v9.field_4 = ((_GameShell->p_YW->_preferences & World::PREF_JOYDISABLE) != 0) + 1;

    _GameShell->button_input_button->SetState(&v9);

    v9.butID = 1061;
    v9.field_4 = (_GameShell->altJoystickEnabled == false) + 1;
    _GameShell->button_input_button->SetState(&v9);

    v9.butID = 1055;
    v9.field_4 = ((_GameShell->p_YW->_preferences & World::PREF_FFDISABLE) != 0) + 1;
    _GameShell->button_input_button->SetState(&v9);

    if ( _GameShell->inpListActiveElement )
    {
        int v7 = _GameShell->inpListActiveElement - 1;
        int v8 = _GameShell->input_listview.maxShownEntries + _GameShell->input_listview.firstShownEntries;

        if ( v7 >= _GameShell->input_listview.firstShownEntries && v7 < v8 )
        {
            if ( v8 > _GameShell->input_listview.numEntries )
                _GameShell->input_listview.firstShownEntries = _GameShell->input_listview.numEntries - _GameShell->input_listview.maxShownEntries;

            if ( _GameShell->input_listview.firstShownEntries < 0 )
                _GameShell->input_listview.firstShownEntries = 0;
        }
        else if ( _GameShell->input_listview.numEntries - v7 <= _GameShell->input_listview.maxShownEntries )
        {
            _GameShell->input_listview.firstShownEntries = _GameShell->input_listview.numEntries - _GameShell->input_listview.maxShownEntries;
        }
        else
        {
            _GameShell->input_listview.firstShownEntries = v7;
        }
    }
}


size_t NC_STACK_ypaworld::ypaworld_func168(NC_STACK_ypabact *bact)
{
    if ( bact->_bact_type == BACT_TYPES_GUN || bact->_bact_type == BACT_TYPES_MISSLE )
        return 1;

    if ( bact->_owner == _userRobo->_owner )
    {
        if ( bact->_pSector->PurposeType == cellArea::PT_GATEOPENED )
        {
            if ( _userRobo == bact )
            {
                _levelInfo.State = TLevelInfo::STATE_COMPLETED;
                _levelInfo.GateCompleteID = bact->_pSector->PurposeIndex;
            }
            else
            {
                _beamEnergyCurrent += (bact->_energy_max + 99) / 100;

                if ( _beamEnergyCurrent <= _beamEnergyCapacity )
                    _levelInfo.Buddies.push_back( TMapBuddy( bact->_commandID, bact->_vehicleID, bact->_energy ) );
                else
                    return 0;
            }
        }
    }
    return 1;
}

int NC_STACK_ypaworld::LoadingParseSaveFile(const std::string &filename)
{
    int lvlnum;
    ScriptParser::HandlersList parsers
    {
        new World::Parsers::UserParser(this),
        new World::Parsers::SaveRoboParser(this),
        new World::Parsers::SaveSquadParser(this), // commander and units
        new World::Parsers::SaveGemParser(this),
        new World::Parsers::VhclProtoParser(this),
        new World::Parsers::WeaponProtoParser(this),
        new World::Parsers::BuildProtoParser(this),
        new World::Parsers::SaveExtraViewParser(this),
        new World::Parsers::SaveKwFactorParser(this),
        new World::Parsers::SaveGlobalsParser(this),
        new World::Parsers::SaveOwnerMapParser(this),
        new World::Parsers::SaveBuildingMapParser(this),
        new World::Parsers::SaveEnergyMapParser(this),
        new World::Parsers::SaveLevelNumParser(this, &lvlnum),
        new World::Parsers::LevelStatusParser(this, true),
        new World::Parsers::SaveHistoryParser(this),
        new World::Parsers::SaveMasksParser(this),
        new World::Parsers::SaveSuperBombParser(this),
    };

    parsers.push_back( new World::Parsers::SaveLuaScriptParser(this) );

    return ScriptParser::ParseFile(filename, parsers, ScriptParser::FLAG_NO_SCOPE_SKIP);
}

void NC_STACK_ypaworld::LoadingUnitsRefresh()
{
    for ( NC_STACK_ypabact* &station : _unitsList )
    {
        RefreshUnitPRT(station, station, true);

        for ( NC_STACK_ypabact* &commander : station->_kidList )
        {
            RefreshUnitPRT(commander, station, false);

            for ( NC_STACK_ypabact* &slave : commander->_kidList )
                RefreshUnitPRT(slave, station, false);
        }
    }

    if ( _extraViewEnable )
    {
        NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(_userRobo);

        if ( robo->_roboGuns.at(_extraViewNumber).gun_obj )
        {
            robo->_roboGuns.at(_extraViewNumber).gun_obj->setBACT_viewer(true);
            robo->_roboGuns.at(_extraViewNumber).gun_obj->setBACT_inputting(true);
        }
    }
}

size_t NC_STACK_ypaworld::LoadGame(const std::string &saveFile)
{
    bool loadOK = false;

    if ( !uaFileExist(saveFile) )
        return 1;

    if ( saveFile.find(".sgm") != std::string::npos || saveFile.find(".SGM") != std::string::npos )
    {
        _maxReloadConst = 0;
        _maxRoboEnergy = 0;
    }

    int lvlnum;

    ScriptParser::HandlersList parsers
    {
        new World::Parsers::SaveLevelNumParser(this, &lvlnum),
    };

    ScriptParser::ParseFile(saveFile, parsers, 0);

    _extraViewNumber = -1;
    _extraViewEnable = false;

    TLevelDescription mapp;

    _particles.Clear();

    if ( LevelCommonLoader(&mapp, lvlnum, 0) )
    {
        if ( LoadTypeMap(mapp.TypStr) )
        {
            if ( LoadOwnerMap(mapp.OwnStr) )
            {
                if ( LoadHightMap(mapp.HgtStr) )
                {
                    if ( LoadBlgMap(mapp.BlgStr) )
                        loadOK = true;
                }
            }
        }
    }

    if ( !loadOK )
    {
        DeleteLevel();
        return 0;
    }

    _levelInfo.OwnerMask = 0;
    _levelInfo.UserMask = 0;

    bact_id = 0x10000;
    dword_5A7A80 = 0;

    InitSuperItems();

    _lvlPrimevalTypeMap = _lvlTypeMap;
    _lvlPrimevalOwnMap = _lvlOwnMap;

    if ( !LoadingParseSaveFile(saveFile) )
        return 0;

    dword_5A7A80++;
    bact_id++;

    if ( _userRobo )
        dynamic_cast<NC_STACK_yparobo *>(_userRobo) ->setROBO_commCount(dword_5A7A80);

    LoadingUnitsRefresh();

    if ( saveFile.find(".fin") != std::string::npos || saveFile.find(".FIN") != std::string::npos )
        InitBuddies();

    for(int y = 0; y < _mapSize.y; y++)
    {
        for(int x = 0; x < _mapSize.x; x++)
        {
            CellCheckHealth(&_cells(x, y), World::OWNER_NOCHANGE, NULL);
        }
    }

    InitGates();
    UpdatePowerEnergy();

    PrepareAllFillers();

    if ( !sb_0x451034(this) )
        return 0;

    return 1;
}


size_t NC_STACK_ypaworld::SaveGame(const std::string &saveFile)
{
    bool write_ok = true;

    if ( saveFile.find(".sgm") != std::string::npos || saveFile.find(".SGM") != std::string::npos )
        uaDeleteFile( fmt::sprintf("save:%s/sgisold.txt", _GameShell->UserName) );

    int write_modifers;
    int write_user;
    int write_level_statuses;

    bool isfin_save = saveFile.find(".fin") != std::string::npos || saveFile.find(".FIN") != std::string::npos;

    if ( isfin_save )
    {
        _maxRoboEnergy = _userRobo->_energy_max;
        write_modifers = 0;
        write_user = 0;
        write_level_statuses = 0;
        _maxReloadConst = _userRobo->_reload_const;
    }
    else
    {
        write_level_statuses = 1;
        write_modifers = 1;
        write_user = 1;
    }

    // Force to write last frame timestamp into history
    if (_historyLastIsTimeStamp)
        _history.Write( _historyLastFrame.MakeByteArray() );

    FSMgr::FileHandle *fil = uaOpenFileAlloc( saveFile, "w");

    if ( !fil )
    {
        ypa_log_out("Unable to open savegame file\n");
        return 0;
    }

    if ( write_modifers )
    {
        if ( !yw_write_item_modifers(this, fil) )
            write_ok = false;
    }

    if ( write_user )
    {
        if ( !yw_write_user(fil, _GameShell) )
            write_ok = false;
    }

    if ( write_ok )
    {
        if ( yw_write_levelnum(this, fil) )
        {
            yw_write_ownermap(this, fil);
            yw_write_buildmap(this, fil);
            yw_write_energymap(this, fil);

            if ( yw_write_units(fil) )
            {
                if ( yw_write_wunderinfo(this, fil) )
                {
                    if ( yw_write_kwfactor(this, fil) )
                    {
                        if ( yw_write_globals(this, fil) )
                        {
                            if ( yw_write_superbomb(this, fil) )
                                write_ok = true;
                        }
                    }
                }
            }
        }
    }

    if ( write_ok && write_level_statuses )
    {
        for (int i = 0; i < 256; i++)
        {
            if ( _globalMapRegions.MapRegions[i].Status != TMapRegionInfo::STATUS_NONE )
            {
                if ( !yw_write_level_status(fil, this, i) )
                {
                    write_ok = false;
                    break;
                }
            }
        }
    }

    if ( write_ok )
    {
        if ( !isfin_save )
        {
            yw_write_history(this, fil);
            yw_write_masks(this, fil);
        }
    }

    if (_script)
    {
        std::string buf = _script->GetSaveString();

        if (!buf.empty())
        {
            fil->printf("\nbegin_luascript\n");
            fil->printf("%s\n", buf.c_str());
            fil->printf("EOF\n");
            fil->printf("end\n");
        }
    }

    delete fil;
    return write_ok;
}


// Saving game
bool NC_STACK_ypaworld::SaveSettings(UserData *usr, const std::string &fileName, uint32_t sdfMask)
{
    if ( fileName.empty() )
    {
        ypa_log_out("No names for save action\n");
        return false;
    }

    FSMgr::FileHandle *sfil = uaOpenFileAlloc(fmt::sprintf("save:%s", fileName), "w");

    if ( !sfil )
        return false;

    if ( (sdfMask & World::SDF_USER) && !yw_write_user(sfil, usr) )
    {
        ypa_log_out("Unable to write user data to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_INPUT) && !yw_write_input(sfil, usr) )
    {
        ypa_log_out("Unable to write input data to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_SOUND) && !yw_write_sound(sfil, usr) )
    {
        ypa_log_out("Unable to write sound data to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_VIDEO) && !yw_write_video(sfil, usr) )
    {
        ypa_log_out("Unable to write video data to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_SCORE) && !yw_write_levels_statuses(sfil, usr->p_YW) )
    {
        ypa_log_out("Unable to write score data to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_BUDDY) && !yw_write_buddies(sfil, usr->p_YW) )
    {
        ypa_log_out("Unable to write buddies to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_SHELL) && !yw_write_shell(sfil, usr) )
    {
        ypa_log_out("Unable to write shell data to file\n");
        delete sfil;
        return false;
    }

    if ( (sdfMask & World::SDF_PROTO) && !yw_write_item_modifers(usr->p_YW, sfil) )
    {
        ypa_log_out("Unable to write build info to file\n");
        delete sfil;
        return false;
    }

    delete sfil;
    return true;
}



int NC_STACK_ypaworld::ParseSettingsFile(const std::string &fname, uint32_t sdfMask)
{
    ScriptParser::HandlersList parsers;
    if ( sdfMask & World::SDF_USER )
        parsers += new World::Parsers::UserParser(this);

    if ( sdfMask & World::SDF_INPUT )
        parsers += new World::Parsers::InputParser(this);

    if ( sdfMask & World::SDF_VIDEO )
        parsers += new World::Parsers::VideoParser(this);

    if ( sdfMask & World::SDF_SOUND )
        parsers += new World::Parsers::SoundParser(this);

    if ( sdfMask & World::SDF_SCORE )
        parsers += new World::Parsers::LevelStatusParser(this, true);

    if ( sdfMask & World::SDF_BUDDY )
        parsers += new World::Parsers::BuddyParser(this);

    if ( sdfMask & World::SDF_SHELL )
        parsers += new World::Parsers::ShellParser(this);

    if ( sdfMask & World::SDF_PROTO )
    {
        parsers += new World::Parsers::VhclProtoParser(this);
        parsers += new World::Parsers::WeaponProtoParser(this);
        parsers += new World::Parsers::BuildProtoParser(this);
    }

    return ScriptParser::ParseFile(fname, parsers, 0);
}

// Load user settings (Global save)
size_t NC_STACK_ypaworld::LoadSettings(const std::string &fileName, const std::string &userName, uint32_t sdfMask, bool updateGameShell, bool playIntro)
{
    if ( sdfMask & World::SDF_SCORE )
    {
        if ( _GameShell->IgnoreScoreSaving )
        {
            _GameShell->IgnoreScoreSaving = false;
        }
        else
        {
            SaveSettings(_GameShell, fmt::sprintf("%s/user.txt", _GameShell->UserName), World::SDF_ALL);
        }
    }

    _GameShell->savedDataFlags = 0;

    if ( sdfMask & World::SDF_BUDDY )
        _levelInfo.Buddies.clear();
    if ( !ParseSettingsFile(fmt::sprintf("save:%s", fileName), sdfMask) )
    {
        ypa_log_out("Error while loading information from %s\n", fileName.c_str());
        return 0;
    }

    if (playIntro && !_GameShell->remoteMode)
    {
        SetGameShellVideoMode( _GameShell->IsWindowedFlag() );
        PlayIntroMovie();
    }

    if ( updateGameShell && !_GameShell->HasInited && !OpenGameShell() ) // Init menus
    {
        ypa_log_out("Unable to open GameShell\n");
        return 0;
    }


    if ( (sdfMask & World::SDF_SCORE) && (_GameShell->savedDataFlags & World::SDF_SCORE) )
    {
        _GameShell->UserName = userName;
    }

    if ( sdfMask & World::SDF_INPUT )
        _GameShell->InputConfCopyToBackup();

    if ( updateGameShell )
        UpdateGameShell(); // Update menu values

    return 1;
}


bool NC_STACK_ypaworld::ReloadInput(size_t id)
{
    std::string keyConfStr;

    if ( id < 1 || id >= _GameShell->InputConfig.size() )
        return false;

    UserData::TInputConf &kconf = _GameShell->InputConfig.at(id);

    if ( Input::Engine.KeyNamesTable.at(kconf.PKeyCode).Name.empty() )
        return false;

    if ( kconf.Type == World::INPUT_BIND_TYPE_SLIDER && Input::Engine.KeyNamesTable.at(kconf.NKeyCode).Name.empty() )
        return false;

    if ( kconf.Type == World::INPUT_BIND_TYPE_SLIDER )
    {
        keyConfStr += "~#";
        keyConfStr += "winp:";
        keyConfStr += Input::Engine.KeyNamesTable.at(kconf.NKeyCode).Name;
        keyConfStr += " #";
        keyConfStr += "winp:";
    }
    else if ( kconf.Type == World::INPUT_BIND_TYPE_BUTTON )
    {
        keyConfStr += "winp:";
    }

    if ( Input::Engine.KeyNamesTable.at(kconf.PKeyCode).Name.empty() )
        return false;

    keyConfStr += Input::Engine.KeyNamesTable.at(kconf.PKeyCode).Name;

    if ( kconf.Type == World::INPUT_BIND_TYPE_HOTKEY )
    {
        if ( !Input::Engine.SetHotKey(kconf.KeyID, keyConfStr) )
            ypa_log_out("input.engine: WARNING: Hotkey[%d] (%s) not accepted.\n", kconf.KeyID, keyConfStr.c_str());
    }
    else
    {
        if ( kconf.Type == World::INPUT_BIND_TYPE_BUTTON )
        {
            if ( !Input::Engine.SetInputExpression(false, kconf.KeyID, keyConfStr) )
                ypa_log_out("input.engine: WARNING: Button[%d] (%s) not accepted.\n", kconf.KeyID, keyConfStr.c_str());
        }
        else
        {
            if ( !Input::Engine.SetInputExpression(true, kconf.KeyID, keyConfStr) )
                ypa_log_out("input.engine: WARNING: Slider[%d] (%s) not accepted.\n", kconf.KeyID, keyConfStr.c_str());
        }
    }
    kconf.NKeyCodeBkp = kconf.NKeyCode;
    kconf.PKeyCodeBkp = kconf.PKeyCode;
    return true;
}


size_t NC_STACK_ypaworld::SetGameShellVideoMode(bool windowed)
{
    UserData *usr = _GameShell;

    if (System::IniConf::MenuWindowed.Get<bool>())
        windowed = true;

    if ( _shellGfxMode == GFX::Engine.GetGfxMode() && windowed == GFX::Engine.GetGfxMode().windowed )
        return 1;

    int v6;

    if ( usr->HasInited )
    {
        CloseGameShell();
        v6 = 1;
    }
    else
    {
        v6 = 0;
    }

    GFX::Engine.SetResolution( _shellGfxMode, windowed );

    _screenSize = GFX::Engine.GetScreenSize();

    if ( v6 && !OpenGameShell())
    {
        ypa_log_out("Error: Unable to open GameShell with mode %d x %d\n", _shellGfxMode.x, _shellGfxMode.y);

        return 0;
    }

    if ( usr->GFXFlags & World::GFX_FLAG_SOFTMOUSE )
    {
        GFX::Engine.setWDD_cursor(1);
    }
    else
    {
        GFX::Engine.setWDD_cursor(0);
    }

    std::string fontStr = _screenSize.x >= 512 ? Locale::Text::Font() : Locale::Text::SmallFont();
    fontStr = System::ResolveMenuFontDescr(fontStr);
    GFX::Engine.LoadFontByDescr(fontStr);
    Gui::UA::LoadFont(fontStr);

    return 1;
}


size_t NC_STACK_ypaworld::ReloadLanguage()
{
    if ( !_GameShell->default_lang_dll )
    {
        ypa_log_out("Set Language, but no language selected\n");
        return 0;
    }

    int v6;

    if ( _GameShell->HasInited )
    {
        CloseGameShell();
        v6 = 1;
    }
    else
    {
        v6 = 0;
    }

    if ( !ypaworld_func166(*_GameShell->default_lang_dll) )
        ypa_log_out("Warning: SETLANGUAGE failed\n");

    if ( v6 && !OpenGameShell() )
    {
        ypa_log_out("Unable to open GameShell\n");
        return 0;
    }

    return 1;
}


void NC_STACK_ypaworld::ypaworld_func176(yw_arg176 *arg)
{
    arg->field_4 = _reloadRatioClamped[arg->owner];
    arg->field_8 = _reloadRatioPositive[arg->owner];
}


void NC_STACK_ypaworld::ypaworld_func177(yw_arg177 *arg)
{
    //Reown sectors for new owner

    if ( !arg->field_4 ) //New owner
        return;

    if ( arg->bact == _userRobo )
        _playerHSDestroyed = true;

    if ( _userRobo )
    {
        if ( _userRobo->_owner == arg->field_4 )
            _beamEnergyCapacity += _beamEnergyAdd;
    }

    for ( NC_STACK_ypabact* &unit : _unitsList )
    {
        if ( unit->_bact_type == BACT_TYPES_ROBO && unit != arg->bact && arg->bact->_owner == unit->_owner )
            return;
    }

    for (int i = 0; i < _mapSize.y; i++)
    {
        for (int j = 0; j < _mapSize.x; j++)
        {
            cellArea &v9 = _cells(j, i);

            if ( v9.owner == arg->bact->_owner )
                CellSetOwner(&v9, arg->field_4);
        }
    }

    if ( !_userRobo )
        return;

    if ( _userRobo->_owner != arg->field_4 )
        return;

    for (int i = 0; i < _mapSize.y; i++)
    {
        for (int j = 0; j < _mapSize.x; j++)
        {
            cellArea &v15 = _cells(j, i);

            if ( v15.PurposeType == cellArea::PT_TECHUPGRADE && v15.owner == _userRobo->_owner )
            {

                if ( _isNetGame )
                    sub_47C29C(this, &v15, v15.PurposeIndex);
                else
                    yw_ActivateWunderstein(&v15, v15.PurposeIndex);

                HistoryEventAdd( World::History::Upgrade(j, i, v15.owner, _techUpgrades[ _upgradeId ].Type, _upgradeVehicleId, _upgradeWeaponId, _upgradeBuildId) );
            }

        }
    }
}

//179 method in yw_net


void NC_STACK_ypaworld::ypaworld_func180(yw_arg180 *arg)
{
    if ( _shellConfIsParsed )
    {
        if ( _preferences & (World::PREF_JOYDISABLE | World::PREF_FFDISABLE) )
            return;
    }

    switch ( arg->effects_type )
    {
    case 0:
        Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_MISSILEFIRE);
        break;

    case 1:
        Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_GRENADEFIRE);
        break;

    case 2:
        Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_BOMBFIRE);
        break;

    case 3:
        Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_MINIGUN);
        break;

    case 4:
        Input::Engine.ForceFeedback(Input::FF_STATE_STOP, Input::FF_TYPE_MINIGUN);
        break;

    case 5:
    {
        NC_STACK_ypabact *bct = _userUnit;
        Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_COLLISION,
            arg->field_4, 0.0,
            (arg->field_C - bct->_position.z) * bct->_rotation.m02 + (arg->field_8 - bct->_position.x) * bct->_rotation.m00,
            -((arg->field_8 - bct->_position.x) * bct->_rotation.m20 + (arg->field_C - bct->_position.z) * bct->_rotation.m22));

    }
    break;

    default:
        break;
    }
}


bool NC_STACK_ypaworld::NetSendMessage(uamessage_base *data, size_t dataSize, const std::string &recvID, bool garantee)
{
    if (_GameShell->noSent)
        return false;

    return _netDriver->Send(data, dataSize, recvID, garantee);
}

bool NC_STACK_ypaworld::NetBroadcastMessage(uamessage_base *data, size_t dataSize, bool garantee)
{
    if (_GameShell->noSent)
        return false;

    if ( _GameShell->netPlayerOwner != 0 && data->msgID != UAMSG_VHCLENERGY )
        _GameShell->msgcount++;

    return _netDriver->Broadcast(data, dataSize, garantee);
}

int ypaworld_func183__sub0(int lvlID, const char *userName)
{
    FSMgr::FileHandle *fil = uaOpenFileAlloc( fmt::sprintf("save:%s/%d.fin", userName, lvlID) , "r");

    if ( !fil )
        return 0;

    delete fil;
    return 1;
}

// Advanced Create Level
size_t NC_STACK_ypaworld::ypaworld_func183(yw_arg161 *arg)
{
    int v6;

    if ( _globalMapRegions.MapRegions[ arg->lvlID ].Status == TMapRegionInfo::STATUS_COMPLETED && ypaworld_func183__sub0(arg->lvlID, _GameShell->UserName.c_str()) )
    {
        std::string savename = fmt::sprintf("save:%s/%d.fin", _GameShell->UserName, arg->lvlID);
        v6 = LoadGame(savename);

        if ( !v6 )
            ypa_log_out("Warning: in YWM_ADVANCEDCREATELEVEL: YWM_LOADGAME of %s failed!\n", savename.c_str());

        _userRobo->_energy = _userRobo->_energy_max;
    }
    else
    {
        v6 = ypaworld_func161(arg);

        if ( !v6 )
            ypa_log_out("Warning: in YWM_ADVANCEDCREATELEVEL: YWM_CREATELEVEL %d failed!\n", arg->lvlID);
    }

    if ( v6 )
    {
        if ( !SaveGame(fmt::sprintf("save:%s/%d.rst", _GameShell->UserName, _levelInfo.LevelID)) )
            ypa_log_out("Warning: could not create restart file for level %d, user %s.\n", _levelInfo.LevelID, _GameShell->UserName.c_str());
    }

    _lvlPrimevalTypeMap = _lvlTypeMap;
    _lvlPrimevalOwnMap = _lvlOwnMap;

    return v6;
}


void NC_STACK_ypaworld::HistoryEventAdd(const World::History::Record &arg)
{
    switch ( arg.type )
    {
    case World::History::TYPE_FRAME: // Do not write timestamp every frame, wait for any another data
        _historyLastIsTimeStamp = true;
        _historyLastFrame = static_cast<const World::History::Frame&>(arg);
        break;

    case World::History::TYPE_CONQ:
    case World::History::TYPE_VHCLKILL:
    case World::History::TYPE_VHCLCREATE:
    case World::History::TYPE_POWERST:
    case World::History::TYPE_UPGRADE:

        if (_historyLastIsTimeStamp) // If
            _history.Write(_historyLastFrame.MakeByteArray());

        _history.Write(arg.MakeByteArray());

        _historyLastIsTimeStamp = false;

        if (_GameShell && _GameShell->isHost )
            arg.AddScore(&_gameplayStats);
        break;

    default:
        break;
    }


}


void NC_STACK_ypaworld::ypaworld_func185(const void *arg)
{
    dprintf("MAKE ME %s\n","ypaworld_func185");
}


void NC_STACK_ypaworld::setYW_normVisLimit(int limit)
{
    _normalVizLimit = limit;
}

void NC_STACK_ypaworld::setYW_fadeLength(int len)
{
    _normalFadeLength = len;
}

void NC_STACK_ypaworld::setYW_skyVisLimit(int limit)
{
    _skyVizLimit = limit;
}

void NC_STACK_ypaworld::setYW_skyFadeLength(int len)
{
    _skyFadeLength = len;
}

void NC_STACK_ypaworld::setYW_skyHeight(int hght)
{
    _skyHeight = hght;
}

void NC_STACK_ypaworld::setYW_skyRender(int dorender)
{
    _skyRender = dorender;
}

void NC_STACK_ypaworld::setYW_doEnergyRecalc(int doRecalc)
{
    _doEnergyRecalc = doRecalc;
}

void NC_STACK_ypaworld::setYW_visSectors(int visSectors)
{
    _renderSectors = visSectors;
}

void NC_STACK_ypaworld::setYW_userHostStation(NC_STACK_ypabact *host)
{
    _userRobo = host;
    _playerOwner = host->_owner;
}

void NC_STACK_ypaworld::setYW_userVehicle(NC_STACK_ypabact *bact)
{
    if ( !CanControlUnitInSpectatorMode(bact) )
        return;

    // OpenUA custom: never make a mortar platform the player-controlled vehicle;
    // mortars are commanded only from the 2D strategic map.
    if ( bact && bact->IsMortarPlatform() )
        return;

    if ( bact != _userUnit )
    {
        NC_STACK_ypabact *oldpBact = _userUnit;

        if ( oldpBact )
            _prevUnitId = oldpBact->_gid;

        _userUnit = bact;

        _vehicleTakenControlTimestamp = _timeStamp;
        _vehicleTakenCommandId = _userUnit->_commandID;
        _guiDragDefaultMouse = false;

        if ( _userUnit->_bact_type == BACT_TYPES_ROBO )
        {
            _joyIgnoreY = 1;
            _joyIgnoreX = 1;
        }

        FFeedback_VehicleChanged();

        if ( oldpBact )
            ypaworld_func2__sub0__sub1(this, oldpBact, _userUnit);
    }
}

void NC_STACK_ypaworld::setYW_screenW(int w)
{
    _screenSize.x = w;
}

void NC_STACK_ypaworld::setYW_screenH(int h)
{
    _screenSize.y = h;
}

void NC_STACK_ypaworld::setYW_dontRender(bool drndr)
{
    _doNotRender = drndr;
}


int NC_STACK_ypaworld::getYW_mapSizeX()
{
    return _mapSize.x;
}

int NC_STACK_ypaworld::getYW_mapSizeY()
{
    return _mapSize.y;
}

Common::Point NC_STACK_ypaworld::GetMapSize()
{
    return _mapSize;
}

int NC_STACK_ypaworld::getYW_normVisLimit()
{
    return _normalVizLimit;
}

int NC_STACK_ypaworld::getYW_fadeLength()
{
    return _normalFadeLength;
}

int NC_STACK_ypaworld::getYW_skyHeight()
{
    return _skyHeight;
}

int NC_STACK_ypaworld::getYW_skyRender()
{
    return _skyRender;
}

int NC_STACK_ypaworld::getYW_doEnergyRecalc()
{
    return _doEnergyRecalc;
}

int NC_STACK_ypaworld::getYW_visSectors()
{
    return _renderSectors;
}

NC_STACK_ypabact *NC_STACK_ypaworld::getYW_userHostStation()
{
    return _userRobo;
}

NC_STACK_ypabact *NC_STACK_ypaworld::getYW_userVehicle()
{
    return _userUnit;
}


int NC_STACK_ypaworld::getYW_lvlFinished()
{
    if ( _levelInfo.State != TLevelInfo::STATE_COMPLETED && _levelInfo.State != TLevelInfo::STATE_ABORTED )
        return 0;

    return 1;
}

int NC_STACK_ypaworld::getYW_screenW()
{
    return _screenSize.x;
}

int NC_STACK_ypaworld::getYW_screenH()
{
    return _screenSize.y;
}

TLevelInfo &NC_STACK_ypaworld::GetLevelInfo()
{
    return _levelInfo;
}

int NC_STACK_ypaworld::getYW_destroyFX()
{
    return _fxLimit;
}

NC_STACK_windp *NC_STACK_ypaworld::getYW_pNET()
{
    return _netDriver;
}

int NC_STACK_ypaworld::getYW_invulnerable()
{
    return _invulnerable;
}



int NC_STACK_ypaworld::TestVehicle(int protoID, int job)
{
    const World::TVhclProto &proto = _vhclProtos[ protoID ];

    if ( proto.is_mimic )
    {
        switch ( job )
        {
        case 1:
            return proto.job_fightrobo;

        case 2:
            return proto.job_fighttank;

        case 4:
            return proto.job_fighthelicopter;

        case 3:
            return proto.job_fightflyer;

        case 5:
            return proto.job_reconnoitre;

        case 6:
            return proto.job_conquer;

        default:
            return -1;
        }
    }

    World::TWeapProto *wpn;

    if ( proto.weapon == -1 )
        wpn = NULL;
    else
        wpn = &_weaponProtos.at( proto.weapon );

    switch ( job )
    {
    case 1:
        if ( (((proto.mgun_set && proto.mgun == -1) || (!proto.mgun_set && proto.mgun_shot_time <= 0)) && !wpn) || proto.model_id == BACT_TYPES_UFO )
            return -1;

        return proto.job_fightrobo;
        break;

    case 2:
        if ( (((proto.mgun_set && proto.mgun == -1) || (!proto.mgun_set && proto.mgun_shot_time <= 0)) && !wpn) || proto.model_id == BACT_TYPES_UFO )
            return -1;

        return proto.job_fighttank;
        break;

    case 4:
        if ( (((proto.mgun_set && proto.mgun == -1) || (!proto.mgun_set && proto.mgun_shot_time <= 0)) && !wpn) || proto.model_id == BACT_TYPES_UFO )
            return -1;

        return proto.job_fighthelicopter;
        break;

    case 3:
        if ( (((proto.mgun_set && proto.mgun == -1) || (!proto.mgun_set && proto.mgun_shot_time <= 0)) && !wpn) || proto.model_id == BACT_TYPES_UFO )
            return -1;

        return proto.job_fightflyer;
        break;

    case 5:
        return proto.job_reconnoitre;
        break;

    case 6:
        if ( !wpn || proto.model_id == BACT_TYPES_UFO )
            return -1;

        return proto.job_conquer;
        break;

    default:
        break;
    }
    return -1;
}


void NC_STACK_ypaworld::UpdateGuiSettings()
{
    Gui::UA::_UATextColor = _iniColors[60];
    Gui::UA::_UAButtonTextColor = _iniColors[68];



    /*for (uint8_t i = 0; i < ypaworld.tiles.size(); i++)
        Gui::UA::_UATiles[i] = ypaworld.tiles[i];*/
}

void NC_STACK_ypaworld::LoadGuiFonts()
{
    std::string old = Common::Env.SetPrefix("rsrc", "data:set46");

    Gui::UA::_UATiles[Gui::UA::TILESET_46DEFAULT] = yw_LoadFont("default.font"); //0
    Gui::UA::_UATiles[Gui::UA::TILESET_46MAPC16] = yw_LoadFont("mapcur16.font"); //18
    Gui::UA::_UATiles[Gui::UA::TILESET_46MAPC32] = yw_LoadFont("mapcur32.font"); //19
    Gui::UA::_UATiles[Gui::UA::TILESET_46ENERGY] = yw_LoadFont("energy.font"); //30

    Common::Env.SetPrefix("rsrc", "data:fonts");
    Gui::UA::_UATiles[Gui::UA::TILESET_DEFAULT]     = yw_LoadFont("default.font"); //0
    Gui::UA::_UATiles[Gui::UA::TILESET_MENUGRAY]    = yw_LoadFont("menugray.font"); //6
    Gui::UA::_UATiles[Gui::UA::TILESET_ICONNS]      = yw_LoadFont("icon_ns.font"); //24
    Gui::UA::_UATiles[Gui::UA::TILESET_ICONPS]      = yw_LoadFont("icon_ps.font"); //25
    Gui::UA::_UATiles[Gui::UA::TILESET_MAPHORZ]     = yw_LoadFont("maphorz.font"); //11
    Gui::UA::_UATiles[Gui::UA::TILESET_MAPVERT]     = yw_LoadFont("mapvert.font"); //12
    Gui::UA::_UATiles[Gui::UA::TILESET_MAPVERT1]    = yw_LoadFont("mapvert1.font"); //13

    Common::Env.SetPrefix("rsrc", old);
}

void NC_STACK_ypaworld::CreateNewGuiElements()
{
    _GameShell->_menuMsgBox = new Gui::UABlockMsgBox(Gui::UAMessageBox::TYPE_INMENU);
    _GameShell->_menuMsgBox->SetEnable(false);
    Gui::Root::Instance.AddWidget(_GameShell->_menuMsgBox);
}

void NC_STACK_ypaworld::DeleteNewGuiElements()
{
    delete _GameShell->_menuMsgBox;
    _GameShell->_menuMsgBox = NULL;
}

SDL_Color NC_STACK_ypaworld::GetColor(int colorID)
{
    return _iniColors.at(colorID);
}



cellArea *NC_STACK_ypaworld::GetSector(int32_t x, int32_t y)
{
    if (_cells.empty())
        return NULL;

    if (x >= 0 && x < _mapSize.x
    &&  y >= 0 && y < _mapSize.y)
        return &_cells(x, y);
    return NULL;
}

cellArea *NC_STACK_ypaworld::GetSector(const Common::Point &sec)
{
    if (_cells.empty())
        return NULL;

    if (sec.x >= 0 && sec.x < _mapSize.x
    &&  sec.y >= 0 && sec.y < _mapSize.y)
        return &_cells(sec.x, sec.y);
    return NULL;
}

cellArea *NC_STACK_ypaworld::GetSector(size_t id)
{
    if (_cells.empty())
        return NULL;

    if (id >= 0 && id < _cells.size())
        return &_cells.At(id);
    return NULL;
}

cellArea &NC_STACK_ypaworld::SectorAt(int32_t x, int32_t y)
{
    return _cells(x, y);
}

cellArea &NC_STACK_ypaworld::SectorAt(const Common::Point &sec)
{
    return _cells.At(sec);
}

cellArea &NC_STACK_ypaworld::SectorAt(size_t id)
{
    return _cells.At(id);
}

void NC_STACK_ypaworld::SetMapSize(const Common::Point &sz)
{
    _mapSize = sz;

    _mapLength = vec2d( _mapSize.x * World::CVSectorLength, _mapSize.y * World::CVSectorLength );

    _cells.Clear();
    _cells.Resize(sz);

    int32_t id = 0;
    for (int y = 0; y < sz.y; y++)
    {
        for (int x = 0; x < sz.x; x++)
        {
            cellArea &cell = _cells(x, y);
            cell.Id = id;
            cell.CellId = Common::Point(x, y);

            if (x == 0 || y == 0 || x == sz.x - 1 || y == sz.y - 1)
                cell.Flags |= cellArea::CF_BORDER;

            id++;
        }
    }

    _cellsVFCache.Clear();
    _cellsVFCache.Resize(sz.x, sz.y);

    _cellsHFCache.Clear();
    _cellsHFCache.Resize(sz.x, sz.y);

    _energyAccumMap.Clear();
    _energyAccumMap.Resize(sz);
}

bool NC_STACK_ypaworld::IsGamePlaySector(const Common::Point &sz) const
{
    return sz.x > 0 && sz.x < (_mapSize.x - 1) && sz.y > 0 && sz.y < (_mapSize.y - 1);
}

bool NC_STACK_ypaworld::IsSectorBorder(const Common::Point &sz, int border) const
{
    return sz.x < border || sz.x >= (_mapSize.x - border) || sz.y < border || sz.y >= (_mapSize.y - border);
}

bool NC_STACK_ypaworld::IsSector(const Common::Point &sz) const
{
    return sz.x >= 0 && sz.x < _mapSize.x  && sz.y >= 0 && sz.y < _mapSize.y;
}

Common::PlaneVector<cellArea> &NC_STACK_ypaworld::Sectors()
{
    return _cells;
}

int32_t NC_STACK_ypaworld::GetLegoBld(const cellArea *cell, int bldX, int bldY)
{
    TSubSectorDesc *ssec = _secTypeArray[ cell->type_id ].SubSectors.At(bldX, bldY);
    int32_t hlth = cell->buildings_health.Get(bldX, bldY);
    return ssec->HPModels[ _buildHealthModelId[ hlth ] ];
}

int32_t NC_STACK_ypaworld::GetLegoBld(const Common::Point &cell, int bldX, int bldY)
{
    return GetLegoBld(&SectorAt(cell), bldX, bldY);
}

TCellFillerCh::~TCellFillerCh()
{
    FreeVBO();
}

void TCellFillerCh::FreeVBO()
{
    for (GFX::TMesh &msh : Meshes)
        GFX::Engine.MeshFreeVBO( &msh );
}

void TCellFillerCh::MakeVBO()
{
    for (GFX::TMesh &msh : Meshes)
        GFX::Engine.MeshMakeVBO( &msh );
}
