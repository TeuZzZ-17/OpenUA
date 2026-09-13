#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "yw.h"
#include "ypamissile.h"
#include "yparobo.h"
#include "yw_net.h"
#include "system/inivals.h"

#include "log.h"

#include <algorithm>
#include <math.h>

static float ypamissile_Clamp01(float value)
{
    if ( value < 0.0 )
        return 0.0;

    if ( value > 1.0 )
        return 1.0;

    return value;
}

static bool ypamissile_WeaponTracerFinite(const vec3d &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

static bool ypamissile_WeaponTracerFinite(const World::TVisualTint &tint)
{
    return std::isfinite(tint.r) && std::isfinite(tint.g) &&
           std::isfinite(tint.b) && std::isfinite(tint.a);
}

static bool ypamissile_IsAliveForDeathPush(NC_STACK_ypabact *target)
{
    return target &&
           target->CanReceiveConfiguredPush() &&
           target->_status != BACT_STATUS_DEAD &&
           target->_energy > 0 &&
           !(target->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2));
}

static NC_STACK_ypabact *ypamissile_ResolveDirectPushRecipient(NC_STACK_ypabact *target)
{
    if ( !target || target->_bact_type != BACT_TYPES_GUN )
        return target;

    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(target);
    if ( !gun || (!target->_isUnitGunChild && !gun->IsRoboGun()) )
        return target;

    NC_STACK_ypabact *parent = target->_parent;
    if ( !parent || parent == target || !target->getBACT_pWorld() ||
         parent->getBACT_pWorld() != target->getBACT_pWorld() ||
         parent->_bact_type == BACT_TYPES_MISSLE ||
         !ypamissile_IsAliveForDeathPush(parent) )
        return target;

    return parent;
}

static float ypamissile_GetPushAtDeathMultiplier()
{
    std::string value = System::IniConf::GamePushAtDeathMultiplier.Get<std::string>();
    if ( value.empty() || value.find(',') != std::string::npos )
        return 1.0f;

    try
    {
        size_t pos = 0;
        float multiplier = std::stof(value, &pos);
        if ( value.find_first_not_of(" \t\r\n", pos) != std::string::npos ||
             !isfinite(multiplier) || multiplier < 0.0f )
            return 1.0f;

        return std::min(multiplier, 10.0f);
    }
    catch (...)
    {
        return 1.0f;
    }
}

static float ypamissile_GetTargetPushMultiplier(NC_STACK_ypabact *target)
{
    return target ? target->GetPushResistanceMultiplier() : 1.0f;
}

static float ypamissile_SegmentSegmentDistanceSq(const vec3d &p1, const vec3d &q1, const vec3d &p2, const vec3d &q2)
{
    const float EPSILON = 0.0001f;
    vec3d d1 = q1 - p1;
    vec3d d2 = q2 - p2;
    vec3d r = p1 - p2;
    float a = d1.dot(d1);
    float e = d2.dot(d2);
    float f = d2.dot(r);
    float s = 0.0f;
    float t = 0.0f;

    if ( a <= EPSILON && e <= EPSILON )
        return r.dot(r);

    if ( a <= EPSILON )
    {
        t = ypamissile_Clamp01(f / e);
    }
    else
    {
        float c = d1.dot(r);
        if ( e <= EPSILON )
        {
            s = ypamissile_Clamp01(-c / a);
        }
        else
        {
            float b = d1.dot(d2);
            float denom = a * e - b * b;

            if ( denom != 0.0f )
                s = ypamissile_Clamp01((b * f - c * e) / denom);

            t = (b * s + f) / e;

            if ( t < 0.0f )
            {
                t = 0.0f;
                s = ypamissile_Clamp01(-c / a);
            }
            else if ( t > 1.0f )
            {
                t = 1.0f;
                s = ypamissile_Clamp01((b - c) / a);
            }
        }
    }

    vec3d c1 = p1 + d1 * s;
    vec3d c2 = p2 + d2 * t;
    vec3d delta = c1 - c2;
    return delta.dot(delta);
}

static float ypamissile_AoeFalloffFactor(float distance, float radius, bool falloff)
{
    if ( radius <= 0.0 || distance > radius )
        return 0.0;

    if ( !falloff )
        return 1.0;

    // Same attenuation shape used by NC_STACK_ypacar::DoKamikaze().
    return ypamissile_Clamp01(exp(distance * -2.8 / World::CVSectorLength));
}

static int ypamissile_ScaleAoeEnergy(int energy, float factor)
{
    if ( energy <= 0 || factor <= 0.0 )
        return 0;

    return (int)((float)energy * factor);
}

static bool ypamissile_NormalizeXZ(vec3d *dir)
{
    dir->y = 0.0f;

    float len = dir->length();
    if ( !isfinite(len) || len <= 0.001f )
        return false;

    *dir /= len;
    return true;
}

static bool ypamissile_GetDirectPushDir(NC_STACK_ypamissile *missile, NC_STACK_ypabact *target,
                                        const vec3d &fallbackDir, vec3d *outDir)
{
    if ( !missile || !target || !outDir )
        return false;

    vec3d dir;

    if ( missile->GetLauncherBact() && missile->GetLauncherBact() != target )
    {
        dir = target->_position - missile->GetLauncherBact()->_position;
        if ( ypamissile_NormalizeXZ(&dir) )
        {
            *outDir = dir;
            return true;
        }
    }

    dir = missile->_position - missile->_old_pos;
    if ( ypamissile_NormalizeXZ(&dir) )
    {
        *outDir = dir;
        return true;
    }

    dir = missile->_fly_dir;
    if ( ypamissile_NormalizeXZ(&dir) )
    {
        *outDir = dir;
        return true;
    }

    dir = fallbackDir;
    if ( ypamissile_NormalizeXZ(&dir) )
    {
        *outDir = dir;
        return true;
    }

    return false;
}

static NC_STACK_ypabact *ypamissile_FindLiveBactByGid(World::RefBactList &list, int32_t gid)
{
    for (NC_STACK_ypabact *unit : list)
    {
        if ( unit->_gid == gid )
        {
            if ( unit->_kidRef.IsListType(World::BLIST_CACHE) || unit->_status == BACT_STATUS_DEAD )
                return NULL;

            return unit;
        }

        NC_STACK_ypabact *kid = ypamissile_FindLiveBactByGid(unit->_kidList, gid);
        if ( kid )
            return kid;
    }

    return NULL;
}

size_t NC_STACK_ypamissile::Init(IDVList &stak)
{
    if ( !NC_STACK_ypabact::Init(stak) )
        return 0;

    _bact_type = BACT_TYPES_MISSLE;

    _mislEmitter = NULL;
    _mislSpecificEnergy.fill(1.0f);
    _mislSpecificEnergyDefined.fill(false);
    _mislLifeTime = 5000;
    _mislDelayTime = 0;
    _mislType = MISL_BOMB;
    _mislAoeUnitPush = 0.0f;
    _mislArmorPenetrationRemaining = 0;
    _mislArmorPenetratedGids.clear();
    _mislDirectPushRecipientGids.clear();
    _mislClusterAge = 0;
    _weaponSoundEventsEnabled = true;
    _collisionOldRotationValid = false;
    _mislClusterGeneration = 0;
    _mislClusterDone = false;
    _mislClusterChild = false;
    _mislChainDepth = 0;
    _mislChainEnergy = 0;
    _mislChainSpawned = false;
    _mislChainAllowFriendly = false;
    _mislChainPending = false;
    _mislChainPendingElapsed = 0;
    _mislChainPendingDelay = 0;
    _mislChainPendingTargetGid = 0;
    _mislChainPendingEnergy = 0;
    _mislChainPendingOrigin = vec3d(0.0, 0.0, 0.0);
    _mislChainPendingOriginRadius = 0.0;
    _mislChainHitGids.clear();
    _suicideViewerReturnGids.clear();
    _suicideViewerHostGid = 0;
    _mislAttachedToTarget = false;
    _mislAttachTargetGid = 0;
    _mislAttachOffset = vec3d(0.0, 0.0, 0.0);
    _mislLastAttachedPosition = vec3d(0.0, 0.0, 0.0);
    _mislClusterSoundCarrier.Clear();
    _weaponTracer = World::TWeaponTracerConfig();
    _weaponTracerStarted = false;
    _weaponTracerVisualSeed = 0;
    _weaponTracerPoints.clear();

    for( auto& it : stak )
    {
        IDVPair &val = it.second;

        if ( !val.Skip )
        {
            switch (val.ID)
            {
            case BACT_ATT_VIEWER:
                setBACT_viewer(val.Get<bool>());
                break;

            case MISS_ATT_LAUNCHER:
                SetLauncherBact(val.Get<NC_STACK_ypabact *>());
                break;

            case MISS_ATT_TYPE:
                SetMissileType(val.Get<int32_t>());
                break;

            case MISS_ATT_LIFETIME:
                SetLifeTime(val.Get<int32_t>());
                break;

            case MISS_ATT_DELAY:
                SetDelay(val.Get<int32_t>());
                break;

            case MISS_ATT_DRIVETIME:
                SetDriveTime(val.Get<int32_t>());
                break;

            case MISS_ATT_IGNOREBUILDS:
                SetIgnoreBuilds ( val.Get<int32_t>() );
                break;

            default:
                break;
            }
        }
    }

    return 1;
}

size_t NC_STACK_ypamissile::Deinit()
{
    SFXEngine::SFXe.StopCarrier(&_mislClusterSoundCarrier);
    _mislClusterSoundCarrier.Clear();

    return NC_STACK_ypabact::Deinit();
}

size_t NC_STACK_ypamissile::SetParameters(IDVList &stak)
{
    NC_STACK_ypabact::SetParameters(stak);

    for( auto& it : stak )
    {
        IDVPair &val = it.second;

        if ( !val.Skip )
        {
            switch (val.ID)
            {
            case BACT_ATT_VIEWER:
                setBACT_viewer(val.Get<bool>());
                break;

            case MISS_ATT_LAUNCHER:
                SetLauncherBact(val.Get<NC_STACK_ypabact *>());
                break;

            case MISS_ATT_TYPE:
                SetMissileType(val.Get<int32_t>());
                break;

            case MISS_ATT_LIFETIME:
                SetLifeTime(val.Get<int32_t>());
                break;

            case MISS_ATT_DELAY:
                SetDelay(val.Get<int32_t>());
                break;

            case MISS_ATT_DRIVETIME:
                SetDriveTime(val.Get<int32_t>());
                break;

            case MISS_ATT_IGNOREBUILDS:
                SetIgnoreBuilds ( val.Get<int32_t>() );
                break;

            case MISS_ATT_POW_HELI:
                SetPowerHeli(val.Get<int32_t>());
                break;

            case MISS_ATT_POW_TANK:
                SetPowerTank(val.Get<int32_t>());
                break;

            case MISS_ATT_POW_FLYER:
                SetPowerFlyer(val.Get<int32_t>());
                break;

            case MISS_ATT_POW_ROBO:
                SetPowerRobo(val.Get<int32_t>());
                break;

            case MISS_ATT_RAD_HELI:
                SetRadiusHeli(val.Get<float>());
                break;

            case MISS_ATT_RAD_TANK:
                SetRadiusTank(val.Get<float>());
                break;

            case MISS_ATT_RAD_FLYER:
                SetRadiusFlyer(val.Get<float>());
                break;

            case MISS_ATT_RAD_ROBO:
                SetRadiusRobo(val.Get<float>());
                break;

            case MISS_ATT_STHEIGHT:
                SetStartHeight(val.Get<float>());
                break;

            default:
                break;
            }
        }
    }

    return 1;
}

void NC_STACK_ypamissile::AI_layer1(update_msg *arg)
{
    _collisionOldRotation = _rotation;
    _collisionOldRotationValid = true;

    if ( !_mislClusterSoundCarrier.Sounds.empty() )
        SFXEngine::SFXe.UpdateSoundCarrier(&_mislClusterSoundCarrier);

    UpdatePendingChainJump(arg);

    if ( _status == BACT_STATUS_DEAD )
        _yls_time -= arg->frameTime;


    if ( _primTtype )
    {
        if ( _primTtype == BACT_TGT_TYPE_UNIT )
            _target_vec = _primT.pbact->_position - _position;
        else
            _target_vec = _primTpos - _position;
    }

    AI_layer2(arg);
}

void NC_STACK_ypamissile::AI_layer2(update_msg *arg)
{
    AI_layer3(arg);
    UpdateWeaponTracer();
}

void NC_STACK_ypamissile::ConfigureWeaponTracer(
    const World::TWeaponTracerConfig &config, bool supported)
{
    _weaponTracer = config;
    _weaponTracer.tint.Clamp();
    _weaponTracer.tint_head.Clamp();
    _weaponTracer.tint_tail.Clamp();
    _weaponTracer.enabled = supported && config.enabled &&
                            std::isfinite(config.size_z) && config.size_z > 0.01f &&
                            std::isfinite(config.size_x) && config.size_x > 0.01f &&
                            std::isfinite(config.ResolveSizeY()) &&
                            config.ResolveSizeY() >= 0.0f &&
                            ypamissile_WeaponTracerFinite(config.pos) &&
                            ypamissile_WeaponTracerFinite(config.tint) &&
                            config.tint.a > 0.0f;
    _weaponTracerStarted = false;
    _weaponTracerVisualSeed = 0;
    _weaponTracerPoints.clear();
}

void NC_STACK_ypamissile::StartWeaponTracer()
{
    _weaponTracerStarted = _weaponTracer.enabled && _world &&
                           !_world->_isNetGame &&
                           _mislType != MISL_INTERNAL;
    _weaponTracerPoints.clear();

    if ( !_weaponTracerStarted )
        return;

    // mesh_tracer_pos_* is a projectile-local visual offset. Apply it to every
    // sampled tracer point, starting here at launch, so the complete trail stays
    // attached to the authored position relative to the projectile transform.
    const vec3d point = GetWeaponTracerPosition();
    if ( !ypamissile_WeaponTracerFinite(point) )
    {
        _weaponTracerStarted = false;
        return;
    }

    _weaponTracerVisualSeed =
        (uint32_t)(_gid ? _gid : (_vehicleID * 2654435761u)) ^
        (uint32_t)_clock;

    TWeaponTracerPoint sample;
    sample.pos = point;
    sample.time = _clock;
    _weaponTracerPoints.push_back(sample);
}

vec3d NC_STACK_ypamissile::GetWeaponTracerPosition() const
{
    // Reuse the same final motion delta as the projectile VP and attached FX.
    // Physics owns the centre trajectory; Chaos/Spiral affect its rendered pose.
    vec3d visualOffset;
    mat3x3 visualRotationDelta;
    GetProjectileVisualMotionDelta(&visualOffset, &visualRotationDelta);
    const mat3x3 renderRotation = _rotation.Transpose() * visualRotationDelta;
    return _position + visualOffset + renderRotation.Transform(_weaponTracer.pos);
}

void NC_STACK_ypamissile::UpdateWeaponTracer()
{
    // Loaded projectiles and other legitimate factory paths may not pass
    // through a launch-specific finalization call. Start from their current
    // authoritative transform on the first update without serializing history.
    if ( !_weaponTracerStarted && _weaponTracer.enabled && _world &&
         !_world->_isNetGame && _status == BACT_STATUS_NORMAL &&
         _mislType != MISL_INTERNAL )
    {
        StartWeaponTracer();
    }

    if ( !_weaponTracerStarted || !_world || _world->_isNetGame )
        return;

    // The tracer has no independent lifetime: it exists only while the physical
    // projectile itself is alive. Do not leave a visual tail after impact/death.
    if ( _status != BACT_STATUS_NORMAL )
    {
        _weaponTracerStarted = false;
        _weaponTracerPoints.clear();
        return;
    }

    const vec3d point = GetWeaponTracerPosition();

    if ( ypamissile_WeaponTracerFinite(point) )
    {
        if ( _weaponTracerPoints.empty() )
        {
            TWeaponTracerPoint sample;
            sample.pos = point;
            sample.time = _clock;
            _weaponTracerPoints.push_back(sample);
        }
        else
        {
            const float moved = (point - _weaponTracerPoints.back().pos).length();
            const bool sampleDue =
                _clock - _weaponTracerPoints.back().time >= 16;

            if ( std::isfinite(moved) && moved > 0.01f && sampleDue )
            {
                TWeaponTracerPoint sample;
                sample.pos = point;
                sample.time = _clock;
                _weaponTracerPoints.push_back(sample);
            }
        }
    }

    // Keep a strict recent-path memory/render bound per projectile. The tracer
    // lifetime itself is still owned exclusively by the physical projectile.
    while ( _weaponTracerPoints.size() > 320 )
        _weaponTracerPoints.pop_front();
}

void NC_STACK_ypamissile::RenderWeaponTracer(baseRender_msg *arg)
{
    if ( !arg || !_weaponTracerStarted || !_world || _world->_isNetGame ||
         _status != BACT_STATUS_NORMAL || _weaponTracerPoints.empty() )
        return;

    struct TVisibleTracerSegment
    {
        vec3d start;
        vec3d end;
        float length = 0.0f;
    };

    std::vector<TVisibleTracerSegment> visible;
    float remainingLength = _weaponTracer.size_z;
    float visibleLength = 0.0f;

    // History is sampled at 16 ms; the head must follow the current pose even
    // between samples or after a later shared movement update in this frame.
    vec3d newer = GetWeaponTracerPosition();
    for (size_t index = _weaponTracerPoints.size();
         index > 0 && remainingLength > 0.01f; index--)
    {
        const TWeaponTracerPoint &older = _weaponTracerPoints[index - 1];

        vec3d segment = newer - older.pos;
        const float segmentLength = segment.length();
        if ( !std::isfinite(segmentLength) || segmentLength <= 0.01f )
            continue;

        TVisibleTracerSegment item;
        item.start = older.pos;
        item.end = newer;
        item.length = std::min(segmentLength, remainingLength);

        if ( segmentLength > remainingLength )
            item.start = newer - segment * (remainingLength / segmentLength);

        visible.push_back(item);
        visibleLength += item.length;
        remainingLength -= item.length;
        newer = older.pos;
    }

    if ( visible.empty() || visibleLength <= 0.01f )
        return;

    float renderedFromHead = 0.0f;
    for (const TVisibleTracerSegment &item : visible)
    {
        const float headFactor = 1.0f - renderedFromHead / visibleLength;
        const float tailFactor = 1.0f -
            (renderedFromHead + item.length) / visibleLength;
        _world->RenderWeaponTracerSegment(arg, item.start, item.end,
                                           _weaponTracer,
                                           std::max(0.0f, tailFactor),
                                           std::min(1.0f, headFactor),
                                           _weaponTracerVisualSeed);

        renderedFromHead += item.length;
    }
}

void NC_STACK_ypamissile::Render(baseRender_msg *arg)
{
    // Preserve the complete existing projectile VP path, including particles,
    // tint, scale and projectile visual motion. The procedural tracer is queued in addition.
    NC_STACK_ypabact::Render(arg);
    RenderWeaponTracer(arg);
}

bool NC_STACK_ypamissile::TryClusterSplit(bool impactOrDetonation)
{
    if ( !_world || !_mislEmitter || _mislClusterDone || _mislClusterChild )
        return false;

    std::vector<World::TWeapProto> &weapons = _world->GetWeaponsProtos();

    if ( _vehicleID >= weapons.size() )
        return false;

    World::TWeapProto &parentProto = weapons.at(_vehicleID);
    World::TWeaponClusterConfig &cluster = parentProto.cluster;

    if ( !cluster.enable || cluster.count <= 0 || cluster.weapon_id <= 0 )
        return false;

    if ( !World::IsWeaponClusterTriggerDue(cluster.trigger_time,
                                            _mislClusterAge,
                                            impactOrDetonation) )
        return false;

    int childClusterGeneration = _mislClusterGeneration + 1;

    if ( _mislClusterGeneration > 0 && (cluster.generations <= 0 || childClusterGeneration > cluster.generations) )
    {
        _mislClusterDone = true;
        return false;
    }

    if ( cluster.weapon_id >= weapons.size() )
        return false;

    World::TWeapProto &childProto = weapons.at(cluster.weapon_id);

    if ( !(childProto._weaponFlags & 1) )
        return false;

    _mislClusterDone = true;

    _world->SpawnTransientVisual(cluster.vp, cluster.mesh3ds, cluster.basePath,
                                 _position, _rotation, 1000);

    cluster.snd.LoadSamples();
    TSampleData *clusterSample = cluster.snd.MainSample.Sample ? cluster.snd.MainSample.Sample->GetSampleData() : NULL;
    bool clusterSoundPlayed = false;

    vec3d baseDir = _fly_dir;
    if ( baseDir.normalise() <= 0.001 )
        baseDir = _rotation.AxisZ();
    if ( baseDir.normalise() <= 0.001 )
        baseDir = vec3d::OZ(1.0);

    uint8_t childTargetType = BACT_TGT_TYPE_DRCT;
    BactTarget childTarget = {};
    vec3d childTargetPos = _position + baseDir * 1000.0;

    if ( _primTtype == BACT_TGT_TYPE_UNIT && _primT.pbact && _primT.pbact->_status != BACT_STATUS_DEAD )
    {
        childTargetType = BACT_TGT_TYPE_UNIT;
        childTarget = _primT;
        childTargetPos = _primT.pbact->_position;
    }
    else if ( _primTtype == BACT_TGT_TYPE_CELL )
    {
        childTargetType = BACT_TGT_TYPE_CELL;
        childTarget = _primT;
        childTargetPos = _primTpos;
    }

    int countMin = 1;
    int countMax = 1;
    cluster.GetCountRange(countMin, countMax);
    int spawnCount = World::RandomIntRangeInclusive(countMin, countMax);
    int spawned = 0;

    const bool hasClusterAngles = cluster.horizontal_angle_set ||
                                  cluster.vertical_angle_set;
    float horizontalMin = cluster.horizontal_angle_min;
    float horizontalMax = cluster.horizontal_angle_max;
    if ( horizontalMax < horizontalMin )
        std::swap(horizontalMin, horizontalMax);
    const bool evenlyDistributeYaw =
        cluster.horizontal_angle_set &&
        std::fabs((horizontalMax - horizontalMin) - 360.0f) <= 0.001f;

    for (int i = 0; i < spawnCount; i++)
    {
        ypaworld_arg146 arg147;
        arg147.vehicle_id = cluster.weapon_id;
        arg147.pos = _position;

        NC_STACK_ypamissile *child = _world->ypaworld_func147(&arg147);

        if ( !child )
            continue;

        vec3d childDir = baseDir;
        if ( hasClusterAngles )
        {
            vec3d localDir = World::ResolveYawPitchDirection(
                cluster.horizontal_angle_set,
                horizontalMin,
                horizontalMax,
                cluster.vertical_angle_set,
                cluster.vertical_angle_min,
                cluster.vertical_angle_max,
                i,
                spawnCount,
                evenlyDistributeYaw);
            childDir = _rotation.Transpose().Transform(localDir);
            if ( childDir.normalise() <= 0.001f )
                childDir = baseDir;
        }

        // Cluster spread remains an independent deviation applied after the
        // new yaw/pitch base direction, so both controls compose.
        childDir = World::ApplyDirectionalSpread(_rotation, childDir,
                                                  cluster.spread_x,
                                                  cluster.spread_y);

        NC_STACK_yparobo *childHost = _host_station
                                          ? _host_station
                                          : _mislEmitter->_host_station;
        child->ConfigureSpawnedProjectileBase(_mislEmitter, childHost,
                                               _owner, childDir);
        child->SetClusterSpawnedChild(cluster.generations <= 0);
        child->_mislClusterGeneration = childClusterGeneration;
        if ( childProto.IsArcGrenade() )
        {
            if ( hasClusterAngles )
            {
                // The Cluster angles and subsequent spread are already the
                // complete authored launch direction. Commit that vector to
                // the Arc Grenade runtime without overwriting its pitch.
                const float childSpeed =
                    std::isfinite(childProto.start_speed) && childProto.start_speed > 0.0f
                        ? childProto.start_speed
                        : 0.0f;
                child->SetupArcGrenadeVelocity(child->_fly_dir * childSpeed,
                                               childProto.grenade_arc_gravity);
            }
            else
            {
                // No Cluster angles: preserve the legacy Arc Grenade launch
                // path, including grenade_arc_angle.
                child->SetupArcGrenadeLaunch(childProto.grenade_arc_angle,
                                             childProto.grenade_arc_gravity,
                                             childProto.start_speed);
            }
        }
        else
        {
            child->_fly_dir_length = childProto.start_speed;

            if ( !(childProto._weaponFlags & 0x12) )
                child->_fly_dir_length *= 0.2;

            child->_rotation.SetZ(child->_fly_dir);
            child->_rotation.SetX(_rotation.AxisX());
            child->_rotation.SetY(child->_rotation.AxisZ() * child->_rotation.AxisX());
        }
        child->StartWeaponTracer();

        _world->SpawnTransientVisual(childProto.vp_launch, childProto.visual_3ds.launch,
                                     childProto.visual_base.launch,
                                     child->_position, child->_rotation, 1000,
                                     1.0, World::TVisualTint(), childProto.launch_scale);

        child->_kidRef.Detach();
        child->_parent = NULL;
        _mislEmitter->_missiles_list.push_back(child);

        if ( (clusterSample || cluster.snd.sndPrm.slot || cluster.snd.sndPrm_shk.slot) && !clusterSoundPlayed )
        {
            child->_mislClusterSoundCarrier.Clear();
            child->_mislClusterSoundCarrier.Resize(1);
            child->_mislClusterSoundCarrier.Position = _position;
            child->_mislClusterSoundCarrier.Vector = vec3d(0.0, 0.0, 0.0);

            TSoundSource &snd = child->_mislClusterSoundCarrier.Sounds[0];
            snd.PSample = clusterSample;
            snd.Volume = cluster.snd.volume ? cluster.snd.volume : 120;
            cluster.snd.ConfigureSoundSourcePitch(snd);
            snd.Radius = cluster.snd.radius;
            snd.SetLoop(false);
            snd.SetFragmented(false);

            if ( cluster.snd.sndPrm.slot )
            {
                snd.PPFx = &cluster.snd.sndPrm;
                snd.SetPFx(true);
            }
            else
            {
                snd.PPFx = NULL;
                snd.SetPFx(false);
            }

            if ( cluster.snd.sndPrm_shk.slot )
            {
                snd.PShkFx = &cluster.snd.sndPrm_shk;
                snd.SetShk(true);
            }
            else
            {
                snd.PShkFx = NULL;
                snd.SetShk(false);
            }

            SFXEngine::SFXe.startSound(&child->_mislClusterSoundCarrier, 0);
            SFXEngine::SFXe.UpdateSoundCarrier(&child->_mislClusterSoundCarrier);
            clusterSoundPlayed = true;
        }

        if ( child->GetMissileType() == MISL_TARGETED &&
             childTargetType != BACT_TGT_TYPE_DRCT )
        {
            setTarget_msg arg67;
            arg67.tgt = childTarget;
            arg67.tgt_type = childTargetType;
            arg67.priority = 0;
            arg67.tgt_pos = childTargetPos;

            child->SetTarget(&arg67);

            if ( childTargetType == BACT_TGT_TYPE_CELL )
                child->_primTpos.y = childTargetPos.y;
        }
        else
        {
            child->_primTtype = BACT_TGT_TYPE_DRCT;
            child->_target_dir = child->_fly_dir;
        }

        if ( childTargetType != BACT_TGT_TYPE_UNIT )
        {
            int life_time_nt = childProto.life_time_nt;

            if ( life_time_nt )
                child->SetLifeTime(life_time_nt);
        }

        SFXEngine::SFXe.startSound(&child->_soundcarrier, World::TWeapProto::SND_LAUNCH);
        spawned++;
    }

    if ( spawned <= 0 )
    {
        _mislClusterDone = false;
        return false;
    }

    if ( getBACT_viewer() )
    {
        if ( _mislEmitter )
            ResetViewing();
        else
        {
            setBACT_viewer(false);
            setBACT_inputting(false);
        }
    }

    _hidden = true;
    _fly_dir_length = 0.0;
    _status = BACT_STATUS_DEAD;
    setBACT_yourLastSeconds(0);

    return true;
}

bool NC_STACK_ypamissile::IsChainHit(NC_STACK_ypabact *target) const
{
    if ( !target || !target->_gid )
        return false;

    return std::find(_mislChainHitGids.begin(), _mislChainHitGids.end(), target->_gid) != _mislChainHitGids.end();
}

void NC_STACK_ypamissile::RememberChainHit(NC_STACK_ypabact *target)
{
    if ( target && target->_gid && !IsChainHit(target) )
        _mislChainHitGids.push_back(target->_gid);
}

bool NC_STACK_ypamissile::CanChainToTarget(NC_STACK_ypabact *target, NC_STACK_ypabact *currentHit) const
{
    if ( !target || target == this || target == _mislEmitter || target == currentHit )
        return false;

    if ( !_world || target->getBACT_pWorld() != _world )
        return false;

    if ( _owner == World::OWNER_0 || _owner == World::OWNER_7 ||
         target->_owner == World::OWNER_0 || target->_owner == World::OWNER_7 )
        return false;

    if ( !_mislChainAllowFriendly && target->_owner == _owner )
        return false;

    if ( target->_isDummy )
        return false;

    switch ( target->_bact_type )
    {
    case BACT_TYPES_BACT:
    case BACT_TYPES_TANK:
    case BACT_TYPES_ROBO:
    case BACT_TYPES_FLYER:
    case BACT_TYPES_UFO:
    case BACT_TYPES_CAR:
    case BACT_TYPES_GUN:
        break;

    default:
        return false;
    }

    if ( target->_bact_type == BACT_TYPES_MISSLE ||
         target->_status == BACT_STATUS_DEAD ||
         target->_status == BACT_STATUS_CREATE ||
         target->_status == BACT_STATUS_BEAM ||
         target->_energy <= 0 ||
         target->_energy_max <= 0 ||
         target->IsDestroyed() ||
         (target->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER)) )
        return false;

    if ( target->_bact_type == BACT_TYPES_GUN && target->GetEffectiveShield() >= 100.0f )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(target);
        if ( !gun || (gun->IsRoboGun() && !target->_isUnitGunChild) )
            return false;
    }

    return !IsChainHit(target);
}

NC_STACK_ypabact *NC_STACK_ypamissile::FindNextChainTarget(NC_STACK_ypabact *currentHit) const
{
    if ( !_world || !currentHit )
        return NULL;

    if ( _vehicleID < 0 || (size_t)_vehicleID >= _world->GetWeaponsProtos().size() )
        return NULL;

    const World::TWeapProto &wproto = _world->GetWeaponsProtos().at(_vehicleID);
    if ( wproto.chain.radius <= 0.0 )
        return NULL;

    Common::Point centerCell = World::PositionToSectorID(currentHit->_position);
    int cellRadius = (int)ceil(wproto.chain.radius / World::CVSectorLength) + 1;
    float radiusSq = wproto.chain.radius * wproto.chain.radius;
    float bestScore = radiusSq + 1.0f;
    NC_STACK_ypabact *bestTarget = NULL;

    for ( int dy = -cellRadius; dy <= cellRadius; dy++ )
    {
        for ( int dx = -cellRadius; dx <= cellRadius; dx++ )
        {
            Common::Point cellId = centerCell + Common::Point(dx, dy);

            if ( !_world->IsSector(cellId) )
                continue;

            cellArea &cell = _world->SectorAt(cellId);

            for ( NC_STACK_ypabact *candidate : cell.unitsList )
            {
                if ( !CanChainToTarget(candidate, currentHit) )
                    continue;

                vec3d delta = candidate->_position - currentHit->_position;
                float distSq = delta.dot(delta);

                if ( distSq > radiusSq || distSq >= bestScore )
                    continue;

                bestScore = distSq;
                bestTarget = candidate;
            }
        }
    }

    return bestTarget;
}

bool NC_STACK_ypamissile::SpawnChainProjectile(const vec3d &originPos, float originRadius, NC_STACK_ypabact *nextTarget, int childEnergy)
{
    if ( !_world || !_mislEmitter || !nextTarget || childEnergy <= 0 )
        return false;

    if ( _vehicleID < 0 || (size_t)_vehicleID >= _world->GetWeaponsProtos().size() )
        return false;

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(_vehicleID);

    if ( !wproto.chain.allow || wproto.IsLaser() || wproto.IsArtilleryShell() ||
         wproto.IsKamikaze() ||
         !(wproto._weaponFlags & World::TWeapProto::WEAPON_FLAG_PROJECTILE) )
        return false;

    vec3d chainDir = nextTarget->_position - originPos;
    float chainDirLen = chainDir.length();
    if ( chainDirLen <= 0.001 )
        return false;

    chainDir /= chainDirLen;

    float launchOffset = originRadius + wproto.radius + 1.0f;
    if ( launchOffset < 1.0f )
        launchOffset = 1.0f;

    ypaworld_arg146 arg147;
    arg147.vehicle_id = _vehicleID;
    arg147.pos = originPos + chainDir * launchOffset;

    NC_STACK_ypamissile *child = _world->ypaworld_func147(&arg147);
    if ( !child )
        return false;

    child->SetLauncherBact(_mislEmitter);
    child->SetStartHeight(arg147.pos.y);
    child->_owner = _owner;
    child->_host_station = _host_station;
    child->_energy = childEnergy;
    child->_energy_max = childEnergy;
    child->_fly_dir = chainDir;
    if ( wproto.IsArcGrenade() )
    {
        child->SetupArcGrenadeLaunch(wproto.grenade_arc_angle,
                                     wproto.grenade_arc_gravity,
                                     wproto.start_speed);
    }
    else
    {
        child->_fly_dir_length = wproto.start_speed;

        if ( !(wproto._weaponFlags & 0x12) )
            child->_fly_dir_length *= 0.2;

        child->_rotation.SetZ(child->_fly_dir);
        child->_rotation.SetX(_rotation.AxisX());
        child->_rotation.SetY(child->_rotation.AxisZ() * child->_rotation.AxisX());
    }
    child->StartWeaponTracer();

    _world->SpawnTransientVisual(wproto.vp_launch, wproto.visual_3ds.launch,
                                 wproto.visual_base.launch,
                                 child->_position, child->_rotation, 1000,
                                 1.0, World::TVisualTint(), wproto.launch_scale);

    child->_kidRef.Detach();
    child->_parent = NULL;
    _mislEmitter->_missiles_list.push_back(child);

    child->_mislChainDepth = _mislChainDepth + 1;
    child->_mislChainEnergy = childEnergy;
    child->_mislChainHitGids = _mislChainHitGids;
    child->_mislChainSpawned = false;
    child->_mislChainAllowFriendly = _mislChainAllowFriendly;

    if ( child->GetMissileType() == MISL_TARGETED || wproto.IsHomingBomb() )
    {
        setTarget_msg arg67;
        arg67.tgt.pbact = nextTarget;
        arg67.tgt_type = BACT_TGT_TYPE_UNIT;
        arg67.priority = 0;
        arg67.tgt_pos = nextTarget->_position;

        child->SetTarget(&arg67);
    }
    else
    {
        child->_primTtype = BACT_TGT_TYPE_DRCT;
        child->_target_dir = child->_fly_dir;
    }

    SFXEngine::SFXe.startSound(&child->_soundcarrier, World::TWeapProto::SND_LAUNCH);

    return true;
}

void NC_STACK_ypamissile::UpdatePendingChainJump(update_msg *arg)
{
    if ( !_mislChainPending )
        return;

    if ( !arg )
        return;

    _mislChainPendingElapsed += arg->frameTime;
    if ( _mislChainPendingElapsed < _mislChainPendingDelay )
        return;

    _mislChainPending = false;

    if ( !_world || !_mislChainPendingTargetGid || _mislChainPendingEnergy <= 0 )
        return;

    NC_STACK_ypabact *target = ypamissile_FindLiveBactByGid(_world->_unitsList, _mislChainPendingTargetGid);
    if ( !CanChainToTarget(target, NULL) )
        return;

    SpawnChainProjectile(_mislChainPendingOrigin, _mislChainPendingOriginRadius, target, _mislChainPendingEnergy);
}

void NC_STACK_ypamissile::TrySpawnChainProjectile(NC_STACK_ypabact *currentHit, int appliedDamage)
{
    if ( currentHit && !_mislChainAllowFriendly && _mislChainDepth == 0 && currentHit->_owner == _owner &&
         _mislEmitter && (_mislEmitter->getBACT_inputting() || _mislEmitter->getBACT_viewer()) )
    {
        _mislChainAllowFriendly = true;
    }

    if ( _mislChainSpawned || appliedDamage <= 0 || !CanChainToTarget(currentHit, NULL) )
        return;

    if ( !_world || _vehicleID < 0 || (size_t)_vehicleID >= _world->GetWeaponsProtos().size() )
        return;

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(_vehicleID);
    const World::TWeaponChainConfig &chain = wproto.chain;

    if ( !chain.allow || chain.max_jumps <= 0 || chain.radius <= 0.0 || chain.damage_mult <= 0.0 )
        return;

    if ( wproto.IsLaser() || wproto.IsArtilleryShell() || wproto.IsKamikaze() )
        return;

    if ( !(wproto._weaponFlags & World::TWeapProto::WEAPON_FLAG_PROJECTILE) )
        return;

    if ( _mislChainDepth >= chain.max_jumps )
        return;

    int currentEnergy = _mislChainEnergy > 0 ? _mislChainEnergy : _energy;
    if ( currentEnergy <= 0 )
        return;

    RememberChainHit(currentHit);

    NC_STACK_ypabact *nextTarget = FindNextChainTarget(currentHit);
    if ( !nextTarget )
        return;

    int childEnergy = (int)((float)currentEnergy * chain.damage_mult);
    if ( childEnergy <= 0 )
        return;

    _mislChainEnergy = currentEnergy;
    if ( chain.jump_delay <= 0 )
    {
        _mislChainSpawned = SpawnChainProjectile(currentHit->_position, currentHit->_radius, nextTarget, childEnergy);
        return;
    }

    _mislChainSpawned = true;
    _mislChainPending = true;
    _mislChainPendingElapsed = 0;
    _mislChainPendingDelay = chain.jump_delay;
    _mislChainPendingTargetGid = nextTarget->_gid;
    _mislChainPendingEnergy = childEnergy;
    _mislChainPendingOrigin = currentHit->_position;
    _mislChainPendingOriginRadius = currentHit->_radius;

    if ( _yls_time < _mislChainPendingDelay + 100 )
        setBACT_yourLastSeconds(_mislChainPendingDelay + 100);
}

bool NC_STACK_ypamissile::TubeCollisionTest(bool applyDirectDamage, NC_STACK_ypabact **hitTarget)
{
    _mislDirectHitUnits.clear();
    if ( hitTarget )
        *hitTarget = NULL;

    vec3d collisionSumPosition(0.0, 0.0, 0.0);
    int collisionCount = 0;
    float collisionSumRadius = 0.0;

    bool a5 = _mislEmitter->getBACT_inputting();

    if ( !a5 )
        a5 = getBACT_viewer();

    yw_130arg arg130;
    arg130.pos_x = _old_pos.x;
    arg130.pos_z = _old_pos.z;
    _world->GetSectorInfo(&arg130);

    cellArea *pCells[3];

    pCells[0] = arg130.pcell;

    arg130.pos_x = _position.x;
    arg130.pos_z = _position.z;
    _world->GetSectorInfo(&arg130);

    pCells[2] = arg130.pcell;

    if ( arg130.pcell == pCells[0] )
    {
        pCells[1] = pCells[0];
    }
    else
    {
        arg130.pos_x = (_position.x - _old_pos.x) * 0.5 + _old_pos.x;
        arg130.pos_z = (_position.z - _old_pos.z) * 0.5 + _old_pos.z;
        _world->GetSectorInfo(&arg130);

        pCells[1] = arg130.pcell;
    }

    for (int i = 0; i < 3; i++)
    {
        if ( i == 0 || pCells[i] != pCells[i - 1] )
        {
            if (pCells[i] == NULL)
                ypa_log_out("ypamissile_func70__sub0 NULL sector i = %d, 621: %f %f 62D: %f %f \n", i, _position.x, _position.z, _old_pos.x, _old_pos.z);

            for ( NC_STACK_ypabact* &bct : pCells[i]->unitsList )
            {
                if ( bct == this || bct == _mislEmitter || bct->_status == BACT_STATUS_DEAD )
                    continue;

                if ( IsArmorPenetratedTarget(bct) )
                    continue;

                if ( bct->_bact_type == BACT_TYPES_MISSLE )
                {
                    NC_STACK_ypamissile *otherMissile = dynamic_cast<NC_STACK_ypamissile *>( bct );

                    if ( CanCollideWithWeapon(otherMissile) )
                    {
                        if ( hitTarget )
                            *hitTarget = otherMissile;

                        return true;
                    }

                    continue;
                }

                if (bct->_bact_type == BACT_TYPES_GUN && bct->GetEffectiveShield() >= 100.0f)
                {
                    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( bct );

                    if ( gun->IsRoboGun() )
                        continue;
                }

                // Preserve the legacy/upstream AI friendly-collision rule for normal
                // projectiles. Artillery still detects terminal contact with same-owner
                // units so its explosion is unchanged; ApplyDamageToBact() suppresses
                // only the resulting artillery damage to those units.
                if ( !_isArtilleryShellProjectile && !a5 &&
                     bct->_owner == _mislEmitter->_owner )
                    continue;

                if ( _mislEmitter->_bact_type == BACT_TYPES_GUN )
                {
                    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( _mislEmitter );

                    if (bct->_owner == _owner)
                    {
                        if (gun->IsRoboGun() && !_mislEmitter->_isUnitGunChild)
                        {
                            if (bct->_bact_type == BACT_TYPES_ROBO)
                                continue;

                            if (bct->_bact_type == BACT_TYPES_GUN )
                            {
                                NC_STACK_ypagun *bgun = dynamic_cast<NC_STACK_ypagun *>( bct );

                                if (bgun->IsRoboGun() && !bct->_isUnitGunChild)
                                    continue;
                            }
                        }
                    }
                }

                // Artillery uses its own parametric trajectory and impact point; the
                // legacy bomb start-height gate is unrelated to that trajectory and
                // would incorrectly make valid artillery unit hits disappear.
                if ( !_isArtilleryShellProjectile && _mislType == MISL_BOMB &&
                     bct->_position.y < _mislStartHeight )
                    continue;

                std::vector<TCollisionSphereWorld> targetSpheres;
                bct->GetCollisionSpheres(
                    targetSpheres, bct->_position, bct->_rotation, false);
                if ( targetSpheres.empty() )
                    continue;

                auto legacyWeaponRadiusForTarget =
                    [this](NC_STACK_ypabact *target) -> float
                {
                    float radius = 0.0f;
                    switch ( target->_bact_type )
                    {
                    case BACT_TYPES_BACT:
                        radius = _mislRadiusHeli;
                        break;
                    case BACT_TYPES_TANK:
                    case BACT_TYPES_CAR:
                        radius = _mislRadiusTank;
                        break;
                    case BACT_TYPES_FLYER:
                    case BACT_TYPES_UFO:
                        radius = _mislRadiusFlyer;
                        break;
                    case BACT_TYPES_ROBO:
                        radius = _mislRadiusRobo;
                        break;
                    default:
                        radius = _radius;
                        break;
                    }

                    return radius == 0.0f ? _radius : radius;
                };

                bool collided = false;
                bool penetrated = false;

                if ( HasManualCompoundCollision() )
                {
                    std::vector<TCollisionSphereWorld> oldWeaponSpheres;
                    std::vector<TCollisionSphereWorld> newWeaponSpheres;
                    const mat3x3 &oldRotation =
                        _collisionOldRotationValid ? _collisionOldRotation : _rotation;

                    GetCollisionSpheres(
                        oldWeaponSpheres, _old_pos, oldRotation, false);
                    GetCollisionSpheres(
                        newWeaponSpheres, _position, _rotation, false);

                    const size_t sphereCount =
                        std::min(oldWeaponSpheres.size(), newWeaponSpheres.size());

                    for (size_t wi = 0; wi < sphereCount && !collided; ++wi)
                    {
                        const float weaponRadius =
                            newWeaponSpheres[wi].legacy
                                ? legacyWeaponRadiusForTarget(bct)
                                : newWeaponSpheres[wi].radius;

                        if ( weaponRadius <= 0.0f )
                            continue;

                        for (const TCollisionSphereWorld &targetSphere :
                             targetSpheres)
                        {
                            const float radiusSum =
                                weaponRadius + targetSphere.radius;
                            const float distanceSq =
                                ypamissile_SegmentSegmentDistanceSq(
                                    oldWeaponSpheres[wi].center,
                                    newWeaponSpheres[wi].center,
                                    targetSphere.center,
                                    targetSphere.center);

                            if ( distanceSq > radiusSum * radiusSum )
                                continue;

                            if ( applyDirectDamage &&
                                 ShouldArmorPenetrateTarget(bct) )
                            {
                                ApplyDirectHitToBact(bct);
                                RememberArmorPenetratedTarget(bct);
                                _mislArmorPenetrationRemaining--;
                                ApplyArmorPenetrationUnitImpactFX();
                                penetrated = true;
                                collided = true;
                                break;
                            }

                            collisionSumRadius += targetSphere.radius;
                            collisionCount++;
                            collisionSumPosition += bct->_position;

                            if ( hitTarget && !*hitTarget )
                                *hitTarget = bct;

                            if ( applyDirectDamage )
                                ApplyDirectHitToBact(bct);

                            collided = true;
                            break;
                        }
                    }
                }
                else
                {
                    const float weaponRadius =
                        legacyWeaponRadiusForTarget(bct);

                    for (auto it = targetSpheres.rbegin();
                         it != targetSpheres.rend() && !collided; ++it)
                    {
                        const TCollisionSphereWorld &targetSphere = *it;
                        vec3d to_enemy = targetSphere.center - _old_pos;
                        vec3d dist_vect = _position - _old_pos;

                        if ( to_enemy.dot(_rotation.AxisZ()) < 0.3 )
                            continue;

                        const float dist_vect_len = dist_vect.normalise();
                        const vec3d vp = dist_vect * to_enemy;
                        const float vp_len = vp.length();
                        const float to_enemy_len = to_enemy.length();

                        if ( targetSphere.radius + weaponRadius > vp_len &&
                             sqrt(POW2(dist_vect_len) + POW2(vp_len)) >
                                 fabs(to_enemy_len - weaponRadius) )
                        {
                            if ( applyDirectDamage &&
                                 ShouldArmorPenetrateTarget(bct) )
                            {
                                ApplyDirectHitToBact(bct);
                                RememberArmorPenetratedTarget(bct);
                                _mislArmorPenetrationRemaining--;
                                ApplyArmorPenetrationUnitImpactFX();
                                penetrated = true;
                                collided = true;
                                break;
                            }

                            collisionSumRadius += targetSphere.radius;
                            collisionCount++;
                            collisionSumPosition += bct->_position;

                            if ( hitTarget && !*hitTarget )
                                *hitTarget = bct;

                            if ( applyDirectDamage )
                                ApplyDirectHitToBact(bct);

                            collided = true;
                        }
                    }
                }

                if ( penetrated )
                    continue;
            }
        }
    }

    if ( collisionCount > 0 )
    {
        // Set new position between collided objects
        _position = collisionSumPosition / (float)collisionCount;

        collisionSumRadius /= (float)collisionCount;

        if ( collisionSumRadius >= 50.0 )
        {
            vec3d posDelta = _position - _old_pos;
            float deltaLen = posDelta.length();

            if ( deltaLen < 1.0 )
                deltaLen = 1.0;

            _position -= (posDelta / deltaLen) * collisionSumRadius;
        }
    }

    return collisionCount > 0;
}

int NC_STACK_ypamissile::CalcDamageForBact(NC_STACK_ypabact *bct, int baseEnergy)
{
    if ( !bct || baseEnergy <= 0 )
        return 0;

    int damage = 0;

    switch ( bct->_bact_type )
    {
    case BACT_TYPES_BACT:
        damage = baseEnergy * _mislEnergyHeli;
        break;

    case BACT_TYPES_TANK:
    case BACT_TYPES_CAR:
        damage = baseEnergy * _mislEnergyTank;
        break;

    case BACT_TYPES_FLYER:
    case BACT_TYPES_UFO:
        damage = baseEnergy * _mislEnergyFlyer;
        break;

    case BACT_TYPES_ROBO:
        damage = baseEnergy * _mislEnergyRobo;
        break;

    default:
        damage = baseEnergy;
        break;
    }

    // OpenNeoUA: fine-grained energy_* overrides preserve the exact vanilla
    // heli/tank/flyer/robo result above when absent. Like the legacy values,
    // these were snapshotted when the projectile was created.
    float specificEnergy = 1.0f;
    if ( TryGetSpecificEnergyForTarget(bct, &specificEnergy) )
        damage = (int)((float)baseEnergy * specificEnergy);

    float shieldedDamage = damage * (100.0f - bct->GetEffectiveShield());
    float divisor = ( bct->getBACT_inputting() || bct->getBACT_viewer() ) ? 250.0 : 100.0;

    return ceil(shieldedDamage / divisor);
}

int NC_STACK_ypamissile::ApplyDamageToBact(NC_STACK_ypabact *bct, int baseEnergy)
{
    if ( !bct )
        return 0;

    // Artillery keeps its normal terminal collision and explosion, but never
    // damages units owned by the shell's emitter. This central unit-damage
    // guard covers both the direct-hit and AoE paths without affecting enemy
    // units, buildings, sectors, or other projectile models.
    if ( _isArtilleryShellProjectile && _mislEmitter &&
         bct->_owner == _mislEmitter->_owner )
        return 0;

    NC_STACK_ypabact *userHost = _world ? _world->getYW_userHostStation() : NULL;

    if ( _world && userHost && !(userHost->_owner == _owner || !_world->_isNetGame) )
        return 0;

    World::TWeapProto *wproto = NULL;
    if ( _world && _vehicleID >= 0 && (size_t)_vehicleID < _world->GetWeaponsProtos().size() )
        wproto = &_world->GetWeaponsProtos().at(_vehicleID);

    bool preAppliedDebuff = false;
    if ( wproto && wproto->debuff.allow && wproto->debuff.shield_malus > 0.0f &&
         bct->_energy > 0 && bct->_status != BACT_STATUS_DEAD )
    {
        bct->ApplyDebuff(wproto->debuff, _mislEmitter);
        preAppliedDebuff = true;
    }

    int damage = CalcDamageForBact(bct, baseEnergy);

    if ( !damage )
        return 0;

    bact_arg84 arg84;
    arg84.energy = -damage;
    arg84.unit = _mislEmitter;

    bct->ModifyEnergy(&arg84);

    if ( wproto && wproto->debuff.allow && !preAppliedDebuff && bct->_energy > 0 && bct->_status != BACT_STATUS_DEAD )
        bct->ApplyDebuff(wproto->debuff, _mislEmitter);

    return damage;
}

void NC_STACK_ypamissile::ApplyDirectHitToBact(NC_STACK_ypabact *bct, bool applyDamage)
{
    if ( !bct )
        return;

    bct->_status_flg &= ~BACT_STFLAG_LAND;
    RememberDirectHitUnit(bct);

    NC_STACK_ypabact *pushRecipient = ypamissile_ResolveDirectPushRecipient(bct);
    bool wasAlive = pushRecipient == bct && ypamissile_IsAliveForDeathPush(bct);
    vec3d pushDir(0.0, 0.0, 0.0);
    float pushStrength = 0.0f;
    bool hasDirectPush = !IsDirectPushRecipient(pushRecipient) &&
                         ApplyDirectPushToBact(pushRecipient, &pushDir, &pushStrength,
                                               false, bct);

    int appliedDamage = applyDamage ? ApplyDamageToBact(bct, _energy) : 0;
    if ( hasDirectPush )
    {
        bool diedNow = pushRecipient == bct && applyDamage && wasAlive &&
                       !(bct->_status_flg & BACT_STFLAG_DEATH2) &&
                       (bct->_status_flg & BACT_STFLAG_DEATH1);
        if ( diedNow )
            pushStrength *= ypamissile_GetPushAtDeathMultiplier();

        pushRecipient->ApplyConfiguredPush(pushDir, pushStrength);
        RememberDirectPushRecipient(pushRecipient);
    }

    if ( applyDamage )
        TrySpawnChainProjectile(bct, appliedDamage);
}

bool NC_STACK_ypamissile::IsArmorPenetratedTarget(NC_STACK_ypabact *bct) const
{
    if ( !bct )
        return false;

    return std::find(_mislArmorPenetratedGids.begin(), _mislArmorPenetratedGids.end(), bct->_gid) != _mislArmorPenetratedGids.end();
}

bool NC_STACK_ypamissile::ShouldArmorPenetrateTarget(NC_STACK_ypabact *bct) const
{
    return _mislArmorPenetrationRemaining > 0 &&
           bct &&
           bct->_bact_type != BACT_TYPES_MISSLE &&
           !IsArmorPenetratedTarget(bct);
}

void NC_STACK_ypamissile::RememberArmorPenetratedTarget(NC_STACK_ypabact *bct)
{
    if ( !bct || IsArmorPenetratedTarget(bct) )
        return;

    _mislArmorPenetratedGids.push_back(bct->_gid);
}

void NC_STACK_ypamissile::ApplyArmorPenetrationUnitImpactFX()
{
    SFXEngine::SFXe.startSound(&_soundcarrier, World::TWeapProto::SND_HIT);
    StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_DETONATE);
    StartDestFXByType(World::DestFX::FX_DEATH);
}

bool NC_STACK_ypamissile::ApplyDirectPushToBact(NC_STACK_ypabact *bct, vec3d *appliedDir,
                                                  float *appliedStrength, bool enqueue,
                                                  NC_STACK_ypabact *directionTarget)
{
    if ( !bct || !isfinite(_mislDirectPush) || _mislDirectPush <= 0 )
        return false;

    if ( GetAreaPushSkipReason(bct) )
        return false;

    // A transferred GUN hit keeps the original impact geometry for direction,
    // while eligibility, resistance and the physical impulse belong to the parent.
    NC_STACK_ypabact *pushDirectionTarget = directionTarget ? directionTarget : bct;
    vec3d fallbackDir = pushDirectionTarget->_position - _position;
    vec3d pushDir;
    if ( !ypamissile_GetDirectPushDir(this, pushDirectionTarget, fallbackDir, &pushDir) )
        return false;

    float pushStrength = _mislDirectPush * ypamissile_GetTargetPushMultiplier(bct);
    if ( pushStrength <= 0.0f )
        return false;

    if ( enqueue )
        bct->ApplyConfiguredPush(pushDir, pushStrength);

    if ( appliedDir )
        *appliedDir = pushDir;

    if ( appliedStrength )
        *appliedStrength = pushStrength;

    return true;
}

const char *NC_STACK_ypamissile::GetAreaDamageSkipReason(NC_STACK_ypabact *bct, bool allowFriendly) const
{
    if ( !bct || bct == this || bct == _mislEmitter )
    {
        if ( !bct )
            return "null";
        if ( bct == this )
            return "projectile_self";
        return "emitter";
    }

    if ( bct->_bact_type == BACT_TYPES_MISSLE )
        return "missile";

    if ( bct->_status == BACT_STATUS_DEAD )
        return "dead";

    if ( bct->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2) )
        return "death_fx";

    if (bct->_bact_type == BACT_TYPES_GUN && bct->GetEffectiveShield() >= 100.0f)
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( bct );

        if ( gun && gun->IsRoboGun() )
            return "shielded_robo_gun";
    }

    if ( _mislEmitter && !allowFriendly && bct->_owner == _mislEmitter->_owner )
        return "friendly";

    if ( _mislEmitter && _mislEmitter->_bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( _mislEmitter );

        if ( gun && bct->_owner == _owner && gun->IsRoboGun() && !_mislEmitter->_isUnitGunChild )
        {
            if ( bct->_bact_type == BACT_TYPES_ROBO )
                return "own_robo_gun_robo";

            if ( bct->_bact_type == BACT_TYPES_GUN )
            {
                NC_STACK_ypagun *bgun = dynamic_cast<NC_STACK_ypagun *>( bct );

                if ( bgun && bgun->IsRoboGun() && !bct->_isUnitGunChild )
                    return "own_robo_gun";
            }
        }
    }

    // The bomb start-height rule belongs to the legacy physical bomb path.
    // Artillery has a dedicated ballistic/vertical trajectory and must evaluate
    // unit AoE around its actual timed impact point instead.
    if ( !_isArtilleryShellProjectile && _mislType == MISL_BOMB &&
         bct->_position.y < _mislStartHeight )
        return "bomb_below_start_height";

    return NULL;
}

// Push eligibility filter for normal direct/AoE push. Static guns and final
// DEATH2 wrecks are excluded here; owner policy is applied by the collision/AoE
// call sites so player-controlled legacy behavior remains unchanged.
const char *NC_STACK_ypamissile::GetAreaPushSkipReason(NC_STACK_ypabact *bct) const
{
    if ( !bct || bct == this || bct == _mislEmitter )
        return "self";

    // OpenNeoUA custom: all flak/turret actors use model = gun / BACT_TYPES_GUN.
    // They are static defenses and should never be knocked away by weapon push.
    if ( !bct->CanReceiveConfiguredPush() )
        return "gun";

    // Final death-FX wreck: do not disturb (same guard as legacy Impact()).
    if ( bct->_status_flg & BACT_STFLAG_DEATH2 )
        return "death2";

    return NULL;
}

bool NC_STACK_ypamissile::CanCollideWithWeapon(
        NC_STACK_ypamissile *other)
{
    if ( !System::IniConf::GameWeaponWeaponCollision.Get<bool>() )
        return false;

    if ( !other || other == this || other == _mislEmitter )
        return false;

    if ( _owner == other->_owner )
        return false;

    if ( other->_mislEmitter == this )
        return false;

    if ( _mislClusterAge < 150 || other->_mislClusterAge < 150 )
        return false;

    if ( other->_mislEmitter == _mislEmitter &&
         (_mislClusterAge < 300 || other->_mislClusterAge < 300) )
        return false;

    if ( _status == BACT_STATUS_DEAD ||
         other->_status == BACT_STATUS_DEAD )
        return false;

    if ( (_status_flg | other->_status_flg) &
         (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2) )
        return false;

    if ( !_world || other->getBACT_pWorld() != _world )
        return false;

    // Exact legacy branch: when neither Weapon uses coll_*, keep the original
    // centre-line swept test and generic Weapon radius unchanged.
    if ( !HasManualCompoundCollision() &&
         !other->HasManualCompoundCollision() )
    {
        const float radiusSum = _radius + other->_radius;
        if ( radiusSum <= 0.0f )
            return false;

        const float distSq = ypamissile_SegmentSegmentDistanceSq(
            _old_pos, _position, other->_old_pos, other->_position);
        return distSq <= radiusSum * radiusSum;
    }

    std::vector<TCollisionSphereWorld> selfOld;
    std::vector<TCollisionSphereWorld> selfNow;
    std::vector<TCollisionSphereWorld> otherOld;
    std::vector<TCollisionSphereWorld> otherNow;

    const mat3x3 &selfOldRotation =
        _collisionOldRotationValid ? _collisionOldRotation : _rotation;
    const mat3x3 &otherOldRotation =
        other->_collisionOldRotationValid
            ? other->_collisionOldRotation
            : other->_rotation;

    GetCollisionSpheres(
        selfOld, _old_pos, selfOldRotation, false);
    GetCollisionSpheres(
        selfNow, _position, _rotation, false);
    other->GetCollisionSpheres(
        otherOld, other->_old_pos, otherOldRotation, false);
    other->GetCollisionSpheres(
        otherNow, other->_position, other->_rotation, false);

    const size_t selfCount = std::min(selfOld.size(), selfNow.size());
    const size_t otherCount = std::min(otherOld.size(), otherNow.size());

    for (size_t i = 0; i < selfCount; ++i)
    {
        for (size_t j = 0; j < otherCount; ++j)
        {
            const float radiusSum =
                selfNow[i].radius + otherNow[j].radius;
            if ( radiusSum <= 0.0f )
                continue;

            const float distSq = ypamissile_SegmentSegmentDistanceSq(
                selfOld[i].center, selfNow[i].center,
                otherOld[j].center, otherNow[j].center);
            if ( distSq <= radiusSum * radiusSum )
                return true;
        }
    }

    return false;
}

void NC_STACK_ypamissile::DetonateWeaponCollision(NC_STACK_ypamissile *other)
{
    if ( !other || other == this || _status == BACT_STATUS_DEAD )
        return;

    if ( other->_status != BACT_STATUS_DEAD )
    {
        other->Impact();

        setState_msg otherState;
        otherState.unsetFlags = 0;
        otherState.setFlags = 0;
        otherState.newStatus = BACT_STATUS_DEAD;
        other->SetState(&otherState);
    }

    Impact();

    setState_msg selfState;
    selfState.unsetFlags = 0;
    selfState.setFlags = 0;
    selfState.newStatus = BACT_STATUS_DEAD;
    SetState(&selfState);
}

bool NC_STACK_ypamissile::IsDirectHitUnit(NC_STACK_ypabact *bct) const
{
    if ( !bct )
        return false;

    if ( IsArmorPenetratedTarget(bct) )
        return true;

    return std::find(_mislDirectHitUnits.begin(), _mislDirectHitUnits.end(), bct) != _mislDirectHitUnits.end();
}

void NC_STACK_ypamissile::RememberDirectHitUnit(NC_STACK_ypabact *bct)
{
    if ( bct && !IsDirectHitUnit(bct) )
        _mislDirectHitUnits.push_back(bct);
}

bool NC_STACK_ypamissile::IsDirectPushRecipient(NC_STACK_ypabact *bct) const
{
    if ( !bct )
        return false;

    return std::find(_mislDirectPushRecipientGids.begin(),
                     _mislDirectPushRecipientGids.end(),
                     bct->_gid) != _mislDirectPushRecipientGids.end();
}

void NC_STACK_ypamissile::RememberDirectPushRecipient(NC_STACK_ypabact *bct)
{
    if ( bct && !IsDirectPushRecipient(bct) )
        _mislDirectPushRecipientGids.push_back(bct->_gid);
}

vec3d NC_STACK_ypamissile::GetBuildingSlotCenter(const cellArea &cell, int bldX, int bldY) const
{
    vec3d pos = World::SectorIDToCenterPos3(cell.CellId);
    pos.y = cell.height;

    if ( cell.SectorType != 1 )
    {
        pos.x += (bldX - 1) * 300.0;
        pos.z += (bldY - 1) * 300.0;
    }

    return pos;
}

bool NC_STACK_ypamissile::GetBuildingSlotAtPosition(const vec3d &pos, Common::Point *cellId, int *bldX, int *bldY) const
{
    Common::Point sec = World::PositionToSectorID(pos);

    if ( !_world || !_world->IsSector(sec) )
        return false;

    cellArea &cell = _world->SectorAt(sec);

    if ( !cell.IsGamePlaySector() || cell.PurposeType == cellArea::PT_NONE || cell.PurposeType == cellArea::PT_CONSTRUCTING )
        return false;

    int outX = 0;
    int outY = 0;

    if ( cell.SectorType != 1 )
    {
        int sx = (int)(pos.x / 150.0) % 8;
        int sy = (int)(-pos.z / 150.0) % 8;

        int xSlot = sx < 3 ? 1 : (sx < 5 ? 2 : 3);
        int ySlot = sy < 3 ? 1 : (sy < 5 ? 2 : 3);

        outX = xSlot - 1;
        outY = 2 - (ySlot - 1);
    }

    if ( cell.buildings_health.At(outX, outY) <= 0 )
        return false;

    if ( cellId )
        *cellId = sec;
    if ( bldX )
        *bldX = outX;
    if ( bldY )
        *bldY = outY;

    return true;
}

const char *NC_STACK_ypamissile::GetAreaBuildingSkipReason(const cellArea &cell, int bldX, int bldY) const
{
    if ( !cell.IsGamePlaySector() || cell.PurposeType == cellArea::PT_NONE || cell.PurposeType == cellArea::PT_CONSTRUCTING )
    {
        if ( !cell.IsGamePlaySector() )
            return "non_gameplay_sector";
        if ( cell.PurposeType == cellArea::PT_NONE )
            return "no_functional_building";
        return "constructing";
    }

    if ( cell.buildings_health.Get(bldX, bldY) <= 0 )
        return "destroyed_slot";

    return NULL;
}

bool NC_STACK_ypamissile::IsDirectHitBuilding(const Common::Point &cellId, int bldX, int bldY) const
{
    for ( const TBuildingHitRef &hit : _mislDirectHitBuildings )
    {
        if ( hit.cellId == cellId && hit.bldX == bldX && hit.bldY == bldY )
            return true;
    }

    return false;
}

void NC_STACK_ypamissile::RememberDirectHitBuildingAt(const vec3d &pos)
{
    TBuildingHitRef hit;

    if ( !GetBuildingSlotAtPosition(pos, &hit.cellId, &hit.bldX, &hit.bldY) )
        return;

    if ( !IsDirectHitBuilding(hit.cellId, hit.bldX, hit.bldY) )
        _mislDirectHitBuildings.push_back(hit);
}

bool NC_STACK_ypamissile::GetSectorSlotAtPosition(const vec3d &pos, Common::Point *cellId, int *bldX, int *bldY) const
{
    Common::Point sec = World::PositionToSectorID(pos);

    if ( !_world || !_world->IsSector(sec) )
        return false;

    cellArea &cell = _world->SectorAt(sec);

    if ( !cell.IsGamePlaySector() || cell.PurposeType != cellArea::PT_NONE )
        return false;

    int outX = 0;
    int outY = 0;

    if ( cell.SectorType != 1 )
    {
        int sx = (int)(pos.x / 150.0) % 8;
        int sy = (int)(-pos.z / 150.0) % 8;

        int xSlot = sx < 3 ? 1 : (sx < 5 ? 2 : 3);
        int ySlot = sy < 3 ? 1 : (sy < 5 ? 2 : 3);

        outX = xSlot - 1;
        outY = 2 - (ySlot - 1);
    }

    if ( cell.buildings_health.At(outX, outY) <= 0 )
        return false;

    if ( cellId )
        *cellId = sec;
    if ( bldX )
        *bldX = outX;
    if ( bldY )
        *bldY = outY;

    return true;
}

const char *NC_STACK_ypamissile::GetAreaSectorSkipReason(const cellArea &cell, int bldX, int bldY) const
{
    if ( !cell.IsGamePlaySector() )
        return "non_gameplay_sector";

    if ( cell.PurposeType != cellArea::PT_NONE )
        return "functional_building_layer";

    if ( cell.buildings_health.Get(bldX, bldY) <= 0 )
        return "destroyed_slot";

    return NULL;
}

bool NC_STACK_ypamissile::IsDirectHitSector(const Common::Point &cellId, int bldX, int bldY) const
{
    for ( const TBuildingHitRef &hit : _mislDirectHitSectors )
    {
        if ( hit.cellId == cellId && hit.bldX == bldX && hit.bldY == bldY )
            return true;
    }

    return false;
}

void NC_STACK_ypamissile::RememberDirectHitSectorAt(const vec3d &pos)
{
    TBuildingHitRef hit;

    if ( !GetSectorSlotAtPosition(pos, &hit.cellId, &hit.bldX, &hit.bldY) )
        return;

    if ( !IsDirectHitSector(hit.cellId, hit.bldX, hit.bldY) )
        _mislDirectHitSectors.push_back(hit);
}

void NC_STACK_ypamissile::ApplyAreaDamage()
{
    bool doAoeDamage = (_mislAoeUnitEnergy > 0);
    bool doAoePush   = isfinite(_mislAoeUnitPush) && _mislAoeUnitPush > 0;

    if ( !isfinite(_mislAoeUnitRadius) || _mislAoeUnitRadius <= 0.0 || (!doAoeDamage && !doAoePush) || !_world )
        return;

    // Artillery retains its normal blast target scan, including same-owner units,
    // so collision and explosion behavior are unchanged. ApplyDamageToBact()
    // suppresses artillery damage to those units; normal weapons retain the
    // existing player/AI friendly-fire policy unchanged.
    bool allowFriendly = _isArtilleryShellProjectile || getBACT_viewer();

    if ( _mislEmitter && _mislEmitter->getBACT_inputting() )
        allowFriendly = true;

    Common::Point impactCell = World::PositionToSectorID(_position);

    if ( !_world->IsSector(impactCell) )
        return;

    int cellRadius = (int)ceil(_mislAoeUnitRadius / World::CVSectorLength) + 1;
    std::vector<NC_STACK_ypabact *> damagedUnits;

    for ( int dy = -cellRadius; dy <= cellRadius; dy++ )
    {
        for ( int dx = -cellRadius; dx <= cellRadius; dx++ )
        {
            Common::Point cellId = impactCell + Common::Point(dx, dy);

            if ( !_world->IsSector(cellId) )
                continue;

            cellArea &cell = _world->SectorAt(cellId);

            for ( NC_STACK_ypabact *bct : cell.unitsList.safe_iter() )
            {
                const char *dmgSkip  = GetAreaDamageSkipReason(bct, allowFriendly);
                const char *pushSkip = doAoePush ? GetAreaPushSkipReason(bct) : "push_disabled";

                if ( doAoePush && !pushSkip && !allowFriendly && _mislEmitter &&
                     bct->_owner == _mislEmitter->_owner )
                    pushSkip = "friendly";

                if ( dmgSkip && pushSkip )
                    continue;

                if ( IsArmorPenetratedTarget(bct) )
                    continue;

                vec3d delta = bct->_position - _position;
                float distance = delta.length();

                if ( distance > _mislAoeUnitRadius )
                    continue;

                if ( std::find(damagedUnits.begin(), damagedUnits.end(), bct) != damagedUnits.end() )
                    continue;

                damagedUnits.push_back(bct);

                bool hasAoePush = false;
                bool wasAlive = false;
                vec3d appliedPushDir(0.0, 0.0, 0.0);
                float appliedPushStrength = 0.0f;

                const bool validDirectPush = isfinite(_mislDirectPush) && _mislDirectPush > 0;
                if ( doAoePush && !pushSkip &&
                     !(validDirectPush &&
                       (IsDirectHitUnit(bct) || IsDirectPushRecipient(bct))) )
                {
                    wasAlive = ypamissile_IsAliveForDeathPush(bct);

                    if ( distance > 1.0f )
                        appliedPushDir = delta / distance;
                    else
                        appliedPushDir = vec3d(1.0f, 0.0f, 0.0f);

                    appliedPushStrength =
                        _mislAoeUnitPush *
                        World::AoePushFalloffFactor(distance, _mislAoeUnitRadius,
                                                    _mislAoeFalloff != 0);

                    appliedPushStrength *= ypamissile_GetTargetPushMultiplier(bct);
                    hasAoePush = appliedPushStrength > 0.0f;
                }

                // AoE damage skips direct-hit units (they already received direct damage)
                // and anything the strict damage filter rejected. AoE push has its
                // own eligibility filter but follows the same AI-friendly policy.
                if ( doAoeDamage && !dmgSkip && !IsDirectHitUnit(bct) )
                {
                    int areaEnergy = ypamissile_ScaleAoeEnergy(_mislAoeUnitEnergy, ypamissile_AoeFalloffFactor(distance, _mislAoeUnitRadius, _mislAoeFalloff != 0));
                    if ( areaEnergy > 0 )
                        ApplyDamageToBact(bct, areaEnergy);
                }

                if ( hasAoePush )
                {
                    bool diedNow = wasAlive &&
                                   !(bct->_status_flg & BACT_STFLAG_DEATH2) &&
                                   (bct->_status_flg & BACT_STFLAG_DEATH1);
                    if ( diedNow )
                        appliedPushStrength *= ypamissile_GetPushAtDeathMultiplier();

                    bct->ApplyConfiguredPush(appliedPushDir, appliedPushStrength);
                }
            }
        }
    }
}

void NC_STACK_ypamissile::ApplyBuildingAreaDamage()
{
    if ( _mislAoeBuildingRadius <= 0.0 || _mislAoeBuildingEnergy <= 0 || !_world )
        return;

    if ( _mislFlags & FLAG_MISL_IGNOREBUILDS )
        return;

    if ( _world->_isNetGame && _world->_userRobo->_owner != _owner )
        return;

    Common::Point impactCell = World::PositionToSectorID(_position);

    if ( !_world->IsSector(impactCell) )
        return;

    int cellRadius = (int)ceil(_mislAoeBuildingRadius / World::CVSectorLength) + 1;

    for ( int dy = -cellRadius; dy <= cellRadius; dy++ )
    {
        for ( int dx = -cellRadius; dx <= cellRadius; dx++ )
        {
            Common::Point cellId = impactCell + Common::Point(dx, dy);

            if ( !_world->IsSector(cellId) )
                continue;

            cellArea &cell = _world->SectorAt(cellId);
            int slots = cell.SectorType == 1 ? 1 : 3;

            for ( int bldY = 0; bldY < slots; bldY++ )
            {
                for ( int bldX = 0; bldX < slots; bldX++ )
                {
                    if ( GetAreaBuildingSkipReason(cell, bldX, bldY) )
                        continue;

                    if ( IsDirectHitBuilding(cellId, bldX, bldY) )
                        continue;

                    vec3d bldPos = GetBuildingSlotCenter(cell, bldX, bldY);
                    float distance = (bldPos - _position).length();

                    if ( distance > _mislAoeBuildingRadius )
                        continue;

                    yw_arg129 arg129;
                    arg129.field_0 = 0;
                    arg129.pos = bldPos;
                    arg129.field_10 = ypamissile_ScaleAoeEnergy(_mislAoeBuildingEnergy, ypamissile_AoeFalloffFactor(distance, _mislAoeBuildingRadius, _mislAoeFalloff != 0));
                    if ( arg129.field_10 <= 0 )
                        continue;

                    arg129.unit = _mislEmitter;

                    ChangeSectorEnergy(&arg129);
                }
            }
        }
    }
}

void NC_STACK_ypamissile::ApplySectorAreaDamage()
{
    if ( _mislAoeSectorRadius <= 0.0 || _mislAoeSectorEnergy <= 0 || !_world )
        return;

    if ( _mislFlags & FLAG_MISL_IGNOREBUILDS )
        return;

    if ( _world->_isNetGame && _world->_userRobo->_owner != _owner )
        return;

    Common::Point impactCell = World::PositionToSectorID(_position);

    if ( !_world->IsSector(impactCell) )
        return;

    int cellRadius = (int)ceil(_mislAoeSectorRadius / World::CVSectorLength) + 1;

    for ( int dy = -cellRadius; dy <= cellRadius; dy++ )
    {
        for ( int dx = -cellRadius; dx <= cellRadius; dx++ )
        {
            Common::Point cellId = impactCell + Common::Point(dx, dy);

            if ( !_world->IsSector(cellId) )
                continue;

            cellArea &cell = _world->SectorAt(cellId);
            int slots = cell.SectorType == 1 ? 1 : 3;

            for ( int bldY = 0; bldY < slots; bldY++ )
            {
                for ( int bldX = 0; bldX < slots; bldX++ )
                {
                    if ( GetAreaSectorSkipReason(cell, bldX, bldY) )
                        continue;

                    if ( IsDirectHitSector(cellId, bldX, bldY) )
                        continue;

                    vec3d sectorPos = GetBuildingSlotCenter(cell, bldX, bldY);
                    float distance = (sectorPos - _position).length();

                    if ( distance > _mislAoeSectorRadius )
                        continue;

                    yw_arg129 arg129;
                    arg129.field_0 = 0;
                    arg129.pos = sectorPos;
                    arg129.field_10 = ypamissile_ScaleAoeEnergy(_mislAoeSectorEnergy, ypamissile_AoeFalloffFactor(distance, _mislAoeSectorRadius, _mislAoeFalloff != 0));
                    if ( arg129.field_10 <= 0 )
                        continue;

                    arg129.unit = _mislEmitter;

                    // Sector AoE uses the same path as direct hits on normal sector architecture.
                    ChangeSectorEnergy(&arg129);
                }
            }
        }
    }
}

void NC_STACK_ypamissile::AttachDelayedDetonationToTarget(NC_STACK_ypabact *target)
{
    if ( !target )
        return;

    _mislAttachedToTarget = true;
    _mislAttachTargetGid = target->_gid;
    _mislAttachOffset = _position - target->_position;
    _mislLastAttachedPosition = _position;
    _mislType = MISL_INTERNAL;
    _mislFlags |= FLAG_MISL_COUNTDELAY;
    _fly_dir_length = 0.0;
    _mislDirectPushRecipientGids.clear();
}

NC_STACK_ypabact *NC_STACK_ypamissile::FindAttachedTarget()
{
    if ( !_world || !_mislAttachedToTarget || !_mislAttachTargetGid )
        return NULL;

    return ypamissile_FindLiveBactByGid(_world->_unitsList, _mislAttachTargetGid);
}

void NC_STACK_ypamissile::UpdateAttachedDetonationPosition()
{
    NC_STACK_ypabact *target = FindAttachedTarget();

    if ( target )
    {
        _position = target->_position + _mislAttachOffset;
        _mislLastAttachedPosition = _position;
    }
    else if ( _mislAttachedToTarget )
    {
        _position = _mislLastAttachedPosition;
    }
}

void NC_STACK_ypamissile::ApplyAttachedDirectHitDamage()
{
    NC_STACK_ypabact *target = FindAttachedTarget();

    if ( target )
    {
        ApplyDirectHitToBact(target);
    }
}

vec3d NC_STACK_ypamissile::CalcForceVector()
{
    _thraction = _force;

    return vec3d::Normalise(  _fly_dir * _fly_dir_length * _airconst
                            + _target_dir * _thraction
                            - vec3d(0.0, _mass * 9.80665, 0.0));
}

void NC_STACK_ypamissile::SetupArcGrenadeVelocity(const vec3d &velocity, float gravity)
{
    _arcGrenadeGravity = (std::isfinite(gravity) && gravity > 0.0f)
                             ? std::min(gravity, 1000.0f)
                             : 9.80665f;

    _arcGrenadeVelocity = velocity;
    if ( !std::isfinite(_arcGrenadeVelocity.x) ||
         !std::isfinite(_arcGrenadeVelocity.y) ||
         !std::isfinite(_arcGrenadeVelocity.z) )
    {
        _arcGrenadeVelocity = vec3d(0.0, 0.0, 0.0);
    }

    _fly_dir_length = _arcGrenadeVelocity.length();
    if ( _fly_dir_length > 0.001f )
    {
        _fly_dir = _arcGrenadeVelocity / _fly_dir_length;
        AlignMissile();
    }
}

void NC_STACK_ypamissile::SetupArcGrenadeLaunch(float angleDegrees,
                                                 float gravity,
                                                 float startSpeed)
{
    vec3d aimDir = _fly_dir;
    if ( aimDir.normalise() <= 0.001f )
        aimDir = _rotation.AxisZ();
    if ( aimDir.normalise() <= 0.001f )
        aimDir = vec3d::OZ(1.0f);

    const float speed = (std::isfinite(startSpeed) && startSpeed > 0.0f)
                            ? startSpeed
                            : 0.0f;
    const float angle = (std::isfinite(angleDegrees) && angleDegrees > 0.0f)
                            ? std::min(angleDegrees, 89.0f)
                            : 0.0f;

    vec3d launchDir = aimDir;
    if ( angle > 0.0f )
    {
        // Preserve the normal Weapon aim azimuth, but make elevation relative
        // to world horizontal/gravity. Engine +Y is down, so upward is -Y.
        vec3d horizontal(aimDir.x, 0.0f, aimDir.z);
        if ( horizontal.normalise() <= 0.001f )
        {
            const vec3d fallback = _rotation.AxisZ();
            horizontal = vec3d(fallback.x, 0.0f, fallback.z);
            if ( horizontal.normalise() <= 0.001f )
                horizontal = vec3d::OZ(1.0f);
        }

        const float radians = angle * (float)C_PI_180;
        launchDir = horizontal * std::cos(radians);
        launchDir.y = -std::sin(radians);
        launchDir.normalise();
    }

    SetupArcGrenadeVelocity(launchDir * speed, gravity);
}

void NC_STACK_ypamissile::UpdateArcGrenadeBallistic(float dtime)
{
    if ( dtime <= 0.0f )
        return;

    _old_pos = _position;

    // Dedicated trajectory: no legacy grenade thrust and no target steering.
    // Gravity is an authored downward acceleration applied every frame.
    _arcGrenadeVelocity.y += _arcGrenadeGravity * dtime;

    // Preserve the generic Weapon air resistance as optional linear drag.
    if ( _airconst > 0.0f )
    {
        const float mass = _mass > 0.001f ? _mass : 1.0f;
        const float dragFactor = std::max(0.0f, 1.0f - (_airconst / mass) * dtime);
        _arcGrenadeVelocity *= dragFactor;
    }

    // Missile movement in UA uses the historical x6 world-distance scale.
    _position += _arcGrenadeVelocity * (dtime * 6.0f);

    _fly_dir_length = _arcGrenadeVelocity.length();
    if ( _fly_dir_length > 0.001f )
    {
        _fly_dir = _arcGrenadeVelocity / _fly_dir_length;
        AlignMissile();
    }

    CorrectPositionInLevelBox(NULL);
}

static bool ypamissile_IsHomingBombProto(const World::TWeapProto &wproto)
{
    return wproto.IsHomingBomb();
}

static bool ypamissile_IsHomingBomb(NC_STACK_ypamissile *missile)
{
    if ( !missile || !missile->getBACT_pWorld() || missile->_vehicleID < 0 )
        return false;

    std::vector<World::TWeapProto> &weapons = missile->getBACT_pWorld()->GetWeaponsProtos();
    if ( (size_t)missile->_vehicleID >= weapons.size() )
        return false;

    return ypamissile_IsHomingBombProto(weapons.at(missile->_vehicleID));
}

static bool ypamissile_HasHomingBombTarget(NC_STACK_ypamissile *missile)
{
    if ( !ypamissile_IsHomingBomb(missile) )
        return false;

    if ( missile->_primTtype == BACT_TGT_TYPE_CELL )
        return true;

    if ( missile->_primTtype != BACT_TGT_TYPE_UNIT || !missile->_primT.pbact )
        return false;

    NC_STACK_ypabact *target = missile->_primT.pbact;
    return target->_status != BACT_STATUS_DEAD &&
           target->_status != BACT_STATUS_CREATE &&
           target->_energy > 0 &&
           !target->IsDestroyed() &&
           !(target->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER));
}

void NC_STACK_ypamissile::SteerHomingBombDirection(float dtime)
{
    vec3d desired = _target_dir;
    if ( desired.normalise() <= 0.001 )
        return;

    vec3d current = _fly_dir;
    if ( current.normalise() <= 0.001 )
        current = _rotation.AxisZ();

    if ( current.normalise() <= 0.001 )
    {
        _fly_dir = desired;
        return;
    }

    // mat3x3::AxisAngle() uses the engine's inverted-sine convention.
    // With Transform(), desired x current is the axis that rotates the current
    // flight direction toward the target instead of away from it.
    vec3d axis = desired * current;
    if ( axis.normalise() <= 0.001 )
        return;

    float rotAngle = clp_acos(current.dot(desired));
    float maxAngle = _maxrot * dtime;

    if ( maxAngle > 0.0 && rotAngle > maxAngle )
        rotAngle = maxAngle;

    if ( fabs(rotAngle) > BOMB_MIN_ANGLE )
        _fly_dir = mat3x3::AxisAngle(axis, rotAngle).Transform(_fly_dir);

    _fly_dir.normalise();
}

void NC_STACK_ypamissile::AI_layer3(update_msg *arg)
{
    // OpenNeoUA custom: artillery shells use a fully isolated ballistic path so normal
    // missile/bomb behavior is left completely unchanged.
    if ( _isArtilleryShellProjectile )
    {
        UpdateArtilleryShellBallistic(arg);
        return;
    }

    _world->ypaworld_func145(this);

    float v40 = _target_vec.length();

    if ( v40 > 0.1 )
    {
        if ( _primTtype != BACT_TGT_TYPE_DRCT )
            _target_dir = _target_vec / v40;
    }

    _AI_time1 = 0;

    _thraction = _force;

    float v38 = arg->frameTime * 0.001;

    if ( _status == BACT_STATUS_NORMAL )
    {
        _mislClusterAge += arg->frameTime;

        if ( TryClusterSplit() )
            return;

        if ( _mislFlags & FLAG_MISL_COUNTDELAY)
            _mislDelayTime -= arg->frameTime;

        if ( _mislAttachedToTarget )
        {
            UpdateAttachedDetonationPosition();
            _world->ypaworld_func145(this);
        }

        if ( (_mislFlags & FLAG_MISL_COUNTDELAY)  &&  _mislDelayTime <= 0 )
        {
            bool applySectorDamage = (!(_mislFlags & FLAG_MISL_IGNOREBUILDS) || _pSector->PurposeType == cellArea::PT_NONE) &&
                                     (_world->_userRobo->_owner == _owner || !_world->_isNetGame);
            vec3d directDamagePos = _position + _fly_dir * 5.0;

            if ( applySectorDamage )
            {
                RememberDirectHitBuildingAt(directDamagePos);
                RememberDirectHitSectorAt(directDamagePos);
            }

            if ( _mislAttachedToTarget )
            {
                ApplyAttachedDirectHitDamage();
                _mislAttachedToTarget = false;
                _mislAttachTargetGid = 0;
            }

            Impact();

            _status = BACT_STATUS_DEAD;

            setState_msg arg78;
            arg78.setFlags = BACT_STFLAG_DEATH2;
            arg78.unsetFlags = 0;
            arg78.newStatus = BACT_STATUS_NOPE;

            SetState(&arg78);

            if ( applySectorDamage )
            {
                yw_arg129 v25;

                v25.pos.x = directDamagePos.x;
                v25.pos.z = directDamagePos.z;
                v25.field_10 = _energy;
                v25.unit = _mislEmitter;

                ChangeSectorEnergy(&v25);
            }
        }
        else
        {
            move_msg arg74;

            switch ( _mislType )
            {
            case MISL_BOMB:
                arg74.field_0 = v38;
                arg74.flag = 1;
                if ( ypamissile_HasHomingBombTarget(this) )
                {
                    if ( _force > 0.0 )
                    {
                        arg74.flag = 0;
                        arg74.vec = CalcForceVector();
                    }
                    else
                    {
                        SteerHomingBombDirection(v38);
                    }
                }
                Move(&arg74);
                break;

            case MISL_DIRECT:
                arg74.field_0 = v38;
                arg74.flag = 0;
                arg74.vec = CalcForceVector();
                Move(&arg74);
                break;

            case MISL_TARGETED:
                arg74.field_0 = v38;
                arg74.flag = 0;
                arg74.vec = CalcForceVector();
                Move(&arg74);
                break;

            case MISL_GRENADE:
                arg74.field_0 = v38;
                arg74.vec = _fly_dir;
                arg74.flag = 0;
                Move(&arg74);
                break;

            case MISL_ARC_GRENADE:
                UpdateArcGrenadeBallistic(v38);
                break;

            default:
                break;
            }

            if ( _mislType == MISL_INTERNAL )
                return;

            NC_STACK_ypabact *hitTarget = NULL;
            if ( TubeCollisionTest(_mislDelayTime <= 0, &hitTarget) )
            {
                ResetViewing();

                if ( hitTarget && hitTarget->_bact_type == BACT_TYPES_MISSLE )
                {
                    DetonateWeaponCollision(dynamic_cast<NC_STACK_ypamissile *>(hitTarget));
                    return;
                }

                if ( _mislDelayTime > 0 )
                {
                    AttachDelayedDetonationToTarget(hitTarget);
                    _mislDirectHitUnits.clear();
                    return;
                }

                setState_msg arg78;
                Impact();
                arg78.newStatus = BACT_STATUS_DEAD;
                arg78.unsetFlags = 0;
                arg78.setFlags = 0;

                SetState(&arg78);

                return;
            }

            ypaworld_arg136 arg136;
            arg136.stPos = _old_pos;
            arg136.vect = _position - _old_pos;
            arg136.flags = 0;

            _world->ypaworld_func136(&arg136);

            if ( arg136.isect )
            {
                vec3d impactNormal = arg136.skel->polygons[ arg136.polyID ].Normal();

                // Ground decals consume the actual collision data before a
                // later trace can mutate a shared filler/slurp skeleton.
                StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_IMPACT_WORLD, &arg136);

                AlignMissileByNormal( impactNormal );

                _position = arg136.isectPos;

                ResetViewing();

                _mislType = MISL_INTERNAL;
                _mislFlags |= FLAG_MISL_COUNTDELAY;

                if ( !_mislDelayTime )
                {
                    bool applySectorDamage = (!(_mislFlags & FLAG_MISL_IGNOREBUILDS) || _pSector->PurposeType == cellArea::PT_NONE) &&
                                             (_world->_userRobo->_owner == _owner || !_world->_isNetGame);
                    vec3d directDamagePos = _position + _fly_dir * 5.0;

                    if ( applySectorDamage )
                    {
                        RememberDirectHitBuildingAt(directDamagePos);
                        RememberDirectHitSectorAt(directDamagePos);
                    }

                    Impact();

                    _status = BACT_STATUS_DEAD;

                    setState_msg arg78;
                    arg78.setFlags = BACT_STFLAG_DEATH2;
                    arg78.unsetFlags = 0;
                    arg78.newStatus = BACT_STATUS_NOPE;

                    SetState(&arg78);

                    if ( applySectorDamage )
                    {
                        yw_arg129 v25;

                        v25.pos.x = directDamagePos.x;
                        v25.pos.z = directDamagePos.z;
                        v25.field_10 = _energy;
                        v25.unit = _mislEmitter;

                        ChangeSectorEnergy(&v25);
                    }
                }

                int a4 = _mislEmitter->getBACT_inputting();

                if ( a4 )
                {
                    if ( _mislEmitter->IsParentMyRobo() )
                    {
                        setTarget_msg arg67;
                        arg67.tgt_type = BACT_TGT_TYPE_CELL;
                        arg67.tgt_pos = _position;
                        arg67.priority = 0;

                        _mislEmitter->SetTarget(&arg67);
                    }
                }
            }
            else
            {
                // A purely ballistic Arc Grenade has no powered drive phase.
                // Once delayed homing activates it is MISL_TARGETED and follows
                // the normal drive-time/fallout policy from that point onward.
                if ( _mislType != MISL_ARC_GRENADE )
                {
                    _mislDriveTime -= arg->frameTime;

                    if ( _mislDriveTime < 0 )
                    {
                        _mislType = MISL_BOMB;

                        _airconst = 10.0;
                        _airconst_static = 10.0;
                    }
                }

                _mislLifeTime -= arg->frameTime;

                if ( _mislLifeTime >= 0 )
                {
                    AlignMissile( arg->frameTime * 0.001 );
                }
                else
                {
                    Impact();

                    setState_msg arg78;
                    arg78.unsetFlags = 0;
                    arg78.setFlags = 0;
                    arg78.newStatus = BACT_STATUS_DEAD;

                    SetState(&arg78);

                    ResetViewing();
                }
            }
        }
    }
}

void NC_STACK_ypamissile::User_layer(update_msg *arg)
{
    _old_pos = _position;

    if (_status == BACT_STATUS_NORMAL)
        AI_layer1(arg);
    else
        ResetViewing();
}

int NC_STACK_ypamissile::SetupArtilleryShell(const vec3d &startPos, const vec3d &targetPos,
                                                  float arcHeight, float shellSpeed, bool impactOnSurface)
{
    _isArtilleryShellProjectile = true;
    _artilleryShellVerticalBarrage = false;
    _artilleryShellVerticalTransferred = false;
    _artilleryShellVerticalApexOffset = vec3d(0.0, 0.0, 0.0);
    _artilleryShellVerticalAscentTime = 0;
    _artilleryShellVerticalFallStartTime = 0;
    _artilleryShellVerticalDescentTime = 0;
    _artilleryShellStartPos     = startPos;
    _artilleryShellTargetPos    = targetPos;
    _artilleryShellArcHeight    = (std::isfinite(arcHeight) && arcHeight > 0.0f) ? arcHeight : 0.0f;
    _artilleryShellElapsed      = 0;
    _artilleryShellImpactOnSurface = impactOnSurface;

    // Derive flight time from the authored artillery speed. The trajectory itself is
    // unchanged: sample its real parabolic path so artillery_shell_speed remains an
    // intuitive world-units/second control instead of another fixed time parameter.
    int flightTime = 2500;
    if ( std::isfinite(shellSpeed) && shellSpeed > 0.0f )
    {
        const int SEGMENTS = 16;
        float pathLength = 0.0f;
        vec3d prev = startPos;

        for ( int i = 1; i <= SEGMENTS; ++i )
        {
            float t = (float)i / (float)SEGMENTS;
            vec3d cur;
            cur.x = startPos.x + (targetPos.x - startPos.x) * t;
            cur.z = startPos.z + (targetPos.z - startPos.z) * t;
            float baseY = startPos.y + (targetPos.y - startPos.y) * t;
            float arc = _artilleryShellArcHeight * 4.0f * t * (1.0f - t);
            cur.y = baseY - arc;
            pathLength += (cur - prev).length();
            prev = cur;
        }

        if ( pathLength > 0.0f && std::isfinite(pathLength) )
            flightTime = std::max(1, (int)std::ceil((pathLength / shellSpeed) * 1000.0f));
    }

    _artilleryShellFlightTime = flightTime;
    _position = startPos;
    _old_pos  = startPos;

    // Artillery shells never home; they fly a fixed parametric arc.
    _primTtype = BACT_TGT_TYPE_DRCT;

    // Initial facing toward the target zone (purely cosmetic; refreshed each frame).
    vec3d dir = targetPos - startPos;
    if ( dir.normalise() > 0.001 )
    {
        _fly_dir = dir;
        _rotation.SetZ(_fly_dir);
        vec3d x = vec3d::OY(-1.0) * _fly_dir; // cross product (engine: vec3d operator* = cross)
        if ( x.normalise() > 0.001 )
        {
            _rotation.SetX(x);
            _rotation.SetY(_fly_dir * x);
        }
    }

    return _artilleryShellFlightTime;
}

int NC_STACK_ypamissile::SetupArtilleryShellVerticalBarrage(const vec3d &startPos, const vec3d &targetPos,
                                                            int fallDelay, float fallHeight, float verticalSpeed,
                                                            float spreadX, float spreadZ, bool impactOnSurface)
{
    _isArtilleryShellProjectile = true;
    _artilleryShellVerticalBarrage = true;
    _artilleryShellVerticalTransferred = false;
    _artilleryShellStartPos = startPos;
    _artilleryShellTargetPos = targetPos;
    _artilleryShellArcHeight = (std::isfinite(fallHeight) && fallHeight > 0.0f) ? fallHeight : 0.0f;
    _artilleryShellElapsed = 0;
    _artilleryShellImpactOnSurface = impactOnSurface;

    // Vertical spread is authored like the normal Weapon spread: degrees. It only
    // tilts the visible ascent. The landing point and vertical descent remain
    // controlled exclusively by artillery_shell_barrage_radius/targeting.
    const float safeSpreadX = (std::isfinite(spreadX) && spreadX > 0.0f) ? std::min(spreadX, 45.0f) : 0.0f;
    const float safeSpreadZ = (std::isfinite(spreadZ) && spreadZ > 0.0f) ? std::min(spreadZ, 45.0f) : 0.0f;
    const float angleX = safeSpreadX > 0.0f
        ? ((((float)rand() / (float)RAND_MAX) * 2.0f) - 1.0f) * safeSpreadX
        : 0.0f;
    const float angleZ = safeSpreadZ > 0.0f
        ? ((((float)rand() / (float)RAND_MAX) * 2.0f) - 1.0f) * safeSpreadZ
        : 0.0f;

    _artilleryShellVerticalApexOffset = vec3d(
        tan(angleX * C_PI_180) * _artilleryShellArcHeight,
        0.0f,
        tan(angleZ * C_PI_180) * _artilleryShellArcHeight);

    // Spread is visual-only: it must not change barrage timing or the authored
    // artillery_shell_speed cadence. Ascent/descent time therefore remains based
    // on the vertical arc height exactly as with zero spread.
    const float speed = (std::isfinite(verticalSpeed) && verticalSpeed > 0.0f) ? verticalSpeed : 70.0f;
    const int travelTime = _artilleryShellArcHeight > 0.0f
        ? std::max(1, (int)std::ceil((_artilleryShellArcHeight / speed) * 1000.0f))
        : 0;

    _artilleryShellVerticalAscentTime = travelTime;
    _artilleryShellVerticalFallStartTime = std::max(std::max(fallDelay, 0), travelTime);
    _artilleryShellVerticalDescentTime = travelTime;
    _artilleryShellFlightTime = std::max(1, _artilleryShellVerticalFallStartTime + _artilleryShellVerticalDescentTime);

    _position = startPos;
    _old_pos = startPos;
    _primTtype = BACT_TGT_TYPE_DRCT;

    vec3d ascentDir(_artilleryShellVerticalApexOffset.x, -_artilleryShellArcHeight,
                    _artilleryShellVerticalApexOffset.z);
    if ( ascentDir.normalise() <= 0.001f )
        ascentDir = vec3d(0.0, -1.0, 0.0);
    _fly_dir = ascentDir;
    _rotation.SetZ(_fly_dir);
    vec3d x = vec3d::OY(-1.0) * _fly_dir;
    if ( x.normalise() <= 0.001f )
        x = vec3d::OX(1.0);
    _rotation.SetX(x);
    _rotation.SetY(_fly_dir * x);

    return _artilleryShellFlightTime;
}

void NC_STACK_ypamissile::UpdateArtilleryShellBallistic(update_msg *arg)
{
    if ( _status != BACT_STATUS_NORMAL )
        return;

    _artilleryShellElapsed += arg->frameTime;

    int flightTime = _artilleryShellFlightTime > 0 ? _artilleryShellFlightTime : 2500;
    bool impactNow = _artilleryShellElapsed >= flightTime;
    vec3d prevPos = _position;
    vec3d pos = _position;

    if ( _artilleryShellVerticalBarrage )
    {
        // Mortar-style mode: the physical projectile first climbs straight above the
        // launcher, waits there if fall_delay is longer than the ascent, then the same
        // projectile is transferred above the target and falls vertically.
        if ( _artilleryShellElapsed < _artilleryShellVerticalFallStartTime )
        {
            float ascentT = 1.0f;
            if ( _artilleryShellVerticalAscentTime > 0 )
                ascentT = ypamissile_Clamp01((float)_artilleryShellElapsed / (float)_artilleryShellVerticalAscentTime);

            pos = _artilleryShellStartPos + _artilleryShellVerticalApexOffset * ascentT;
            pos.y -= _artilleryShellArcHeight * ascentT;
        }
        else
        {
            if ( !_artilleryShellVerticalTransferred )
            {
                // This is intentionally the same projectile, not a second spawned shell.
                // Reset tracer history so the transfer never draws a giant horizontal line
                // from launcher apex to target apex.
                _artilleryShellVerticalTransferred = true;
                _position = vec3d(_artilleryShellTargetPos.x,
                                  _artilleryShellTargetPos.y - _artilleryShellArcHeight,
                                  _artilleryShellTargetPos.z);
                _old_pos = _position;
                StartWeaponTracer();
                prevPos = _position;
            }

            float descentT = 1.0f;
            if ( _artilleryShellVerticalDescentTime > 0 )
            {
                descentT = ypamissile_Clamp01(
                    (float)(_artilleryShellElapsed - _artilleryShellVerticalFallStartTime) /
                    (float)_artilleryShellVerticalDescentTime);
            }

            pos.x = _artilleryShellTargetPos.x;
            pos.z = _artilleryShellTargetPos.z;
            pos.y = (_artilleryShellTargetPos.y - _artilleryShellArcHeight) +
                    _artilleryShellArcHeight * descentT;
        }
    }
    else
    {
        float t = ypamissile_Clamp01((float)_artilleryShellElapsed / (float)flightTime);

        // Horizontal interpolation start -> target.
        pos.x = _artilleryShellStartPos.x + (_artilleryShellTargetPos.x - _artilleryShellStartPos.x) * t;
        pos.z = _artilleryShellStartPos.z + (_artilleryShellTargetPos.z - _artilleryShellStartPos.z) * t;

        // Vertical: straight-line baseline + parabolic arc.
        // Engine convention: +Y is DOWN, so "up" means subtracting from Y.
        float baseY = _artilleryShellStartPos.y + (_artilleryShellTargetPos.y - _artilleryShellStartPos.y) * t;
        float arc   = _artilleryShellArcHeight * 4.0f * t * (1.0f - t); // peak == arc_height at t = 0.5
        pos.y = baseY - arc;

    }

    _old_pos  = prevPos;
    _position = pos;

    // Orient the model along its current travel direction (visual only).
    vec3d vel = _position - prevPos;
    if ( vel.normalise() > 0.001 )
    {
        _fly_dir = vel;
        _rotation.SetZ(_fly_dir);
        vec3d x = vec3d::OY(-1.0) * _fly_dir;
        if ( x.normalise() > 0.001 )
        {
            _rotation.SetX(x);
            _rotation.SetY(_fly_dir * x);
        }
    }

    CorrectPositionInLevelBox(NULL);

    if ( !impactNow )
        return;

    // The dedicated artillery trajectory bypasses the normal missile AI path,
    // therefore it never reaches the usual TubeCollisionTest(). Reuse that exact
    // direct-hit path on the terminal flight segment before the timed explosion:
    // a physically intersected unit receives `energy`, is remembered as the direct
    // hit, and Impact() below will then exclude it from aoe_unit_energy to avoid
    // double damage. If no unit is intersected, the shell simply detonates at its
    // authored landing/airburst point as before.
    NC_STACK_ypabact *directHit = NULL;
    TubeCollisionTest(true, &directHit);

    // Ground-burst artillery shells land on a point previously snapped to world
    // collision geometry. Reacquire that same real surface at impact time so
    // ground decals receive valid, short-lived skeleton/poly data; airbursts
    // intentionally have no world-hit context.
    bool hasGroundDecalChainFX = false;
    for (const World::TChainFXConfig &fx : _chainFX)
    {
        if ( fx.mode == World::TChainFXConfig::MODE_GROUND_DECAL &&
             fx.trigger == World::TChainFXConfig::TRIGGER_IMPACT_WORLD )
        {
            hasGroundDecalChainFX = true;
            break;
        }
    }

    if ( _artilleryShellImpactOnSurface && !_world->_isNetGame && hasGroundDecalChainFX )
    {
        ypaworld_arg136 groundHit;
        groundHit.stPos = vec3d(_position.x, -30000.0, _position.z);
        groundHit.vect = vec3d(0.0, 50000.0, 0.0);
        groundHit.flags = 0;
        _world->ypaworld_func136(&groundHit);

        if ( groundHit.isect && std::fabs(groundHit.isectPos.y - _position.y) <= 5.0 )
            StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_IMPACT_WORLD,
                                  &groundHit);
    }

    // Timed impact: reuse the same path a normal bomb uses on contact for AoE
    // damage/push, building/sector damage, VP dead/megadeth and chain FX. The artillery shell
    // F10 AoE rings are suppressed in Impact() so they do not look like target
    // markers in the 3D view.
    bool applySectorDamage = (!(_mislFlags & FLAG_MISL_IGNOREBUILDS) ||
                              (_pSector && _pSector->PurposeType == cellArea::PT_NONE)) &&
                             (_world->_userRobo->_owner == _owner || !_world->_isNetGame);
    vec3d directDamagePos = _position;

    if ( applySectorDamage )
    {
        RememberDirectHitBuildingAt(directDamagePos);
        RememberDirectHitSectorAt(directDamagePos);
    }

    Impact();

    _status = BACT_STATUS_DEAD;

    setState_msg arg78;
    arg78.setFlags   = BACT_STFLAG_DEATH2;
    arg78.unsetFlags = 0;
    arg78.newStatus  = BACT_STATUS_NOPE;

    SetState(&arg78);

    if ( applySectorDamage )
    {
        yw_arg129 v25;
        v25.pos.x    = directDamagePos.x;
        v25.pos.z    = directDamagePos.z;
        v25.field_10 = _energy;
        v25.unit     = _mislEmitter;

        ChangeSectorEnergy(&v25);
    }

    ResetViewing();
}

void NC_STACK_ypamissile::Move(move_msg *arg)
{
    _old_pos = _position;

    float v8;

    if ( _status != BACT_STATUS_DEAD && _mislType != MISL_BOMB )
        v8 = _mass * 9.80665;
    else
        v8 = _mass * 39.2266;

    vec3d v26(0.0, 0.0, 0.0);

    if ( !(arg->flag & 1) )
        v26 = arg->vec * _thraction;

    vec3d vec1 = vec3d(0.0, v8, 0.0) + v26 - _fly_dir * (_fly_dir_length * _airconst);

    float v33 = vec1.normalise();

    if ( v33 > 0.0 )
    {
        vec3d v36 = _fly_dir * _fly_dir_length + vec1 * (v33 / _mass * arg->field_0);

        float v32 = v36.length();

        if ( v32 > 0.0 )
            v36 /= v32;

        _fly_dir = v36;

        _fly_dir_length = v32;
    }

    _position += _fly_dir * (_fly_dir_length * arg->field_0 * 6.0);

    CorrectPositionInLevelBox(NULL);
}

void NC_STACK_ypamissile::SetState(setState_msg *arg)
{
    SetStateInternal(arg);
}

void NC_STACK_ypamissile::Renew()
{
    NC_STACK_ypabact::Renew();

    // Missiles are recycled from the world's dead cache. Never let the
    // previous shot's fine energy snapshot leak into a new projectile before
    // ypaworld_func147() reconfigures it.
    _mislSpecificEnergy.fill(1.0f);
    _mislSpecificEnergyDefined.fill(false);

    _mislFlags  = 0;
    _mislDelayTime = 0;
    _mislAoeFalloff = 0;
    _mislAoeUnitPush = 0.0f;
    _mislDirectPush = 0.0f;
    _mislArmorPenetrationRemaining = 0;
    _mislArmorPenetratedGids.clear();
    _mislDirectPushRecipientGids.clear();
    _mislClusterAge = 0;
    _weaponSoundEventsEnabled = true;
    _collisionOldRotationValid = false;
    _mislClusterGeneration = 0;
    _mislClusterDone = false;
    _mislClusterChild = false;
    _mislChainDepth = 0;
    _mislChainEnergy = 0;
    _mislChainSpawned = false;
    _mislChainAllowFriendly = false;
    _mislChainPending = false;
    _mislChainPendingElapsed = 0;
    _mislChainPendingDelay = 0;
    _mislChainPendingTargetGid = 0;
    _mislChainPendingEnergy = 0;
    _mislChainPendingOrigin = vec3d(0.0, 0.0, 0.0);
    _mislChainPendingOriginRadius = 0.0;
    _mislChainHitGids.clear();
    _suicideViewerReturnGids.clear();
    _suicideViewerHostGid = 0;
    _mislAttachedToTarget = false;
    _mislAttachTargetGid = 0;
    _mislAttachOffset = vec3d(0.0, 0.0, 0.0);
    _mislLastAttachedPosition = vec3d(0.0, 0.0, 0.0);
    SFXEngine::SFXe.StopCarrier(&_mislClusterSoundCarrier);
    _mislClusterSoundCarrier.Clear();
    _weaponTracer = World::TWeaponTracerConfig();
    _weaponTracerStarted = false;
    _weaponTracerVisualSeed = 0;
    _weaponTracerPoints.clear();

    // OpenNeoUA custom: clear Arc Grenade ballistic state on recycle.
    _arcGrenadeVelocity = vec3d(0.0, 0.0, 0.0);
    _arcGrenadeGravity = 9.80665f;

    // OpenNeoUA custom: clear artillery shell state on recycle.
    _isArtilleryShellProjectile = false;
    _artilleryShellStartPos  = vec3d(0.0, 0.0, 0.0);
    _artilleryShellTargetPos = vec3d(0.0, 0.0, 0.0);
    _artilleryShellElapsed    = 0;
    _artilleryShellFlightTime = 0;
    _artilleryShellArcHeight  = 0.0;
    _artilleryShellImpactOnSurface = false;
    _artilleryShellVerticalBarrage = false;
    _artilleryShellVerticalTransferred = false;
    _artilleryShellVerticalApexOffset = vec3d(0.0, 0.0, 0.0);
    _artilleryShellVerticalAscentTime = 0;
    _artilleryShellVerticalFallStartTime = 0;
    _artilleryShellVerticalDescentTime = 0;

    setBACT_yourLastSeconds(3000);
}

size_t NC_STACK_ypamissile::SetStateInternal(setState_msg *arg)
{
    SFXEngine::SFXe.sub_424000(&_soundcarrier, 2);
    SFXEngine::SFXe.sub_424000(&_soundcarrier, 0);
    SFXEngine::SFXe.sub_424000(&_soundcarrier, 1);

    if ( arg->newStatus )
        _status = arg->newStatus;

    if ( arg->setFlags )
        _status_flg |= arg->setFlags;

    if ( arg->unsetFlags )
        _status_flg &= ~arg->unsetFlags;

    if ( arg->newStatus == BACT_STATUS_DEAD )
    {
        FreezeProjectileVisualMotion();
        SetVP(_vp_dead);

        SFXEngine::SFXe.startSound(&_soundcarrier, 2);

        StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_DETONATE);
        StartDestFXByType(World::DestFX::FX_DEATH);

        _fly_dir_length = 0;
    }

    if ( arg->newStatus == BACT_STATUS_NORMAL )
    {
        ResetProjectileVisualMotionFreeze();
        SetVP(_vp_normal);

        SFXEngine::SFXe.startSound(&_soundcarrier, 0);
    }

    if ( arg->unsetFlags == BACT_STFLAG_DEATH2 )
    {
        ResetProjectileVisualMotionFreeze();
        SetVP(_vp_normal);

        SFXEngine::SFXe.startSound(&_soundcarrier, 0);
    }

    if ( arg->setFlags == BACT_STFLAG_DEATH2 )
    {
        _status = BACT_STATUS_DEAD;

        FreezeProjectileVisualMotion();
        SetVP(_vp_megadeth);

        SFXEngine::SFXe.startSound(&_soundcarrier, 2);

        StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_IMPACT_WORLD);
        StartDestFXByType(World::DestFX::FX_MEGADETH);

        _fly_dir_length = 0;
    }

    return 1;
}

void NC_STACK_ypamissile::ConfigureSuicideViewerReturn(
    const std::vector<int32_t> &squadGids, int32_t hostGid)
{
    _suicideViewerReturnGids = squadGids;
    _suicideViewerHostGid = hostGid;
}

static bool ypamissile_IsUsableSuicideReturn(NC_STACK_ypabact *candidate)
{
    return candidate && candidate->_energy > 0 &&
           candidate->_status != BACT_STATUS_DEAD &&
           candidate->_status != BACT_STATUS_CREATE &&
           candidate->_status != BACT_STATUS_BEAM &&
           !(candidate->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) &&
           candidate->_bact_type != BACT_TYPES_MISSLE &&
           candidate->_bact_type != BACT_TYPES_GUN &&
           !candidate->ShouldHideFromStrategicUI();
}

void NC_STACK_ypamissile::ResetViewing()
{
    if ( !getBACT_viewer() )
        return;

    setBACT_viewer(false);
    setBACT_inputting(false);

    NC_STACK_ypabact *returnUnit = NULL;
    if ( !_suicideViewerReturnGids.empty() || _suicideViewerHostGid )
    {
        for ( int32_t gid : _suicideViewerReturnGids )
        {
            NC_STACK_ypabact *candidate = _world ? _world->FindLiveBactByGid(gid) : NULL;
            if ( ypamissile_IsUsableSuicideReturn(candidate) )
            {
                returnUnit = candidate;
                break;
            }
        }

        if ( !returnUnit && _suicideViewerHostGid && _world )
        {
            NC_STACK_ypabact *host = _world->FindLiveBactByGid(_suicideViewerHostGid);
            if ( ypamissile_IsUsableSuicideReturn(host) )
                returnUnit = host;
        }

        _suicideViewerReturnGids.clear();
        _suicideViewerHostGid = 0;
    }

    if ( returnUnit )
    {
        returnUnit->setBACT_viewer(true);
        returnUnit->setBACT_inputting(true);
        returnUnit->ArmSuicideHandoffFireRelease();
        return;
    }

    // Non-suicide cameras retain the original launcher/parent return semantics.
    if ( _mislEmitter )
    {
        if ( _mislEmitter->_status != BACT_STATUS_DEAD || _mislEmitter->_parent == NULL )
        {
            _mislEmitter->setBACT_viewer(true);
            _mislEmitter->setBACT_inputting(true);
        }
        else
        {
            _mislEmitter->_parent->setBACT_viewer(true);
            _mislEmitter->_parent->setBACT_inputting(true);
        }
    }
}

void NC_STACK_ypamissile::Impact()
{
    bact_arg83 arg83;
    arg83.energ = _energy;
    arg83.pos = _position;
    arg83.pos2 = _fly_dir;
    arg83.force = _fly_dir_length;
    arg83.mass = _mass;

    float v16 = _fly_dir_length * _mass;

    if ( v16 > _world->_maxImpulse && _world->_maxImpulse > 0.0 )
    {
        float v7 = _world->_maxImpulse / v16;
        arg83.force *= v7;
        arg83.mass *= v7;
    }

    /* FIXME:
       Needs to check all near sectors too if effective radius affect it*/

    const bool validDirectPush = isfinite(_mislDirectPush) && _mislDirectPush > 0;
    const bool validAoePush = isfinite(_mislAoeUnitPush) && _mislAoeUnitPush > 0 &&
                             isfinite(_mislAoeUnitRadius) &&
                             _mislAoeUnitRadius > 0.0f;
    bool modernPushWeapon = validDirectPush || validAoePush;
    bool hasLegacyImpulseTarget = false;

    for( NC_STACK_ypabact* &bct : _pSector->unitsList )
    {
        if ( bct->_bact_type != BACT_TYPES_MISSLE && bct->_bact_type != BACT_TYPES_ROBO && bct->_bact_type != BACT_TYPES_TANK && bct->_bact_type != BACT_TYPES_CAR && bct->_bact_type != BACT_TYPES_GUN && !(bct->_status_flg & BACT_STFLAG_DEATH2) )
        {
            int v10 = 1;

            if ( _world->_isNetGame && _owner != bct->_owner )
                v10 = 0;

            if ( v10 )
            {
                if ( bct->_status == BACT_STATUS_DEAD ||
                     (bct->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) )
                    continue;

                const bool modernPushTarget =
                    modernPushWeapon && bct->CanReceiveConfiguredPush();

                if ( modernPushTarget )
                    continue;

                hasLegacyImpulseTarget = true;
                bct->ApplyImpulse(&arg83);
            }
            else
            {
                hasLegacyImpulseTarget = true;
            }
        }
    }

    if ( _world->_isNetGame && hasLegacyImpulseTarget )
    {
        uamessage_impulse impMsg;
        impMsg.msgID = UAMSG_IMPULSE;
        impMsg.p[0] = modernPushWeapon ? 1 : 0;
        impMsg.owner = _owner;
        impMsg.id = _gid;
        impMsg.pos = _position;
        impMsg.impulse = _energy;
        impMsg.dir = _fly_dir;
        impMsg.dir_len = _fly_dir_length;
        impMsg.mass = _mass;

        _world->NetBroadcastMessage(&impMsg, sizeof(impMsg), true);
    }

    ApplyAreaDamage();
    ApplyBuildingAreaDamage();
    ApplySectorAreaDamage();

    // A missing, zero or invalid cluster_trigger_time uses the existing
    // impact/detonation path. Positive times are handled only by AI_layer3.
    TryClusterSplit(true);

    // F10 debug overlay: record transient AoE rings at the impact point (no gameplay effect).
    if ( _world && _world->_showCollDebug && !_isArtilleryShellProjectile )
    {
        _world->DebugAddAoeRing(_position, _mislAoeUnitRadius,     255, 140, 0);   // unit AoE: orange
        _world->DebugAddAoeRing(_position, _mislAoeBuildingRadius, 200, 80, 220);  // building AoE: purple
        _world->DebugAddAoeRing(_position, _mislAoeSectorRadius,    80, 200, 220); // sector AoE: light cyan
    }

    _mislDirectHitUnits.clear();
    _mislDirectPushRecipientGids.clear();
    _mislDirectHitBuildings.clear();
    _mislDirectHitSectors.clear();
}

void NC_STACK_ypamissile::DetonateAtContact(NC_STACK_ypabact *directHit)
{
    DetonateKamikazePayload(directHit);
}

void NC_STACK_ypamissile::DetonateKamikazePayload(NC_STACK_ypabact *directHit)
{
    if ( _status == BACT_STATUS_DEAD )
        return;

    if ( directHit &&
         directHit != this &&
         directHit != _mislEmitter &&
         directHit->_bact_type != BACT_TYPES_MISSLE &&
         directHit->_status != BACT_STATUS_DEAD )
    {
        ApplyDirectHitToBact(directHit);
    }

    Impact();

    setState_msg arg78;
    arg78.unsetFlags = 0;
    arg78.setFlags = 0;
    arg78.newStatus = BACT_STATUS_DEAD;

    SetState(&arg78);

    StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_IMPACT_WORLD);
    StartDestFXByType(World::DestFX::FX_MEGADETH);
}

void NC_STACK_ypamissile::AlignMissile(float dtime)
{
    if ( _fly_dir != vec3d(0.0, 0.0, 0.0) )
    {
        vec3d dir = _rotation.AxisZ(); // Get Z-axis, as dir
        vec3d u = vec3d::Normalise(dir * _fly_dir); // vector cross product

        // If length == 0 - no rotation
        if ( u.length() > 0.0 )
        {
            //scalar cross product
            float rotAngle = clp_acos( dir.dot(_fly_dir) );

            if ( _mislType == MISL_BOMB )
            {
                if ( dtime != 0.0 )
                {
                    float mxrot = _maxrot * dtime;

                    if ( rotAngle < -mxrot )
                        rotAngle = -mxrot;

                    if ( rotAngle > mxrot )
                        rotAngle = mxrot;
                }
            }

            if ( fabs(rotAngle) > BOMB_MIN_ANGLE )
                _rotation *= mat3x3::AxisAngle(u, rotAngle);
        }

        // Fix camera Z-axis rotation
        if ( _mislFlags & FLAG_MISL_VIEW )
        {
            float ZAngle = clp_acos( _rotation.AxisX().XZ().length() ); // Get degree of current Z-axis rotation

            if ( _rotation.m11 < 0.0 )
                ZAngle = C_PI - ZAngle;

            if ( _rotation.m01 < 0.0 )
                ZAngle = -ZAngle;

            _rotation = mat3x3::RotateZ(-ZAngle) * _rotation;
        }
    }
}

void NC_STACK_ypamissile::AlignMissileByNormal(const vec3d &normal)
{
    vec3d UpVector = _rotation.AxisY();

    vec3d vaxis = UpVector * normal;

    if ( vaxis.normalise() != 0.0 )
    {
        float angle = clp_acos( UpVector.dot(normal) );

        if ( fabs(angle) > BACT_MIN_ANGLE )
            _rotation *= mat3x3::AxisAngle(vaxis, angle);
    }
}


NC_STACK_ypamissile::NC_STACK_ypamissile()
{
    _mislType = 0;
    _mislEmitter = NULL;
    _mislLifeTime = 0;
    _mislDriveTime = 0;
    _mislDelayTime = 0;
    _mislFlags = 0;
    _mislStartHeight = 0.;
    _mislEnergyHeli = 0.;
    _mislEnergyTank = 0.;
    _mislEnergyFlyer = 0.;
    _mislEnergyRobo = 0.;
    _mislAoeFalloff = 0;
    _mislClusterGeneration = 0;
    _artilleryShellImpactOnSurface = false;
}


void NC_STACK_ypamissile::setBACT_viewer(bool vwr)
{
    NC_STACK_ypabact::setBACT_viewer(vwr);

    if ( vwr )
        _mislFlags |= FLAG_MISL_VIEW;
    else
        _mislFlags &= ~FLAG_MISL_VIEW;
}

void NC_STACK_ypamissile::SetLauncherBact(NC_STACK_ypabact *bact)
{
    _mislEmitter = bact;

}

void NC_STACK_ypamissile::ConfigureSpecificEnergyMultipliers(const World::TWeapProto &proto)
{
    _mislSpecificEnergy.fill(1.0f);
    _mislSpecificEnergyDefined.fill(false);

    for (int i = (int)World::VEHICLE_COMBAT_CLASS_UNKNOWN + 1;
         i < (int)World::VEHICLE_COMBAT_CLASS_COUNT; ++i)
    {
        float energy = 1.0f;
        const World::VehicleCombatClass targetClass =
            static_cast<World::VehicleCombatClass>(i);
        if ( World::TryGetSpecificWeaponEnergy(proto, targetClass, &energy) )
        {
            // Match the existing projectile snapshot path exactly: legacy
            // energy_* values are converted to integer thousandths before
            // SetPower*() stores them back as floats. Keep the new fine-class
            // overrides on the same precision so enabling a more specific key
            // cannot subtly change projectile damage rounding.
            const int energyThousandths = (int)(energy * 1000.0f);
            _mislSpecificEnergy[(size_t)i] = energyThousandths * 0.001f;
            _mislSpecificEnergyDefined[(size_t)i] = true;
        }
    }
}

bool NC_STACK_ypamissile::TryGetSpecificEnergyForTarget(NC_STACK_ypabact *bct, float *outEnergy) const
{
    if ( !bct )
        return false;

    const int targetClass = (int)World::ResolveVehicleCombatClass(bct);
    if ( targetClass <= (int)World::VEHICLE_COMBAT_CLASS_UNKNOWN ||
         targetClass >= (int)World::VEHICLE_COMBAT_CLASS_COUNT ||
         !_mislSpecificEnergyDefined[(size_t)targetClass] )
        return false;

    if ( outEnergy )
        *outEnergy = _mislSpecificEnergy[(size_t)targetClass];
    return true;
}

void NC_STACK_ypamissile::SetMissileType(int tp)
{
    _mislType = tp;
}

void NC_STACK_ypamissile::SetLifeTime(int time)
{
    _mislLifeTime = time;
}

void NC_STACK_ypamissile::SetDelay(int delay)
{
    _mislDelayTime = delay;
}

void NC_STACK_ypamissile::SetDriveTime(int time)
{
    _mislDriveTime = time;
}

void NC_STACK_ypamissile::SetIgnoreBuilds(int ign)
{
    if ( ign )
        _mislFlags |= FLAG_MISL_IGNOREBUILDS;
    else
        _mislFlags &= ~FLAG_MISL_IGNOREBUILDS;
}

void NC_STACK_ypamissile::SetPowerHeli(int po)
{
    _mislEnergyHeli = po * 0.001;
}

void NC_STACK_ypamissile::SetPowerTank(int po)
{
    _mislEnergyTank = po * 0.001;
}

void NC_STACK_ypamissile::SetPowerFlyer(int po)
{
    _mislEnergyFlyer = po * 0.001;
}

void NC_STACK_ypamissile::SetPowerRobo(int po)
{
    _mislEnergyRobo = po * 0.001;
}

void NC_STACK_ypamissile::SetRadiusHeli(float rad)
{
    _mislRadiusHeli = rad;
}

void NC_STACK_ypamissile::SetRadiusTank(float rad)
{
    _mislRadiusTank = rad;
}

void NC_STACK_ypamissile::SetRadiusFlyer(float rad)
{
    _mislRadiusFlyer = rad;
}

void NC_STACK_ypamissile::SetRadiusRobo(float rad)
{
    _mislRadiusRobo = rad;
}

void NC_STACK_ypamissile::SetAreaDamage(float unitRadius, int unitEnergy, float buildingRadius, int buildingEnergy,
                                        float sectorRadius, int sectorEnergy, int falloff)
{
    _mislAoeUnitRadius = unitRadius;
    _mislAoeUnitEnergy = unitEnergy;
    _mislAoeBuildingRadius = buildingRadius;
    _mislAoeBuildingEnergy = buildingEnergy;
    _mislAoeSectorRadius = sectorRadius;
    _mislAoeSectorEnergy = sectorEnergy;
    _mislAoeFalloff = falloff ? 1 : 0;
}

void NC_STACK_ypamissile::SetAoeUnitPush(float push)
{
    _mislAoeUnitPush = isfinite(push) ? std::max(0.0f, std::min(push, 10.0f)) : 0.0f;
}

void NC_STACK_ypamissile::SetDirectPush(float push)
{
    _mislDirectPush = isfinite(push) ? std::max(0.0f, std::min(push, 10.0f)) : 0.0f;
}

void NC_STACK_ypamissile::SetArmorPenetrationTargets(int targets)
{
    _mislArmorPenetrationRemaining = std::max(targets, 0);
    _mislArmorPenetratedGids.clear();
    _mislDirectPushRecipientGids.clear();
}

void NC_STACK_ypamissile::SetStartHeight(float posy)
{
    _mislStartHeight = posy;
}

void NC_STACK_ypamissile::SetClusterSpawnedChild(bool child)
{
    _mislClusterChild = child;
    _mislClusterDone = child;
    _mislClusterAge = 0;
}

void NC_STACK_ypamissile::ConfigureSpawnedProjectileBase(
    NC_STACK_ypabact *launcher,
    NC_STACK_yparobo *hostStation,
    uint8_t owner,
    const vec3d &direction)
{
    SetLauncherBact(launcher);
    SetStartHeight(_position.y);
    _owner = owner;
    _host_station = hostStation;
    _fly_dir = direction;
}



NC_STACK_ypabact *NC_STACK_ypamissile::GetLauncherBact()
{
    return _mislEmitter;
}

int NC_STACK_ypamissile::GetMissileType()
{
    return _mislType;
}

int NC_STACK_ypamissile::GetLifeTime()
{
    return _mislLifeTime;
}

int NC_STACK_ypamissile::GetDelay()
{
    return _mislDelayTime;
}

int NC_STACK_ypamissile::GetDriveTime()
{
    return _mislDriveTime;
}

int NC_STACK_ypamissile::GetIgnoreBuilds()
{
    return (_mislFlags & FLAG_MISL_IGNOREBUILDS) != 0;
}

int NC_STACK_ypamissile::GetPowerHeli()
{
    return _mislEnergyHeli * 1000.0;
}

int NC_STACK_ypamissile::GetPowerTank()
{
    return _mislEnergyTank * 1000.0;
}

int NC_STACK_ypamissile::GetPowerFlyer()
{
    return _mislEnergyFlyer * 1000.0;
}

int NC_STACK_ypamissile::GetPowerRobo()
{
    return _mislEnergyRobo * 1000.0;
}

float NC_STACK_ypamissile::GetRadiusHeli()
{
    return _mislRadiusHeli;
}

float NC_STACK_ypamissile::GetRadiusTank()
{
    return _mislRadiusTank;
}

float NC_STACK_ypamissile::GetRadiusFlyer()
{
    return _mislRadiusFlyer;
}

float NC_STACK_ypamissile::GetRadiusRobo()
{
    return _mislRadiusRobo;
}

float NC_STACK_ypamissile::GetStartHeight()
{
    return _mislStartHeight;
}
