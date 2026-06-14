#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "yw.h"
#include "ypamissile.h"
#include "yparobo.h"
#include "yw_net.h"

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

static vec3d ypamissile_ApplyDirectionalSpread(const mat3x3 &rotation, const vec3d &direction, float spreadX, float spreadY)
{
    if ( spreadX <= 0.0 && spreadY <= 0.0 )
        return direction;

    vec3d aimDir = direction;

    if ( aimDir.normalise() <= 0.001 )
        return direction;

    vec3d right = rotation.AxisX();
    right -= aimDir * right.dot(aimDir);

    if ( right.normalise() <= 0.001 )
    {
        vec3d refAxis = fabs(aimDir.y) < 0.99 ? vec3d::OY(1.0) : vec3d::OX(1.0);
        right = refAxis * aimDir;
    }

    if ( right.normalise() <= 0.001 )
        return aimDir;

    vec3d up = aimDir * right;

    if ( up.normalise() <= 0.001 )
        return aimDir;

    float randX = 0.0;
    float randY = 0.0;

    if ( spreadX > 0.0 )
        randX = (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * tan(spreadX * C_PI_180);

    if ( spreadY > 0.0 )
        randY = (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * tan(spreadY * C_PI_180);

    aimDir += right * randX + up * randY;

    if ( aimDir.normalise() > 0.001 )
        return aimDir;

    return direction;
}

size_t NC_STACK_ypamissile::Init(IDVList &stak)
{
    if ( !NC_STACK_ypabact::Init(stak) )
        return 0;

    _bact_type = BACT_TYPES_MISSLE;

    _mislEmitter = NULL;
    _mislLifeTime = 5000;
    _mislDelayTime = 0;
    _mislType = MISL_BOMB;
    _mislClusterAge = 0;
    _mislClusterDone = false;
    _mislClusterChild = false;
    _mislAttachedToTarget = false;
    _mislAttachTargetGid = 0;
    _mislAttachOffset = vec3d(0.0, 0.0, 0.0);
    _mislLastAttachedPosition = vec3d(0.0, 0.0, 0.0);
    _mislClusterSoundCarrier.Clear();

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
    if ( !_mislClusterSoundCarrier.Sounds.empty() )
        SFXEngine::SFXe.UpdateSoundCarrier(&_mislClusterSoundCarrier);

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
}

bool NC_STACK_ypamissile::TryClusterSplit()
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

    if ( _mislClusterAge < cluster.trigger_time )
        return false;

    if ( cluster.weapon_id >= weapons.size() )
        return false;

    World::TWeapProto &childProto = weapons.at(cluster.weapon_id);

    if ( !(childProto._weaponFlags & 1) )
        return false;

    _mislClusterDone = true;

    if ( cluster.vp > 0 )
        _world->SpawnTransientVP(cluster.vp, _position, _rotation, 1000);

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

    int spawnCount = std::min(cluster.count, 64);
    int spawned = 0;

    for (int i = 0; i < spawnCount; i++)
    {
        ypaworld_arg146 arg147;
        arg147.vehicle_id = cluster.weapon_id;
        arg147.pos = _position;

        NC_STACK_ypamissile *child = _world->ypaworld_func147(&arg147);

        if ( !child )
            continue;

        vec3d childDir = ypamissile_ApplyDirectionalSpread(_rotation, baseDir, cluster.spread_x, cluster.spread_y);

        child->SetLauncherBact(_mislEmitter);
        child->SetClusterSpawnedChild(true);
        child->SetStartHeight(arg147.pos.y);
        child->_owner = _owner;
        child->_host_station = _host_station;
        child->_fly_dir = childDir;
        child->_fly_dir_length = childProto.start_speed;

        if ( !(childProto._weaponFlags & 0x12) )
            child->_fly_dir_length *= 0.2;

        child->_rotation.SetZ(child->_fly_dir);
        child->_rotation.SetX(_rotation.AxisX());
        child->_rotation.SetY(child->_rotation.AxisZ() * child->_rotation.AxisX());

        if ( childProto.vp_launch > 0 )
            _world->SpawnTransientVP(childProto.vp_launch, child->_position, child->_rotation, 1000);

        child->_kidRef.Detach();
        child->_parent = NULL;
        _mislEmitter->_missiles_list.push_back(child);

        if ( clusterSample && !clusterSoundPlayed )
        {
            child->_mislClusterSoundCarrier.Clear();
            child->_mislClusterSoundCarrier.Resize(1);
            child->_mislClusterSoundCarrier.Position = _position;
            child->_mislClusterSoundCarrier.Vector = vec3d(0.0, 0.0, 0.0);

            TSoundSource &snd = child->_mislClusterSoundCarrier.Sounds[0];
            snd.PSample = clusterSample;
            snd.SampleVariants.clear();
            snd.SampleVariants.push_back(clusterSample);
            snd.Volume = cluster.snd.volume ? cluster.snd.volume : 120;
            snd.Pitch = cluster.snd.pitch;
            snd.SetLoop(false);
            snd.SetFragmented(false);
            snd.SetPFx(false);
            snd.SetShk(false);

            SFXEngine::SFXe.startSound(&child->_mislClusterSoundCarrier, 0);
            SFXEngine::SFXe.UpdateSoundCarrier(&child->_mislClusterSoundCarrier);
            clusterSoundPlayed = true;
        }

        if ( child->GetMissileType() == MISL_TARGETED && childTargetType != BACT_TGT_TYPE_DRCT )
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
                if ( bct == this || bct == _mislEmitter || bct->_bact_type == BACT_TYPES_MISSLE || bct->_status == BACT_STATUS_DEAD )
                    continue;

                if (bct->_bact_type == BACT_TYPES_GUN && bct->GetEffectiveShield() >= 100.0f)
                {
                    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( bct );

                    if ( gun->IsRoboGun() )
                        continue;
                }

                if ( !a5 && bct->_owner == _mislEmitter->_owner )
                {
                    continue;
                }

                if (_mislEmitter->_bact_type == BACT_TYPES_GUN)
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

                if ( _mislType == MISL_BOMB && bct->_position.y < _mislStartHeight )
                    continue;

                World::rbcolls *v82 = bct->getBACT_collNodes();

                int v7;
                if ( v82 )
                    v7 = v82->roboColls.size();
                else
                    v7 = 1;

                for (int j = v7 - 1; j >= 0; j--)
                {
                    float radius;
                    vec3d ttmp;

                    if ( v82 )
                    {
                        World::TRoboColl *v8 = &v82->roboColls[j];
                        radius = v8->robo_coll_radius;

                        ttmp = bct->_position + bct->_rotation.Transpose().Transform(v8->coll_pos);
                    }
                    else
                    {
                        ttmp = bct->_position;
                        radius = bct->_radius;
                    }

                    if ( !v82 || radius >= 0.01 )
                    {
                        vec3d to_enemy = ttmp - _old_pos;
                        vec3d dist_vect = _position - _old_pos;

                        if ( to_enemy.dot( _rotation.AxisZ() )>= 0.3 )
                        {
                            float dist_vect_len = dist_vect.normalise();

                            vec3d vp = dist_vect * to_enemy;

                            // Experimental OpenUA behavior: unit collision uses only weapon.radius.
                            // Legacy class-specific weapon radii are parsed but intentionally ignored.
                            float wpn_radius = _radius;

                            float vp_len = vp.length();
                            float to_enemy_len = to_enemy.length();

                            if ( radius + wpn_radius > vp_len )
                            {
                                /*  Tube collision test, not cylinder!
                                    Will hit only when distance ~ wpn_radius */
                                if ( sqrt( POW2(dist_vect_len) + POW2(vp_len) ) > fabs(to_enemy_len - wpn_radius) )
                                {
                                    collisionSumRadius += radius;
                                    collisionCount++;
                                    collisionSumPosition += bct->_position;

                                    if ( hitTarget && !*hitTarget )
                                        *hitTarget = bct;

                                    if ( applyDirectDamage )
                                    {
                                        ApplyDirectHitToBact(bct);
                                    }

                                    break;
                                }
                            }
                        }
                    }
                }

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

    float shieldedDamage = damage * (100.0f - bct->GetEffectiveShield());
    float divisor = ( bct->getBACT_inputting() || bct->getBACT_viewer() ) ? 250.0 : 100.0;

    return ceil(shieldedDamage / divisor);
}

int NC_STACK_ypamissile::ApplyDamageToBact(NC_STACK_ypabact *bct, int baseEnergy)
{
    if ( !bct )
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
        bct->ApplyWeaponDebuff(wproto->debuff, _mislEmitter);
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
        bct->ApplyWeaponDebuff(wproto->debuff, _mislEmitter);

    return damage;
}

void NC_STACK_ypamissile::ApplyDirectHitToBact(NC_STACK_ypabact *bct)
{
    if ( !bct )
        return;

    bct->_status_flg &= ~BACT_STFLAG_LAND;
    RememberDirectHitUnit(bct);
    ApplyDamageToBact(bct, _energy);
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

    if ( _mislType == MISL_BOMB && bct->_position.y < _mislStartHeight )
        return "bomb_below_start_height";

    return NULL;
}

// Push eligibility filter for aoe_unit_push.
// More permissive than GetAreaDamageSkipReason on purpose:
// units that just got killed by this same explosion (direct hit or strong AoE)
// MUST still be thrown, so we do NOT skip BACT_STATUS_DEAD / BACT_STFLAG_DEATH1 here.
// Only the final death-FX wreck (DEATH2) is left undisturbed, matching legacy Impact().
const char *NC_STACK_ypamissile::GetAreaPushSkipReason(NC_STACK_ypabact *bct, bool allowFriendly) const
{
    if ( !bct || bct == this || bct == _mislEmitter )
        return "self";

    if ( bct->_bact_type == BACT_TYPES_MISSLE )
        return "missile";

    // Robo / Host Station are always immune to push.
    if ( bct->_bact_type == BACT_TYPES_ROBO )
        return "robo";

    // Final death-FX wreck: do not disturb (same guard as legacy Impact()).
    if ( bct->_status_flg & BACT_STFLAG_DEATH2 )
        return "death2";

    if ( bct->_bact_type == BACT_TYPES_GUN && bct->GetEffectiveShield() >= 100.0f )
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
            if ( bct->_bact_type == BACT_TYPES_GUN )
            {
                NC_STACK_ypagun *bgun = dynamic_cast<NC_STACK_ypagun *>( bct );

                if ( bgun && bgun->IsRoboGun() && !bct->_isUnitGunChild )
                    return "own_robo_gun";
            }
        }
    }

    return NULL;
}

bool NC_STACK_ypamissile::IsDirectHitUnit(NC_STACK_ypabact *bct) const
{
    return std::find(_mislDirectHitUnits.begin(), _mislDirectHitUnits.end(), bct) != _mislDirectHitUnits.end();
}

void NC_STACK_ypamissile::RememberDirectHitUnit(NC_STACK_ypabact *bct)
{
    if ( bct && !IsDirectHitUnit(bct) )
        _mislDirectHitUnits.push_back(bct);
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
    bool doAoePush   = (_mislAoeUnitPush > 0);

    if ( _mislAoeUnitRadius <= 0.0 || (!doAoeDamage && !doAoePush) || !_world )
        return;

    bool allowFriendly = getBACT_viewer();

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
                // Damage and push use SEPARATE eligibility filters.
                // Damage keeps the strict filter (no re-hitting dead/dying units).
                // Push uses a permissive filter so units killed by THIS explosion
                // (direct hit or strong AoE) are still thrown (Bug #2/#3/#4 fix).
                const char *dmgSkip  = GetAreaDamageSkipReason(bct, allowFriendly);
                const char *pushSkip = doAoePush ? GetAreaPushSkipReason(bct, allowFriendly) : "push_disabled";

                if ( dmgSkip && pushSkip )
                    continue;

                vec3d delta = bct->_position - _position;
                float distance = delta.length();

                if ( distance > _mislAoeUnitRadius )
                    continue;

                if ( std::find(damagedUnits.begin(), damagedUnits.end(), bct) != damagedUnits.end() )
                    continue;

                damagedUnits.push_back(bct);

                // Uniform knockback for every unit class.
                // We hand the unit a residual knockback (AddAoePush) that its own
                // per-frame UpdateAoePush() then plays out smoothly over time. This
                // avoids both the instant "teleport" of a direct position move and the
                // class-dependent chaos of touching _fly_dir (some classes never
                // integrate velocity while idle). Result: tanks, flyers and UFOs all
                // get the SAME smooth push, independent of mass and ground/air state.
                if ( doAoePush && !pushSkip )
                {
                    // Radial 3D direction away from the blast, with a safe fallback
                    // when the unit sits exactly at the blast center.
                    vec3d pushDir;
                    if ( distance > 1.0f )
                        pushDir = delta / distance;
                    else
                        pushDir = vec3d(1.0f, 0.0f, 0.0f);

                    // Knockback distance (in world units) = aoe_unit_push, optionally
                    // reduced by linear falloff when aoe_falloff = 1.
                    float pushStrength = (float)_mislAoeUnitPush;
                    if ( _mislAoeFalloff )
                    {
                        float t = 1.0f - distance / _mislAoeUnitRadius;
                        if ( t < 0.0f ) t = 0.0f;
                        pushStrength *= t;
                    }

                    if ( pushStrength > 0.0f )
                        bct->AddAoePush(pushDir, pushStrength);
                }

                // AoE damage skips direct-hit units (they already received direct damage)
                // and anything the strict filter rejected (dead/dying/friendly/...).
                if ( doAoeDamage && !dmgSkip && !IsDirectHitUnit(bct) )
                {
                    int areaEnergy = ypamissile_ScaleAoeEnergy(_mislAoeUnitEnergy, ypamissile_AoeFalloffFactor(distance, _mislAoeUnitRadius, _mislAoeFalloff != 0));
                    if ( areaEnergy > 0 )
                        ApplyDamageToBact(bct, areaEnergy);
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
        ApplyDirectHitToBact(target);
}

vec3d NC_STACK_ypamissile::CalcForceVector()
{
    _thraction = _force;

    return vec3d::Normalise(  _fly_dir * _fly_dir_length * _airconst
                            + _target_dir * _thraction
                            - vec3d(0.0, _mass * 9.80665, 0.0));
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

    vec3d axis = current * desired;
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

            default:
                break;
            }

            if ( _mislType == MISL_INTERNAL )
                return;

            NC_STACK_ypabact *hitTarget = NULL;
            if ( TubeCollisionTest(_mislDelayTime <= 0, &hitTarget) )
            {
                ResetViewing();

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
                AlignMissileByNormal( arg136.skel->polygons[ arg136.polyID ].Normal() );

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
                _mislDriveTime -= arg->frameTime;

                if ( _mislDriveTime < 0 )
                {
                    _mislType = MISL_BOMB;

                    _airconst = 10.0;
                    _airconst_static = 10.0;
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

    _mislFlags  = 0;
    _mislDelayTime = 0;
    _mislAoeFalloff = 0;
    _mislClusterAge = 0;
    _mislClusterDone = false;
    _mislClusterChild = false;
    _mislAttachedToTarget = false;
    _mislAttachTargetGid = 0;
    _mislAttachOffset = vec3d(0.0, 0.0, 0.0);
    _mislLastAttachedPosition = vec3d(0.0, 0.0, 0.0);
    SFXEngine::SFXe.StopCarrier(&_mislClusterSoundCarrier);
    _mislClusterSoundCarrier.Clear();

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
        SetVP(_vp_dead);

        SFXEngine::SFXe.startSound(&_soundcarrier, 2);

        if ( !StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_DETONATE) )
            StartDestFXByType(World::DestFX::FX_DEATH);

        _fly_dir_length = 0;
    }

    if ( arg->newStatus == BACT_STATUS_NORMAL )
    {
        SetVP(_vp_normal);

        SFXEngine::SFXe.startSound(&_soundcarrier, 0);
    }

    if ( arg->unsetFlags == BACT_STFLAG_DEATH2 )
    {
        SetVP(_vp_normal);

        SFXEngine::SFXe.startSound(&_soundcarrier, 0);
    }

    if ( arg->setFlags == BACT_STFLAG_DEATH2 )
    {
        _status = BACT_STATUS_DEAD;

        SetVP(_vp_megadeth);

        SFXEngine::SFXe.startSound(&_soundcarrier, 2);

        if ( !StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_IMPACT_WORLD) )
            StartDestFXByType(World::DestFX::FX_MEGADETH);

        _fly_dir_length = 0;
    }

    return 1;
}

void NC_STACK_ypamissile::ResetViewing()
{
    if ( getBACT_viewer() )
    {
        setBACT_viewer(false);
        setBACT_inputting(false);

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

    for( NC_STACK_ypabact* &bct : _pSector->unitsList )
    {
        if ( bct->_bact_type != BACT_TYPES_MISSLE && bct->_bact_type != BACT_TYPES_ROBO && bct->_bact_type != BACT_TYPES_TANK && bct->_bact_type != BACT_TYPES_CAR && bct->_bact_type != BACT_TYPES_GUN && bct->_bact_type != BACT_TYPES_HOVER && !(bct->_status_flg & BACT_STFLAG_DEATH2) )
        {
            int v10 = 1;

            if ( _world->_isNetGame )
            {
                if ( _owner != bct->_owner )
                    v10 = 0;
            }

            if ( v10 )
            {
                if ( bct->_status == BACT_STATUS_DEAD ||
                     (bct->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) )
                {
                    continue;
                }

                bct->ApplyImpulse(&arg83);
            }
        }
    }

    if ( _world->_isNetGame )
    {
        uamessage_impulse impMsg;
        impMsg.msgID = UAMSG_IMPULSE;
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

    // F10 debug overlay: record transient AoE rings at the impact point (no gameplay effect).
    if ( _world && _world->_showCollDebug )
    {
        _world->DebugAddAoeRing(_position, _mislAoeUnitRadius,     255, 140, 0);   // unit AoE: orange
        _world->DebugAddAoeRing(_position, _mislAoeBuildingRadius, 200, 80, 220);  // building AoE: purple
        _world->DebugAddAoeRing(_position, _mislAoeSectorRadius,    80, 200, 220); // sector AoE: light cyan
    }

    _mislDirectHitUnits.clear();
    _mislDirectHitBuildings.clear();
    _mislDirectHitSectors.clear();
}

void NC_STACK_ypamissile::DetonateAtContact(NC_STACK_ypabact *directHit)
{
    DetonateSeekAndExplodePayload(directHit);
}

void NC_STACK_ypamissile::DetonateSeekAndExplodePayload(NC_STACK_ypabact *directHit)
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

    if ( !StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_IMPACT_WORLD) )
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
    _mislRadiusHeli = 0.;
    _mislRadiusTank = 0.;
    _mislRadiusFlyer = 0.;
    _mislRadiusRobo = 0.;
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

void NC_STACK_ypamissile::SetAoeUnitPush(int push)
{
    _mislAoeUnitPush = push;
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
