#include <algorithm>
#include <cmath>

#include "includes.h"
#include "yw.h"
#include "loaders.h"

namespace
{

static bool yw_ImpactScarCellIsValid(const NC_STACK_ypaworld *world, const Common::Point &cell)
{
    return cell.x >= 0 && cell.x < world->_mapSize.x &&
           cell.y >= 0 && cell.y < world->_mapSize.y;
}

static bool yw_ImpactScarHitNormal(const ypaworld_arg136 &hit, const vec3d &preferred, vec3d *out)
{
    if ( !hit.isect || !hit.skel || hit.polyID < 0 ||
         (size_t)hit.polyID >= hit.skel->polygons.size() )
        return false;

    vec3d normal = hit.skel->polygons[hit.polyID].Normal();
    if ( normal.normalise() <= 0.001 )
        return false;

    vec3d preferredNormal = preferred;
    if ( preferredNormal.normalise() > 0.001 && normal.dot(preferredNormal) < 0.0 )
        normal = -normal;

    *out = normal;
    return true;
}

static void yw_ImpactScarBasis(const vec3d &normal, vec3d *tangent, vec3d *bitangent)
{
    vec3d ref = fabs(normal.y) < 0.9 ? vec3d(0.0, 1.0, 0.0) : vec3d(1.0, 0.0, 0.0);
    vec3d t = ref * normal;
    if ( t.normalise() <= 0.001 )
        t = vec3d(1.0, 0.0, 0.0);

    vec3d b = normal * t;
    if ( b.normalise() <= 0.001 )
        b = vec3d(0.0, 0.0, 1.0);

    *tangent = t;
    *bitangent = b;
}

static bool yw_ImpactScarIsTerrain(NC_STACK_ypaworld *world,
                                   const ypaworld_arg136 &hit,
                                   const vec3d &normal)
{
    if ( !yw_ImpactScarCellIsValid(world, hit.hitCell) )
        return false;

    const cellArea &cell = world->_cells(hit.hitCell);
    if ( cell.PurposeType != cellArea::PT_NONE )
        return false;

    if ( hit.hitCollisionType != 1 )
        return true;

    // Ordinary terrain and buildings can share the legacy lego collision type.
    // A near-height, non-vertical face is terrain; raised roofs and walls retain
    // their strict lego-slot identity and are clipped as building surfaces.
    return fabs(hit.isectPos.y - cell.height) <= 300.0f && fabs(normal.y) >= 0.25f;
}

static bool yw_ImpactScarSameSurface(const ypaworld_arg136 &reference,
                                     const ypaworld_arg136 &candidate,
                                     bool terrain)
{
    if ( terrain )
        return candidate.hitCollisionType != 0;

    if ( candidate.hitCell != reference.hitCell ||
         candidate.hitCollisionType != reference.hitCollisionType )
        return false;

    if ( reference.hitCollisionType == 1 )
    {
        return candidate.hitBldX == reference.hitBldX &&
               candidate.hitBldY == reference.hitBldY &&
               candidate.hitModelID == reference.hitModelID;
    }

    return candidate.hitMicroX == reference.hitMicroX &&
           candidate.hitMicroZ == reference.hitMicroZ;
}

static bool yw_ImpactScarTraceCell(NC_STACK_ypaworld *world,
                                   const ypaworld_arg136 &reference,
                                   bool terrain,
                                   const vec3d &planePoint,
                                   const vec3d &impactNormal,
                                   float traceHalfLength,
                                   ypaworld_arg136 *out,
                                   vec3d *outNormal)
{
    ypaworld_arg136 ray;
    ray.stPos = planePoint + impactNormal * traceHalfLength;
    ray.vect = impactNormal * (-2.0f * traceHalfLength);
    ray.flags = 0;
    world->ypaworld_func136(&ray);

    if ( !ray.isect || !yw_ImpactScarCellIsValid(world, ray.hitCell) )
        return false;

    vec3d normal;
    if ( !yw_ImpactScarHitNormal(ray, impactNormal, &normal) || normal.dot(impactNormal) < 0.82f )
        return false;

    if ( terrain != yw_ImpactScarIsTerrain(world, ray, normal) ||
         !yw_ImpactScarSameSurface(reference, ray, terrain) )
        return false;

    *out = ray;
    *outNormal = normal;
    return true;
}

static bool yw_BuildImpactScarMesh(NC_STACK_ypaworld *world,
                                   NC_STACK_ypaworld::TImpactScar *scar,
                                   const ypaworld_arg136 &reference,
                                   ResBitmap *texture)
{
    static const int GRID = 8;
    static const float SURFACE_OFFSET = 2.0f;

    GFX::TMesh &mesh = scar->mesh;
    mesh.Vertexes.clear();
    mesh.Indixes.clear();
    mesh.CoordsCache.clear();
    mesh.Mat = GFX::TRenderParams(GFX::RFLAGS_TEXTURED |
                                  GFX::RFLAGS_FOG |
                                  GFX::RFLAGS_DISABLE_ZWRITE |
                                  GFX::RFLAGS_ALPHABLEND);
    mesh.Mat.Tex = texture;

    vec3d tangent, bitangent;
    yw_ImpactScarBasis(scar->normal, &tangent, &bitangent);

    uint32_t seed = (uint32_t)(world->_timeStamp * 1103515245u) ^
                    (uint32_t)((int)scar->pos.x * 73856093u) ^
                    (uint32_t)((int)scar->pos.z * 83492791u);
    float angle = (float)(seed & 0xffffu) * (6.28318530718f / 65535.0f);
    float cs = cosf(angle);
    float sn = sinf(angle);
    vec3d rotatedTangent = tangent * cs + bitangent * sn;
    vec3d rotatedBitangent = bitangent * cs - tangent * sn;
    tangent = rotatedTangent;
    bitangent = rotatedBitangent;

    float radius = scar->radius;
    if ( radius <= 0.0f )
        return false;

    float step = (radius * 2.0f) / (float)GRID;
    float traceHalfLength = std::max(18.0f, std::min(80.0f, step * 2.0f));

    static const int VERTS_PER_SIDE = GRID + 1;
    static const int VERTEX_COUNT = VERTS_PER_SIDE * VERTS_PER_SIDE;
    std::vector<vec3d> positions(VERTEX_COUNT);
    std::vector<vec3d> normals(VERTEX_COUNT);
    std::vector<uint8_t> valid(VERTEX_COUNT, 0);

    mesh.Vertexes.reserve(VERTEX_COUNT);
    mesh.Indixes.reserve(GRID * GRID * 6);

    auto gridIndex = [](int x, int y)
    {
        return y * VERTS_PER_SIDE + x;
    };

    GFX::TGLColor white(1.0f, 1.0f, 1.0f, 1.0f);
    for (int y = 0; y <= GRID; ++y)
    {
        for (int x = 0; x <= GRID; ++x)
        {
            float u = (float)x / (float)GRID;
            float v = (float)y / (float)GRID;
            float fx = u * 2.0f - 1.0f;
            float fy = v * 2.0f - 1.0f;
            int index = gridIndex(x, y);

            vec3d planePoint = scar->pos + tangent * (fx * radius) + bitangent * (fy * radius);
            ypaworld_arg136 hit;
            vec3d localNormal;
            if ( yw_ImpactScarTraceCell(world, reference, scar->terrain, planePoint,
                                        scar->normal, traceHalfLength, &hit, &localNormal) )
            {
                positions[index] = hit.isectPos + localNormal * SURFACE_OFFSET;
                normals[index] = localNormal;
                valid[index] = 1;
            }
            else
                positions[index] = planePoint;

            mesh.Vertexes.emplace_back(vec3f(positions[index] - scar->pos), tUtV(u, v), white);
        }
    }

    auto edgeIsContinuous = [&positions, step](int a, int b)
    {
        return (positions[a] - positions[b]).length() <= step * 1.6f;
    };

    auto appendTriangle = [&mesh, &positions, &normals](int a, int b, int c)
    {
        vec3d expectedNormal = normals[a] + normals[b] + normals[c];
        if ( expectedNormal.normalise() <= 0.001 )
            return;

        vec3d winding = (positions[b] - positions[a]) * (positions[c] - positions[a]);
        mesh.Indixes.push_back(a);
        if ( winding.dot(expectedNormal) >= 0.0 )
        {
            mesh.Indixes.push_back(b);
            mesh.Indixes.push_back(c);
        }
        else
        {
            mesh.Indixes.push_back(c);
            mesh.Indixes.push_back(b);
        }
    };

    for (int y = 0; y < GRID; ++y)
    {
        for (int x = 0; x < GRID; ++x)
        {
            int a = gridIndex(x, y);
            int b = gridIndex(x + 1, y);
            int c = gridIndex(x + 1, y + 1);
            int d = gridIndex(x, y + 1);

            if ( !valid[a] || !valid[b] || !valid[c] || !valid[d] )
                continue;

            if ( normals[a].dot(normals[b]) < 0.85f ||
                 normals[b].dot(normals[c]) < 0.85f ||
                 normals[c].dot(normals[d]) < 0.85f ||
                 normals[d].dot(normals[a]) < 0.85f )
                continue;

            if ( !edgeIsContinuous(a, b) || !edgeIsContinuous(b, c) ||
                 !edgeIsContinuous(c, d) || !edgeIsContinuous(d, a) )
                continue;

            appendTriangle(a, b, c);
            appendTriangle(a, c, d);
        }
    }

    if ( mesh.Indixes.empty() )
        return false;

    mesh.RecalcBoundBox();
    GFX::Engine.MeshMakeVBO(&mesh);
    return true;
}

}

bool NC_STACK_ypaworld::AddImpactScar(const World::TWeaponImpactScarConfig &config,
                                      const ypaworld_arg136 &hit,
                                      const vec3d &incomingDirection)
{
    if ( !config.enabled || config.radius <= 0.0f || config.duration <= 0 ||
         !hit.isect || !yw_ImpactScarCellIsValid(this, hit.hitCell) )
        return false;

    vec3d preferred = -incomingDirection;
    if ( preferred.length() <= 0.001 )
        preferred = -hit.vect;
    vec3d normal;
    if ( !yw_ImpactScarHitNormal(hit, preferred, &normal) )
        return false;

    bool terrain = yw_ImpactScarIsTerrain(this, hit, normal);
    if ( terrain && !config.terrain )
        return false;

    if ( !_impactScarTextureLoadAttempted )
    {
        _impactScarTextureLoadAttempted = true;
        _impactScarTexture = Utils::ProxyLoadImage({
            {NC_STACK_rsrc::RSRC_ATT_NAME, std::string("ImpactScars/impact_scorch.png")},
            {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1}});

        if ( _impactScarTexture && _impactScarTexture->GetBitmap() )
            _impactScarTexture->PrepareTexture(false);
        else
            Common::DeleteAndNull(&_impactScarTexture);
    }

    if ( !_impactScarTexture || !_impactScarTexture->GetBitmap() )
        return false;

    static const size_t IMPACT_SCAR_MAX = 32;
    while ( _impactScars.size() >= IMPACT_SCAR_MAX )
        _impactScars.pop_front();

    _impactScars.emplace_back();
    TImpactScar &scar = _impactScars.back();
    scar.pos = hit.isectPos;
    scar.normal = normal;
    scar.radius = config.radius;
    scar.terrain = terrain;
    scar.hitCell = hit.hitCell;
    scar.hitCollisionType = hit.hitCollisionType;
    scar.hitBldX = hit.hitBldX;
    scar.hitBldY = hit.hitBldY;
    scar.hitModelID = hit.hitModelID;
    scar.startTime = _timeStamp;
    scar.duration = config.duration;
    scar.fadeTime = std::min(config.fade_time, config.duration);
    scar.color = config.color;

    if ( !yw_BuildImpactScarMesh(this, &scar, hit, _impactScarTexture->GetBitmap()) )
    {
        _impactScars.pop_back();
        return false;
    }

    return true;
}

bool NC_STACK_ypaworld::AddImpactScarAtWorldHit(const World::TWeaponImpactScarConfig &config,
                                                const vec3d &position,
                                                const vec3d &normal,
                                                const vec3d &incomingDirection)
{
    vec3d outward = normal;
    if ( outward.normalise() <= 0.001 )
        outward = -incomingDirection;
    if ( outward.normalise() <= 0.001 )
        return false;
    if ( outward.dot(incomingDirection) > 0.0 )
        outward = -outward;

    ypaworld_arg136 hit;
    hit.stPos = position + outward * 18.0f;
    hit.vect = outward * -36.0f;
    hit.flags = 0;
    ypaworld_func136(&hit);

    if ( !hit.isect )
    {
        hit.stPos = position - outward * 18.0f;
        hit.vect = outward * 36.0f;
        hit.flags = 0;
        ypaworld_func136(&hit);
    }

    return AddImpactScar(config, hit, incomingDirection);
}

void NC_STACK_ypaworld::ClearImpactScars()
{
    _impactScars.clear();
}

void NC_STACK_ypaworld::RenderImpactScars(baseRender_msg *arg)
{
    TF::TForm3D *view = TF::Engine.GetViewPoint();
    if ( !view )
        return;

    for (auto it = _impactScars.begin(); it != _impactScars.end(); )
    {
        int32_t age = std::max(0, _timeStamp - it->startTime);
        bool invalidSurface = !yw_ImpactScarCellIsValid(this, it->hitCell);

        if ( !invalidSurface )
        {
            const cellArea &cell = _cells(it->hitCell);
            if ( it->terrain && cell.PurposeType != cellArea::PT_NONE )
                invalidSurface = true;
            if ( !invalidSurface && it->hitCollisionType == 1 )
                invalidSurface = GetLegoBld(&cell, it->hitBldX, it->hitBldY) != it->hitModelID;
        }

        if ( age >= it->duration || invalidSurface )
        {
            it = _impactScars.erase(it);
            continue;
        }

        float fade = 1.0f;
        if ( it->fadeTime > 0 && age >= it->duration - it->fadeTime )
            fade = (float)(it->duration - age) / (float)it->fadeTime;

        mat4x4 world;
        world += it->pos;
        mat4x4 tf = view->CalcSclRot;
        tf *= (world - view->CalcPos);
        if ( tf.m23 > 4.0 )
            tf.m23 -= 4.0;

        GFX::TRenderNode &rend = GFX::Engine.AllocRenderNode();
        rend = GFX::TRenderNode(GFX::TRenderNode::TYPE_MESH);
        rend.Mesh = &it->mesh;
        rend.Tex = it->mesh.Mat.Tex;
        rend.Flags = it->mesh.Mat.Flags | arg->flags;
        rend.Color = it->mesh.Mat.Color;
        rend.ColorMul = GFX::TGLColor(it->color.r, it->color.g, it->color.b, it->color.a * fade);
        rend.TForm = tf;
        rend.Distance = tf.getTranslate().length();
        rend.TimeStamp = arg->globTime;
        rend.FrameTime = arg->frameTime;
        rend.FogStart = (float)(_normalVizLimit - _normalFadeLength);
        rend.FogLength = (float)_normalFadeLength;

        arg->adeCount += it->mesh.Indixes.size() / 3;
        GFX::Engine.QueueRenderMesh(&rend);
        ++it;
    }
}
