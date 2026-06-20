#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stack>
#include <algorithm>
#include <limits>
#include <vector>
#include "yw.h"
#include "ypabact.h"
#include "yparobo.h"
#include "ypagun.h"
#include "ypamissile.h"
#include "yw_net.h"

#include "world/clonebalance.h"

#include "log.h"


int ypabact_id = 1;
extern int dword_5B1128;

static bool ypabact_IsSeekAndExplodeArmed(NC_STACK_ypabact *unit);
static void ypabact_FireProximityDefenseAtDeath(NC_STACK_ypabact *unit);

static void ypabact_ResetDamagedFX(NC_STACK_ypabact *bact)
{
    bact->_damaged_fx = World::TDamagedFXConfig();
    bact->_damaged_fx_next_time = 0;
}

static bool ypabact_IsDamagedFXSystemDisabled(const NC_STACK_ypabact *bact)
{
    return bact->_damaged_fx.threshold <= 0.0 || bact->_damaged_fx.threshold >= 1.0;
}

static float ypabact_GetDamagedThreshold(const NC_STACK_ypabact *bact)
{
    float threshold = bact->_damaged_fx.threshold;

    if ( threshold < 0.0 )
        return 0.0;

    if ( threshold > 1.0 )
        return 1.0;

    return threshold;
}

bool NC_STACK_ypabact::ShouldHideFromStrategicUI() const
{
    if ( _isUnitGunChild || _isDummy )
        return true;

    if ( _bact_type != BACT_TYPES_GUN )
        return false;

    const NC_STACK_ypagun *gun = dynamic_cast<const NC_STACK_ypagun *>(this);
    return gun && (gun->_gunFlags & NC_STACK_ypagun::GUN_FLAGS_ROBO);
}

static bool ypabact_CanUseGameplayStatusMechanics(NC_STACK_ypabact *bact)
{
    // Gameplay status mechanics must not depend on strategic/UI visibility.
    // Some real gameplay actors, such as host/flak/attached gun objects, may be
    // intentionally hidden from strategic UI while still being valid damageable
    // actors. UI code can still filter them separately before rendering icons.
    return bact &&
           bact->getBACT_pWorld() &&
           bact->_owner != World::OWNER_0 &&
           bact->_energy > 0 &&
           bact->_energy_max > 0 &&
           bact->_bact_type != BACT_TYPES_MISSLE &&
           bact->_status != BACT_STATUS_DEAD &&
           bact->_status != BACT_STATUS_CREATE &&
           bact->_status != BACT_STATUS_BEAM &&
           !(bact->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER));
}

static bool ypabact_CanSpawnDecorationFX(NC_STACK_ypabact *bact)
{
    return bact &&
           bact->getBACT_pWorld() &&
           bact->_energy > 0 &&
           bact->_status != BACT_STATUS_DEAD &&
           bact->_status != BACT_STATUS_CREATE &&
           bact->_status != BACT_STATUS_BEAM &&
           !(bact->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER));
}

static bool ypabact_IsUsableControlFallback(NC_STACK_ypabact *bact, NC_STACK_ypabact *dying)
{
    return bact &&
           bact != dying &&
           bact->_status != BACT_STATUS_DEAD &&
           !(bact->_status_flg & BACT_STFLAG_DEATH1) &&
           !bact->IsMortarPlatform(); // OpenUA: mortars are never a control fallback
}

static void ypabact_SafeDetachControlFrom(NC_STACK_ypabact *dying, NC_STACK_ypabact *preferredFallback);

static bool ypabact_IsMindcontrolUnitType(NC_STACK_ypabact *bact)
{
    if ( !bact )
        return false;

    if ( bact->_bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(bact);
        if ( !gun || gun->IsRoboGun() )
            return false;
    }

    switch ( bact->_bact_type )
    {
    case BACT_TYPES_BACT:
    case BACT_TYPES_TANK:
    case BACT_TYPES_FLYER:
    case BACT_TYPES_UFO:
    case BACT_TYPES_CAR:
    case BACT_TYPES_GUN:
    case BACT_TYPES_HOVER:
        return true;

    default:
        return false;
    }
}

static bool ypabact_CanBeMindcontrolled(NC_STACK_ypabact *target, NC_STACK_ypabact *source)
{
    return target &&
           source &&
           target != source &&
           target->getBACT_pWorld() &&
           target->_bact_type != BACT_TYPES_ROBO &&
           target->_bact_type != BACT_TYPES_MISSLE &&
           target->_owner != source->_owner &&
           source->_owner != World::OWNER_0 &&
           target->_energy > 0 &&
           target->_energy_max > 0 &&
           ypabact_IsMindcontrolUnitType(target) &&
           target->_status != BACT_STATUS_DEAD &&
           target->_status != BACT_STATUS_CREATE &&
           target->_status != BACT_STATUS_BEAM &&
           !(target->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2));
}

static NC_STACK_yparobo *ypabact_GetMindcontrolHost(NC_STACK_ypabact *source)
{
    if ( !source )
        return NULL;

    if ( source->_host_station &&
         source->_host_station->_status != BACT_STATUS_DEAD &&
         !(source->_host_station->_status_flg & BACT_STFLAG_DEATH1) )
        return source->_host_station;

    if ( source->_bact_type == BACT_TYPES_ROBO &&
         source->_status != BACT_STATUS_DEAD &&
         !(source->_status_flg & BACT_STFLAG_DEATH1) )
        return dynamic_cast<NC_STACK_yparobo *>(source);

    return NULL;
}

static void ypabact_ApplyMindcontrol(NC_STACK_ypabact *target, NC_STACK_ypabact *source)
{
    if ( !ypabact_CanBeMindcontrolled(target, source) )
        return;

    NC_STACK_ypaworld *world = target->getBACT_pWorld();
    NC_STACK_yparobo *newHost = ypabact_GetMindcontrolHost(source);
    uint8_t oldOwner = target->_owner;
    uint8_t newOwner = source->_owner;

    if ( world->_userUnit == target || world->_viewerBact == target )
        ypabact_SafeDetachControlFrom(target, world->_userRobo);

    target->_owner = newOwner;
    target->_m_owner = 0;
    target->_killer = NULL;
    target->_killer_owner = 0;

    if ( oldOwner < world->_countUnitsPerOwner.size() && world->_countUnitsPerOwner[oldOwner] > 0 )
        world->_countUnitsPerOwner[oldOwner]--;

    if ( newOwner < world->_countUnitsPerOwner.size() )
        world->_countUnitsPerOwner[newOwner]++;

    if ( newHost )
    {
        target->_host_station = newHost;
        int commandId = newHost->getROBO_commCount();
        target->_commandID = commandId;
        if ( world->_isNetGame )
            target->_commandID |= newOwner << 24;
        newHost->setROBO_commCount(commandId + 1);

        if ( target->_parent != newHost )
            newHost->AddSubject(target);
    }

    target->_primTtype = BACT_TGT_TYPE_NONE;
    target->_secndTtype = BACT_TGT_TYPE_NONE;
    target->_assess_time = 0;
}

static void ypabact_SafeDetachControlFrom(NC_STACK_ypabact *dying, NC_STACK_ypabact *preferredFallback)
{
    NC_STACK_ypaworld *world = dying->getBACT_pWorld();
    if ( !world )
        return;

    bool controlled = world->_userUnit == dying || world->_viewerBact == dying;
    bool hovered = world->_bactOnMouse == dying;

    if ( world->_guiVisor.field_18 == dying )
        world->_guiVisor.field_18 = NULL;

    if ( hovered )
    {
        world->_bactOnMouse = NULL;
        world->_guiActFlags &= ~0x20;
    }

    if ( !controlled )
        return;

    NC_STACK_ypabact *fallback = NULL;
    if ( ypabact_IsUsableControlFallback(preferredFallback, dying) )
        fallback = preferredFallback;
    else if ( ypabact_IsUsableControlFallback(dying->_parent, dying) && !dying->_parent->ShouldHideFromStrategicUI() )
        fallback = dying->_parent;
    else if ( ypabact_IsUsableControlFallback(world->_userRobo, dying) )
        fallback = world->_userRobo;

    dying->setBACT_inputting(false);
    dying->setBACT_viewer(false);

    if ( fallback )
    {
        fallback->setBACT_inputting(true);
        fallback->setBACT_viewer(true);
        world->_viewerBact = fallback;
        world->setYW_userVehicle(fallback);
    }

    world->_playerInHSGun = false;
}

static bool ypabact_IsDamagedStateActive(const NC_STACK_ypabact *bact)
{
    if ( ypabact_IsDamagedFXSystemDisabled(bact) || bact->_energy <= 0 || bact->_energy_max <= 0 )
        return false;

    float threshold = ypabact_GetDamagedThreshold(bact);
    if ( threshold <= 0.0 || threshold >= 1.0 )
        return false;

    return ((float)bact->_energy / (float)bact->_energy_max) <= threshold;
}

static float ypabact_SafeDamageMult(float mult)
{
    return mult >= 0.0 ? mult : 1.0;
}

static float ypabact_DebuffMalusToMult(float malus)
{
    if ( malus < 0.0 )
        malus = 0.0;
    else if ( malus > 1.0 )
        malus = 1.0;

    return 1.0 - malus;
}

static vec3d ypabact_BuildRandomAttachedFXOffset(float radius, float overeof)
{
    if ( radius < 0.0 )
        radius = 0.0;

    vec3d localOffset(0.0, 0.0, 0.0);

    if ( radius > 0.0 )
    {
        float angle = ((float)rand() / (float)RAND_MAX) * (2.0 * C_PI);
        float dist = ((float)rand() / (float)RAND_MAX) * radius;

        localOffset.x += cos(angle) * dist;
        localOffset.z += sin(angle) * dist;
    }

    float heightOffset = overeof * 0.25;
    if ( heightOffset < 5.0 )
        heightOffset = 5.0;
    else if ( heightOffset > 60.0 )
        heightOffset = 60.0;

    localOffset.y -= heightOffset;
    return localOffset;
}

static int ypabact_GetDebuffFXLifetime(const TActiveDebuffState &debuff)
{
    int lifetime = debuff.tick_time + 100;

    if ( lifetime < 1000 )
        lifetime = 1000;
    else if ( lifetime > 5000 )
        lifetime = 5000;

    return lifetime;
}

static void ypabact_SpawnDebuffFXEvent(NC_STACK_ypabact *bact, int lifetime)
{
    if ( !bact || !bact->_active_debuff.active )
        return;

    NC_STACK_ypaworld *world = bact->getBACT_pWorld();
    if ( !world )
        return;

    for (int16_t fxVp : bact->_active_debuff.fx_vps)
    {
        if ( fxVp <= 0 )
            continue;

        vec3d localOffset = ypabact_BuildRandomAttachedFXOffset(bact->_active_debuff.fx_random_pos, bact->_overeof);
        world->SpawnAttachedTransientVP(fxVp, bact, localOffset, lifetime);
    }
}

static void ypabact_ApplyDamagedRuntime(NC_STACK_ypabact *bact, bool active)
{
    bact->_damaged_fx_active = active;

    float forceMult = active ? ypabact_DebuffMalusToMult(bact->_damaged_force_malus) : 1.0;
    float maxrotMult = active ? ypabact_DebuffMalusToMult(bact->_damaged_maxrot_malus) : 1.0;

    if ( bact->_active_debuff.active )
    {
        forceMult *= ypabact_DebuffMalusToMult(bact->_active_debuff.force_malus);
        maxrotMult *= ypabact_DebuffMalusToMult(bact->_active_debuff.maxrot_malus);
    }

    // OpenUA Black Sect clone balance: imperfect grey clones (owner 5) get slightly
    // weaker thrust and turn rate. This runs every frame and always recomputes the
    // effective _force/_maxrot from the unmodified _base_force/_base_maxrot, so the
    // clone malus is folded into the same multiplier chain as the debuff/damaged
    // maluses and can never accumulate over time or across save/load/respawn.
    if ( World::CloneBalance::IsCloneActor(bact) )
    {
        float cloneFactor = World::CloneBalance::DownFactor();
        forceMult *= cloneFactor;
        maxrotMult *= cloneFactor;
    }

    bact->_force = bact->_base_force * forceMult;
    bact->_maxrot = bact->_base_maxrot * maxrotMult;
}

static int ypabact_ScaledPitch(TSoundSource &snd, int basePitch, float mult)
{
    if ( mult == 1.0 )
        return basePitch;

    if ( snd.PSample && snd.PSample->SampleRate > 0 )
    {
        float rate = (float)(snd.PSample->SampleRate + basePitch) * mult;

        // Very low or invalid playback rates can make some looping vehicle sounds
        // effectively disappear, especially heli/hover idle loops. Keep the
        // effective rate valid while still allowing obvious pitch reduction.
        float minRate = (float)snd.PSample->SampleRate * 0.10f;
        if ( !isnormal(rate) || rate < minRate )
            rate = minRate;

        return (int)rate - snd.PSample->SampleRate;
    }

    return (int)(basePitch * mult);
}

static void ypabact_ApplyDamagedSoundPitch(NC_STACK_ypabact *bact)
{
    if ( bact->_soundcarrier.Sounds.size() <= World::TVhclProto::SND_WAIT )
        return;

    float pitchMult = 1.0;

    if ( bact->_damaged_fx_active )
        pitchMult *= ypabact_SafeDamageMult(bact->_damaged_snd_pitch_mult);

    if ( bact->_active_debuff.active )
        pitchMult *= ypabact_SafeDamageMult(bact->_active_debuff.snd_pitch_mult);

    // OpenUA Black Sect clone balance: imperfect grey clones (owner 5) emit their
    // engine/idle loops at a slightly lower pitch, reinforcing the clone identity.
    // Folded into the same per-frame pitch chain as the damaged/debuff multipliers,
    // recomputed from the prototype base pitch each frame (never compounds).
    if ( World::CloneBalance::IsCloneActor(bact) )
        pitchMult *= World::CloneBalance::DownFactor();

    TSoundSource &normal = bact->_soundcarrier.Sounds[World::TVhclProto::SND_NORMAL];
    TSoundSource &wait = bact->_soundcarrier.Sounds[World::TVhclProto::SND_WAIT];

    if ( pitchMult != 1.0 )
    {
        normal.Pitch = ypabact_ScaledPitch(normal, normal.Pitch, pitchMult);

        // SND_WAIT can be the active loop for heli hover/idle. Unlike SND_NORMAL,
        // it is not always rebuilt by Move(), so scale it from the prototype base
        // pitch instead of repeatedly scaling the previous frame's pitch.
        wait.Pitch = ypabact_ScaledPitch(wait, bact->_base_snd_wait_pitch, pitchMult);
    }
}

static bool ypabact_EnsureDamagedShakeCarrier(NC_STACK_ypabact *bact)
{
    if ( !bact || !bact->getBACT_pWorld() )
        return false;

    std::vector<World::TVhclProto> &protos = bact->getBACT_pWorld()->GetVhclProtos();

    if ( bact->_vehicleID >= protos.size() )
        return false;

    World::TDamagedFXConfig &damagedFX = protos[bact->_vehicleID].damaged_fx;
    if ( damagedFX.shake.slot == 0 )
        return false;

    if ( bact->_damaged_shake_carrier.Sounds.empty() )
        bact->_damaged_shake_carrier.Resize(1);

    TSoundSource &snd = bact->_damaged_shake_carrier.Sounds[0];
    snd.PSample = NULL;
    snd.SampleVariants.clear();
    snd.Volume = 0;
    snd.Pitch = 0;
    snd.PriorityBias = 0;
    snd.SetLoop(false);
    snd.SetFragmented(false);
    snd.PPFx = NULL;
    snd.SetPFx(false);
    snd.PShkFx = &damagedFX.shake;
    snd.SetShk(true);

    return true;
}

static void ypabact_UpdateStatusSoundCarrier(NC_STACK_ypabact *bact, TSndCarrier *carrier)
{
    if ( !bact || !carrier || carrier->Sounds.empty() )
        return;

    TSoundSource &snd = carrier->Sounds[0];
    if ( !snd.IsEnabled() && !snd.IsPFxEnabled() && !snd.IsShkEnabled() )
        return;

    carrier->Position = bact->_position;
    carrier->Vector = bact->_fly_dir * bact->_fly_dir_length;

    SFXEngine::SFXe.UpdateSoundCarrier(carrier);
}

static void ypabact_StartStatusSoundIfIdle(NC_STACK_ypabact *bact, TSndCarrier *carrier, int volume, int pitch)
{
    if ( !bact || !carrier || carrier->Sounds.empty() )
        return;

    carrier->Position = bact->_position;
    carrier->Vector = bact->_fly_dir * bact->_fly_dir_length;

    TSoundSource &snd = carrier->Sounds[0];
    snd.Volume = volume;
    snd.Pitch = pitch;
    snd.PriorityBias = 0;

    if ( !snd.IsEnabled() && !snd.IsPFxEnabled() && !snd.IsShkEnabled() )
    {
        SFXEngine::SFXe.startSound(carrier, 0);
        SFXEngine::SFXe.UpdateSoundCarrier(carrier);
    }
}

static NC_STACK_ypabact *ypabact_FindLiveBactByGid(World::RefBactList &list, int32_t gid)
{
    for (NC_STACK_ypabact *unit : list)
    {
        if ( unit->_gid == gid )
        {
            if ( unit->_kidRef.IsListType(World::BLIST_CACHE) || unit->_status == BACT_STATUS_DEAD )
                return NULL;

            return unit;
        }

        NC_STACK_ypabact *kid = ypabact_FindLiveBactByGid(unit->_kidList, gid);
        if ( kid )
            return kid;
    }

    return NULL;
}

static bool ypabact_IsCarrierSpawnAliveUnit(NC_STACK_ypabact *unit)
{
    if ( !unit )
        return false;

    if ( unit->_kidRef.IsListType(World::BLIST_CACHE) )
        return false;

    if ( unit->_status == BACT_STATUS_DEAD )
        return false;

    if ( unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2) )
        return false;

    return true;
}

static int ypabact_CountCarrierSpawnedUnits(NC_STACK_ypabact *carrier)
{
    NC_STACK_ypaworld *world = carrier->getBACT_pWorld();

    if ( !world )
        return 0;

    int aliveCount = 0;

    for (auto it = carrier->_carrier_spawned_gids.begin(); it != carrier->_carrier_spawned_gids.end();)
    {
        NC_STACK_ypabact *unit = ypabact_FindLiveBactByGid(world->_unitsList, *it);

        if ( ypabact_IsCarrierSpawnAliveUnit(unit) )
        {
            aliveCount++;
            ++it;
        }
        else
        {
            it = carrier->_carrier_spawned_gids.erase(it);
        }
    }

    return aliveCount;
}

static bool ypabact_CanCarrierSpawn(NC_STACK_ypabact *carrier)
{
    if ( !carrier || !carrier->getBACT_pWorld() )
        return false;

    if ( !carrier->_spawn_units )
        return false;

    if ( carrier->_spawn_vehicle <= 0 || (size_t)carrier->_spawn_vehicle >= carrier->getBACT_pWorld()->GetVhclProtos().size() )
        return false;

    if ( carrier->_spawn_trigger_radius <= 0.0 )
        return false;

    if ( carrier->_carrier_spawn_root_vehicle > 0 && carrier->_spawn_vehicle == carrier->_carrier_spawn_root_vehicle )
        return false;

    if ( carrier->_owner == World::OWNER_0 )
        return false;

    if ( carrier->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( carrier->_status == BACT_STATUS_DEAD ||
         carrier->_status == BACT_STATUS_CREATE ||
         carrier->_status == BACT_STATUS_BEAM )
        return false;

    if ( carrier->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER) )
        return false;

    return true;
}

static bool ypabact_IsCarrierSpawnEnemy(NC_STACK_ypabact *carrier, NC_STACK_ypabact *unit)
{
    if ( !ypabact_IsCarrierSpawnAliveUnit(unit) )
        return false;

    if ( unit == carrier )
        return false;

    if ( unit->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( unit->_status == BACT_STATUS_CREATE || unit->_status == BACT_STATUS_BEAM )
        return false;

    if ( unit->_owner == World::OWNER_0 || unit->_owner == carrier->_owner )
        return false;

    return true;
}

static bool ypabact_IsLaserDamageTarget(NC_STACK_ypabact *shooter, NC_STACK_ypabact *unit)
{
    if ( !ypabact_IsCarrierSpawnAliveUnit(unit) )
        return false;

    if ( unit == shooter )
        return false;

    if ( unit->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( unit->_status == BACT_STATUS_CREATE || unit->_status == BACT_STATUS_BEAM )
        return false;

    if ( unit->_status_flg & BACT_STFLAG_NORENDER )
        return false;

    return true;
}

static bool ypabact_IsLaserAimTarget(NC_STACK_ypabact *shooter, NC_STACK_ypabact *unit)
{
    if ( !ypabact_IsLaserDamageTarget(shooter, unit) )
        return false;

    if ( !shooter )
        return true;

    if ( unit->_owner == World::OWNER_0 || unit->_owner == shooter->_owner )
        return false;

    return true;
}

static bool ypabact_HasEnemyNearby(NC_STACK_ypabact *carrier, float radius)
{
    NC_STACK_ypaworld *world = carrier->getBACT_pWorld();
    if ( !world )
        return false;

    if ( radius <= 0.0 )
        return false;

    float radiusSq = radius * radius;
    int sectorRadius = (int)(radius / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(carrier->_position);

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);

            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            for (NC_STACK_ypabact *unit : cell.unitsList)
            {
                if ( !ypabact_IsCarrierSpawnEnemy(carrier, unit) )
                    continue;

                if ( (unit->_position.XZ() - carrier->_position.XZ()).square() <= radiusSq )
                    return true;
            }
        }
    }

    return false;
}

static bool ypabact_CarrierHasEnemyNearby(NC_STACK_ypabact *carrier)
{
    return ypabact_HasEnemyNearby(carrier, carrier->_spawn_trigger_radius);
}

static bool ypabact_IsCarrierSpawnPositionValid(NC_STACK_ypabact *carrier, const vec3d &pos)
{
    NC_STACK_ypaworld *world = carrier->getBACT_pWorld();
    if ( !world )
        return false;

    yw_130arg sect;
    sect.pos_x = pos.x;
    sect.pos_z = pos.z;

    if ( !world->GetSectorInfo(&sect) || !sect.pcell )
        return false;

    return true;
}

static bool ypabact_FindCarrierSpawnPosition(NC_STACK_ypabact *carrier, vec3d *outPos)
{
    int attempts = carrier->_spawn_random_pos > 0.0 ? 8 : 1;

    for (int i = 0; i < attempts; i++)
    {
        vec3d pos = carrier->_position;

        if ( carrier->_spawn_random_pos > 0.0 )
        {
            float angle = ((float)rand() / (float)RAND_MAX) * (2.0 * C_PI);
            float dist = ((float)rand() / (float)RAND_MAX) * carrier->_spawn_random_pos;
            vec3d localOffset(cos(angle) * dist, 0.0, sin(angle) * dist);

            pos += carrier->_rotation.Transpose().Transform(localOffset);
        }

        if ( ypabact_IsCarrierSpawnPositionValid(carrier, pos) )
        {
            *outPos = pos;
            return true;
        }
    }

    if ( carrier->_spawn_random_pos > 0.0 && ypabact_IsCarrierSpawnPositionValid(carrier, carrier->_position) )
    {
        *outPos = carrier->_position;
        return true;
    }

    return false;
}

static NC_STACK_ypabact *ypabact_CreateCarrierSpawnedUnit(NC_STACK_ypabact *carrier, const vec3d &pos)
{
    NC_STACK_ypaworld *world = carrier->getBACT_pWorld();
    if ( !world )
        return NULL;

    ypaworld_arg146 arg146;
    arg146.vehicle_id = carrier->_spawn_vehicle;
    arg146.pos = pos;

    NC_STACK_ypabact *unit = world->ypaworld_func146(&arg146);
    if ( !unit )
        return NULL;

    unit->_owner = carrier->_owner;
    unit->_host_station = carrier->_host_station;
    unit->_carrier_spawn_root_gid = carrier->_carrier_spawn_root_gid ? carrier->_carrier_spawn_root_gid : carrier->_gid;
    unit->_carrier_spawn_root_vehicle = carrier->_carrier_spawn_root_vehicle > 0 ? carrier->_carrier_spawn_root_vehicle : carrier->_vehicleID;

    if ( unit->_spawn_units )
        unit->_spawn_last_time = unit->_clock > 0 ? unit->_clock : 1;

    NC_STACK_yparobo *carrierRobo = dynamic_cast<NC_STACK_yparobo *>(carrier);
    if ( !unit->_host_station )
        unit->_host_station = carrierRobo;

    unit->setBACT_bactCollisions(carrier->getBACT_bactCollisions());

    if ( !carrier->_spawn_instant )
    {
        setState_msg state;
        state.setFlags = 0;
        state.unsetFlags = 0;
        state.newStatus = BACT_STATUS_CREATE;
        unit->SetState(&state);

        unit->_scale_time = unit->_energy_max * 0.2;
    }

    world->HistoryAktCreate(unit);

    return unit;
}

static void ypabact_AttachCarrierSpawnLeader(NC_STACK_ypabact *carrier, NC_STACK_ypabact *leader)
{
    NC_STACK_ypaworld *world = carrier->getBACT_pWorld();
    if ( !world || !leader )
        return;

    NC_STACK_yparobo *carrierRobo = dynamic_cast<NC_STACK_yparobo *>(carrier);
    if ( carrierRobo )
        carrier->AddSubject(leader);
    else if ( carrier->_parent )
        carrier->_parent->AddSubject(leader);
    else
        world->ypaworld_func134(leader);
}

static NC_STACK_yparobo *ypabact_FindSpawnAtDeathOwnerRobo(NC_STACK_ypaworld *world, uint8_t owner)
{
    if ( !world )
        return NULL;

    for (NC_STACK_ypabact *unit : world->_unitsList)
    {
        NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(unit);
        if ( robo &&
             robo->_owner == owner &&
             robo->_status != BACT_STATUS_DEAD &&
             !(robo->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER)) )
            return robo;
    }

    return NULL;
}

static bool ypabact_IsSpawnAtDeathPositionValid(NC_STACK_ypabact *parent, const vec3d &pos)
{
    NC_STACK_ypaworld *world = parent->getBACT_pWorld();
    if ( !world )
        return false;

    yw_130arg sect;
    sect.pos_x = pos.x;
    sect.pos_z = pos.z;

    if ( !world->GetSectorInfo(&sect) || !sect.pcell )
        return false;

    return true;
}

static bool ypabact_FindSpawnAtDeathPosition(NC_STACK_ypabact *parent, vec3d *outPos)
{
    int attempts = parent->_spawn_at_death_random_pos > 0.0 ? 8 : 1;

    for (int i = 0; i < attempts; i++)
    {
        vec3d pos = parent->_position;

        if ( parent->_spawn_at_death_random_pos > 0.0 )
        {
            float angle = ((float)rand() / (float)RAND_MAX) * (2.0 * C_PI);
            float dist = ((float)rand() / (float)RAND_MAX) * parent->_spawn_at_death_random_pos;

            pos.x += cos(angle) * dist;
            pos.z += sin(angle) * dist;
        }

        if ( ypabact_IsSpawnAtDeathPositionValid(parent, pos) )
        {
            *outPos = pos;
            return true;
        }
    }

    if ( parent->_spawn_at_death_random_pos > 0.0 && ypabact_IsSpawnAtDeathPositionValid(parent, parent->_position) )
    {
        *outPos = parent->_position;
        return true;
    }

    return false;
}

static void ypabact_EnableSpawnAtDeathProtection(NC_STACK_ypabact *unit, int immunityTime)
{
    if ( !unit || immunityTime <= 0 )
        return;

    unit->_spawn_at_death_protection_end_time = unit->_clock + immunityTime;
    unit->_spawn_at_death_restore_vulnerable = !unit->_invulnerable;
    unit->_invulnerable = true;
}

static void ypabact_UpdateSpawnAtDeathProtection(NC_STACK_ypabact *unit)
{
    if ( !unit || unit->_spawn_at_death_protection_end_time <= 0 )
        return;

    if ( unit->_clock < unit->_spawn_at_death_protection_end_time )
        return;

    if ( unit->_spawn_at_death_restore_vulnerable )
        unit->_invulnerable = false;

    unit->_spawn_at_death_protection_end_time = 0;
    unit->_spawn_at_death_restore_vulnerable = false;
}

static NC_STACK_yparobo *ypabact_GetSpawnAtDeathOwnerRobo(NC_STACK_ypabact *parent)
{
    if ( !parent )
        return NULL;

    if ( parent->_host_station &&
         parent->_host_station != parent &&
         parent->_host_station->_status != BACT_STATUS_DEAD &&
         !(parent->_host_station->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) )
        return parent->_host_station;

    return ypabact_FindSpawnAtDeathOwnerRobo(parent->getBACT_pWorld(), parent->_owner);
}

static NC_STACK_ypabact *ypabact_CreateSpawnAtDeathUnit(NC_STACK_ypabact *parent, const vec3d &pos)
{
    NC_STACK_ypaworld *world = parent->getBACT_pWorld();
    if ( !world )
        return NULL;

    ypaworld_arg146 arg146;
    arg146.vehicle_id = parent->_spawn_at_death_vehicle;
    arg146.pos = pos;

    NC_STACK_ypabact *unit = world->ypaworld_func146(&arg146);
    if ( !unit )
        return NULL;

    unit->_owner = parent->_owner;
    unit->_carrier_spawn_root_gid = 0;
    unit->_carrier_spawn_root_vehicle = 0;
    unit->_aggr = parent->_aggr;
    ypabact_EnableSpawnAtDeathProtection(unit, parent->_spawn_at_death_immunity_time);

    if ( unit->_spawn_units )
        unit->_spawn_last_time = unit->_clock > 0 ? unit->_clock : 1;

    NC_STACK_yparobo *ownerRobo = ypabact_GetSpawnAtDeathOwnerRobo(parent);

    unit->_host_station = ownerRobo;
    if ( ownerRobo )
        unit->setBACT_bactCollisions(ownerRobo->getBACT_bactCollisions());
    else
        unit->setBACT_bactCollisions(parent->getBACT_bactCollisions());

    setTarget_msg target;
    target.tgt_type = BACT_TGT_TYPE_CELL;
    target.priority = 0;
    target.tgt_pos = pos;
    unit->SetTarget(&target);

    if ( !parent->_spawn_at_death_instant )
    {
        setState_msg state;
        state.setFlags = 0;
        state.unsetFlags = 0;
        state.newStatus = BACT_STATUS_CREATE;
        unit->SetState(&state);
        unit->_scale_time = unit->_energy_max * 0.2;
    }

    world->HistoryAktCreate(unit);

    return unit;
}

static void ypabact_AttachSpawnAtDeathLeader(NC_STACK_ypabact *parent, NC_STACK_ypabact *leader)
{
    NC_STACK_ypaworld *world = parent->getBACT_pWorld();
    if ( !world || !leader )
        return;

    NC_STACK_yparobo *ownerRobo = ypabact_GetSpawnAtDeathOwnerRobo(parent);
    if ( ownerRobo )
        ownerRobo->AddSubject(leader);
    else
        world->ypaworld_func134(leader);
}

static void ypabact_TrySpawnAtDeath(NC_STACK_ypabact *parent)
{
    if ( !parent ||
         parent->_spawn_at_death_done ||
         !parent->_spawn_at_death_units ||
         parent->_bact_type == BACT_TYPES_MISSLE ||
         parent->_energy > 0 ||
         (parent->_status_flg & BACT_STFLAG_CLEAN) )
        return;

    parent->_spawn_at_death_done = true;

    NC_STACK_ypaworld *world = parent->getBACT_pWorld();
    if ( !world || world->_isNetGame )
        return;

    const std::vector<World::TVhclProto> &protos = world->GetVhclProtos();
    if ( parent->_spawn_at_death_vehicle <= 0 || (size_t)parent->_spawn_at_death_vehicle >= protos.size() )
        return;

    const World::TVhclProto &proto = protos[parent->_spawn_at_death_vehicle];
    if ( proto.model_id == BACT_TYPES_NOPE )
        return;

    int spawnCount = parent->_spawn_at_death_count > 0 ? parent->_spawn_at_death_count : 1;
    if ( spawnCount > 8 )
        spawnCount = 8;

    NC_STACK_ypabact *squadLeader = NULL;
    int squadCommandId = dword_5B1128;

    for (int i = 0; i < spawnCount; i++)
    {
        vec3d spawnPos;
        if ( !ypabact_FindSpawnAtDeathPosition(parent, &spawnPos) )
            continue;

        NC_STACK_ypabact *unit = ypabact_CreateSpawnAtDeathUnit(parent, spawnPos);
        if ( !unit )
            continue;

        unit->_commandID = squadCommandId;

        if ( !squadLeader )
        {
            squadLeader = unit;
            ypabact_AttachSpawnAtDeathLeader(parent, squadLeader);
        }
        else
        {
            squadLeader->AddSubject(unit);
        }
    }

    if ( squadLeader )
        dword_5B1128++;
}


NC_STACK_ypabact::NC_STACK_ypabact()
: _kidList(this, GetKidRefNode, World::BLIST_KIDS)
{
    _wrldSize = vec2d();
    _wrldSectors = Common::Point();
    _bact_type = 0;
    _gid = 0;
    _vehicleID = 0;
    _bflags = 0;
    _commandID = 0;
    _host_station = NULL;
    _parent = NULL;
    
    _soundFlags = 0;
    _volume = 0;
    _pitch = 0;
    _pitch_max = 0.0;
    _base_snd_normal_pitch = 0;
    _base_snd_wait_pitch = 0;
    _energy = 0;
    _energy_max = 0;
    _invulnerable = false;
    _reload_const = 0;
    _shield = 0;
    _radar = 0;
    _owner = 0;
    _aggr = 0;
    _status = BACT_STATUS_NOPE;
    _status_flg = 0;
    _primTtype = 0;
    _secndTtype = 0;
    _primT_cmdID = 0;
    _secndT_cmdID = 0;
    _primT.pbact = NULL;
    _secndT.pbact = NULL;
    _adist_sector = 0.0;
    _adist_bact = 0.0;
    _sdist_sector = 0.0;
    _sdist_bact = 0.0;
    _current_waypoint = 0;
    _waypoints_count = 0;
    _m_cmdID = 0;
    _m_owner = 0;
    _fe_cmdID = 0;
    _fe_time = 0;
    _mass = 0.0;
    _base_force = 0.0;
    _base_maxrot = 0.0;
    _force = 0.0;
    _airconst = 0.0;
    _airconst_static = 0.0;
    _maxrot = 0.0;
    _viewer_horiz_angle = 0.0;
    _viewer_vert_angle = 0.0;
    _viewer_max_up = 0.0;
    _viewer_max_down = 0.0;
    _viewer_max_side = 0.0;
    _thraction = 0.0;
    _fly_dir_length = 0.0;
    _height = 0.0;
    _height_max_user = 0.0;
    _visual_scale = 1.0;
    _visual_scale_vec = vec3d(1.0, 1.0, 1.0);
    _visual_tint = World::TVisualTint();
    ypabact_ResetDamagedFX(this);
    _decoration_fx = World::TDecorationFXConfig();
    _decoration_fx_next_time = 0;
    _damaged_force_malus = 0.0;
    _damaged_maxrot_malus = 0.0;
    _damaged_snd_pitch_mult = 1.0;
    _damaged_fx_active = false;
    _active_debuff.Clear();
    _debuff_soundcarrier.Clear();
    _damaged_shake_carrier.Clear();

    _vp_active = 0;

    _vp_extra_mode = 0;

    _radius = 0.0;
    _viewer_radius = 0.0;
    _overeof = 0.0;
    _viewer_overeof = 0.0;
    _clock = 0;
    _AI_time1 = 0;
    _AI_time2 = 0;
    _search_time1 = 0;
    _search_time2 = 0;
    _scale_time = 0;
    _brkfr_time = 0;
    _brkfr_time2 = 0;
    _newtarget_time = 0;
    _assess_time = 0;
    _waitCol_time = 0;
    _slider_time = 0;
    _dead_time = 0;
    _beam_time = 0;
    _energy_time = 0;
    _weapon = 0;
    _extra_weapons = {0, 0, 0};
    _weapon_switch_mode = 0;
    _weapon_slot_index = 0;
    _current_weapon_id = -1;
    _lowhp_weapon_enable = 0;
    _lowhp_threshold = 0.30;
    _lowhp_weapon = 0;
    _weapon_flags = 0;
    _mgun = 0;
    _num_mguns = 1;
    _weapon_spread_x = 0.0;
    _weapon_spread_y = 0.0;
    _mgun_spread_x = 0.0;
    _mgun_spread_y = 0.0;
    _weapon_spread_x_user = 0.0;
    _weapon_spread_y_user = 0.0;
    _mgun_spread_x_user = 0.0;
    _mgun_spread_y_user = 0.0;
    _weapon_spread_x_user_set = false;
    _weapon_spread_y_user_set = false;
    _mgun_spread_x_user_set = false;
    _mgun_spread_y_user_set = false;
    _num_weapons = 0;
    _weapon_time = 0;
    _gun_angle = 0.0;
    _gun_angle_user = 0.0;
    _gun_leftright = 0.0;
    _gun_radius = 0.0;
    _gun_power = 0.0;
    _mgun_fire_x = 0.0;
    _mgun_time = 0;
    _salve_counter = 0;
    _kill_after_shot = 0;
    _spawn_units = 0;
    _spawn_vehicle = 0;
    _spawn_interval = 5000;
    _spawn_trigger_radius = 0.0;
    _spawn_random_pos = 0.0;
    _spawn_max_active = 0;
    _spawn_count = 1;
    _spawn_instant = 0;
    _spawn_last_time = 0;
    _spawn_at_death_units = 0;
    _spawn_at_death_vehicle = 0;
    _spawn_at_death_count = 1;
    _spawn_at_death_random_pos = 0.0;
    _spawn_at_death_instant = 0;
    _spawn_at_death_immunity_time = 0;
    _spawn_at_death_done = false;
    _spawn_at_death_protection_end_time = 0;
    _spawn_at_death_restore_vulnerable = false;
    _carrier_spawn_root_gid = 0;
    _carrier_spawn_root_vehicle = 0;
    _carrier_spawned_gids.clear();
    _proximity_defense_enable = 0;
    _proximity_defense_weapon = 0;
    _proximity_defense_trigger_radius = 0.0;
    _proximity_defense_interval = 1000;
    _proximity_defense_shots = 12;
    _proximity_defense_fire_pos = vec3d(0.0, 0.0, 0.0);
    _proximity_defense_vp_launch = -1;
    _proximity_defense_fire_mode = 0;
    _proximity_defense_sequence_delay = 100;
    _proximity_defense_at_death = 0;
    _proximity_defense_random_yaw_set = false;
    _proximity_defense_random_yaw_min = 0.0;
    _proximity_defense_random_yaw_max = 360.0;
    _proximity_defense_random_pitch_set = false;
    _proximity_defense_random_pitch_min = -10.0;
    _proximity_defense_random_pitch_max = 45.0;
    _proximity_defense_sequence_active = false;
    _proximity_defense_sequence_shots_fired = 0;
    _proximity_defense_next_shot_time = 0;
    _proximity_defense_next_activation_time = 0;
    _proximity_defense_at_death_done = false;
    _mortar_barrage_active = false;
    _mortar_shots_remaining = 0;
    _mortar_next_shot_time = 0;
    _mortar_next_activation_time = 0;
    _mortar_next_scan_time = 0;
    _mortar_target_center = vec3d(0.0, 0.0, 0.0);
    _mortar_has_pending = false;
    _mortar_pending_target = vec3d(0.0, 0.0, 0.0);
    StopLaser();
    StopVerticalLaser();
    _seek_and_explode = 0;
    _seek_and_explode_weapon = 0;
    _seek_and_explode_trigger_radius = 0.0;
    _seek_and_explode_triggered = false;
    _gunDisplayName.clear();
    _unitGunsParentRotation = mat3x3::Ident();
    _unitGunsSpawned = false;
    _unitGunsHaveParentRotation = false;
    _isUnitGunChild = false;
    _unitDummiesParentRotation = mat3x3::Ident();
    _unitDummiesSpawned = false;
    _unitDummiesHaveParentRotation = false;
    _isDummy = false;
    _collNodes = World::rbcolls();
    _heading_speed = 0.0;
    _killer = NULL;
    _killer_owner = 0;
    _reb_count = 0;
    _atk_ret = 0;
    _lastFrmStamp = 0;
    _scale_start = 0.0;
    _scale_speed = 0.0;
    _scale_accel = 0.0;
    _scale_duration = 0;
    _scale_pos = 0;
    _scale_delay = 0;

    for (NC_STACK_base *& vp_fx : _vp_fx_models)
        vp_fx = NULL;


    _oflags = 0;
    _yls_time = 0;
    
    _world = NULL;
}

NC_STACK_ypabact::~NC_STACK_ypabact()
{
    Common::DeleteAndNull(&_current_vp);
}


size_t NC_STACK_ypabact::Init(IDVList &stak)
{
    if ( !NC_STACK_nucleus::Init(stak) )
        return 0;

    _attackersList.clear();
    _kidList.clear();
    _missiles_list.clear();

    _gid = ypabact_id;
    _bact_type = BACT_TYPES_BACT;
//    ypabact.field_3DA = 0;
    _host_station = NULL;
    _viewer_rotation = mat3x3::Ident();
    _fly_dir = vec3d(0.0, 0.0, 0.0);
    _fly_dir_length = 0;
    _target_vec = vec3d(0.0, 0.0, 0.0);
    
    //_kidRef.bact = this;



    ypabact_id++;

    _rotation = _viewer_rotation;

    _mass = 400.0;
    _base_force = 5000.0;
    _base_maxrot = 0.5;
    _force = 5000.0;
    _airconst = 500.0;
    _maxrot = 0.5;
    _height = 150.0;
    _radius = 20.0;
    _viewer_radius = 40.0;
    _overeof = 10.0;
    _viewer_overeof = 40.0;
    _energy = 10000;
    _shield = 0;
    _base_snd_normal_pitch = 0;
    _base_snd_wait_pitch = 0;
    _heading_speed = 0.7;
    _yls_time = 3000;
    _aggr = 50;
    _energy_max = 10000;
    _invulnerable = false;
    ypabact_ResetDamagedFX(this);
    _visual_scale = 1.0;
    _visual_scale_vec = vec3d(1.0, 1.0, 1.0);
    _visual_tint = World::TVisualTint();
    _decoration_fx = World::TDecorationFXConfig();
    _decoration_fx_next_time = 0;
    _damaged_force_malus = 0.0;
    _damaged_maxrot_malus = 0.0;
    _damaged_snd_pitch_mult = 1.0;
    _damaged_fx_active = false;
    _active_debuff.Clear();
    _debuff_soundcarrier.Clear();
    _damaged_shake_carrier.Clear();
//    ypabact.field_3CE = 0;
    _height_max_user = 1600.0;
    _gun_radius = 5.0;
    _gun_power = 4000.0;
    _spawn_units = 0;
    _spawn_vehicle = 0;
    _spawn_interval = 5000;
    _spawn_trigger_radius = 0.0;
    _spawn_random_pos = 0.0;
    _spawn_max_active = 0;
    _spawn_count = 1;
    _spawn_instant = 0;
    _spawn_last_time = 0;
    _spawn_at_death_units = 0;
    _spawn_at_death_vehicle = 0;
    _spawn_at_death_count = 1;
    _spawn_at_death_random_pos = 0.0;
    _spawn_at_death_instant = 0;
    _spawn_at_death_immunity_time = 0;
    _spawn_at_death_done = false;
    _spawn_at_death_protection_end_time = 0;
    _spawn_at_death_restore_vulnerable = false;
    _carrier_spawn_root_gid = 0;
    _carrier_spawn_root_vehicle = 0;
    _carrier_spawned_gids.clear();
    _proximity_defense_enable = 0;
    _proximity_defense_weapon = 0;
    _proximity_defense_trigger_radius = 0.0;
    _proximity_defense_interval = 1000;
    _proximity_defense_shots = 12;
    _proximity_defense_fire_pos = vec3d(0.0, 0.0, 0.0);
    _proximity_defense_vp_launch = -1;
    _proximity_defense_fire_mode = 0;
    _proximity_defense_sequence_delay = 100;
    _proximity_defense_at_death = 0;
    _proximity_defense_random_yaw_set = false;
    _proximity_defense_random_yaw_min = 0.0;
    _proximity_defense_random_yaw_max = 360.0;
    _proximity_defense_random_pitch_set = false;
    _proximity_defense_random_pitch_min = -10.0;
    _proximity_defense_random_pitch_max = 45.0;
    _proximity_defense_sequence_active = false;
    _proximity_defense_sequence_shots_fired = 0;
    _proximity_defense_next_shot_time = 0;
    _proximity_defense_next_activation_time = 0;
    _proximity_defense_at_death_done = false;
    _mortar_barrage_active = false;
    _mortar_shots_remaining = 0;
    _mortar_next_shot_time = 0;
    _mortar_next_activation_time = 0;
    _mortar_next_scan_time = 0;
    _mortar_target_center = vec3d(0.0, 0.0, 0.0);
    _mortar_has_pending = false;
    _mortar_pending_target = vec3d(0.0, 0.0, 0.0);
    StopLaser();
    StopVerticalLaser();
    _seek_and_explode = 0;
    _seek_and_explode_weapon = 0;
    _seek_and_explode_trigger_radius = 0.0;
    _seek_and_explode_triggered = false;
    _unitGuns.clear();
    _gunDisplayName.clear();
    _unitGunsParentRotation = mat3x3::Ident();
    _unitGunsSpawned = false;
    _unitGunsHaveParentRotation = false;
    _isUnitGunChild = false;
    _unitDummies.clear();
    _unitDummiesParentRotation = mat3x3::Ident();
    _unitDummiesSpawned = false;
    _unitDummiesHaveParentRotation = false;
    _isDummy = false;
    _collNodes = World::rbcolls();
    _adist_sector = 800.0;
    _adist_bact = 650.0;
    _sdist_sector = 200.0;
    _sdist_bact = 100.0;
    _oflags = BACT_OFLAG_EXACTCOLL;

    _world = stak.Get<NC_STACK_ypaworld *>(BACT_ATT_WORLD, NULL);// get ypaworld

    if ( _world )
    {
        for( auto& it : stak )
        {
            IDVPair &val = it.second;

            if ( !val.Skip )
            {
                switch (val.ID)
                {
                case BACT_ATT_VIEWER:
                {
                    uamessage_viewer viewMsg;

                    if ( val.Get<int32_t>() )
                    {
                        _world->ypaworld_func131(this); //Set current bact

                        _oflags |= BACT_OFLAG_VIEWER;

                        if ( _world->_isNetGame )
                            viewMsg.view = 1;

                        SFXEngine::SFXe.startSound(&_soundcarrier, 8);
                    }
                    else
                    {
                        _oflags &= ~BACT_OFLAG_VIEWER;

                        if ( _world->_isNetGame )
                            viewMsg.view = 0;

                        SFXEngine::SFXe.sub_424000(&_soundcarrier, 8);
                    }

                    if ( _world->_isNetGame ) // Network message send routine?
                    {
                        viewMsg.msgID = UAMSG_VIEWER;
                        viewMsg.owner = _owner;
                        viewMsg.classID = _bact_type;
                        viewMsg.id = _gid;

                        if ( viewMsg.classID == BACT_TYPES_MISSLE )
                        {
                            NC_STACK_ypamissile *miss = dynamic_cast<NC_STACK_ypamissile *>(this);
                            viewMsg.launcher = miss->GetLauncherBact()->_gid;
                        }

                        _world->NetBroadcastMessage(&viewMsg, sizeof(viewMsg), true);
                    }
                }
                break;

                case BACT_ATT_INPUTTING:
                    if ( val.Get<int32_t>() )
                    {
                        _oflags |= BACT_OFLAG_USERINPT;
                        _world->setYW_userVehicle(this);
                    }
                    else
                    {
                        _oflags &= ~BACT_OFLAG_USERINPT;
                    }
                    break;

                case BACT_ATT_EXACTCOLL:
                    setBACT_exactCollisions(val.Get<bool>());
                    break;

                case BACT_ATT_BACTCOLL:
                    setBACT_bactCollisions ( val.Get<bool>() );
                    break;

                case BACT_ATT_AIRCONST:
                    setBACT_airconst(val.Get<int32_t>());
                    break;

                case BACT_ATT_LANDINGONWAIT:
                    setBACT_landingOnWait ( val.Get<bool>() );
                    break;

                case BACT_ATT_YOURLS:
                    setBACT_yourLastSeconds(val.Get<int32_t>());
                    break;

                case BACT_ATT_VISPROT:
                    SetVP( val.Get<NC_STACK_base *>());
                    break;

                case BACT_ATT_AGGRESSION:
                    setBACT_aggression(val.Get<int32_t>());
                    break;

                case BACT_ATT_EXTRAVIEWER:
                    setBACT_extraViewer ( val.Get<bool>() );
                    break;

                case BACT_ATT_ALWAYSRENDER:
                    setBACT_alwaysRender ( val.Get<bool>() );
                    break;

                default:
                    break;
                }
            }
        }
    }

    _tForm.Pos = _position;

    _tForm.SclRot = _rotation;

    _status = BACT_STATUS_NORMAL;

    _wrldSectors = _world->GetMapSize();

    _wrldSize = World::SectorIDToPos2( _wrldSectors );

    return 1;
}

size_t NC_STACK_ypabact::Deinit()
{
    SFXEngine::SFXe.StopCarrier(&_soundcarrier);
    SFXEngine::SFXe.StopCarrier(&_debuff_soundcarrier);
    SFXEngine::SFXe.StopCarrier(&_damaged_shake_carrier);
    SFXEngine::SFXe.StopCarrier(&_laser_soundcarrier);
    SFXEngine::SFXe.StopCarrier(&_vertical_laser_soundcarrier);
    _active_debuff.Clear();

    _status_flg |= BACT_STFLAG_CLEAN;

    if ( !(_status_flg & BACT_STFLAG_DEATH1) )
        Die();

    if ( _pSector )
        _cellRef.Detach();

    _kidRef.Detach();

    CleanupUnitGuns(true);
    CleanupUnitDummies(true);

    while (!_kidList.empty())
        _kidList.front()->Delete();

    while (!_missiles_list.empty())
    {
        _missiles_list.front()->Delete();
        _missiles_list.pop_front();        
    }

    return NC_STACK_nucleus::Deinit();
}


void NC_STACK_ypabact::SetUnitGuns(const std::vector<World::TRoboGun> &guns)
{
    CleanupUnitGuns(true);

    _unitGuns = guns;

    for (World::TRoboGun &gun : _unitGuns)
        gun.gun_obj = NULL;

    _unitGunsParentRotation = mat3x3::Ident();
    _unitGunsSpawned = _unitGuns.empty();
    _unitGunsHaveParentRotation = false;
}

void NC_STACK_ypabact::CleanupUnitGuns(bool releaseGuns, bool parentDying)
{
    for (World::TRoboGun &gun : _unitGuns)
    {
        NC_STACK_ypabact *gunObj = gun.gun_obj;
        gun.gun_obj = NULL;

        if ( !gunObj )
            continue;

        NC_STACK_ypabact *fallback = parentDying ? _world->_userRobo : this;
        ypabact_SafeDetachControlFrom(gunObj, fallback);

        if ( !gunObj->IsDestroyed() && !(gunObj->_status_flg & BACT_STFLAG_DEATH1) )
        {
            gunObj->_killer = _killer;
            gunObj->Die();
        }

        if ( releaseGuns )
            gunObj->Release();
    }

    _unitGunsSpawned = false;
    _unitGunsHaveParentRotation = false;
}

void NC_STACK_ypabact::ClearUnitGunPointer(NC_STACK_ypabact *gunObj)
{
    for (World::TRoboGun &gun : _unitGuns)
    {
        if ( gun.gun_obj == gunObj )
            gun.gun_obj = NULL;
    }
}

void NC_STACK_ypabact::UpdateUnitGuns(update_msg *)
{
    if ( _unitGuns.empty() || _isUnitGunChild || !_world )
        return;

    if ( _status == BACT_STATUS_DEAD || (_status_flg & BACT_STFLAG_DEATH1) )
    {
        CleanupUnitGuns(true);
        return;
    }

    mat3x3 parentRotation = _rotation.Transpose();
    mat3x3 parentRotationDelta = mat3x3::Ident();
    bool applyParentRotationDelta = false;

    if ( !_unitGunsHaveParentRotation )
    {
        _unitGunsParentRotation = parentRotation;
        _unitGunsHaveParentRotation = true;
    }
    else
    {
        parentRotationDelta = parentRotation * _unitGunsParentRotation.Transpose();
        applyParentRotationDelta = true;
        _unitGunsParentRotation = parentRotation;
    }

    if ( !_unitGunsSpawned )
    {
        _unitGunsSpawned = true;

        for (World::TRoboGun &gun : _unitGuns)
        {
            if ( !gun.robo_gun_type )
                continue;

            ypaworld_arg146 gunReq;
            gunReq.vehicle_id = gun.robo_gun_type;
            gunReq.pos = _position + _rotation.Transpose().Transform(gun.pos);
            gunReq.skip_unit_guns = true;

            NC_STACK_ypabact *gunObj = _world->ypaworld_func146(&gunReq);
            gun.gun_obj = gunObj;

            if ( gunObj )
            {
                if ( NC_STACK_ypagun *attachedGun = dynamic_cast<NC_STACK_ypagun *>(gunObj) )
                {
                    attachedGun->ypagun_func128(_rotation.Transpose().Transform(gun.dir), false);
                    attachedGun->setGUN_roboGun(1);
                }

                gunObj->_owner = _owner;
                gunObj->_commandID = dword_5B1128++;
                gunObj->_host_station = NULL;
                gunObj->_aggr = 60;
                gunObj->_isUnitGunChild = true;
                gunObj->_gunDisplayName = gun.robo_gun_name;

                if ( _world->_isNetGame )
                {
                    gunObj->_gid |= gunObj->_owner << 24;
                    gunObj->_commandID |= gunObj->_owner << 24;
                }

                AddSubject(gunObj);

                setState_msg createState;
                createState.setFlags = 0;
                createState.unsetFlags = 0;
                createState.newStatus = BACT_STATUS_CREATE;
                gunObj->SetState(&createState);
                gunObj->_scale_time = gunObj->_energy_max * 0.2;
            }
            else
            {
                ypa_log_out("Unable to create Unit-Gun\n");
            }
        }
    }

    for (World::TRoboGun &gun : _unitGuns)
    {
        NC_STACK_ypabact *gunObj = gun.gun_obj;

        if ( !gunObj )
            continue;

        if ( gunObj->IsDestroyed() || (gunObj->_status_flg & BACT_STFLAG_DEATH1) )
        {
            gun.gun_obj = NULL;
            continue;
        }

        bact_arg80 posArg;
        posArg.pos = _position + _rotation.Transpose().Transform(gun.pos);
        posArg.field_C = 4;

        gunObj->_owner = _owner;
        gunObj->SetPosition(&posArg);

        if ( applyParentRotationDelta )
        {
            if ( NC_STACK_ypagun *attachedGun = dynamic_cast<NC_STACK_ypagun *>(gunObj) )
            {
                attachedGun->_rotation.SetX(parentRotationDelta.Transform(attachedGun->_rotation.AxisX()));
                attachedGun->_rotation.SetY(parentRotationDelta.Transform(attachedGun->_rotation.AxisY()));
                attachedGun->_rotation.SetZ(parentRotationDelta.Transform(attachedGun->_rotation.AxisZ()));
                attachedGun->_gunBasis = parentRotationDelta.Transform(attachedGun->_gunBasis);
                attachedGun->_gunRott = parentRotationDelta.Transform(attachedGun->_gunRott);
            }
        }
    }
}


// ================= OpenUA custom: modular dummy attachments =================
// Mirrors the unit-gun child machinery (SetUnitGuns/UpdateUnitGuns/...), but
// each slot references a full model = dummy prototype, the child is marked
// _isDummy (fully inert) and never aims/fires, and a per-slot visual scale and
// protect flag are supported. Reusing the unit-gun child path gives us spawn,
// transform-following, UI/minimap/squad hiding and parent cleanup for free.

void NC_STACK_ypabact::SetUnitDummies(const std::vector<World::TUnitDummy> &dummies)
{
    CleanupUnitDummies(true);

    _unitDummies = dummies;

    for (World::TUnitDummy &dmy : _unitDummies)
        dmy.dummy_obj = NULL;

    _unitDummiesParentRotation = mat3x3::Ident();
    _unitDummiesSpawned = _unitDummies.empty();
    _unitDummiesHaveParentRotation = false;
}

void NC_STACK_ypabact::CleanupUnitDummies(bool releaseDummies, bool parentDying)
{
    for (World::TUnitDummy &dmy : _unitDummies)
    {
        NC_STACK_ypabact *dummyObj = dmy.dummy_obj;
        dmy.dummy_obj = NULL;

        if ( !dummyObj )
            continue;

        NC_STACK_ypabact *fallback = parentDying ? _world->_userRobo : this;
        ypabact_SafeDetachControlFrom(dummyObj, fallback);

        if ( !dummyObj->IsDestroyed() && !(dummyObj->_status_flg & BACT_STFLAG_DEATH1) )
        {
            dummyObj->_killer = _killer;
            dummyObj->Die();
        }

        if ( releaseDummies )
            dummyObj->Release();
    }

    _unitDummiesSpawned = false;
    _unitDummiesHaveParentRotation = false;
}

void NC_STACK_ypabact::ClearUnitDummyPointer(NC_STACK_ypabact *dummyObj)
{
    for (World::TUnitDummy &dmy : _unitDummies)
    {
        if ( dmy.dummy_obj == dummyObj )
            dmy.dummy_obj = NULL;
    }
}

void NC_STACK_ypabact::UpdateUnitDummies(update_msg *)
{
    if ( _unitDummies.empty() || _isUnitGunChild || _isDummy || !_world )
        return;

    if ( _status == BACT_STATUS_DEAD || (_status_flg & BACT_STFLAG_DEATH1) )
    {
        CleanupUnitDummies(true);
        return;
    }

    mat3x3 parentRotation = _rotation.Transpose();
    mat3x3 parentRotationDelta = mat3x3::Ident();
    bool applyParentRotationDelta = false;

    if ( !_unitDummiesHaveParentRotation )
    {
        _unitDummiesParentRotation = parentRotation;
        _unitDummiesHaveParentRotation = true;
    }
    else
    {
        parentRotationDelta = parentRotation * _unitDummiesParentRotation.Transpose();
        applyParentRotationDelta = true;
        _unitDummiesParentRotation = parentRotation;
    }

    if ( !_unitDummiesSpawned )
    {
        _unitDummiesSpawned = true;

        const std::vector<World::TVhclProto> &protos = _world->GetVhclProtos();

        for (World::TUnitDummy &dmy : _unitDummies)
        {
            if ( dmy.vehicle_id <= 0 || (size_t)dmy.vehicle_id >= protos.size() )
            {
                if ( dmy.vehicle_id != 0 )
                    ypa_log_out("Dummy attachment: invalid unit_dummy_vehicle %d, slot skipped\n", dmy.vehicle_id);
                continue;
            }

            if ( !protos[dmy.vehicle_id].is_dummy )
            {
                ypa_log_out("Dummy attachment: prototype %d is not model = dummy, slot skipped\n", dmy.vehicle_id);
                continue;
            }

            ypaworld_arg146 dmyReq;
            dmyReq.vehicle_id = dmy.vehicle_id;
            dmyReq.pos = _position + _rotation.Transpose().Transform(dmy.pos);
            dmyReq.skip_unit_guns = true;

            NC_STACK_ypabact *dummyObj = _world->ypaworld_func146(&dmyReq);
            dmy.dummy_obj = dummyObj;

            if ( dummyObj )
            {
                dummyObj->_isDummy = true;

                if ( NC_STACK_ypagun *attachedDummy = dynamic_cast<NC_STACK_ypagun *>(dummyObj) )
                {
                    attachedDummy->ypagun_func128(_rotation.Transpose().Transform(dmy.dir), false);
                    attachedDummy->setGUN_fireType(NC_STACK_ypagun::GUN_TYPE_DUMMY);
                }

                dummyObj->_owner = _owner;
                dummyObj->_commandID = dword_5B1128++;
                dummyObj->_host_station = NULL;
                dummyObj->_aggr = 0;
                dummyObj->_isUnitGunChild = true; // hide from strategic UI/minimap/squad
                dummyObj->setBACT_bactCollisions(false); // decorative module: no unit-vs-unit shove

                if ( _world->_isNetGame )
                {
                    dummyObj->_gid |= dummyObj->_owner << 24;
                    dummyObj->_commandID |= dummyObj->_owner << 24;
                }

                AddSubject(dummyObj);

                setState_msg createState;
                createState.setFlags = 0;
                createState.unsetFlags = 0;
                createState.newStatus = BACT_STATUS_CREATE;
                dummyObj->SetState(&createState);
                dummyObj->_scale_time = dummyObj->_energy_max * 0.2;
            }
            else
            {
                ypa_log_out("Unable to create Unit-Dummy\n");
            }
        }
    }

    for (World::TUnitDummy &dmy : _unitDummies)
    {
        NC_STACK_ypabact *dummyObj = dmy.dummy_obj;

        if ( !dummyObj )
            continue;

        if ( dummyObj->IsDestroyed() || (dummyObj->_status_flg & BACT_STFLAG_DEATH1) )
        {
            dmy.dummy_obj = NULL;
            continue;
        }

        bact_arg80 posArg;
        posArg.pos = _position + _rotation.Transpose().Transform(dmy.pos);
        posArg.field_C = 4;

        dummyObj->_owner = _owner;
        dummyObj->SetPosition(&posArg);

        if ( applyParentRotationDelta )
        {
            if ( NC_STACK_ypagun *attachedDummy = dynamic_cast<NC_STACK_ypagun *>(dummyObj) )
            {
                attachedDummy->_rotation.SetX(parentRotationDelta.Transform(attachedDummy->_rotation.AxisX()));
                attachedDummy->_rotation.SetY(parentRotationDelta.Transform(attachedDummy->_rotation.AxisY()));
                attachedDummy->_rotation.SetZ(parentRotationDelta.Transform(attachedDummy->_rotation.AxisZ()));
                attachedDummy->_gunBasis = parentRotationDelta.Transform(attachedDummy->_gunBasis);
                attachedDummy->_gunRott = parentRotationDelta.Transform(attachedDummy->_gunRott);
            }
        }

        // Keep the first-person/extra-view camera aligned with the dummy's real
        // facing (unit_dummy_dir_* + parent following). The gun class normally
        // does this in User_layer, which is disabled for dummies, so sync here.
        dummyObj->_viewer_rotation = dummyObj->_rotation;
    }
}

// Pick the active protective dummy to absorb an incoming hit. Prefer the one
// closest to the attacker; fall back to the first active protective dummy.
NC_STACK_ypabact *NC_STACK_ypabact::SelectProtectiveDummy(NC_STACK_ypabact *attacker)
{
    NC_STACK_ypabact *best = NULL;
    NC_STACK_ypabact *firstActive = NULL;
    float bestDist = 0.0;
    bool haveAttacker = (attacker != NULL);
    vec3d srcPos;

    if ( haveAttacker )
        srcPos = attacker->_position;

    for (World::TUnitDummy &dmy : _unitDummies)
    {
        if ( !dmy.protect )
            continue;

        NC_STACK_ypabact *dummyObj = dmy.dummy_obj;
        if ( !dummyObj ||
             dummyObj->IsDestroyed() ||
             (dummyObj->_status_flg & BACT_STFLAG_DEATH1) ||
             dummyObj->_status == BACT_STATUS_DEAD ||
             dummyObj->_energy <= 0 )
            continue;

        if ( !firstActive )
            firstActive = dummyObj;

        if ( haveAttacker )
        {
            float d = (dummyObj->_position - srcPos).length();
            if ( !best || d < bestDist )
            {
                best = dummyObj;
                bestDist = d;
            }
        }
    }

    return (haveAttacker && best) ? best : firstActive;
}


void NC_STACK_ypabact::CopyTargetOf(NC_STACK_ypabact *unit)
{
    NC_STACK_ypabact *v6 = NULL;

    _waypoints_count = 0;
    _m_cmdID = 0;
    _status_flg &= ~(BACT_STFLAG_WAYPOINT | BACT_STFLAG_WAYPOINTCCL);

    int tgType;
    vec2d wTo;

    if ( unit->_status_flg & BACT_STFLAG_WAYPOINT )
    {
        if ( !unit->_m_cmdID )
        {
            int v9 = unit->_waypoints_count - 1;

            wTo = unit->_waypoints[v9].XZ();

            tgType = BACT_TGT_TYPE_CELL;
        }
        else
        {
            v6 = _world->FindBactByCmdOwn(unit->_m_cmdID, unit->_m_owner);

            if ( v6 )
                tgType = BACT_TGT_TYPE_UNIT;
            else
                tgType = BACT_TGT_TYPE_NONE;
        }
    }
    else
    {
        if ( unit->_primTtype == BACT_TGT_TYPE_UNIT )
        {
            v6 = unit->_primT.pbact;
            tgType = BACT_TGT_TYPE_UNIT;
        }
        else if ( unit->_primTtype == BACT_TGT_TYPE_CELL )
        {
            wTo = unit->_primTpos.XZ();
            tgType = BACT_TGT_TYPE_CELL;
        }
        else
            tgType = BACT_TGT_TYPE_NONE;
    }

    if ( tgType == BACT_TGT_TYPE_NONE )
    {
        tgType = BACT_TGT_TYPE_UNIT;
        v6 = unit;
    }

    if ( _bact_type != BACT_TYPES_TANK && _bact_type != BACT_TYPES_CAR )
    {
        setTarget_msg arg67;
        arg67.tgt_type = tgType;
        arg67.priority = 0;

        if ( tgType == BACT_TGT_TYPE_UNIT )
        {
            arg67.tgt.pbact = v6;
        }
        else
        {
            arg67.tgt_pos.x = wTo.x;
            arg67.tgt_pos.z = wTo.y;
        }

        SetTarget(&arg67);
    }
    else
    {
        bact_arg124 arg125;

        if ( tgType == BACT_TGT_TYPE_UNIT )
        {
            arg125.to = v6->_position.XZ();
        }
        else
        {
            arg125.to = wTo;
        }

        arg125.steps_cnt = 32;
        arg125.from = _position.XZ();
        arg125.field_12 = 1;

        SetPath(&arg125);

        if ( tgType == BACT_TGT_TYPE_UNIT )
        {
            _m_cmdID = v6->_commandID;
            _m_owner = v6->_owner;
        }
    }
}

size_t NC_STACK_ypabact::SetParameters(IDVList &stak)
{
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

            case BACT_ATT_INPUTTING:
                setBACT_inputting(val.Get<bool>());
                break;

            case BACT_ATT_EXACTCOLL:
                setBACT_exactCollisions ( val.Get<bool>() );
                break;

            case BACT_ATT_BACTCOLL:
                setBACT_bactCollisions ( val.Get<bool>() );
                break;

            case BACT_ATT_AIRCONST:
                setBACT_airconst(val.Get<int32_t>());
                break;

            case BACT_ATT_LANDINGONWAIT:
                setBACT_landingOnWait ( val.Get<bool>() );
                break;

            case BACT_ATT_YOURLS:
                setBACT_yourLastSeconds(val.Get<int32_t>());
                break;

            case BACT_ATT_VISPROT:
                SetVP( val.Get<NC_STACK_base *>());
                break;

            case BACT_ATT_AGGRESSION:
                setBACT_aggression(val.Get<int32_t>());
                break;

            case BACT_ATT_EXTRAVIEWER:
                setBACT_extraViewer ( val.Get<bool>() );
                break;

            case BACT_ATT_ALWAYSRENDER:
                setBACT_alwaysRender ( val.Get<bool>() );
                break;

            default:
                break;
            }
        }
    }

    return 1;
}


void NC_STACK_ypabact::FixSectorFall()
{
    ypaworld_arg136 arg136;
    arg136.stPos = vec3d(_position.x, -30000.0, _position.z);
    arg136.vect = vec3d(0.0, 50000.0, 0.0);
    arg136.flags = 0;

    _world->ypaworld_func136(&arg136);

    if ( arg136.isect )
        _position.y = arg136.isectPos.y - 50.0;
    else
        _position.y = _pSector->height  - 50.0;
}


void NC_STACK_ypabact::FixBeyondTheWorld()
{
    vec2d mv = World::SectorIDToPos2( _world->GetMapSize() );

    if ( _position.x > mv.x )
        _position.x = mv.x - World::CVSectorHalfLength;

    if ( _position.x < 0.0 )
        _position.x = World::CVSectorHalfLength;

    if ( _position.z < mv.y )
        _position.z = mv.y + World::CVSectorHalfLength;

    if ( _position.z > 0.0 )
        _position.z = -World::CVSectorHalfLength;

    FixSectorFall();
}

void sub_481F94(NC_STACK_ypabact *bact)
{
    for (World::MissileList::iterator it = bact->_missiles_list.begin(); it != bact->_missiles_list.end(); )
    {
        NC_STACK_ypamissile *misl = *it;
        if ( misl->getBACT_yourLastSeconds() <= 0 )
        {
            setTarget_msg arg67;
            arg67.tgt_type = BACT_TGT_TYPE_NONE;
            arg67.priority = 0;

            misl->SetTarget(&arg67);

            misl->_parent = NULL;

            misl->Release();

            it = bact->_missiles_list.erase(it);
        }
        else
            it++;
    }
}


void NC_STACK_ypabact::BeforeSoundCarrierUpdate()
{
}

void NC_STACK_ypabact::Update(update_msg *arg)
{
    if ( _kidRef.IsListType(World::BLIST_CACHE) ) // Do not update units in dead list
        return;
        
    static TF::TForm3D bact_cam;
    TF::Engine.SetViewPoint(&bact_cam);

    yw_130arg sect_info;
    sect_info.pos_x = _position.x;
    sect_info.pos_z = _position.z;

    if ( !_world->GetSectorInfo(&sect_info) )
    {
        FixBeyondTheWorld();

        sect_info.pos_x = _position.x;
        sect_info.pos_z = _position.z;

        _world->GetSectorInfo(&sect_info);
    }

    cellArea *oldcell = _pSector;

    _cellId = sect_info.CellId;

//    bact->pos_x_cntr = sect_info.pos_x_cntr;
//    bact->pos_y_cntr = sect_info.pos_y_cntr;

    _pSector = sect_info.pcell;

    if ( _invulnerable && _energy <= 0 )
        _energy = _energy_max > 0 ? _energy_max : 1;

    if ( oldcell != sect_info.pcell ) // If cell changed
    {
        _cellRef.Detach();  //Remove unit from old cell
        _cellRef = _pSector->unitsList.push_back(this);  // Add unit to new cell
    }

    // Test if bact fall through sector
    if ( _pSector->height + 1000.0 < _position.y )
        FixSectorFall();

    NC_STACK_ypabact *roboHost = _world->getYW_userHostStation();

    if ( _pSector->PurposeType == cellArea::PT_GATEOPENED && _bact_type == BACT_TYPES_ROBO && this == roboHost )
        ((NC_STACK_yparobo *)roboHost)->ypabact_func65__sub0();

    if ( !(_status_flg & BACT_STFLAG_DEATH1) && _energy <= 0 && _bact_type != BACT_TYPES_MISSLE )
    {
        Die();

        if ( !IsDestroyed() )
        {
            setState_msg v38;
            v38.setFlags = 0;
            v38.unsetFlags = 0;
            v38.newStatus = BACT_STATUS_IDLE;

            SetState(&v38);
        }

        _status = BACT_STATUS_DEAD;
        _status_flg &= ~BACT_STFLAG_LAND;
    }

    _clock += arg->frameTime;
    ypabact_UpdateSpawnAtDeathProtection(this);

    UpdateActiveDebuff(arg);
    UpdateDamageFX(arg);
    UpdateDecorationFX(arg);
    UpdateCarrierSpawn(arg);
    UpdateProximityDefense(arg);
    UpdateMortar(arg);
    AI_layer1(arg);
    UpdateLaser(arg); // OpenUA custom: process this frame's laser fire request (must run after AI_layer1 firing)
    UpdateVerticalLaser(arg);
    UpdateAoePush(arg);
    UpdateSeekAndExplode(arg);
    UpdateUnitGuns(arg);
    UpdateUnitDummies(arg);

    for( NC_STACK_ypamissile *misl : Utils::IterateListCopy<NC_STACK_ypamissile *>(_missiles_list))
        misl->Update(arg);

    sub_481F94(this);

    if ( _oflags & BACT_OFLAG_VIEWER )
    {
        if ( _oflags & BACT_OFLAG_EXTRAVIEW )
            bact_cam.Pos = _position + _rotation.Transpose().Transform(_viewer_position);
        else
            bact_cam.Pos = _position;

        if ( _oflags & BACT_OFLAG_EXTRAVIEW )
            bact_cam.SclRot = _viewer_rotation;
        else
            bact_cam.SclRot = _rotation;

        GFX::Engine.matrixAspectCorrection(bact_cam.SclRot, false);
    }

    _tForm.Pos = _position;

    if ( _status_flg & BACT_STFLAG_SCALE )
        _tForm.SclRot = _rotation.Transpose() * mat3x3::Scale( _scale );
    else
        _tForm.SclRot = _rotation.Transpose();

    int numbid = arg->units_count;

    arg->units_count = 0;

    /** 
     * Because missiles can cause 'ModifyEnergy' and 'Die' methods of upper bact
     * in hierarchy Update->Update - it can remove all bacts from list in
     * iteration. So we just needs to get safe copy of list for modify without
     * worry of lists modify.
     **/
    for ( NC_STACK_ypabact *bnod : _kidList.safe_iter() )
    {
        bnod->Update(arg);

        arg->units_count++;
    }

    arg->units_count = numbid;

    _soundcarrier.Position = _position;

    if ( _oflags & BACT_OFLAG_VIEWER )
        _soundcarrier.Position += _rotation.AxisY() * 400.0;

    _soundcarrier.Vector = _fly_dir * _fly_dir_length;

    ypabact_ApplyDamagedSoundPitch(this);
    BeforeSoundCarrierUpdate();

    SFXEngine::SFXe.UpdateSoundCarrier(&_soundcarrier);
    ypabact_UpdateStatusSoundCarrier(this, &_debuff_soundcarrier);
    ypabact_UpdateStatusSoundCarrier(this, &_damaged_shake_carrier);
}

void NC_STACK_ypabact::ClearActiveDebuff()
{
    _active_debuff.Clear();
    SFXEngine::SFXe.StopCarrier(&_debuff_soundcarrier);
}

void NC_STACK_ypabact::ApplyWeaponDebuff(World::TWeaponDebuffConfig &debuff, NC_STACK_ypabact *source)
{
    if ( !debuff.allow || debuff.duration <= 0 )
        return;

    if ( !ypabact_CanUseGameplayStatusMechanics(this) )
        return;

    if ( debuff.mindcontrol && !ypabact_CanBeMindcontrolled(this, source) )
        return;

    bool canMindcontrol = debuff.mindcontrol;

    _active_debuff.active = true;
    _active_debuff.name = debuff.name.empty() ? "debuff" : debuff.name;
    _active_debuff.icon = debuff.icon;
    _active_debuff.damage = debuff.damage > 0 ? debuff.damage : 0;
    _active_debuff.damage_percent = debuff.damage_percent > 0.0 ? debuff.damage_percent : 0.0;
    _active_debuff.tick_time = debuff.tick_time > 0 ? debuff.tick_time : 1000;
    _active_debuff.expire_time = _clock + debuff.duration;
    _active_debuff.next_tick_time = _clock + _active_debuff.tick_time;
    _active_debuff.force_malus = std::max(0.0f, std::min(debuff.force_malus, 1.0f));
    _active_debuff.maxrot_malus = std::max(0.0f, std::min(debuff.maxrot_malus, 1.0f));
    _active_debuff.shield_malus = std::max(0.0f, std::min(debuff.shield_malus, 1.0f));
    _active_debuff.snd_pitch_mult = ypabact_SafeDamageMult(debuff.snd_pitch_mult);
    _active_debuff.fx_vps = debuff.fx_vps;
    _active_debuff.fx_random_pos = debuff.fx_random_pos > 0.0 ? debuff.fx_random_pos : 0.0;
    _active_debuff.source_gid = source ? source->_gid : 0;
    _active_debuff.snd_sample = NULL;
    _active_debuff.snd_volume = debuff.tick_snd.volume ? debuff.tick_snd.volume : 120;
    _active_debuff.snd_pitch = debuff.tick_snd.pitch;

    if ( debuff.tick_snd.MainSample.Sample )
        _active_debuff.snd_sample = debuff.tick_snd.MainSample.Sample->GetSampleData();

    if ( _debuff_soundcarrier.Sounds.empty() )
        _debuff_soundcarrier.Resize(1);

    TSoundSource &snd = _debuff_soundcarrier.Sounds[0];
    snd.PSample = _active_debuff.snd_sample;
    snd.SampleVariants.clear();
    if ( snd.PSample )
        snd.SampleVariants.push_back(snd.PSample);
    snd.Volume = _active_debuff.snd_volume;
    snd.Pitch = _active_debuff.snd_pitch;
    snd.PriorityBias = 0;
    snd.SetLoop(false);
    snd.SetFragmented(false);

    if ( debuff.tick_snd.sndPrm.slot )
    {
        snd.PPFx = &debuff.tick_snd.sndPrm;
        snd.SetPFx(true);
    }
    else
    {
        snd.PPFx = NULL;
        snd.SetPFx(false);
    }

    if ( debuff.tick_snd.sndPrm_shk.slot )
    {
        snd.PShkFx = &debuff.tick_snd.sndPrm_shk;
        snd.SetShk(true);
    }
    else
    {
        snd.PShkFx = NULL;
        snd.SetShk(false);
    }

    if ( canMindcontrol )
        ypabact_ApplyMindcontrol(this, source);

    ypabact_SpawnDebuffFXEvent(this, ypabact_GetDebuffFXLifetime(_active_debuff));
}

void NC_STACK_ypabact::UpdateActiveDebuff(update_msg *)
{
    if ( !_active_debuff.active )
        return;

    bool invalid = !_world ||
                   _energy <= 0 ||
                   _bact_type == BACT_TYPES_MISSLE ||
                   _status == BACT_STATUS_DEAD ||
                   (_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2));

    if ( invalid || _clock >= _active_debuff.expire_time )
    {
        ClearActiveDebuff();
        return;
    }

    if ( _clock < _active_debuff.next_tick_time )
        return;

    _active_debuff.next_tick_time += _active_debuff.tick_time;
    if ( _active_debuff.next_tick_time <= _clock )
        _active_debuff.next_tick_time = _clock + _active_debuff.tick_time;

    NC_STACK_ypabact *source = NULL;
    if ( _active_debuff.source_gid )
        source = ypabact_FindLiveBactByGid(_world->_unitsList, _active_debuff.source_gid);

    int tickDamage = _active_debuff.damage;
    if ( _active_debuff.damage_percent > 0.0 && _energy_max > 0 )
    {
        double percentDamage = (double)_energy_max * ((double)_active_debuff.damage_percent / 100.0);
        if ( percentDamage > 0.0 )
        {
            double maxExtra = (double)std::numeric_limits<int>::max() - (double)tickDamage;
            if ( percentDamage >= maxExtra )
                tickDamage = std::numeric_limits<int>::max();
            else
                tickDamage += (int)(percentDamage + 0.5);
        }
    }

    if ( tickDamage > 0 )
    {
        bact_arg84 arg84;
        arg84.energy = -tickDamage;
        arg84.unit = source;
        ModifyEnergy(&arg84);
    }

    ypabact_SpawnDebuffFXEvent(this, ypabact_GetDebuffFXLifetime(_active_debuff));

    if ( !_debuff_soundcarrier.Sounds.empty() )
    {
        TSoundSource &snd = _debuff_soundcarrier.Sounds[0];
        snd.PSample = _active_debuff.snd_sample;

        ypabact_StartStatusSoundIfIdle(this, &_debuff_soundcarrier, _active_debuff.snd_volume, _active_debuff.snd_pitch);
    }

    if ( _energy <= 0 || _status == BACT_STATUS_DEAD )
        ClearActiveDebuff();
}

static void ypabact_SpawnDamagedFXEvent(NC_STACK_ypabact *bact)
{
    if ( !bact )
        return;

    NC_STACK_ypaworld *world = bact->getBACT_pWorld();
    if ( !world )
        return;

    for (int16_t vp : bact->_damaged_fx.vps)
    {
        if ( vp <= 0 )
            continue;

        // Damaged FX are status effects that visually belong to the damaged unit.
        // They must follow the owner while they live; otherwise moving units leave
        // smoke/fire stuck in world-space behind them.
        //
        // Use the same attached transient VP path already used by debuff FX. That
        // render path intentionally does not clamp to coarse sector height, which
        // avoids the old slope bug where FX popped/floated above the unit on hills.
        vec3d localOffset = ypabact_BuildRandomAttachedFXOffset(bact->_damaged_fx.random_pos, bact->_overeof);
        world->SpawnAttachedTransientVP(vp, bact, localOffset, 1000);
    }
}

static int ypabact_RandomInRange(int minValue, int maxValue)
{
    if ( maxValue < minValue )
        std::swap(minValue, maxValue);

    if ( minValue == maxValue )
        return minValue;

    double randomPart = (double)rand() / ((double)RAND_MAX + 1.0);
    int64_t range = (int64_t)maxValue - minValue;
    return minValue + (int)((range + 1) * randomPart);
}

static vec3d ypabact_BuildRandomLocalDecorationFXOffset(float radius)
{
    if ( radius <= 0.0 )
        return vec3d(0.0, 0.0, 0.0);

    vec3d localOffset;
    localOffset.x = (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * radius;
    localOffset.y = (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * radius;
    localOffset.z = (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * radius;
    return localOffset;
}

static bool ypabact_GetDecorationFXSpawnCount(const World::TDecorationFXConfig &config, int &spawnCount)
{
    int countMin = std::max(0, std::min(config.count_min, 32));
    int countMax = std::max(0, std::min(config.count_max, 32));

    if ( countMin <= 0 || countMax <= 0 )
        return false;

    if ( countMax < countMin )
        std::swap(countMin, countMax);

    spawnCount = ypabact_RandomInRange(countMin, countMax);
    return spawnCount > 0;
}

static void ypabact_SpawnDecorationFXEvent(NC_STACK_ypabact *bact)
{
    if ( !bact || bact->_decoration_fx.vp <= 0 )
        return;

    NC_STACK_ypaworld *world = bact->getBACT_pWorld();
    if ( !world )
        return;

    int spawnCount = 0;
    if ( !ypabact_GetDecorationFXSpawnCount(bact->_decoration_fx, spawnCount) )
        return;

    for (int i = 0; i < spawnCount; i++)
    {
        // Vehicle decoration FX visually belong to the moving owner.
        // Spawn them through the attached transient VP path so smoke/spores/glitches
        // follow mobile vehicles instead of being left behind in world-space.
        //
        // This render path also avoids clamping to the coarse sector height, so it
        // preserves the old slope fix used by damaged/debuff FX and prevents random
        // decoration VPs from popping above the unit on hills.
        vec3d localOffset = bact->_decoration_fx.offset + ypabact_BuildRandomLocalDecorationFXOffset(bact->_decoration_fx.random_pos);
        world->SpawnAttachedTransientVP(bact->_decoration_fx.vp,
                                        bact,
                                        localOffset,
                                        1000,
                                        bact->_decoration_fx.scale,
                                        true);
    }
}

static void ypabact_PlayDamagedEventShake(NC_STACK_ypabact *bact)
{
    if ( !ypabact_EnsureDamagedShakeCarrier(bact) )
        return;

    ypabact_StartStatusSoundIfIdle(bact, &bact->_damaged_shake_carrier, 0, 0);
}

void NC_STACK_ypabact::UpdateDamageFX(update_msg *)
{
    bool canUseDamaged = ypabact_CanUseGameplayStatusMechanics(this);
    bool damaged = canUseDamaged && ypabact_IsDamagedStateActive(this);

    ypabact_ApplyDamagedRuntime(this, damaged);

    if ( !canUseDamaged || !damaged )
    {
        _damaged_fx_next_time = 0;
        return;
    }

    if ( !_world->UpdateRandomFXTimer(_damaged_fx.interval_min, _damaged_fx.interval_max, _damaged_fx_next_time) )
        return;

    ypabact_SpawnDamagedFXEvent(this);
    ypabact_PlayDamagedEventShake(this);
}

void NC_STACK_ypabact::UpdateDecorationFX(update_msg *)
{
    if ( !ypabact_CanSpawnDecorationFX(this) || _decoration_fx.vp <= 0 )
    {
        _decoration_fx_next_time = 0;
        return;
    }

    if ( !_world->UpdateRandomFXTimer(_decoration_fx.interval_min, _decoration_fx.interval_max, _decoration_fx_next_time) )
        return;

    ypabact_SpawnDecorationFXEvent(this);
}

// OpenUA aoe_unit_push knockback.
// AddAoePush() is called once by the explosion. It converts the requested push
// distance into a residual velocity so that, integrated and decayed over the
// next ~AOE_PUSH_TAU seconds, the unit travels about `distance` world units.
// Using a residual velocity on the unit (rather than moving _position instantly
// or touching the class-specific _fly_dir physics) makes the knockback both
// smooth (spread over several frames) and uniform across every vehicle class.
static const float AOE_PUSH_TAU = 0.30f; // knockback time constant, seconds
static const float AOE_PUSH_MAX_DT = 0.05f;
static const float AOE_PUSH_MAX_STEP = 80.0f;

static bool ypabact_IsAoePushGroundAlignedUnit(NC_STACK_ypabact *unit)
{
    if ( !unit )
        return false;

    return unit->_bact_type == BACT_TYPES_TANK ||
           unit->_bact_type == BACT_TYPES_CAR ||
           unit->_bact_type == BACT_TYPES_HOVER;
}

static bool ypabact_SnapAoePushGroundUnit(NC_STACK_ypabact *unit)
{
    ypaworld_arg136 ground;
    ground.stPos = unit->_position.X0Z() - vec3d::OY(30000.0);
    ground.vect = vec3d::OY(50000.0);
    ground.flags = 0;

    unit->getBACT_pWorld()->ypaworld_func136(&ground);

    if ( !ground.isect )
        return false;

    unit->_position.y = ground.isectPos.y - (unit->getBACT_viewer() ? unit->_viewer_overeof : unit->_overeof);
    unit->_status_flg |= BACT_STFLAG_LAND;
    return true;
}

void NC_STACK_ypabact::AddAoePush(const vec3d &dir, float distance)
{
    if ( distance <= 0.0f )
        return;

    float dirLen = dir.length();
    if ( !isfinite(dirLen) || dirLen <= 0.001f )
        return;

    _aoePushVel += (dir / dirLen) * (distance / AOE_PUSH_TAU);
}

void NC_STACK_ypabact::UpdateAoePush(update_msg *arg)
{
    float pushSpeed = _aoePushVel.length();
    if ( !isfinite(pushSpeed) || pushSpeed < 1.0f )
    {
        _aoePushVel = vec3d(0.0, 0.0, 0.0);
        return;
    }

    float dtime = arg->frameTime / 1000.0;
    if ( !isfinite(dtime) || dtime <= 0.0f )
        return;

    if ( dtime > AOE_PUSH_MAX_DT )
        dtime = AOE_PUSH_MAX_DT;

    // Move this frame's slice, terrain-checked so we never shove a unit through
    // the ground or a wall (same safety as the engine's ApplyImpulse).
    vec3d totalStep = _aoePushVel * dtime;
    float stepLen = totalStep.length();
    if ( !isfinite(stepLen) || stepLen <= 0.0f )
    {
        _aoePushVel = vec3d(0.0, 0.0, 0.0);
        return;
    }

    int slices = (int)ceil(stepLen / AOE_PUSH_MAX_STEP);
    if ( slices < 1 )
        slices = 1;

    vec3d step = totalStep / (float)slices;
    bool groundAligned = ypabact_IsAoePushGroundAlignedUnit(this);

    for (int i = 0; i < slices; i++)
    {
        ypaworld_arg136 moveTest;
        moveTest.stPos = _position;
        moveTest.vect  = step;
        moveTest.flags = 0;

        _world->ypaworld_func136(&moveTest);

        if ( moveTest.isect )
        {
            _aoePushVel = vec3d(0.0, 0.0, 0.0); // hit terrain: stop the knockback
            break;
        }

        _position += step;

        if ( groundAligned && !ypabact_SnapAoePushGroundUnit(this) )
        {
            _aoePushVel = vec3d(0.0, 0.0, 0.0);
            break;
        }
    }

    // Exponential decay toward zero.
    _aoePushVel *= expf(-dtime / AOE_PUSH_TAU);
}

void NC_STACK_ypabact::Render(baseRender_msg *arg)
{
    // OpenUA Black Sect clone balance: imperfect grey clones (owner 5) always wear the
    // grey clone identity tint. In V1 this deliberately overrides any manual per-prototype
    // visual_tint so the clone is always visually readable. This is render-only: it reads
    // the cached config and never mutates _visual_tint or the shared prototype.
    const World::TVisualTint effectiveTint =
        World::CloneBalance::IsCloneActor(this) ? World::CloneBalance::Tint() : _visual_tint;

    auto shouldApplyVisualScale = [this](NC_STACK_base *base)
    {
        if ( _visual_scale_vec.x == 1.0 && _visual_scale_vec.y == 1.0 && _visual_scale_vec.z == 1.0 )
            return false;

        if ( _bact_type == BACT_TYPES_MISSLE )
        {
            // Weapon visual_scale is only the live projectile body scale.
            return base == _vp_normal || base == _vp_fire || base == _vp_wait;
        }

        // Vehicle visual_scale is limited to normal live/create visuals, not death effects.
        return base == _vp_normal || base == _vp_fire || base == _vp_wait || base == _vp_genesis;
    };

    // OpenUA custom visual_tint: same eligible visual prototypes as visual_scale.
    // Tint is a visual-only per-instance RGBA multiplier; never affects gameplay.
    // effectiveTint already folds in the Black Sect grey clone override (see above).
    auto shouldApplyVisualTint = [this, &effectiveTint](NC_STACK_base *base)
    {
        if ( effectiveTint.IsNeutral() )
            return false;

        if ( _bact_type == BACT_TYPES_MISSLE )
            return base == _vp_normal || base == _vp_fire || base == _vp_wait;

        return base == _vp_normal || base == _vp_fire || base == _vp_wait || base == _vp_genesis;
    };

    // Apply the tint only around eligible visual prototypes, then restore the
    // neutral default so nothing else rendered with this message gets tinted.
    auto setTint = [&effectiveTint, arg](bool on)
    {
        if ( on )
            arg->tint = GFX::TGLColor(effectiveTint.r, effectiveTint.g, effectiveTint.b, effectiveTint.a);
        else
            arg->tint = GFX::TGLColor(1.0, 1.0, 1.0, 1.0);
    };

    if ( _current_vp )
    {
        if ( !(_status_flg & BACT_STFLAG_NORENDER) )
        {
            if ( !(_oflags & BACT_OFLAG_VIEWER) || _oflags & BACT_OFLAG_ALWAYSREND )
            {
                _current_vp->Bas->TForm().Pos = _tForm.Pos;
                _current_vp->Bas->TForm().SclRot = _tForm.SclRot;

                if ( shouldApplyVisualScale(_current_vp->Bas) )
                    _current_vp->Bas->TForm().SclRot *= mat3x3::Scale(_visual_scale_vec);

                bool tinted = shouldApplyVisualTint(_current_vp->Bas);
                setTint(tinted);
                _current_vp->Bas->Render(arg, _current_vp);
                if ( tinted )
                    setTint(false);
            }
        }
    }

    for (int i = 0; i < 3; i++)
    {
        extra_vproto *bd = &_vp_extra[i];

        if ( bd->vp )
        {
            if ( bd->flags & EVPROTO_FLAG_ACTIVE )
            {
                bd->vp->Bas->TForm().Pos = bd->pos;

                if ( bd->flags & EVPROTO_FLAG_SCALE )
                    bd->vp->Bas->TForm().SclRot = bd->rotate.Transpose() * mat3x3::Scale( vec3d(bd->scale, bd->scale, bd->scale) );
                else
                    bd->vp->Bas->TForm().SclRot = bd->rotate.Transpose();

                if ( shouldApplyVisualScale(bd->vp->Bas) )
                    bd->vp->Bas->TForm().SclRot *= mat3x3::Scale(_visual_scale_vec);

                bool tinted = shouldApplyVisualTint(bd->vp->Bas);
                setTint(tinted);
                bd->vp->Bas->Render(arg, bd->vp);
                if ( tinted )
                    setTint(false);
            }
        }
    }

}

void NC_STACK_ypabact::SetTarget(setTarget_msg *arg)
{
    _assess_time = 0;
    yw_130arg arg130;
    
    constexpr float CurSectrLength = World::CVSectorLength + 10.0;

    if ( _status_flg & BACT_STFLAG_DEATH1 && arg->tgt_type == BACT_TGT_TYPE_UNIT )
    {
        ypa_log_out("ALARM!!! bact-settarget auf logische Leiche owner %d, class %d, prio %d\n", _owner, _bact_type, arg->priority);
    }
    else if ( arg->priority )
    {
        if ( _secndTtype == BACT_TGT_TYPE_UNIT )
            _secndT.pbact->DeleteAttacker(this, 1);

        switch ( arg->tgt_type )
        {
        case BACT_TGT_TYPE_CELL:
        case BACT_TGT_TYPE_CELL_IND:
            _secndTtype = BACT_TGT_TYPE_CELL;

            arg130.pos_x = arg->tgt_pos.x;
            arg130.pos_z = arg->tgt_pos.z;

            if ( arg130.pos_x < CurSectrLength )
                arg130.pos_x = CurSectrLength;

            if ( arg130.pos_x > _wrldSize.x - CurSectrLength )
                arg130.pos_x = _wrldSize.x - CurSectrLength;

            if ( arg130.pos_z > -CurSectrLength )
                arg130.pos_z = -CurSectrLength;

            if ( arg130.pos_z < _wrldSize.y + CurSectrLength )
                arg130.pos_z = _wrldSize.y + CurSectrLength;

            if ( _world->GetSectorInfo(&arg130) )
            {
                _secndT.pcell = arg130.pcell;
                _sencdTpos.x = arg130.pos_x;
                _sencdTpos.z = arg130.pos_z;
                _sencdTpos.y = arg130.pcell->height;
            }
            else
            {
                _secndTtype = BACT_TGT_TYPE_NONE;
                _secndT.pcell = NULL;
            }
            break;

        case BACT_TGT_TYPE_UNIT:
        case BACT_TGT_TYPE_UNIT_IND:
            _secndT.pbact = arg->tgt.pbact;
            _secndTtype = BACT_TGT_TYPE_UNIT;

            if ( _secndT.pbact )
            {
                if ( _secndT.pbact->_status_flg & BACT_STFLAG_DEATH1 )
                {
                    ypa_log_out("totes vehicle als nebenziel, owner %d, class %d\n", arg->tgt.pbact->_owner, arg->tgt.pbact->_bact_type);
                    _secndTtype = BACT_TGT_TYPE_NONE;
                }
                else
                {
                    _sencdTpos = _secndT.pbact->_position;
                    _secndT.pbact->AddAttacker(this, 1);
                }
            }
            else
            {
                ypa_log_out("Yppsn\n");
                _secndTtype = BACT_TGT_TYPE_NONE;
            }
            break;

        case BACT_TGT_TYPE_FRMT:
            _secndTtype = BACT_TGT_TYPE_FRMT;
            _sencdTpos = arg->tgt_pos;
            break;

        case BACT_TGT_TYPE_NONE:
            _secndT.pbact = NULL;
            _secndTtype = BACT_TGT_TYPE_NONE;
            break;

        default:
            _secndTtype = arg->tgt_type;
            break;
        }
    }
    else
    {
        if ( _primTtype == BACT_TGT_TYPE_UNIT )
            _primT.pbact->DeleteAttacker(this, 0);

        switch ( arg->tgt_type )
        {
        case BACT_TGT_TYPE_CELL:
        case BACT_TGT_TYPE_CELL_IND:
            _primT_cmdID = 0;
            _primTtype = BACT_TGT_TYPE_CELL;

            arg130.pos_x = arg->tgt_pos.x;
            arg130.pos_z = arg->tgt_pos.z;

            if ( arg130.pos_x < CurSectrLength )
                arg130.pos_x = CurSectrLength;

            if ( arg130.pos_x > _wrldSize.x - CurSectrLength )
                arg130.pos_x = _wrldSize.x - CurSectrLength;

            if ( arg130.pos_z > -CurSectrLength )
                arg130.pos_z = -CurSectrLength;

            if ( arg130.pos_z < _wrldSize.y + CurSectrLength )
                arg130.pos_z = _wrldSize.y + CurSectrLength;

            if ( _world->GetSectorInfo(&arg130) )
            {
                _primT.pcell = arg130.pcell;
                _primTpos.x = arg130.pos_x;
                _primTpos.z = arg130.pos_z;
                _primTpos.y = arg130.pcell->height;
            }
            else
            {
                _primTtype = BACT_TGT_TYPE_NONE;
                _primT.pcell = NULL;
            }
            break;

        case BACT_TGT_TYPE_UNIT:
        case BACT_TGT_TYPE_UNIT_IND:
            _primT.pbact = arg->tgt.pbact;
            _primTtype = BACT_TGT_TYPE_UNIT;

            if ( _primT.pbact )
            {
                if ( _primT.pbact->_status_flg & BACT_STFLAG_DEATH1 )
                {
                    ypa_log_out("totes vehicle als hauptziel, owner %d, class %d - ich bin class %d\n", arg->tgt.pbact->_owner, arg->tgt.pbact->_bact_type, _bact_type);
                    _primTtype = BACT_TGT_TYPE_NONE;
                    return;
                }

                _primTpos = _primT.pbact->_position;

                _primT.pbact->AddAttacker(this, 0);

                _primT_cmdID = arg->tgt.pbact->_commandID;
            }
            else
            {
                ypa_log_out("PrimT. without a pointer\n");
                _primTtype = BACT_TGT_TYPE_NONE;
            }
            break;

        case BACT_TGT_TYPE_FRMT:
            _primTtype = BACT_TGT_TYPE_FRMT;
            _primT_cmdID = 0;
            _primTpos = arg->tgt_pos;
            break;

        case BACT_TGT_TYPE_NONE:
            _primT.pbact = NULL;
            _waypoints_count = 0;
            _primTtype = BACT_TGT_TYPE_NONE;
            _status_flg &= ~BACT_STFLAG_WAYPOINT;
            break;

        case BACT_TGT_TYPE_DRCT:
            _target_dir = arg->tgt_pos;
            _primTtype = BACT_TGT_TYPE_DRCT;
            _primT.pbact = NULL;
            _primT_cmdID = 0;
            break;

        default:
            _primTtype = arg->tgt_type;
            break;
        }

        if ( arg->tgt_type == BACT_TGT_TYPE_CELL || arg->tgt_type == BACT_TGT_TYPE_UNIT )
        {
            for ( NC_STACK_ypabact* &node : _kidList )
            {
                if ( node->_status != BACT_STATUS_DEAD)
                {
                    node->SetTarget(arg);
                    if ( !(_status_flg & BACT_STFLAG_WAYPOINT)  )
                        node->_status_flg &= ~(BACT_STFLAG_WAYPOINT | BACT_STFLAG_WAYPOINTCCL);
                }
            }
        }
    }
}

void NC_STACK_ypabact::AI_layer1(update_msg *arg)
{
    setTarget_msg v36;

    if ( _mass == 1.0 )
    {
        if ( _status_flg & BACT_STFLAG_DEATH2 )
        {
            _yls_time -= arg->frameTime;

            if ( _yls_time < 0 )
                _world->ypaworld_func144(this);
        }
        else
        {
            setState_msg v37;
            v37.newStatus = BACT_STATUS_NOPE;
            v37.unsetFlags = 0;
            v37.setFlags = BACT_STFLAG_DEATH2;

            SetState(&v37);

            _yls_time = 6000;
        }
        return;
    }

    if ( _bact_type != BACT_TYPES_MISSLE )
        EnergyInteract(arg);

    if ( _status == BACT_STATUS_DEAD )
    {
        if ( _status_flg & BACT_STFLAG_LAND )
            _yls_time -= arg->frameTime;
    }

    _airconst = _airconst_static;

    _soundcarrier.Sounds[0].Pitch = _pitch;
    _soundcarrier.Sounds[0].Volume = _volume;

    if ( _clock - _AI_time1 < 250 || 
         _primTtype == BACT_TGT_TYPE_DRCT || 
         _bact_type == BACT_TYPES_GUN || 
         _status == BACT_STATUS_DEAD || 
         _status == BACT_STATUS_BEAM ||
         _status == BACT_STATUS_CREATE )
    {
        AI_layer2(arg);
        return;
    }

    _AI_time1 = _clock;
    _target_vec = vec3d(0.0, 0.0, 0.0);

    if ( _clock - _brkfr_time > 5000 )
    {
        _brkfr_time = _clock;

        StuckFree(arg);
    }

    if ( _status == BACT_STATUS_NORMAL && _primTtype != BACT_TGT_TYPE_NONE )
    {
        if ( _primTtype == BACT_TGT_TYPE_UNIT )
        {
            _target_vec = _primT.pbact->_position - _position;

            if ( _primT.pbact->_status != BACT_STATUS_DEAD)
                _primTpos = _primT.pbact->_position;
        }
        else
        {
            _target_vec = _primTpos - _position;
        }

        if ( _target_vec.length() > 2000.0 )
            _target_vec.y = 0;

        if ( IsParentMyRobo() &&
             (_oflags & BACT_OFLAG_VIEWER) )
        {
            bool doFight = _target_vec.length() < 800.0;

            int unitId = 0;
            for (NC_STACK_ypabact* &node : _kidList)
            {
                if ( node->_status != BACT_STATUS_DEAD )
                {
                    if ( doFight )
                    {
                        v36.tgt_type = _primTtype;
                        v36.priority = 0;
                        v36.tgt.pbact = _primT.pbact;
                        v36.tgt_pos = _primTpos;
                    }
                    else
                    {
                        bact_arg94 v35;
                        v35.field_0 = unitId;
                        GetFormationPosition(&v35);

                        v36.tgt_type = BACT_TGT_TYPE_FRMT;
                        v36.priority = 0;
                        v36.tgt_pos = v35.pos1;
                    }

                    node->SetTarget(&v36);
                }
                unitId++;
            }
        }
    }

    if ( _primTtype == BACT_TGT_TYPE_NONE)
    {
        if ( _host_station && _primT_cmdID )
        {
            v36.priority = _primT_cmdID;

            if ( _host_station->yparobo_func132(&v36) )
            {
                v36.priority = 0;
            }
            else
            {
                _primT_cmdID = 0;

                v36.tgt_type = BACT_TGT_TYPE_CELL;
                v36.priority = 0;
                v36.tgt_pos = _primTpos;
            }

            SetTarget(&v36);
        }
    }

    if ( _oflags & BACT_OFLAG_USERINPT )
    {
        if ( _primTtype == BACT_TGT_TYPE_UNIT && 
             _primT.pbact )
        {
            if ( !_primT.pbact->_pSector->IsCanSee(_owner) )
            {
                v36.tgt_type = BACT_TGT_TYPE_NONE;
                v36.priority = 0;

                SetTarget(&v36);
            }
        }
    }
    else if ( _vp_active == 6 && _status == BACT_STATUS_NORMAL )
    {
        setState_msg v38;
        v38.newStatus = BACT_STATUS_NORMAL;
        v38.setFlags = 0;
        v38.unsetFlags = 0;
        SetState(&v38);
    }

    AI_layer2(arg);
}

void NC_STACK_ypabact::AI_layer2(update_msg *arg)
{
    constexpr float CurSectrLength = 1.05 * World::CVSectorLength;
    
    if ( (_clock - _AI_time2) < 250 
       || _owner == 0 
       || _secndTtype == BACT_TGT_TYPE_DRCT 
       || _status == BACT_STATUS_CREATE 
       || _status == BACT_STATUS_DEAD 
       || _status == BACT_STATUS_BEAM )
    {
        if ( _oflags & BACT_OFLAG_USERINPT )
            User_layer(arg);
        else
            AI_layer3(arg);
        
        return;
    }
    
    _AI_time2 = _clock;

    if ( _status_flg & BACT_STFLAG_ESCAPE && 
         _target_vec.XZ().length() > 3600.0 )
    {
        setTarget_msg arg67;
        arg67.tgt_type = BACT_TGT_TYPE_NONE;
        arg67.priority = 1;

        for(NC_STACK_ypabact* &nod : _kidList)
            nod->SetTarget(&arg67);

        SetTarget(&arg67);

        if ( _oflags & BACT_OFLAG_USERINPT )
            User_layer(arg);
        else
            AI_layer3(arg);
        return;
    }

    NC_STACK_ypabact *wee = _world->getYW_userHostStation();

    if ( _status == BACT_STATUS_NORMAL || _status == BACT_STATUS_IDLE )
    {
        if ( _clock - _search_time1 > 500 )
        {
            _search_time1 = _clock;

            NC_STACK_ypabact *enemy = GetSectorTarget(_cellId);
            if ( enemy )
            {
                if ( enemy->_bact_type != BACT_TYPES_ROBO && IsParentMyRobo() && _host_station == wee && enemy->_commandID != _fe_cmdID && _clock - _fe_time > 45000 )
                {
                    bool isRoboGun = false;
                    if ( enemy->_bact_type == BACT_TYPES_GUN )
                    {
                        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( enemy );
                        isRoboGun = gun->IsRoboGun();
                    }

                    if ( !isRoboGun )
                    {
                        _fe_cmdID = enemy->_commandID;
                        _fe_time = _clock;

                        robo_arg134 arg134;
                        arg134.field_4 = 7;
                        arg134.field_8 = enemy->_commandID;
                        arg134.field_C = 0;
                        arg134.field_10 = 0;
                        arg134.unit = this;
                        arg134.field_14 = 46;

                        _host_station->placeMessage(&arg134);
                    }
                }
            }

            if ( _status == BACT_STATUS_IDLE || 
                 (  _aggr >= 50 && 
                  !(_status_flg & BACT_STFLAG_ESCAPE) && 
                     (_secndTtype == BACT_TGT_TYPE_NONE || 
                      _secndTtype == BACT_TGT_TYPE_CELL || 
                      _secndTtype == BACT_TGT_TYPE_FRMT) ) )
            {
                if ( enemy )
                {
                    _secndT_cmdID = enemy->_commandID;

                    setTarget_msg arg67;
                    arg67.tgt_type = BACT_TGT_TYPE_UNIT;
                    arg67.priority = 1;
                    arg67.tgt.pbact = enemy;

                    SetTarget(&arg67);
                }

                if ( (_clock - _search_time2) > 2000 && 
                     _aggr == 75 && 
                    !(_oflags & BACT_OFLAG_VIEWER) && 
                     IsParentMyRobo() &&
                     (_secndTtype == BACT_TGT_TYPE_FRMT || 
                      _secndTtype == BACT_TGT_TYPE_NONE) )
                {
                    if (  _position.x > CurSectrLength && 
                          _position.x < _wrldSize.x + -CurSectrLength && 
                          _position.z < -CurSectrLength && 
                          _position.z > _wrldSize.y + CurSectrLength )
                    {
                        _search_time2 = _clock;

                        if ( _owner != _pSector->owner )
                        {
                            setTarget_msg arg67;
                            arg67.priority = 1;
                            arg67.tgt_type = BACT_TGT_TYPE_CELL;
                            arg67.tgt_pos.x = _position.x;
                            arg67.tgt_pos.z = _position.z;

                            SetTarget(&arg67);
                        }
                    }
                }

                if ( IsParentMyRobo() && _secndTtype == BACT_TGT_TYPE_CELL )
                {
                    for(NC_STACK_ypabact* &nod : _kidList)
                    {
                        if ( nod->_secndTtype == BACT_TGT_TYPE_NONE || nod->_secndTtype == BACT_TGT_TYPE_FRMT )
                        {
                            setTarget_msg arg67;
                            arg67.tgt_type = BACT_TGT_TYPE_CELL;
                            arg67.tgt_pos = _sencdTpos;
                            arg67.priority = 1;
                            nod->SetTarget(&arg67);
                        }
                    }
                }
            }
        }

        if ( _secndTtype == BACT_TGT_TYPE_UNIT )
            _target_vec = _secndT.pbact->_position - _position;
        else if ( _secndTtype == BACT_TGT_TYPE_CELL)
            _target_vec = _sencdTpos - _position;

        if ( _target_vec.length() > 2000.0 )
            _target_vec.y = 0;
    }

    if ( _oflags & BACT_OFLAG_USERINPT )
    {
        if ( _secndTtype == BACT_TGT_TYPE_UNIT && _secndT.pbact )
        {
            if ( !_secndT.pbact->_pSector->IsCanSee(_owner) || 
                  (_position.XZ() - _secndT.pbact->_position.XZ()).length() > 2160.0 )
            {
                setTarget_msg arg67;
                arg67.priority = 1;
                arg67.tgt_type = BACT_TGT_TYPE_NONE;
                SetTarget(&arg67);
            }
        }
    }
    
    if ( _oflags & BACT_OFLAG_USERINPT )
        User_layer(arg);
    else
        AI_layer3(arg);
}

void AI_layer3__sub1(NC_STACK_ypabact *bact, update_msg *arg)
{
    bact->_thraction = bact->_force;

    float v39 = arg->frameTime / 1000.0;

    float top = -bact->_target_dir.y;

    if ( top == 1.0 )
        top = 0.99998999;

    if ( top == -1.0 )
        top = -0.99998999;

    float weight = bact->_mass * 9.80665;
    float thraction = bact->_thraction;

    if ( thraction == 0.0 )
        thraction = 0.1;

    float v5 = sqrt( (1.0 - POW2(top)) ) * (top * -0.5);

    float v6 = (1.0 - 0.25 * POW2(top) + 0.25 * POW2(top) * POW2(top)) * (POW2(weight) / POW2(thraction));

    float v58 = sqrt( (1.0 - v6) ) + (weight * v5 / thraction);

    vec3d tmp;
    tmp.y = -cos( clp_asin(v58) );

    if ( bact->_target_dir.z != 0.0 )
    {
        float v62 = (1.0 - POW2(tmp.y)) / (POW2(bact->_target_dir.x) / POW2(bact->_target_dir.z) + 1.0);

        if ( v62 < 0.0 )
            v62 = 0.0;

        tmp.z = sqrt(v62);

        if ( bact->_target_dir.z < 0.0 )
            tmp.z = -tmp.z;
    }
    else
    {
        tmp.z = 0.0;
    }

    if ( bact->_target_dir.x != 0.0 )
    {
        float v57 = (1.0 - POW2(tmp.y)) / (POW2(bact->_target_dir.z) / POW2(bact->_target_dir.x) + 1.0);

        if ( v57 < 0.0 )
            v57 = 0.0;

        tmp.x = sqrt(v57);

        if ( bact->_target_dir.x < 0.0 )
            tmp.x = -tmp.x;
    }
    else
    {
        tmp.x = 0.0;
    }

    vec3d vaxis = (-bact->_rotation.AxisY()) * tmp;;

    if ( vaxis.normalise() != 0.0 )
    {
        float maxrot = bact->_maxrot * v39;

        float v56 = clp_acos( tmp.dot( -bact->_rotation.AxisY() ) );

        if ( v56 > maxrot )
            v56 = maxrot;

        if ( fabs(v56) > BACT_MIN_ANGLE )
            bact->_rotation *= mat3x3::AxisAngle(vaxis, v56);
    }
}

void AI_layer3__sub0(NC_STACK_ypabact *bact, int a2)
{
    if ( clp_acos(bact->_rotation.m11) > 0.003 && (bact->_fly_dir.z != 0.0 || bact->_fly_dir.x != 0.0) && bact->_fly_dir_length > 0.0 )
    {
        float v11 = a2 / 1000.0;

        vec2d flydir = bact->_fly_dir.XZ();
        vec2d axisZ = bact->_rotation.AxisZ().XZ();

        float tmpsq = flydir.length();        
        float v18 = 0.0;
        
        if ( isnormal(tmpsq) ) // Not NULL, NAN, INF
            v18 = flydir.dot(axisZ) / tmpsq;

        tmpsq = axisZ.length();
        
        if ( isnormal(tmpsq) ) // Not NULL, NAN, INF
            v18 /= tmpsq;
        else
            v18 = 0.0;

        float v20 = clp_acos( v18 );

        float v13 = bact->_maxrot * v11 * (fabs(v20) * 0.8 + 0.2);

        if ( v20 > v13 )
            v20 = v13;

        if ( bact->_fly_dir.x * bact->_rotation.m22 - bact->_rotation.m20 * bact->_fly_dir.z < 0.0 )
            v20 = -v20;

        bact->_rotation *= mat3x3::RotateY(-v20);
    }
}

void NC_STACK_ypabact::AI_layer3(update_msg *arg)
{
    float v75 = arg->frameTime / 1000.0;

    float v77 = _target_vec.length();

    if ( v77 > 0.0 )
        _target_dir = _target_vec / v77;

    int v82 = _oflags & BACT_OFLAG_VIEWER;
    int v70 = _oflags & BACT_OFLAG_EXACTCOLL;

    int v80 = _world->ypaworld_func145(this);

    int v79;

    if ( v82 )
        v79 = _viewer_radius;
    else
        v79 = _radius;

    switch ( _status )
    {
    case BACT_STATUS_NORMAL:
    {
        if ( _oflags & BACT_OFLAG_BACTCOLL )
        {
            if ( (v80 || (_secndTtype == BACT_TGT_TYPE_NONE && v77 < World::CVSectorLength)) && !(_status_flg & BACT_STFLAG_LAND) )
            {
                CollisionWithBact(arg->frameTime);
            }
        }

        if ( !_primTtype && !_secndTtype )
        {
            _status = BACT_STATUS_IDLE;

            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                setState_msg arg78;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.setFlags = 0;
                arg78.unsetFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }
            break;
        }

        ypaworld_arg136 arg136;

        arg136.isect = false;
        arg136.stPos = _old_pos;
        arg136.vect = _position - _old_pos;
        arg136.vect.y = 0;

        float len = arg136.vect.length();

        if ( len > 0.0 )
            arg136.vect *= 300.0 / len;
        else
            arg136.vect = _rotation.AxisZ() * 300.0;

        arg136.isect = false;
        arg136.flags = 0;

        ypaworld_arg136 arg136_1;
        arg136_1.isect = false;
        arg136_1.flags = 0;

        if ( v82 || (_status_flg & BACT_STFLAG_DODGE_RIGHT) || (v80 && v70) )
        {
            arg136_1.stPos = _old_pos;
            arg136_1.vect.x = arg136.vect.x * 0.93969 - arg136.vect.z * 0.34202;
            arg136_1.vect.y = arg136.vect.y;
            arg136_1.vect.z = arg136.vect.z * 0.93969 + arg136.vect.x * 0.34202;

            _world->ypaworld_func136(&arg136_1);
        }

        ypaworld_arg136 arg136_2;
        arg136_2.isect = false;
        arg136_2.flags = 0;

        if ( v82 || (_status_flg & BACT_STFLAG_DODGE_LEFT) || (v80 && v70) )
        {
            arg136_2.stPos = _old_pos;
            arg136_2.vect.x = arg136.vect.x * 0.93969 + arg136.vect.z * 0.34202;
            arg136_2.vect.y = arg136.vect.y;
            arg136_2.vect.z = arg136.vect.z * 0.93969 - arg136.vect.x * 0.34202;

            _world->ypaworld_func136(&arg136_2);
        }

        if ( v82 || !(_status_flg & (BACT_STFLAG_DODGE_LEFT | BACT_STFLAG_DODGE_RIGHT)) || (v80 && v70) )
            _world->ypaworld_func136(&arg136);

        int v18 = 0;

        bact_arg88 arg88;
        arg88.pos1 = vec3d(0.0, 0.0, 0.0);

        if ( arg136.isect )
        {
            if ( len + v79 > arg136.tVal * 300.0 )
            {
                arg88.pos1 = arg136.skel->polygons[arg136.polyID].Normal();
                v18++;
            }
        }

        if ( arg136_1.isect )
        {
            if ( len + v79 > arg136_1.tVal * 300.0 )
            {
                arg88.pos1 += arg136_1.skel->polygons[arg136_1.polyID].Normal();
                v18++;
            }
        }

        if ( arg136_2.isect )
        {
            if ( len + v79 > arg136_2.tVal * 300.0 )
            {
                arg88.pos1 += arg136_2.skel->polygons[arg136_2.polyID].Normal();
                v18++;
            }
        }

        if ( !arg136.isect && !arg136_1.isect && !arg136_2.isect )
        {
            _status_flg &= ~(BACT_STFLAG_DODGE_LEFT | BACT_STFLAG_DODGE_RIGHT | BACT_STFLAG_MOVE);
            _status_flg |= BACT_STFLAG_MOVE;
        }

        if ( !(_status_flg & (BACT_STFLAG_DODGE_LEFT | BACT_STFLAG_DODGE_RIGHT)) )
        {

            if ( arg136_1.isect == 1 && arg136_2.isect == 1 )
            {
                if ( arg136_1.tVal >= arg136_2.tVal )
                    _status_flg |= BACT_STFLAG_DODGE_LEFT;
                else
                    _status_flg |= BACT_STFLAG_DODGE_RIGHT;
            }

            if ( arg136_1.isect == 1 && !arg136_2.isect )
                _status_flg |= BACT_STFLAG_DODGE_RIGHT;

            if ( !arg136_1.isect && arg136_2.isect == 1 )
                _status_flg |= BACT_STFLAG_DODGE_LEFT;

            if ( !arg136_1.isect && !arg136_2.isect && arg136.isect == 1 )
                _status_flg |= BACT_STFLAG_DODGE_LEFT;
        }

        float v21 = _mass * 9.80665;

        if ( v21 <= _force )
            v21 = _force;

        float v88;

        if ( _airconst >= 10.0 )
            v88 = _airconst;
        else
            v88 = 10.0;


        ypaworld_arg136 arg136_3;

        arg136_3.vect.x = (_fly_dir.x * 200.0 * _fly_dir_length) / (v21 / v88);

        if ( arg136_3.vect.x < -200.0 )
            arg136_3.vect.x = -200.0;

        if ( arg136_3.vect.x > 200.0 )
            arg136_3.vect.x = 200.0;

        arg136_3.vect.y = _height;

        arg136_3.vect.z = (_fly_dir.z * 200.0 * _fly_dir_length) / (v21 / v88);

        if ( arg136_3.vect.z < -200.0 )
            arg136_3.vect.z = -200.0;

        if ( arg136_3.vect.z > 200.0 )
            arg136_3.vect.z = 200.0;

        arg136_3.stPos = _old_pos;
        arg136_3.flags = 0;

        _world->ypaworld_func136(&arg136_3);

        if ( arg136_3.isect )
        {
            _target_dir.y = -(1.0 - arg136_3.tVal);
        }
        else
        {
            NC_STACK_ypabact *a4 = _world->getYW_userVehicle();

            if ( ((_secndTtype != BACT_TGT_TYPE_UNIT || (a4 != _secndT.pbact && _secndT.pbact->_bact_type != BACT_TYPES_ROBO)) &&
                    (_primTtype != BACT_TGT_TYPE_UNIT || (a4 != _primT.pbact && _primT.pbact->_bact_type != BACT_TYPES_ROBO)))
                    || _target_dir.y >= -0.01 )
            {
                if ( _target_dir.y < 0.15 )
                    _target_dir.y = 0.15;
            }
        }

        if ( _status_flg & (BACT_STFLAG_DODGE_LEFT | BACT_STFLAG_DODGE_RIGHT) )
            _target_dir.y = -0.2;

        if ( arg136_3.isect )
        {
            if ( arg136_3.tVal * _height < _radius && _fly_dir.y > 0.0 )
            {
                arg88.pos1 += arg136_3.skel->polygons[arg136_3.polyID].Normal();

                v18++;
            }
        }

        if ( v18 )
        {
            float v29 = v18;

            arg88.pos1 /= v29;

            Recoil(&arg88);
        }
        else
        {
            _status_flg &= ~BACT_STFLAG_LCRASH;
        }

        if ( _target_dir.y != 0.0 )
            _target_dir.normalise();

        float tmpsq = arg136.vect.XZ().length();
        if (isnormal(tmpsq)) // not NULL, NAN, INF
        {
            if ( _status_flg & BACT_STFLAG_DODGE_LEFT )
            {
                _target_dir.x = -arg136.vect.z / tmpsq;
                _target_dir.z = arg136.vect.x / tmpsq;
            }
            else if ( _status_flg & BACT_STFLAG_DODGE_RIGHT )
            {
                _target_dir.x = arg136.vect.z / tmpsq;
                _target_dir.z = -arg136.vect.x / tmpsq;
            }
        }
        else // emulate watcom div 0.0
        {
            if ( _status_flg & (BACT_STFLAG_DODGE_LEFT | BACT_STFLAG_DODGE_RIGHT) )
            {
                _target_dir.x = 0.0;
                _target_dir.z = 0.0;
            }
        }

        ApplySeekAndExplodeRammingGuidance(false);

        AI_layer3__sub1(this, arg);

        /*if ( bact->status_flg & BACT_STFLAG_DODGE ) //Unused flag
            bact->fly_dir_length *= 0.95;*/

        _thraction = (0.85 - _target_dir.y) * _force;

        move_msg arg74;
        arg74.flag = 0;
        arg74.field_0 = arg->frameTime / 1000.0;

        Move(&arg74);

        AI_layer3__sub0(this, arg->frameTime);

        bact_arg75 arg75;

        arg75.fperiod = v75;
        arg75.g_time = _clock;

        if ( _secndTtype == BACT_TGT_TYPE_UNIT )
        {
            arg75.target.pbact = _secndT.pbact;
            arg75.prio = 1;

            FightWithBact(&arg75);
        }
        else if ( _secndTtype == BACT_TGT_TYPE_CELL )
        {
            arg75.pos = _sencdTpos;
            arg75.target.pcell = _secndT.pcell;
            arg75.prio = 1;

            FightWithSect(&arg75);
        }
        else if ( _primTtype == BACT_TGT_TYPE_UNIT )
        {
            arg75.target.pbact = _primT.pbact;
            arg75.prio = 0;

            FightWithBact(&arg75);
        }
        else if ( _primTtype == BACT_TGT_TYPE_CELL )
        {
            arg75.pos = _primTpos;
            arg75.target.pcell = _primT.pcell;
            arg75.prio = 0;

            FightWithSect(&arg75);
        }
        else
        {
            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                setState_msg arg78;
                arg78.unsetFlags = BACT_STFLAG_FIRE;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.setFlags = 0;

                SetState(&arg78);
            }

            _status_flg &= ~(BACT_STFLAG_FIGHT_P | BACT_STFLAG_FIGHT_S);
        }
    }
    break;

    case BACT_STATUS_DEAD:
        DeadTimeUpdate(arg);
        break;

    case BACT_STATUS_IDLE:

        if ( _clock - _newtarget_time > 500 )
        {
            _newtarget_time = _clock;

            bact_arg110 arg110;
            arg110.tgType = _secndTtype;
            arg110.priority = 1;

            int v46 = TargetAssess(&arg110);

            arg110.priority = 0;
            arg110.tgType = _primTtype;
            int v48 = TargetAssess(&arg110);

            if ( v46 != TA_IGNORE || v48 != TA_IGNORE )
            {
                setTarget_msg arg67;

                if ( v46 == TA_CANCEL )
                {
                    arg67.tgt_type = BACT_TGT_TYPE_NONE;
                    arg67.priority = 1;
                    SetTarget(&arg67);
                }

                if ( v48 == TA_CANCEL )
                {
                    arg67.tgt_type = BACT_TGT_TYPE_CELL;
                    arg67.tgt_pos.x = _position.x;
                    arg67.tgt_pos.z = _position.z;
                    arg67.priority = 0;
                    SetTarget(&arg67);
                }

                if ( _primTtype || _secndTtype )
                {
                    setState_msg arg78;
                    arg78.unsetFlags = BACT_STFLAG_LAND;
                    arg78.setFlags = 0;
                    arg78.newStatus = BACT_STATUS_NORMAL;
                    SetState(&arg78);
                    break;
                }
            }
        }

        if ( _oflags & BACT_OFLAG_LANDONWAIT )
        {
            _thraction = 0;

            if ( _status_flg & BACT_STFLAG_LAND )
            {
                setState_msg arg78;
                arg78.unsetFlags = 0;
                arg78.setFlags = 0;
                arg78.newStatus = BACT_STATUS_IDLE;
                SetState(&arg78);

                ypaworld_arg136 v52;
                v52.stPos = _position;
                v52.vect = vec3d(0.0, _overeof + 50.0, 0.0);
                v52.flags = 0;

                _world->ypaworld_func136(&v52);

                if ( v52.isect )
                    _position.y = v52.isectPos.y - _overeof;
            }
            else
            {
                bact_arg86 v65;
                v65.field_one = 0;
                v65.field_two = arg->frameTime;

                CrashOrLand(&v65);
            }
        }
        break;

    case BACT_STATUS_CREATE:
        CreationTimeUpdate(arg);
        break;

    case BACT_STATUS_BEAM:
        BeamingTimeUpdate(arg);
        break;
    }
}

void NC_STACK_ypabact::User_layer(update_msg *arg)
{
    _airconst = _airconst_static;

    float v106 = arg->frameTime / 1000.0;

    if ( _status == BACT_STATUS_NORMAL || _status == BACT_STATUS_IDLE )
    {

        _old_pos = _position;

        if ( _oflags & BACT_OFLAG_BACTCOLL )
        {
            if ( !(_status_flg & BACT_STFLAG_LAND) )
            {
                CollisionWithBact(arg->frameTime);
            }
        }

        float v98;

        if ( _status_flg & BACT_STFLAG_LAND )
            v98 = 0.1;
        else
            v98 = 1.0;

        setState_msg arg78;

        if ( v98 <= fabs(_fly_dir_length) )
        {
            if ( !(_status_flg & BACT_STFLAG_FIRE) )
            {
                arg78.newStatus = BACT_STATUS_NORMAL;
                arg78.unsetFlags = 0;
                arg78.setFlags = 0;
                SetState(&arg78);
            }

            _status_flg &= ~BACT_STFLAG_LAND;
        }
        else
        {
            ypaworld_arg136 arg136;

            arg136.stPos = _position;
            arg136.vect = vec3d(0.0, 0.0, 0.0);

            float v8;

            if ( _viewer_overeof <= _viewer_radius )
                v8 = _viewer_radius;
            else
                v8 = _viewer_overeof;

            arg136.flags = 0;
            arg136.vect.y = v8 * 1.5;

            _world->ypaworld_func136(&arg136);

            if ( arg136.isect && _thraction <= _mass * 9.80665 )
            {
                _fly_dir_length = 0;
                _status_flg |= BACT_STFLAG_LAND;
                _position.y = arg136.isectPos.y - _viewer_overeof;
                _thraction = _mass * 9.80665;
            }
            else
            {
                _status_flg &= ~BACT_STFLAG_LAND;
            }

            if ( _primTtype != BACT_TGT_TYPE_CELL || (_primTpos.XZ() - _position.XZ()).length() <= 800.0 )
            {
                if ( _status_flg & BACT_STFLAG_LAND )
                {
                    if ( !(_status_flg & BACT_STFLAG_FIRE) )
                    {
                        arg78.unsetFlags = 0;
                        arg78.setFlags = 0;
                        arg78.newStatus = BACT_STATUS_IDLE;
                        SetState(&arg78);
                    }
                }

                _status = BACT_STATUS_NORMAL;
            }
            else
            {
                _status = BACT_STATUS_IDLE;

                if ( _status_flg & BACT_STFLAG_LAND )
                {
                    if ( !(_status_flg & BACT_STFLAG_FIRE) )
                    {
                        arg78.newStatus = BACT_STATUS_IDLE;
                        arg78.unsetFlags = 0;
                        arg78.setFlags = 0;
                        SetState(&arg78);
                    }
                }
            }
        }

        float v110 = arg->inpt->Sliders[1] * _maxrot * v106;
        float v103 = -arg->inpt->Sliders[0] * _maxrot * v106;

        if ( (fabs(_fly_dir.y) > 0.98 || _fly_dir_length == 0.0) && _rotation.m11 > 0.996 && arg->inpt->Sliders[1] == 0.0 )
        {
            vec2d axisX = _rotation.AxisX().XZ();;

            if ( axisX.normalise() == 0.0 )
                ypa_log_out("Null on div occur %s:%d\n", __FILE__, __LINE__);

            vec2d axisZ = _rotation.AxisZ().XZ();

            if ( axisZ.normalise() == 0.0 )
                ypa_log_out("Null on div occur %s:%d\n", __FILE__, __LINE__);

            _rotation.SetX( vec3d::X0Z(axisX) );
            _rotation.SetY( vec3d(0.0, 1.0, 0.0) );
            _rotation.SetZ( vec3d::X0Z(axisZ) );
        }

//    float v84 = sqrt( POW2(bact->field_651.m20) + POW2(bact->field_651.m22) );
//    v84 /= sqrt( POW2(bact->field_651.m20) + POW2(bact->field_651.m21) + POW2(bact->field_651.m22) );
//
//    float v75 = v84;
//
//    if ( v84 > 1.0 )
//      v75 = 1.0;

        float v111 = clp_acos( _rotation.AxisX().XZ().length() / _rotation.AxisX().length() );

        if ( _rotation.m01 < 0.0 )
            v111 = -v111;

        if ( fabs(v111) < 0.01 )
            v111 = 0.0;

        float v36 = fabs(v111);

        float v101 = _heading_speed * v36 + _heading_speed * 0.25;

        float v102 = _maxrot * v106   *  v101;

        if ( v102 > v36 )
            v102 = v36;

        if ( v111 > 0.0 )
            v102 = -v102;

        float v104 = -v103 + v102;

        if ( fabs(v104 + v111) > 1.0 )
        {
            if ( v104 >= 0.0 )
                v104 = 1.0 - fabs(v111);
            else
                v104 = fabs(v111) - 1.0;
        }


        if ( fabs(v104) > _maxrot * 2.0 * v106 * v101 )
        {
            if ( v104 < 0.0 )
                v104 = _maxrot * -2.0 * v106 * v101;

            if ( v104 >= 0.0 )
                v104 = _maxrot * 2.0 * v106 * v101;
        }

        if ( fabs(v104) < 0.001 )
            v104 = 0.0;

        _rotation = mat3x3::RotateX(v110 * 0.5) * _rotation; // local
        _rotation = mat3x3::RotateZ(v104 * 0.5) * _rotation; // local
        _rotation *= mat3x3::RotateY(v103 * 0.5); // global

        _thraction += _force * v106 * 0.5 * arg->inpt->Sliders[2];

        if ( _thraction < 0.0 )
            _thraction = 0;

        if ( _thraction > _force )
            _thraction = _force;

        float v99 = _thraction;

        float v47 = _pSector->height - _position.y;
        float v94 = _height_max_user * 0.8;

        if ( v47 > v94 )
        {
            float v91 = _mass * 9.80665 - _force;
            float v89 = _height_max_user * 0.2;
            float v86 = (v47 - v94) * v91 / v89;

            if ( _thraction > v86 )
                _thraction = v86;

            if ( _thraction < 0.0 )
                _thraction = 0;
        }

        bact_arg79 v61;

        v61.tgType = BACT_TGT_TYPE_DRCT;
        v61.tgt_pos = _rotation.AxisZ();

        bact_arg106 v64;
        v64.field_0 = 5;
        v64.field_4 = _rotation.AxisZ();

        if ( UserTargeting(&v64) )
        {
            v61.target.pbact = v64.ret_bact;
            v61.tgType = BACT_TGT_TYPE_UNIT;
        }

        if ( arg->inpt->Buttons.IsAny({0, 1}) )
        {
            v61.direction = vec3d(0.0, 0.0, 0.0);
            v61.weapon = _weapon;
            v61.g_time = _clock;

            if ( v61.g_time % 2 )
                v61.start_point.x = _fire_pos.x;
            else
                v61.start_point.x = -_fire_pos.x;

            v61.start_point.y = _fire_pos.y;
            v61.start_point.z = _fire_pos.z;
            v61.flags = (arg->inpt->Buttons.Is(1) ? 1 : 0) | 2;

            LaunchMissile(&v61);
        }

        if ( _mgun != -1 )
        {
            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                if ( !(arg->inpt->Buttons.Is(2)) )
                {
                    arg78.setFlags = 0;
                    arg78.newStatus = BACT_STATUS_NOPE;
                    arg78.unsetFlags = BACT_STFLAG_FIRE;

                    SetState(&arg78);
                }
            }

            if ( arg->inpt->Buttons.Is(2) )
            {
                if ( !(_status_flg & BACT_STFLAG_FIRE) )
                {
                    arg78.unsetFlags = 0;
                    arg78.newStatus = BACT_STATUS_NOPE;
                    arg78.setFlags = BACT_STFLAG_FIRE;

                    SetState(&arg78);
                }

                bact_arg105 arg105;

                arg105.field_0 = _rotation.AxisZ();
                arg105.field_C = v106;
                arg105.field_10 = _clock;

                FireMinigun(&arg105);
            }
        }

        if ( arg->inpt->Buttons.Is(3) )
        {
            HandBrake(arg);

            v99 = _thraction;
        }

        if ( _status_flg & BACT_STFLAG_LAND )
        {
            move_msg arg74;
            arg74.flag = 0;
            arg74.field_0 = v106;

            Move(&arg74);
        }
        else
        {
            vec3d v81(0.0, 0.0, 0.0);

            yw_137col v43[10];

            for (int i = 3; i >= 0; i--)
            {
                move_msg arg74;
                arg74.flag = 0;
                arg74.field_0 = v106;

                Move(&arg74);

                int v50 = 0;

                ypaworld_arg137 arg137;
                arg137.pos = _position;
                arg137.pos2 = _fly_dir;
                arg137.radius = 32.0;
                arg137.collisions = v43;
                arg137.field_30 = 0;
                arg137.coll_max = 10;

                _world->ypaworld_func137(&arg137);

                if ( arg137.coll_count )
                {
                    v81 = vec3d(0.0, 0.0, 0.0);

                    for (int j = arg137.coll_count - 1; j >= 0; j--)
                    {
                        yw_137col *v31 = &arg137.collisions[ j ];

                        v81 += v31->pos2;
                    }

                    bact_arg88 arg88;

                    float ln = v81.length();
                    if ( ln != 0.0 )
                        arg88.pos1 = v81 / ln;
                    else
                        arg88.pos1 = _fly_dir;



                    Recoil(&arg88);

                    v50 = 1;
                }

                if ( !v50 )
                {
                    ypaworld_arg136 arg136;
                    arg136.stPos = _old_pos;
                    arg136.vect = _position - _old_pos;
                    arg136.flags = 0;

                    _world->ypaworld_func136(&arg136);

                    if ( arg136.isect )
                    {
                        bact_arg88 arg88;
                        arg88.pos1 = arg136.skel->polygons[arg136.polyID].Normal();

                        Recoil(&arg88);

                        v50 = 1;
                    }
                }

                if ( !v50 )
                {
                    _status_flg &= ~BACT_STFLAG_LCRASH;
                    break;
                }

                if ( !_soundcarrier.Sounds[5].IsEnabled() )
                {
                    if ( !(_status_flg & BACT_STFLAG_LCRASH) )
                    {
                        _status_flg |= BACT_STFLAG_LCRASH;

                        SFXEngine::SFXe.startSound(&_soundcarrier, 5);

                        yw_arg180 arg180;
                        arg180.effects_type = 5;
                        arg180.field_4 = 1.0;
                        arg180.field_8 = v81.x * 10.0 + _position.x;
                        arg180.field_C = v81.z * 10.0 + _position.z;

                        _world->ypaworld_func180(&arg180);
                    }
                }
            }
        }

        _thraction = v99;
    }
    else if ( _status == BACT_STATUS_DEAD )
    {
        DeadTimeUpdate(arg);
    }
}

void NC_STACK_ypabact::AddSubject(NC_STACK_ypabact *kid)
{
    newMaster_msg arg73;

    arg73.bact = this;
    arg73.list = &_kidList;

    kid->SetNewMaster(&arg73);
}

void NC_STACK_ypabact::SetNewMaster(newMaster_msg *arg)
{
    _kidRef.Detach();

    _kidRef = arg->list->push_front(this);
    
    _parent = arg->bact;
}

void NC_STACK_ypabact::Move(move_msg *arg)
{
    _old_pos = _position;

    float weight;

    if ( _status == BACT_STATUS_DEAD )
        weight = _mass * 39.2266;
    else
        weight = _mass * 9.80665;

    float thraction = 0.0;
    vec3d v54(0.0, 0.0, 0.0);

    if ( !(arg->flag & 1) )
    {
        v54 = -_rotation.AxisY();

        thraction = _thraction;

        if ( _oflags & BACT_OFLAG_USERINPT )
        {
            v54.x = fSign(v54.x) * sqrt( fabs(v54.x) );
            v54.y = fSign(v54.y) * v54.y * v54.y;
            v54.z = fSign(v54.z) * sqrt( fabs(v54.z) );
        }
    }

    vec3d v41 = vec3d::OY(weight) + v54 * thraction - _fly_dir * (_fly_dir_length * _airconst);

    float len = v41.length();

    if ( _oflags & BACT_OFLAG_USERINPT )
    {
        if ( v41.y >= 0.0 )
            v41.y *= 3.0;
        else
            v41.y *= 5.0;
    }

    if ( len > 0.0 )
    {
        //vec3d v42 = bact->fly_dir * bact->fly_dir_length + (v41 / len) * (len / bact->mass * arg->field_0);
        vec3d v42 = _fly_dir * _fly_dir_length + v41 * (arg->field_0 / _mass);

        _fly_dir_length = v42.length();

        if ( _fly_dir_length > 0.0 )
            _fly_dir = v42 / _fly_dir_length;
    }

    if ( fabs(_fly_dir_length) > 0.1 )
        _position += _fly_dir * (_fly_dir_length * arg->field_0 * 6.0);

    CorrectPositionInLevelBox(NULL);

    _soundcarrier.Sounds[0].Pitch = _pitch;
    _soundcarrier.Sounds[0].Volume = _volume;

    float v50;
    if ( _pitch_max <= -0.8 )
        v50 = 1.2;
    else
        v50 = _pitch_max;

    float v30 = fabs(_fly_dir_length) * v50;
    float v31 = _force * _force - _mass * 100.0 * _mass;

    float v43 = 0.0;
    if ( v31 > 0.0 && isnormal(v31) && _airconst_static != 0.0 )
    {
        float denom = sqrt(v31) / _airconst_static;
        if ( denom > 0.0 && isnormal(denom) )
            v43 = v30 / denom;
    }

    // Heli vehicles use the base BACT movement/audio path. If a debuff lowers
    // force far enough, the old sqrt(_force^2 - mass*100*mass) expression can
    // become NaN and poison Pitch, which makes the heli loop go silent. Treat
    // invalid speed-pitch contribution as no extra movement pitch.
    if ( !isnormal(v43) || v43 < 0.0 )
        v43 = 0.0;

    if ( v43 > v50 )
        v43 = v50;

    if ( _soundcarrier.Sounds[0].PSample )
        _soundcarrier.Sounds[0].Pitch += (_soundcarrier.Sounds[0].PSample->SampleRate + _soundcarrier.Sounds[0].Pitch) * v43;
}

void NC_STACK_ypabact::FightWithBact(bact_arg75 *arg)
{
    constexpr float CurSectrLen = 1.1 * World::CVSectorLength;
    
    arg->pos = arg->target.pbact->_position;

    vec3d v40 = arg->target.pbact->_position - _position;
    float v45 = v40.normalise();

    bact_arg110 arg110;

    vec3d *foePos;
    bool isSecTarget = false;
    bool isPrimTarget = 0;

    if ( _secndT.pbact == arg->target.pbact )
    {
        foePos = &_secndT.pbact->_position;
        arg110.priority = 1;
        isSecTarget = true;
    }
    else
    {
        foePos = &_primT.pbact->_position;
        arg110.priority = 0;
        isPrimTarget = true;
    }

    NC_STACK_ypabact *a4 = _world->getYW_userHostStation();

    if ( _clock - _assess_time > 500 || _clock < 500 )
    {
        _assess_time = _clock;

        arg110.tgType = BACT_TGT_TYPE_UNIT;
        _atk_ret = TargetAssess(&arg110);
    }

    if ( _atk_ret == TA_FIGHT )
    {
        float foeDistance = ( foePos->XZ() - _position.XZ() ).length();
        bool seekAndExplodeArmed = ypabact_IsSeekAndExplodeArmed(this);

        if ( seekAndExplodeArmed )
        {
            _status_flg &= ~BACT_STFLAG_APPROACH;
            _status_flg |= BACT_STFLAG_ATTACK;
            _target_vec = *foePos - _position;
        }
        else if ( _status_flg & BACT_STFLAG_APPROACH )
        {
            _status_flg &= ~BACT_STFLAG_ATTACK;

            if ( (_position.x < CurSectrLen || _position.z > -CurSectrLen || _position.x > _wrldSize.x - CurSectrLen || _position.z < _wrldSize.y + CurSectrLen) || _adist_bact < foeDistance )
            {
                _status_flg &= ~BACT_STFLAG_APPROACH;
            }
            else
            {
                _AI_time2 = _clock;
                _AI_time1 = _clock;
            }
        }
        else
        {
            if ( _sdist_bact <= foeDistance )
            {
                if ( _adist_sector <= foeDistance )
                    _status_flg &= ~BACT_STFLAG_ATTACK;
                else
                    _status_flg |= BACT_STFLAG_ATTACK;
            }
            else
            {
                _status_flg &= ~BACT_STFLAG_ATTACK;

                /*if ( bact->field_3D1 == 2 || (arg->g_time & 1 && bact->field_3D1 == 3) )
                {
                    bact->target_vec.x = bact->fly_dir.x;
                    bact->target_vec.z = bact->fly_dir.z;
                }
                else*/
                {
                    _target_vec.x = -_fly_dir.x;
                    _target_vec.z = -_fly_dir.z;
                }

                _AI_time2 = _clock;
                _AI_time1 = _clock;
                _status_flg |= BACT_STFLAG_APPROACH;
            }
        }
    }
    else
    {
        _status_flg &= ~(BACT_STFLAG_APPROACH | BACT_STFLAG_ATTACK);
    }

    switch( _atk_ret )
    {
        case TA_CANCEL:
        {
            if ( isPrimTarget )
            {
                _status_flg &= ~BACT_STFLAG_FIGHT_P;

                setTarget_msg arg67;
                arg67.priority = 0;
                arg67.tgt_type = BACT_TGT_TYPE_CELL;
                arg67.tgt_pos = _primTpos;

                SetTarget(&arg67);
            }

            if ( isSecTarget )
            {
                _status_flg &= ~BACT_STFLAG_FIGHT_S;

                setTarget_msg arg67;
                arg67.priority = 1;
                arg67.tgt_type = BACT_TGT_TYPE_NONE;

                SetTarget(&arg67);
            }

            _status_flg &= ~BACT_STFLAG_APPROACH;

            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                setState_msg arg78;
                arg78.setFlags = 0;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.unsetFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }
        }
        break;
        
        case TA_MOVE:
        {
            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                setState_msg arg78;
                arg78.setFlags = 0;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.unsetFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }
        }
        break;
        
        case TA_FIGHT:
        {
            bact_arg101 arg101;
            arg101.pos = arg->target.pbact->_position;
            arg101.unkn = 2;
            arg101.radius = arg->target.pbact->_radius;

            if ( CheckFireAI(&arg101) )
            {
                if ( isSecTarget )
                    _status_flg |= BACT_STFLAG_FIGHT_S;
                else
                    _status_flg |= BACT_STFLAG_FIGHT_P;

                vec3d rotZ = _rotation.AxisZ();

                bact_arg79 arg79;

                arg79.direction.x = rotZ.x;

                if ( _bact_type == BACT_TYPES_TANK )
                    arg79.direction.y = v40.y;
                else
                    arg79.direction.y = rotZ.y - _gun_angle;

                arg79.direction.z = rotZ.z;
                arg79.tgType = BACT_TGT_TYPE_UNIT;
                arg79.target.pbact = arg->target.pbact;
                arg79.tgt_pos = arg->pos;
                arg79.weapon = _weapon;
                arg79.g_time = _clock;

                if ( arg->g_time & 1 )
                    arg79.start_point.x = _fire_pos.x;
                else
                    arg79.start_point.x = -_fire_pos.x;

                arg79.start_point.y = _fire_pos.y;
                arg79.start_point.z = _fire_pos.z;
                arg79.flags = 0;

                LaunchMissile(&arg79);
            }
            else
            {
                if ( ypabact_IsSeekAndExplodeArmed(this) )
                    _status_flg |= BACT_STFLAG_ATTACK;
                else
                    _status_flg &= ~BACT_STFLAG_ATTACK;
            }

            if ( v45 < 1000.0 &&   _mgun != -1 &&   v40.dot(_rotation.AxisZ()) > 0.85 )
            {
                if ( isSecTarget )
                    _status_flg |= BACT_STFLAG_FIGHT_S;
                else
                    _status_flg |= BACT_STFLAG_FIGHT_P;

                if ( !(_status_flg & BACT_STFLAG_FIRE) )
                {
                    setState_msg arg78;
                    arg78.unsetFlags = 0;
                    arg78.newStatus = BACT_STATUS_NOPE;
                    arg78.setFlags = BACT_STFLAG_FIRE;

                    SetState(&arg78);
                }

                bact_arg105 arg105;

                arg105.field_C = arg->fperiod;
                arg105.field_10 = _clock;
                arg105.field_0 = _rotation.AxisZ();

                FireMinigun(&arg105);
            }
            else if ( _status_flg & BACT_STFLAG_FIRE )
            {
                setState_msg arg78;
                arg78.setFlags = 0;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.unsetFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }
        }
        break;
        
        case TA_IGNORE:
        {
            _status_flg &= ~BACT_STFLAG_APPROACH;

            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                setState_msg arg78;
                arg78.setFlags = 0;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.unsetFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }

            if ( _secndT.pbact == arg->target.pbact )
            {
                _status_flg &= ~BACT_STFLAG_FIGHT_S;

                setTarget_msg arg67;
                arg67.tgt_type = BACT_TGT_TYPE_NONE;
                arg67.priority = 1;

                SetTarget(&arg67);

                isSecTarget = 0;
            }
            else
            {
                _status_flg &= ~BACT_STFLAG_FIGHT_P;

                if ( (IsParentMyRobo() && _host_station == a4) && _status != BACT_STATUS_IDLE && !(_status_flg & BACT_STFLAG_ESCAPE) )
                {
                    robo_arg134 arg134;
                    arg134.unit = this;
                    arg134.field_4 = 1;
                    arg134.field_10 = 0;
                    arg134.field_C = 0;
                    arg134.field_8 = 0;
                    arg134.field_14 = 32;

                    _host_station->placeMessage(&arg134);
                }

                setState_msg arg78;
                arg78.unsetFlags = 0;
                arg78.setFlags = 0;
                arg78.newStatus = BACT_STATUS_NORMAL;

                SetState(&arg78);

                _status = BACT_STATUS_IDLE;
            }
        }
        break;
        
        default:
        break;
    }
}

void NC_STACK_ypabact::FightWithSect(bact_arg75 *arg)
{
    constexpr float CurSectrLen = 1.1 * World::CVSectorLength;
    
    int v64 = 0;
    int v68 = 0;

    vec3d *cellPos;

    bact_arg110 arg110;

    if ( _secndT.pcell == arg->target.pcell )
    {
        cellPos = &_sencdTpos;
        v64 = 1;

        arg110.priority = 1;
    }
    else
    {
        cellPos = &_primTpos;
        v68 = 1;

        arg110.priority = 0;
    }

    NC_STACK_ypabact *a4 = _world->getYW_userHostStation();

    int v65 = IsParentMyRobo() && _host_station == a4;

    float v62 = (_position.XZ() - cellPos->XZ()).length();

    if ( _clock - _assess_time > 500 || _clock < 500 )
    {
        _assess_time = _clock;

        arg110.tgType = BACT_TGT_TYPE_CELL;
        _atk_ret = TargetAssess(&arg110);
    }

    if ( _atk_ret == TA_FIGHT )
    {
        float cellDistance = (cellPos->XZ() - _position.XZ()).length();

        if ( _status_flg & BACT_STFLAG_APPROACH )
        {
            _status_flg &= ~BACT_STFLAG_ATTACK;

            if ( (_position.x < CurSectrLen || _position.z > -CurSectrLen || _position.x > _wrldSize.x - CurSectrLen || _position.z < _wrldSize.y + CurSectrLen) || _adist_sector < cellDistance )
            {
                _status_flg &= ~BACT_STFLAG_APPROACH;
            }
            else
            {
                _AI_time2 = _clock;
                _AI_time1 = _clock;
            }
        }
        else if ( _sdist_sector <= cellDistance )
        {
            if ( _adist_sector <= cellDistance )
                _status_flg &= ~BACT_STFLAG_ATTACK;
            else
                _status_flg |= BACT_STFLAG_ATTACK;
        }
        else
        {
            _status_flg &= ~BACT_STFLAG_ATTACK;

            /*if ( bact->field_3D1 == 2 || (arg->g_time & 1 && bact->field_3D1 == 3) )
            {
                bact->target_vec.x = bact->fly_dir.x;
                bact->target_vec.z = bact->fly_dir.z;
                bact->target_vec.y = bact->fly_dir.y;
            }
            else*/
            {
                _target_vec = -_fly_dir;
            }

            _AI_time2 = _clock;
            _AI_time1 = _clock;

            _status_flg |= BACT_STFLAG_APPROACH;
        }
    }
    else
    {
        _status_flg &= ~(BACT_STFLAG_APPROACH | BACT_STFLAG_ATTACK);
    }

    if ( _status_flg & BACT_STFLAG_FIRE )
    {
        setState_msg arg78;
        arg78.unsetFlags = BACT_STFLAG_FIRE;
        arg78.setFlags = 0;
        arg78.newStatus = BACT_STATUS_NOPE;

        SetState(&arg78);
    }
    
    switch(_atk_ret)
    {
        case TA_CANCEL:
        {
            _status_flg &= ~BACT_STFLAG_APPROACH;

            if ( v68 )
            {
                if ( v65 )
                {
                    robo_arg134 arg134;
                    
                    Common::Point tmp = World::PositionToSectorID(_primTpos);

                    arg134.unit = this;
                    arg134.field_4 = 4;
                    arg134.field_8 = tmp.x;
                    arg134.field_C = tmp.y;
                    arg134.field_14 = 18;
                    arg134.field_10 = 0;

                    _host_station->placeMessage(&arg134);
                }

                _status_flg &= ~BACT_STFLAG_FIGHT_P;

                setTarget_msg arg67;
                arg67.tgt_type = BACT_TGT_TYPE_CELL;
                arg67.tgt_pos.x = _position.x;
                arg67.tgt_pos.z = _position.z;
                arg67.priority = 0;

                SetTarget(&arg67);
            }

            if ( v64 )
            {
                if ( v65 )
                {
                    robo_arg134 arg134;
                    
                    Common::Point tmp = World::PositionToSectorID(_sencdTpos);

                    arg134.unit = this;
                    arg134.field_4 = 4;
                    arg134.field_8 = tmp.x;
                    arg134.field_10 = 0;
                    arg134.field_14 = 18;
                    arg134.field_C = tmp.y;

                    _host_station->placeMessage(&arg134);
                }

                _status_flg &= ~BACT_STFLAG_FIGHT_S;

                setTarget_msg arg67;
                arg67.tgt_type = BACT_TGT_TYPE_NONE;
                arg67.priority = 1;
                SetTarget(&arg67);
            }
        }
        break;
        
        case TA_MOVE:
        {
        }
        break;
        
        case TA_FIGHT:
        {
            if ( v68 )
            {
                if ( v62 < World::CVSectorLength )
                {
                    if ( !(_status_flg & BACT_STFLAG_FIGHT_P) && v65 && _secndT.pcell != _primT.pcell )
                    {
                        robo_arg134 arg134;
                        
                        Common::Point tmp = World::PositionToSectorID(_primTpos);

                        arg134.field_4 = 3;
                        arg134.field_8 = tmp.x;
                        arg134.field_C = tmp.y;
                        arg134.unit = this;
                        arg134.field_10 = 0;
                        arg134.field_14 = 20;

                        _host_station->placeMessage(&arg134);
                    }

                    _status_flg |= BACT_STFLAG_FIGHT_P;
                }

                GetBestSectorPart(&_primTpos);

                arg->pos = _primTpos;
            }

            if ( v64 )
            {
                if ( v62 < World::CVSectorLength )
                {
                    if ( v65 && !(_status_flg & BACT_STFLAG_FIGHT_S) )
                    {
                        robo_arg134 arg134;
                        
                        Common::Point tmp = World::PositionToSectorID(_sencdTpos);

                        arg134.field_4 = 3;
                        arg134.field_8 = tmp.x;
                        arg134.field_C = tmp.y;
                        arg134.unit = this;
                        arg134.field_10 = 0;
                        arg134.field_14 = 20;

                        _host_station->placeMessage(&arg134);
                    }

                    _status_flg |= BACT_STFLAG_FIGHT_S;
                }

                GetBestSectorPart(&_sencdTpos);

                arg->pos = _sencdTpos;
            }

            bact_arg101 arg101;
            arg101.unkn = 1;
            arg101.pos = arg->pos;

            if ( CheckFireAI(&arg101) )
            {
                vec3d tmp = _position + _fire_pos - arg->pos;

                float v60 = tmp.length();

                if ( v60 < 0.01 )
                    v60 = 0.01;

                if ( v64 )
                    _status_flg |= BACT_STFLAG_FIGHT_S;
                else
                    _status_flg |= BACT_STFLAG_FIGHT_P;

                bact_arg79 arg79;

                arg79.direction = -(_position + _fire_pos - arg->pos) / v60;
                arg79.tgType = BACT_TGT_TYPE_CELL;
                arg79.target.pbact = arg->target.pbact;
                arg79.tgt_pos = arg->pos;
                arg79.weapon = _weapon;
                arg79.g_time = _clock;

                if ( arg->g_time & 1 )
                    arg79.start_point.x = _fire_pos.x;
                else
                    arg79.start_point.x = -_fire_pos.x;

                arg79.start_point.y = _fire_pos.y;
                arg79.start_point.z = _fire_pos.z;
                arg79.flags = 0;

                LaunchMissile(&arg79);
            }
            else
            {
                _status_flg &= ~BACT_STFLAG_ATTACK;
            }
        }
        break;
        
        case TA_IGNORE:
        {
            _status_flg &= ~BACT_STFLAG_APPROACH;

            if ( v64 )
            {
                _status_flg &= ~BACT_STFLAG_FIGHT_S;

                if ( v65 && _secndT.pcell != _primT.pcell )
                {
                    robo_arg134 arg134;
                    
                    Common::Point tmp = World::PositionToSectorID(_sencdTpos);

                    arg134.field_4 = 2;
                    arg134.field_8 = tmp.x;
                    arg134.field_C = tmp.y;
                    arg134.field_10 = 0;
                    arg134.field_14 = 22;
                    arg134.unit = this;

                    _host_station->placeMessage(&arg134);
                }

                setTarget_msg arg67;
                arg67.priority = 1;
                arg67.tgt_type = BACT_TGT_TYPE_NONE;

                SetTarget(&arg67);

                v64 = 0;
            }
            else
            {
                _status_flg &= ~BACT_STFLAG_FIGHT_P;

                if ( v65 && _status != BACT_STATUS_IDLE )
                {
                    robo_arg134 arg134;

                    arg134.field_10 = 0;
                    arg134.field_C = 0;
                    arg134.field_8 = 0;
                    arg134.unit = this;
                    arg134.field_4 = 1;
                    arg134.field_14 = 32;

                    _host_station->placeMessage(&arg134);
                }

                _status = BACT_STATUS_IDLE;
            }
        }
        break;
        
        default:
            break;
    }
}

void NC_STACK_ypabact::CopyWaypointsStuff( NC_STACK_ypabact *bact)
{
    if ( bact->_status_flg & BACT_STFLAG_WAYPOINT )
    {
        for (int i = 0; i < 32; i++)
            _waypoints[i] = bact->_waypoints[i];

        _status_flg |= BACT_STFLAG_WAYPOINT;

        if ( bact->_status_flg & BACT_STFLAG_WAYPOINTCCL )
            _status_flg |= BACT_STFLAG_WAYPOINTCCL;
        else
            _status_flg &= ~BACT_STFLAG_WAYPOINTCCL;

        _waypoints_count = bact->_waypoints_count;
        _current_waypoint = bact->_current_waypoint;
    }
}

void NC_STACK_ypabact::Die()
{
    if ( _status_flg & BACT_STFLAG_DEATH1 )
        return;

    if ( _isUnitGunChild )
        ypabact_SafeDetachControlFrom(this, _parent);

    ClearActiveDebuff();
    _carrier_spawned_gids.clear();
    CleanupUnitGuns(true, true);
    CleanupUnitDummies(true, true);

    int maxy = _world->getYW_mapSizeY();
    int maxx = _world->getYW_mapSizeX();

    uamessage_dead deadMsg;
    deadMsg.msgID = UAMSG_DEAD;
    deadMsg.owner = _owner;
    deadMsg.id = _gid;
    deadMsg.newParent = 0;
    deadMsg.landed = 0;
    deadMsg.classID = _bact_type;

    if ( _killer )
        deadMsg.killer = _killer->_gid;
    else
        deadMsg.killer = 0;

    deadMsg.killerOwner = _killer_owner;

    NC_STACK_ypabact *deputy = NULL;

    if (!_kidList.empty())
    {

        for (World::RefBactList::iterator it = _kidList.begin(); it != _kidList.end(); )
        {
            // Forward dereference iterator and next
            NC_STACK_ypabact *kid = *it;
            it++;

            if ( kid->_status == BACT_STATUS_DEAD )
            {
                if ( _parent )
                    _parent->AddSubject(kid);
                else
                    _world->ypaworld_func134(kid);                  

                kid->_status_flg |= BACT_STFLAG_NOMSG;
            }
            else
            {
                float kidLen = (kid->_position.XZ() - _position.XZ()).square();

                float deputyLen;

                if ( deputy )
                    deputyLen = (deputy->_position.XZ() - _position.XZ()).square();
                else
                    deputyLen = (POW2(maxx) + POW2(maxy)) * World::CVSectorLength * World::CVSectorLength;

                if ( kid->_bact_type == BACT_TYPES_UFO )
                    kidLen = (POW2(maxx) + POW2(maxy)) * World::CVSectorLength * World::CVSectorLength - 1000.0;

                if ( kidLen <= deputyLen )
                    deputy = kid;
            }
        }

        if ( deputy )
        {
            if ( _parent )
                _parent->AddSubject(deputy);
            else
                _world->ypaworld_func134(deputy);

            while ( !_kidList.empty() )
                deputy->AddSubject(_kidList.front());

            setTarget_msg arg67;
            arg67.tgt_pos = _primTpos;
            arg67.tgt.pbact = _primT.pbact;
            arg67.tgt_type = _primTtype;
            arg67.priority = 0;

            deputy->SetTarget(&arg67);

            deputy->CopyWaypointsStuff(this);

            deputy->_commandID = _commandID;
            deputy->_aggr = _aggr;

            if ( _world->_isNetGame )
            {
                if (_owner)
                    deadMsg.newParent = deputy->_gid;
            }
        }
        else
        {
            for(World::RefBactList::iterator kidXit = _kidList.begin(); kidXit != _kidList.end(); )
            {
                NC_STACK_ypabact *kidX = *kidXit;
                kidXit++;
                
                for ( World::RefBactList::iterator kidYit = kidX->_kidList.begin(); kidYit != kidX->_kidList.end(); )
                {
                    NC_STACK_ypabact *kidY = *kidYit;
                    kidYit++;

                    _world->ypaworld_func134(kidY);

                    if ( kidY->_status != BACT_STATUS_DEAD )
                        ypa_log_out("Scheisse, da hфngt noch ein Lebendiger unter der Leiche! owner %d, state %d, class %d\n", kidY->_owner, kidY->_status, _bact_type);
                }
                _world->ypaworld_func134(kidX);
            }
        }
    }

    NC_STACK_ypabact *v76 = _world->getYW_userHostStation();

    if ( !deputy && IsParentMyRobo()&& !(_status_flg & BACT_STFLAG_NOMSG) )
    {
        robo_arg134 v53;

        if ( v76 == _host_station )
        {
            if ( _bact_type == BACT_TYPES_GUN )
            {
                if ( _weapon != -1 || -1 != _mgun )
                {
                    v53.field_14 = 80;
                    v53.field_4 = 31;
                }
                else
                {
                    v53.field_14 = 80;
                    v53.field_4 = 32;
                }

                v53.field_10 = 0;
                v53.field_C = 0;
                v53.field_8 = 0;
                v53.unit = this;

                _host_station->placeMessage(&v53);
            }
            else
            {
                if ( !(_status_flg & BACT_STFLAG_CLEAN) )
                {
                    v53.field_8 = _commandID;
                    v53.unit = this;
                    v53.field_10 = 0;
                    v53.field_14 = 44;
                    v53.field_C = 0;
                    v53.field_4 = 8;

                    _host_station->placeMessage(&v53);
                }
            }
        }
        else
        {
            if ( _killer && v76 == _killer->_host_station )
            {
                v53.field_4 = 5;
                v53.unit = _killer;
                v53.field_8 = _primT_cmdID;
                v53.field_10 = 0;
                v53.field_C = 0;
                v53.field_14 = 36;

                _host_station->placeMessage(&v53);
            }
        }

    }

    CleanAttackersTarget();

    if ( _parent )
    {
        for (World::MissileList::iterator it = _missiles_list.begin(); it != _missiles_list.end(); it = _missiles_list.erase(it))
        {
            NC_STACK_ypamissile *miss = *it;

            _parent->_missiles_list.push_back(miss);
            miss->SetLauncherBact( _parent );
        }
    }
    else
    {
        for (World::MissileList::iterator it = _missiles_list.begin(); it != _missiles_list.end(); it = _missiles_list.erase(it))
        {
            NC_STACK_ypamissile *miss = *it;

            miss->ResetViewing();

            setState_msg arg119;
            arg119.newStatus = BACT_STATUS_DEAD;
            arg119.unsetFlags = 0;
            arg119.setFlags = 0;
            miss->SetStateInternal(&arg119);

            setTarget_msg arg67;
            arg67.tgt_type = BACT_TGT_TYPE_NONE;
            arg67.priority = 0;
            miss->SetTarget(&arg67);

            miss->_parent = NULL;

            _world->ypaworld_func144(miss);
        }
    }
    

    if ( _secndTtype == BACT_TGT_TYPE_UNIT )
        _secndT.pbact->DeleteAttacker(this, 1);

    if ( _primTtype == BACT_TGT_TYPE_UNIT )
        _primT.pbact->DeleteAttacker(this, 0);


    _secndTtype = BACT_TGT_TYPE_NONE;
    _primTtype = BACT_TGT_TYPE_NONE;

    ypabact_FireProximityDefenseAtDeath(this);
    ypabact_TrySpawnAtDeath(this);

    _status = BACT_STATUS_DEAD;
    _commandID = 0;
    _status_flg |= BACT_STFLAG_DEATH1;
    _dead_time = _clock;

    if ( _status_flg & BACT_STFLAG_LAND )
    {
        if ( _vp_active == 1 || _vp_active == 6 )
        {
            setState_msg arg119;
            arg119.unsetFlags = 0;
            arg119.newStatus = BACT_STATUS_NOPE;
            arg119.setFlags = BACT_STFLAG_DEATH2;

            SetStateInternal(&arg119);

            if ( _world->_isNetGame )
            {
                if (_owner)
                    deadMsg.landed = 1;
            }
        }
    }

    if ( _oflags & BACT_OFLAG_USERINPT )
    {
        if ( !(_oflags & BACT_OFLAG_VIEWER) )
        {
            if ( _parent )
                setBACT_inputting(false);
        }
    }

    if ( _world->_isNetGame )
    {
        if ( _owner )
        {
            if ( _bact_type != BACT_TYPES_ROBO )
                _world->NetBroadcastMessage(&deadMsg, sizeof(deadMsg), true);
        }
    }

    if ( _owner )
    {
        if ( !(_status_flg & BACT_STFLAG_CLEAN) )
            _world->HistoryAktKill(this);
    }
}

void NC_STACK_ypabact::SetState(setState_msg *arg)
{
    if ( _invulnerable && arg->newStatus == BACT_STATUS_DEAD )
    {
        if ( _energy <= 0 )
            _energy = _energy_max > 0 ? _energy_max : 1;

        return;
    }

    if ( (_bact_type == BACT_TYPES_TANK || _bact_type == BACT_TYPES_CAR) && arg->newStatus == BACT_STATUS_DEAD )
    {
        setState_msg newarg;
        newarg.unsetFlags = 0;
        newarg.newStatus = BACT_STATUS_NOPE;
        newarg.setFlags = BACT_STFLAG_DEATH2;

        SetState(&newarg);
    }
    else
    {
        int v6 = SetStateInternal(arg);

        if ( _world->_isNetGame )
        {
            if ( v6 && _owner && _bact_type != BACT_TYPES_MISSLE )
            {
                uamessage_setState ssMsg;
                ssMsg.msgID = UAMSG_SETSTATE;
                ssMsg.owner = _owner;
                ssMsg.id = _gid;
                ssMsg.newStatus = arg->newStatus;
                ssMsg.setFlags = arg->setFlags;
                ssMsg.unsetFlags = arg->unsetFlags;
                ssMsg.classID = _bact_type;

                _world->NetBroadcastMessage(&ssMsg, sizeof(ssMsg), true);
            }
        }
    }
}

static vec3d ypabact_ApplyDirectionalSpread(const mat3x3 &rotation, const vec3d &direction, float spreadX, float spreadY);

static bool ypabact_IsValidWeaponId(NC_STACK_ypabact *bact, int weaponId)
{
    NC_STACK_ypaworld *world = bact->getBACT_pWorld();
    return world && weaponId >= 0 && weaponId < (int)world->GetWeaponsProtos().size();
}

static bool ypabact_IsValidFireWeaponId(NC_STACK_ypabact *bact, int weaponId)
{
    if ( !ypabact_IsValidWeaponId(bact, weaponId) )
        return false;

    return (bact->getBACT_pWorld()->GetWeaponsProtos().at(weaponId)._weaponFlags & 1) != 0;
}

static bool ypabact_IsLowHPWeaponActive(NC_STACK_ypabact *bact)
{
    if ( !bact ||
         !bact->_lowhp_weapon_enable ||
         bact->_energy_max <= 0 ||
         bact->_energy > bact->_energy_max * bact->_lowhp_threshold )
        return false;

    return ypabact_IsValidFireWeaponId(bact, bact->_lowhp_weapon);
}

static bool ypabact_IsSeekAndExplodeArmed(NC_STACK_ypabact *unit)
{
    if ( !unit ||
         !unit->_seek_and_explode ||
         unit->_seek_and_explode_triggered ||
         !unit->getBACT_pWorld() ||
         unit->_status == BACT_STATUS_DEAD ||
         unit->_status == BACT_STATUS_CREATE ||
         unit->_status == BACT_STATUS_BEAM ||
         (unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) )
    {
        return false;
    }

    if ( unit->getBACT_pWorld()->IsSpectatorBact(unit) )
        return false;

    if ( unit->_seek_and_explode_weapon <= 0 )
        return true;

    World::TWeapProto &wproto = unit->getBACT_pWorld()->GetWeaponsProtos().at(unit->_seek_and_explode_weapon);
    return (wproto._weaponFlags & 1) != 0;
}

static bool ypabact_IsValidSeekAndExplodeTarget(NC_STACK_ypabact *unit, NC_STACK_ypabact *target)
{
    if ( !unit ||
         !target ||
         unit == target ||
         target->_isDummy ||
         !unit->getBACT_pWorld() ||
         target->getBACT_pWorld() != unit->getBACT_pWorld() ||
         !ypabact_CanUseGameplayStatusMechanics(target) )
    {
        return false;
    }

    if ( unit->_owner == World::OWNER_0 ||
         target->_owner == World::OWNER_0 ||
         target->_owner == unit->_owner )
    {
        return false;
    }

    if ( unit->getBACT_pWorld()->IsSpectatorBact(target) )
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
    case BACT_TYPES_HOVER:
        break;

    default:
        return false;
    }

    if ( target->_bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(target);
        if ( !gun || (gun->IsRoboGun() && target->GetEffectiveShield() >= 100.0f) )
            return false;
    }

    return true;
}

static bool ypabact_IsSeekAndExplodeVerticalFlightUnit(NC_STACK_ypabact *unit)
{
    if ( !unit )
        return false;

    // Script parser maps model=heli to BACT_TYPES_BACT; plane/glider/zeppelin
    // use BACT_TYPES_FLYER, while model=ufo has its own runtime class.
    return unit->_bact_type == BACT_TYPES_BACT ||
           unit->_bact_type == BACT_TYPES_FLYER ||
           unit->_bact_type == BACT_TYPES_UFO;
}

static bool ypabact_ShouldSeekAndExplodeUseHorizontalAirRamming(NC_STACK_ypabact *unit, NC_STACK_ypabact *target)
{
    bool unitIsAir = ypabact_IsSeekAndExplodeVerticalFlightUnit(unit);
    bool targetIsAir = ypabact_IsSeekAndExplodeVerticalFlightUnit(target);

    return unitIsAir && !targetIsAir;
}

static bool ypabact_IsSeekAndExplodeContact(NC_STACK_ypabact *unit, NC_STACK_ypabact *target)
{
    if ( !ypabact_IsValidSeekAndExplodeTarget(unit, target) )
        return false;

    float detonationDistance = unit->_seek_and_explode_trigger_radius > 0.0 ? unit->_seek_and_explode_trigger_radius : unit->_radius + target->_radius;
    if ( detonationDistance <= 0.0 )
        return false;

    vec3d delta = target->_position - unit->_position;
    if ( ypabact_ShouldSeekAndExplodeUseHorizontalAirRamming(unit, target) )
        return delta.XZ().length() <= detonationDistance;

    return delta.length() <= detonationDistance;
}

static NC_STACK_ypabact *ypabact_FindSeekAndExplodeContactTarget(NC_STACK_ypabact *unit)
{
    if ( !ypabact_IsSeekAndExplodeArmed(unit) )
        return NULL;

    if ( unit->_secndTtype == BACT_TGT_TYPE_UNIT && ypabact_IsSeekAndExplodeContact(unit, unit->_secndT.pbact) )
        return unit->_secndT.pbact;

    if ( unit->_primTtype == BACT_TGT_TYPE_UNIT && ypabact_IsSeekAndExplodeContact(unit, unit->_primT.pbact) )
        return unit->_primT.pbact;

    float fuseRadius = unit->_seek_and_explode_trigger_radius > 0.0 ? unit->_seek_and_explode_trigger_radius : unit->_radius;
    if ( fuseRadius < unit->_radius )
        fuseRadius = unit->_radius;

    int sectorRadius = (int)ceil(fuseRadius / World::CVSectorLength) + 2;
    if ( sectorRadius < 2 )
        sectorRadius = 2;

    for (int y = -sectorRadius; y <= sectorRadius; y++)
    {
        for (int x = -sectorRadius; x <= sectorRadius; x++)
        {
            Common::Point cellId(unit->_cellId.x + x, unit->_cellId.y + y);

            if ( !unit->getBACT_pWorld()->IsGamePlaySector(cellId) )
                continue;

            cellArea &cell = unit->getBACT_pWorld()->SectorAt(cellId);
            for ( NC_STACK_ypabact *target : cell.unitsList )
            {
                if ( ypabact_IsSeekAndExplodeContact(unit, target) )
                    return target;
            }
        }
    }

    return NULL;
}

static NC_STACK_ypabact *ypabact_GetSeekAndExplodeRammingTarget(NC_STACK_ypabact *unit)
{
    if ( !ypabact_IsSeekAndExplodeArmed(unit) )
        return NULL;

    if ( unit->_secndTtype == BACT_TGT_TYPE_UNIT )
    {
        if ( ypabact_IsValidSeekAndExplodeTarget(unit, unit->_secndT.pbact) )
            return unit->_secndT.pbact;

        setTarget_msg arg67;
        arg67.tgt_type = BACT_TGT_TYPE_NONE;
        arg67.priority = 1;
        unit->SetTarget(&arg67);
    }

    if ( unit->_primTtype == BACT_TGT_TYPE_UNIT )
    {
        if ( ypabact_IsValidSeekAndExplodeTarget(unit, unit->_primT.pbact) )
            return unit->_primT.pbact;

        setTarget_msg arg67;
        arg67.tgt_type = BACT_TGT_TYPE_CELL;
        arg67.tgt_pos = unit->_position;
        arg67.priority = 0;
        unit->SetTarget(&arg67);
    }

    return NULL;
}

bool NC_STACK_ypabact::ApplySeekAndExplodeRammingGuidance(bool clearAvoidanceFlags)
{
    if ( _oflags & BACT_OFLAG_USERINPT )
        return false;

    NC_STACK_ypabact *target = ypabact_GetSeekAndExplodeRammingTarget(this);
    if ( !target )
        return false;

    bool horizontalAirRamming = ypabact_ShouldSeekAndExplodeUseHorizontalAirRamming(this, target);
    float safeVerticalDir = _target_dir.y;

    vec3d desired = target->_position - _position;
    if ( horizontalAirRamming )
        desired.y = 0.0;

    float desiredLen = desired.length();
    if ( desiredLen <= 0.001 )
        return false;

    _target_vec = desired;
    _target_dir = desired / desiredLen;

    if ( horizontalAirRamming )
    {
        // Keep the vertical correction calculated by the unit's own flight AI
        // so heli/flyer/ufo ramming does not cancel ground-avoidance lift.
        _target_dir.y = safeVerticalDir;
        if ( _target_dir.normalise() <= 0.001 )
            _target_dir = desired / desiredLen;
    }

    _status_flg |= BACT_STFLAG_MOVE | BACT_STFLAG_ATTACK;
    _status_flg &= ~BACT_STFLAG_APPROACH;

    if ( horizontalAirRamming )
        _status_flg &= ~BACT_STFLAG_LAND;

    if ( clearAvoidanceFlags )
        _status_flg &= ~(BACT_STFLAG_DODGE_LEFT | BACT_STFLAG_DODGE_RIGHT);

    return true;
}

static int ypabact_GetPrimaryWeaponSlots(NC_STACK_ypabact *bact, int *outSlots)
{
    int count = 0;

    if ( ypabact_IsValidWeaponId(bact, bact->_weapon) )
        outSlots[count++] = bact->_weapon;

    for (int weaponId : bact->_extra_weapons)
    {
        if ( weaponId > 0 && ypabact_IsValidWeaponId(bact, weaponId) )
            outSlots[count++] = weaponId;
    }

    return count;
}

struct TMissileMultiTargetCandidate
{
    NC_STACK_ypabact *target = NULL;
    float score = 0.0;
};

constexpr int YPA_WEAPON_FLAG_PROJECTILE = World::TWeapProto::WEAPON_FLAG_PROJECTILE;
constexpr int YPA_WEAPON_FLAGS_MISSILE = World::TWeapProto::WEAPON_FLAGS_MISSILE;
constexpr float YPA_MISSILE_MULTI_TARGET_RANGE = 2000.0;

static bool ypabact_IsMissileMultiTargetWeapon(const World::TWeapProto &wproto)
{
    return wproto.missile_multi_target > 0 && wproto._weaponFlags == YPA_WEAPON_FLAGS_MISSILE;
}

static bool ypabact_IsHomingBombWeapon(const World::TWeapProto &wproto)
{
    return wproto.IsHomingBomb();
}

static bool ypabact_IsBombMultiTargetWeapon(const World::TWeapProto &wproto)
{
    return ypabact_IsHomingBombWeapon(wproto) && wproto.homing_bomb_multi_target > 0;
}

static int ypabact_GetMissileMultiTargetLimit(const World::TWeapProto &wproto, int weaponCount)
{
    if ( !ypabact_IsMissileMultiTargetWeapon(wproto) || weaponCount <= 1 )
        return 0;

    int maxTargets = wproto.missile_multi_target;
    if ( maxTargets > weaponCount )
        maxTargets = weaponCount;

    return maxTargets;
}

static int ypabact_GetBombMultiTargetLimit(const World::TWeapProto &wproto, int weaponCount)
{
    if ( !ypabact_IsBombMultiTargetWeapon(wproto) || weaponCount <= 1 )
        return 0;

    int maxTargets = wproto.homing_bomb_multi_target;
    if ( maxTargets > weaponCount )
        maxTargets = weaponCount;

    return maxTargets;
}

static bool ypabact_IsMissileMultiTargetUnitType(NC_STACK_ypabact *target)
{
    switch ( target->_bact_type )
    {
    case BACT_TYPES_BACT:
    case BACT_TYPES_TANK:
    case BACT_TYPES_ROBO:
    case BACT_TYPES_FLYER:
    case BACT_TYPES_UFO:
    case BACT_TYPES_CAR:
    case BACT_TYPES_GUN:
    case BACT_TYPES_HOVER:
        return true;

    default:
        return false;
    }
}

static bool ypabact_IsValidMissileMultiTarget(NC_STACK_ypabact *launcher, NC_STACK_ypabact *target)
{
    if ( !launcher || !target || launcher == target || target->_isDummy || !launcher->getBACT_pWorld() )
        return false;

    if ( target->getBACT_pWorld() != launcher->getBACT_pWorld() )
        return false;

    if ( launcher->_owner == World::OWNER_0 || target->_owner == World::OWNER_0 || target->_owner == launcher->_owner )
        return false;

    if ( !ypabact_IsMissileMultiTargetUnitType(target) )
        return false;

    if ( target->_bact_type == BACT_TYPES_MISSLE ||
         target->_status == BACT_STATUS_DEAD ||
         target->_status == BACT_STATUS_CREATE ||
         target->_status == BACT_STATUS_BEAM ||
         target->_energy <= 0 ||
         target->_energy_max <= 0 ||
         target->IsDestroyed() ||
         (target->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER)) )
        return false;

    if ( target->_bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(target);
        if ( !gun || (gun->IsRoboGun() && target->GetEffectiveShield() >= 100.0f) )
            return false;
    }

    return true;
}

static bool ypabact_GetMissileMultiTargetScore(NC_STACK_ypabact *launcher, NC_STACK_ypabact *target, const vec3d &aimDir, float weaponRadius, float *outScore)
{
    if ( !ypabact_IsValidMissileMultiTarget(launcher, target) )
        return false;

    vec3d targetDelta = target->_position - launcher->_old_pos;
    if ( targetDelta.dot(aimDir) < 0.0 )
        return false;

    float targetDistance = targetDelta.length();
    if ( targetDistance >= YPA_MISSILE_MULTI_TARGET_RANGE )
        return false;

    vec3d lineDelta = aimDir * targetDelta;
    float lineDistance = lineDelta.length();
    float lockRadius = targetDistance * 0.5 + 20.0;

    if ( lineDistance >= lockRadius && lineDistance >= target->_radius + weaponRadius )
        return false;

    if ( outScore )
        *outScore = lineDistance + targetDistance * 0.001;

    return true;
}

static void ypabact_AddMissileMultiTargetCandidate(std::vector<TMissileMultiTargetCandidate> &targets, NC_STACK_ypabact *target, float score)
{
    if ( !target )
        return;

    for (const TMissileMultiTargetCandidate &candidate : targets)
    {
        if ( candidate.target == target )
            return;
    }

    TMissileMultiTargetCandidate candidate;
    candidate.target = target;
    candidate.score = score;
    targets.push_back(candidate);
}

static void ypabact_CollectMissileMultiTargetsFromCell(std::vector<TMissileMultiTargetCandidate> &candidates, NC_STACK_ypabact *launcher, cellArea *cell, const vec3d &aimDir, float weaponRadius)
{
    if ( !cell )
        return;

    for( NC_STACK_ypabact* &unit : cell->unitsList )
    {
        float score = 0.0;
        if ( ypabact_GetMissileMultiTargetScore(launcher, unit, aimDir, weaponRadius, &score) )
            ypabact_AddMissileMultiTargetCandidate(candidates, unit, score);
    }
}

static void ypabact_CollectHomingBombTargetsFromCell(std::vector<TMissileMultiTargetCandidate> &candidates, NC_STACK_ypabact *launcher, cellArea *cell, const vec3d &referencePos, float weaponRadius)
{
    if ( !cell )
        return;

    for( NC_STACK_ypabact* &unit : cell->unitsList )
    {
        if ( !ypabact_IsValidMissileMultiTarget(launcher, unit) )
            continue;

        vec3d targetDelta = unit->_position - referencePos;
        float horizontalDistance = targetDelta.XZ().length();
        if ( horizontalDistance >= YPA_MISSILE_MULTI_TARGET_RANGE )
            continue;

        float score = horizontalDistance + fabs(targetDelta.y) * 0.05 + weaponRadius * 0.001;
        ypabact_AddMissileMultiTargetCandidate(candidates, unit, score);
    }
}

static std::vector<NC_STACK_ypabact *> ypabact_CollectMissileMultiTargets(NC_STACK_ypabact *launcher, const bact_arg79 *arg, const World::TWeapProto &wproto, int maxTargets, bool useTargetAimDir = false)
{
    std::vector<TMissileMultiTargetCandidate> candidates;
    std::vector<NC_STACK_ypabact *> targets;

    if ( !launcher || !arg || !launcher->getBACT_pWorld() || maxTargets <= 0 )
        return targets;

    vec3d aimDir = arg->direction;
    if ( useTargetAimDir )
    {
        if ( arg->tgType == BACT_TGT_TYPE_UNIT && arg->target.pbact )
            aimDir = arg->target.pbact->_position - launcher->_old_pos;
        else if ( arg->tgType == BACT_TGT_TYPE_CELL )
            aimDir = arg->tgt_pos - launcher->_old_pos;
    }

    float aimLen = aimDir.length();
    if ( aimLen <= 0.001 )
    {
        aimDir = launcher->_rotation.AxisZ();
        aimLen = aimDir.length();
    }

    if ( aimLen <= 0.001 )
        return targets;

    aimDir = aimDir / aimLen;

    if ( arg->tgType == BACT_TGT_TYPE_UNIT && ypabact_IsValidMissileMultiTarget(launcher, arg->target.pbact) )
        ypabact_AddMissileMultiTargetCandidate(candidates, arg->target.pbact, -1.0);

    int sectorRadius = (int)(YPA_MISSILE_MULTI_TARGET_RANGE / World::CVSectorLength) + 2;
    for (int y = -sectorRadius; y <= sectorRadius; y++)
    {
        for (int x = -sectorRadius; x <= sectorRadius; x++)
        {
            Common::Point cellId(launcher->_cellId.x + x, launcher->_cellId.y + y);

            if ( !launcher->getBACT_pWorld()->IsGamePlaySector(cellId) )
                continue;

            ypabact_CollectMissileMultiTargetsFromCell(candidates, launcher, &launcher->getBACT_pWorld()->SectorAt(cellId), aimDir, wproto.radius);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const TMissileMultiTargetCandidate &a, const TMissileMultiTargetCandidate &b) {
        return a.score < b.score;
    });

    if ( maxTargets <= 0 || maxTargets > (int)candidates.size() )
        maxTargets = candidates.size();

    for (int i = 0; i < maxTargets; i++)
        targets.push_back(candidates[i].target);

    return targets;
}

static std::vector<NC_STACK_ypabact *> ypabact_CollectHomingBombTargets(NC_STACK_ypabact *launcher, const bact_arg79 *arg, const World::TWeapProto &wproto, int maxTargets)
{
    std::vector<TMissileMultiTargetCandidate> candidates;
    std::vector<NC_STACK_ypabact *> targets;

    if ( !launcher || !arg || !launcher->getBACT_pWorld() || maxTargets <= 0 )
        return targets;

    if ( arg->tgType == BACT_TGT_TYPE_UNIT && ypabact_IsValidMissileMultiTarget(launcher, arg->target.pbact) )
        ypabact_AddMissileMultiTargetCandidate(candidates, arg->target.pbact, -1.0);

    vec3d referencePos = launcher->_position;
    if ( arg->tgType == BACT_TGT_TYPE_CELL )
        referencePos = arg->tgt_pos;

    int sectorRadius = (int)(YPA_MISSILE_MULTI_TARGET_RANGE / World::CVSectorLength) + 2;
    for (int y = -sectorRadius; y <= sectorRadius; y++)
    {
        for (int x = -sectorRadius; x <= sectorRadius; x++)
        {
            Common::Point cellId(launcher->_cellId.x + x, launcher->_cellId.y + y);

            if ( !launcher->getBACT_pWorld()->IsGamePlaySector(cellId) )
                continue;

            ypabact_CollectHomingBombTargetsFromCell(candidates, launcher, &launcher->getBACT_pWorld()->SectorAt(cellId), referencePos, wproto.radius);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const TMissileMultiTargetCandidate &a, const TMissileMultiTargetCandidate &b) {
        return a.score < b.score;
    });

    if ( maxTargets <= 0 || maxTargets > (int)candidates.size() )
        maxTargets = candidates.size();

    for (int i = 0; i < maxTargets; i++)
        targets.push_back(candidates[i].target);

    return targets;
}

static NC_STACK_ypabact *ypabact_GetDistributedMissileTarget(const std::vector<NC_STACK_ypabact *> &targets, int shotIndex)
{
    if ( targets.empty() )
        return NULL;

    return targets[shotIndex % targets.size()];
}

static void ypabact_StoreHUDMissileMultiLockTargets(NC_STACK_ypabact *launcher, const std::vector<NC_STACK_ypabact *> &missileTargets)
{
    if ( !launcher || !launcher->getBACT_pWorld() || !(launcher->_oflags & BACT_OFLAG_USERINPT) )
        return;

    if ( missileTargets.size() > 1 )
        launcher->getBACT_pWorld()->_hudMissileMultiLockTargets = missileTargets;
    else
        launcher->getBACT_pWorld()->_hudMissileMultiLockTargets.clear();
}

static void ypabact_UpdateHUDMissileMultiLockTargets(NC_STACK_ypabact *launcher, const bact_arg79 *arg, const World::TWeapProto &wproto, int weaponCount)
{
    if ( !launcher || !launcher->getBACT_pWorld() || !(launcher->_oflags & BACT_OFLAG_USERINPT) )
        return;

    int maxTargets = ypabact_GetMissileMultiTargetLimit(wproto, weaponCount);
    if ( maxTargets <= 1 )
    {
        launcher->getBACT_pWorld()->_hudMissileMultiLockTargets.clear();
        return;
    }

    std::vector<NC_STACK_ypabact *> missileTargets = ypabact_CollectMissileMultiTargets(launcher, arg, wproto, maxTargets);
    ypabact_StoreHUDMissileMultiLockTargets(launcher, missileTargets);
}

static int ypabact_SelectPrimaryWeaponSlot(NC_STACK_ypabact *bact, int requestedWeapon)
{
    if ( requestedWeapon != bact->_weapon )
        return requestedWeapon;

    int slots[4];
    int count = ypabact_GetPrimaryWeaponSlots(bact, slots);

    if ( count <= 0 )
        return -1;

    if ( count == 1 )
        return slots[0];

    if ( bact->_weapon_switch_mode == 1 )
        return slots[rand() % count];

    int index = bact->_weapon_slot_index % count;
    if ( index < 0 )
        index = 0;

    return slots[index];
}

static void ypabact_AdvancePrimaryWeaponSlot(NC_STACK_ypabact *bact, int requestedWeapon)
{
    if ( requestedWeapon != bact->_weapon || bact->_weapon_switch_mode == 1 )
        return;

    int slots[4];
    int count = ypabact_GetPrimaryWeaponSlots(bact, slots);

    if ( count > 1 )
        bact->_weapon_slot_index = (bact->_weapon_slot_index + 1) % count;
    else
        bact->_weapon_slot_index = 0;
}

int NC_STACK_ypabact::GetCurrentWeaponId()
{
    if ( ypabact_IsLowHPWeaponActive(this) )
        return _lowhp_weapon;

    int slots[4];
    int count = ypabact_GetPrimaryWeaponSlots(this, slots);

    if ( count <= 0 )
        return -1;

    if ( count == 1 )
        return slots[0];

    if ( _weapon_switch_mode == 1 )
    {
        if ( ypabact_IsValidWeaponId(this, _current_weapon_id) )
            return _current_weapon_id;

        return slots[0];
    }

    int index = _weapon_slot_index % count;
    if ( index < 0 )
        index = 0;

    return slots[index];
}

static bool ypabact_CanUseProximityDefense(NC_STACK_ypabact *unit)
{
    if ( !unit || !unit->getBACT_pWorld() )
        return false;

    if ( !unit->_proximity_defense_enable )
        return false;

    if ( unit->_proximity_defense_at_death )
        return false;

    if ( unit->_proximity_defense_weapon <= 0 || (size_t)unit->_proximity_defense_weapon >= unit->getBACT_pWorld()->GetWeaponsProtos().size() )
        return false;

    if ( unit->_proximity_defense_trigger_radius <= 0.0 )
        return false;

    if ( unit->_proximity_defense_shots <= 0 )
        return false;

    if ( unit->_owner == World::OWNER_0 )
        return false;

    if ( unit->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( unit->_status == BACT_STATUS_DEAD ||
         unit->_status == BACT_STATUS_CREATE ||
         unit->_status == BACT_STATUS_BEAM )
        return false;

    if ( unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER) )
        return false;

    return true;
}

static vec3d ypabact_GetProximityDefenseLocalDirection(NC_STACK_ypabact *unit, int shotIndex, int totalShots)
{
    float yaw = ((float)shotIndex / (float)totalShots) * 360.0;
    float pitch = 0.0;

    if ( unit->_proximity_defense_random_yaw_set )
    {
        float yawMin = unit->_proximity_defense_random_yaw_min;
        float yawMax = unit->_proximity_defense_random_yaw_max;

        if ( yawMin > yawMax )
            std::swap(yawMin, yawMax);

        yaw = yawMin + ((float)rand() / (float)RAND_MAX) * (yawMax - yawMin);
    }

    if ( unit->_proximity_defense_random_pitch_set )
    {
        float pitchMin = unit->_proximity_defense_random_pitch_min;
        float pitchMax = unit->_proximity_defense_random_pitch_max;

        if ( pitchMin > pitchMax )
            std::swap(pitchMin, pitchMax);

        pitch = pitchMin + ((float)rand() / (float)RAND_MAX) * (pitchMax - pitchMin);
    }

    float yawRad = yaw * C_PI / 180.0;
    float pitchRad = pitch * C_PI / 180.0;
    float horizontal = cos(pitchRad);

    return vec3d(sin(yawRad) * horizontal, sin(pitchRad), cos(yawRad) * horizontal);
}

static bool ypabact_IsLiveProximityMissileOwner(NC_STACK_ypabact *candidate, NC_STACK_ypabact *unit)
{
    if ( !candidate || candidate == unit )
        return false;

    if ( candidate->_status == BACT_STATUS_DEAD )
        return false;

    if ( candidate->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_CLEAN) )
        return false;

    return true;
}

static NC_STACK_ypabact *ypabact_GetProximityDefenseAtDeathMissileOwner(NC_STACK_ypabact *unit)
{
    if ( !unit )
        return NULL;

    if ( ypabact_IsLiveProximityMissileOwner(unit->_parent, unit) && unit->_parent->_owner == unit->_owner )
        return unit->_parent;

    if ( ypabact_IsLiveProximityMissileOwner(unit->_host_station, unit) && unit->_host_station->_owner == unit->_owner )
        return unit->_host_station;

    NC_STACK_ypaworld *world = unit->getBACT_pWorld();
    if ( world )
    {
        NC_STACK_ypabact *userHost = world->getYW_userHostStation();
        if ( ypabact_IsLiveProximityMissileOwner(userHost, unit) && userHost->_owner == unit->_owner )
            return userHost;
    }

    return NULL;
}

static bool ypabact_FireProximityDefenseShot(NC_STACK_ypabact *unit, int shotIndex, int totalShots, bool trackLauncherMissile = true, NC_STACK_ypabact *missileListOwner = NULL)
{
    if ( !unit || !unit->getBACT_pWorld() || totalShots <= 0 )
        return false;

    NC_STACK_ypaworld *world = unit->getBACT_pWorld();
    if ( (size_t)unit->_proximity_defense_weapon >= world->GetWeaponsProtos().size() )
        return false;

    World::TWeapProto &wproto = world->GetWeaponsProtos().at(unit->_proximity_defense_weapon);
    if ( !(wproto._weaponFlags & YPA_WEAPON_FLAG_PROJECTILE) )
        return false;

    vec3d localDir = ypabact_GetProximityDefenseLocalDirection(unit, shotIndex, totalShots);
    vec3d shotDir = unit->_rotation.Transpose().Transform(localDir);
    float shotDirLen = shotDir.length();
    if ( shotDirLen <= 0.001 )
        return false;

    shotDir = shotDir / shotDirLen;

    ypaworld_arg146 arg147;
    arg147.vehicle_id = unit->_proximity_defense_weapon;
    arg147.pos = unit->_position + unit->_rotation.Transpose().Transform(unit->_proximity_defense_fire_pos);

    NC_STACK_ypamissile *wobj = world->ypaworld_func147(&arg147);
    if ( !wobj )
        return false;

    NC_STACK_ypabact *liveLauncher = missileListOwner ? missileListOwner : unit;

    wobj->SetLauncherBact(liveLauncher);
    wobj->SetStartHeight(arg147.pos.y);
    wobj->_owner = unit->_owner;
    wobj->_fly_dir = shotDir;
    wobj->_fly_dir_length = unit->_fly_dir_length + wproto.start_speed;

    if ( !(wproto._weaponFlags & 0x12) )
        wobj->_fly_dir_length *= 0.2;

    wobj->_rotation.SetZ(wobj->_fly_dir);
    wobj->_rotation.SetX(unit->_rotation.AxisX());
    wobj->_rotation.SetY(wobj->_rotation.AxisZ() * wobj->_rotation.AxisX());

    if ( unit->_proximity_defense_vp_launch > 0 )
        world->SpawnTransientVP(unit->_proximity_defense_vp_launch, wobj->_position, wobj->_rotation, 1000);

    wobj->_kidRef.Detach();
    wobj->_parent = NULL;

    if ( trackLauncherMissile )
    {
        NC_STACK_ypabact *listOwner = missileListOwner ? missileListOwner : unit;
        if ( listOwner )
            listOwner->_missiles_list.push_back(wobj);
    }

    int missileType = wobj->GetMissileType();
    if ( missileType == NC_STACK_ypamissile::MISL_DIRECT )
    {
        wobj->_primTtype = BACT_TGT_TYPE_DRCT;
        wobj->_target_dir = wobj->_fly_dir;
    }
    else if ( missileType == NC_STACK_ypamissile::MISL_TARGETED )
    {
        setTarget_msg target = {};
        target.tgt_type = BACT_TGT_TYPE_DRCT;
        target.priority = 0;
        target.tgt_pos = arg147.pos + shotDir * 1000.0;
        wobj->SetTarget(&target);
    }

    wobj->_host_station = unit->_host_station ? unit->_host_station : (liveLauncher ? liveLauncher->_host_station : NULL);
    SFXEngine::SFXe.startSound(&wobj->_soundcarrier, 1);

    if ( wobj->_primTtype != BACT_TGT_TYPE_UNIT && wproto.life_time_nt )
        wobj->SetLifeTime(wproto.life_time_nt);

    return true;
}

static void ypabact_FireProximityDefenseBurst(NC_STACK_ypabact *unit, bool trackLauncherMissiles = true, NC_STACK_ypabact *missileListOwner = NULL)
{
    int shots = unit->_proximity_defense_shots > 0 ? unit->_proximity_defense_shots : 1;
    for (int i = 0; i < shots; i++)
        ypabact_FireProximityDefenseShot(unit, i, shots, trackLauncherMissiles, missileListOwner);
}

static bool ypabact_CanUseProximityDefenseAtDeath(NC_STACK_ypabact *unit)
{
    if ( !unit || !unit->getBACT_pWorld() )
        return false;

    if ( unit->getBACT_pWorld()->_isNetGame )
        return false;

    if ( !unit->_proximity_defense_enable || !unit->_proximity_defense_at_death || unit->_proximity_defense_at_death_done )
        return false;

    if ( unit->_proximity_defense_weapon <= 0 || (size_t)unit->_proximity_defense_weapon >= unit->getBACT_pWorld()->GetWeaponsProtos().size() )
        return false;

    if ( unit->_proximity_defense_shots <= 0 )
        return false;

    if ( unit->_owner == World::OWNER_0 )
        return false;

    if ( unit->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( unit->_status == BACT_STATUS_CREATE || unit->_status == BACT_STATUS_BEAM )
        return false;

    if ( unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER | BACT_STFLAG_CLEAN) )
        return false;

    return true;
}

static void ypabact_FireProximityDefenseAtDeath(NC_STACK_ypabact *unit)
{
    if ( !ypabact_CanUseProximityDefenseAtDeath(unit) )
        return;

    NC_STACK_ypabact *missileOwner = ypabact_GetProximityDefenseAtDeathMissileOwner(unit);
    if ( !missileOwner )
        return;

    unit->_proximity_defense_at_death_done = true;
    ypabact_FireProximityDefenseBurst(unit, true, missileOwner);
}

void NC_STACK_ypabact::UpdateCarrierSpawn(update_msg *)
{
    if ( !ypabact_CanCarrierSpawn(this) )
        return;

    // V1 is single-player/local only; avoiding unsynchronised network spawns is safer.
    if ( _world->_isNetGame )
        return;

    int interval = _spawn_interval > 0 ? _spawn_interval : 5000;
    if ( interval < 1000 )
        interval = 1000;

    if ( _spawn_last_time && _clock - _spawn_last_time < interval )
        return;

    int maxActive = _spawn_max_active > 0 ? _spawn_max_active : 1;
    int activeCount = ypabact_CountCarrierSpawnedUnits(this);
    if ( activeCount >= maxActive )
        return;

    if ( !ypabact_CarrierHasEnemyNearby(this) )
        return;

    _spawn_last_time = _clock;

    int spawnCount = _spawn_count > 0 ? _spawn_count : 1;
    if ( spawnCount > 8 )
        spawnCount = 8;

    int remainingSlots = maxActive - activeCount;
    if ( spawnCount > remainingSlots )
        spawnCount = remainingSlots;

    NC_STACK_ypabact *squadLeader = NULL;
    int squadCommandId = dword_5B1128;

    for (int i = 0; i < spawnCount; i++)
    {
        vec3d spawnPos;
        if ( !ypabact_FindCarrierSpawnPosition(this, &spawnPos) )
            continue;

        NC_STACK_ypabact *unit = ypabact_CreateCarrierSpawnedUnit(this, spawnPos);
        if ( !unit )
            continue;

        unit->_commandID = squadCommandId;

        if ( !squadLeader )
        {
            squadLeader = unit;
            ypabact_AttachCarrierSpawnLeader(this, squadLeader);
        }
        else
        {
            squadLeader->AddSubject(unit);
        }

        _carrier_spawned_gids.push_back(unit->_gid);
    }

    if ( squadLeader )
        dword_5B1128++;
}

void NC_STACK_ypabact::UpdateProximityDefense(update_msg *)
{
    if ( !ypabact_CanUseProximityDefense(this) )
    {
        _proximity_defense_sequence_active = false;
        return;
    }

    // V1 is single-player/local only; avoiding unsynchronised network projectiles is safer.
    if ( _world->_isNetGame )
        return;

    int interval = _proximity_defense_interval > 0 ? _proximity_defense_interval : 1000;
    int shots = _proximity_defense_shots > 0 ? _proximity_defense_shots : 1;

    if ( _proximity_defense_fire_mode == 1 && _proximity_defense_sequence_active )
    {
        int delay = _proximity_defense_sequence_delay > 0 ? _proximity_defense_sequence_delay : 100;

        if ( _clock < _proximity_defense_next_shot_time )
            return;

        ypabact_FireProximityDefenseShot(this, _proximity_defense_sequence_shots_fired, shots);
        _proximity_defense_sequence_shots_fired++;

        if ( _proximity_defense_sequence_shots_fired >= shots )
        {
            _proximity_defense_sequence_active = false;
            _proximity_defense_sequence_shots_fired = 0;
            _proximity_defense_next_activation_time = _clock + interval;
        }
        else
        {
            _proximity_defense_next_shot_time = _clock + delay;
        }

        return;
    }

    if ( _clock < _proximity_defense_next_activation_time )
        return;

    if ( !ypabact_HasEnemyNearby(this, _proximity_defense_trigger_radius) )
        return;

    if ( _proximity_defense_fire_mode == 1 )
    {
        int delay = _proximity_defense_sequence_delay > 0 ? _proximity_defense_sequence_delay : 100;
        _proximity_defense_sequence_active = true;
        _proximity_defense_sequence_shots_fired = 1;

        ypabact_FireProximityDefenseShot(this, 0, shots);

        if ( _proximity_defense_sequence_shots_fired >= shots )
        {
            _proximity_defense_sequence_active = false;
            _proximity_defense_sequence_shots_fired = 0;
            _proximity_defense_next_activation_time = _clock + interval;
        }
        else
        {
            _proximity_defense_next_shot_time = _clock + delay;
        }

        return;
    }

    ypabact_FireProximityDefenseBurst(this);
    _proximity_defense_next_activation_time = _clock + interval;
}

// ===== OpenUA custom: radar-guided mortar barrage =========================

// Resolve a mortar weapon id from one unit's own main/extra weapon slots.
// Mounted unit-guns are handled separately so their child BACT owns the barrage
// runtime state and the shell launch position.
static int ypabact_GetMortarWeaponId(NC_STACK_ypabact *unit)
{
    if ( !unit || !unit->getBACT_pWorld() )
        return 0;

    std::vector<World::TWeapProto> &weapons = unit->getBACT_pWorld()->GetWeaponsProtos();

    int candidates[4] = { unit->_weapon, unit->_extra_weapons[0], unit->_extra_weapons[1], unit->_extra_weapons[2] };

    for (int i = 0; i < 4; i++)
    {
        int id = candidates[i];
        if ( id > 0 && (size_t)id < weapons.size() && weapons.at(id).IsMortar() )
            return id;
    }

    return 0;
}

static int ypabact_GetVehicleProtoMortarWeaponId(NC_STACK_ypaworld *world, int vehicleId)
{
    if ( !world || vehicleId <= 0 || (size_t)vehicleId >= world->GetVhclProtos().size() )
        return 0;

    const World::TVhclProto &proto = world->GetVhclProtos().at(vehicleId);
    std::vector<World::TWeapProto> &weapons = world->GetWeaponsProtos();

    int candidates[4] = { proto.weapon, proto.extra_weapons[0], proto.extra_weapons[1], proto.extra_weapons[2] };

    for (int i = 0; i < 4; i++)
    {
        int id = candidates[i];
        if ( id > 0 && (size_t)id < weapons.size() && weapons.at(id).IsMortar() )
            return id;
    }

    return 0;
}

static bool ypabact_IsLiveMortarGunActor(NC_STACK_ypabact *gunObj, int *outWeaponId = NULL)
{
    if ( outWeaponId )
        *outWeaponId = 0;

    if ( !gunObj || !gunObj->getBACT_pWorld() )
        return false;

    if ( gunObj->IsDestroyed() ||
         gunObj->_status == BACT_STATUS_DEAD ||
         gunObj->_status == BACT_STATUS_CREATE ||
         (gunObj->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER)) )
        return false;

    int weaponId = ypabact_GetMortarWeaponId(gunObj);
    if ( weaponId <= 0 )
        return false;

    if ( outWeaponId )
        *outWeaponId = weaponId;

    return true;
}

static NC_STACK_ypabact *ypabact_GetMortarActorFromGunList(std::vector<World::TRoboGun> &guns, int *outWeaponId = NULL)
{
    if ( outWeaponId )
        *outWeaponId = 0;

    for (World::TRoboGun &gun : guns)
    {
        int weaponId = 0;
        if ( ypabact_IsLiveMortarGunActor(gun.gun_obj, &weaponId) )
        {
            if ( outWeaponId )
                *outWeaponId = weaponId;
            return gun.gun_obj;
        }
    }

    return NULL;
}

static int ypabact_GetProtoMortarWeaponIdFromGunList(NC_STACK_ypaworld *world, const std::vector<World::TRoboGun> &guns)
{
    if ( !world )
        return 0;

    for (const World::TRoboGun &gun : guns)
    {
        int weaponId = ypabact_GetVehicleProtoMortarWeaponId(world, gun.robo_gun_type);
        if ( weaponId > 0 )
            return weaponId;
    }

    return 0;
}

static NC_STACK_ypabact *ypabact_GetMountedMortarActor(NC_STACK_ypabact *unit, int *outWeaponId = NULL)
{
    if ( outWeaponId )
        *outWeaponId = 0;

    if ( !unit || !unit->getBACT_pWorld() )
        return NULL;

    // Vehicle-mounted unit guns. The parent is selected/rendered in UI, while
    // the child gun owns barrage cooldown/state and launch position.
    if ( NC_STACK_ypabact *actor = ypabact_GetMortarActorFromGunList(unit->_unitGuns, outWeaponId) )
        return actor;

    // Host Station / Robo guns use a separate gun list. Treat them like mounted
    // unit guns for mortar purposes: click/draw on the robo parent, fire from the
    // real gun child.
    if ( NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(unit) )
    {
        if ( NC_STACK_ypabact *actor = ypabact_GetMortarActorFromGunList(robo->_roboGuns, outWeaponId) )
            return actor;
    }

    return NULL;
}

static int ypabact_GetMountedMortarWeaponId(NC_STACK_ypabact *unit)
{
    if ( !unit || !unit->getBACT_pWorld() )
        return 0;

    int liveWeaponId = 0;
    if ( ypabact_GetMountedMortarActor(unit, &liveWeaponId) )
        return liveWeaponId;

    // Prototype fallback: this lets the parent be recognized as a mortar platform
    // even before the mounted gun child has finished its create cycle. The live
    // child still owns actual firing once manual/AI orders are executed.
    int weaponId = ypabact_GetProtoMortarWeaponIdFromGunList(unit->getBACT_pWorld(), unit->_unitGuns);
    if ( weaponId > 0 )
        return weaponId;

    if ( NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(unit) )
    {
        weaponId = ypabact_GetProtoMortarWeaponIdFromGunList(unit->getBACT_pWorld(), robo->_roboGuns);
        if ( weaponId > 0 )
            return weaponId;
    }

    return 0;
}

static NC_STACK_ypabact *ypabact_GetManualMortarActor(NC_STACK_ypabact *unit, int *outWeaponId = NULL)
{
    if ( outWeaponId )
        *outWeaponId = 0;

    int ownWeaponId = ypabact_GetMortarWeaponId(unit);
    if ( ownWeaponId > 0 )
    {
        if ( outWeaponId )
            *outWeaponId = ownWeaponId;
        return unit;
    }

    return ypabact_GetMountedMortarActor(unit, outWeaponId);
}

// OpenUA custom: true if this unit carries a "model = mortar" weapon in any own
// slot, or if one of its mounted unit-gun / robo-gun children carries one. Used
// to keep mortar platforms map-only (no first-person possession).
bool NC_STACK_ypabact::IsMortarPlatform()
{
    return ypabact_GetMortarWeaponId(this) > 0 || ypabact_GetMountedMortarWeaponId(this) > 0;
}

// OpenUA custom: true if this is a mortar platform usable via manual map-click.
// Manual map-click control is always enabled for mortars (there is no opt-in flag).
bool NC_STACK_ypabact::IsManualMortarPlatform()
{
    return IsMortarPlatform();
}

// OpenUA custom: bombardment zone radius of this unit's mortar weapon (0 if none).
// Used to draw the white aiming preview ring on the 2D map.
float NC_STACK_ypabact::GetMortarBarrageRadius()
{
    if ( !_world )
        return 0.0f;

    int weaponId = 0;
    if ( NC_STACK_ypabact *actor = ypabact_GetManualMortarActor(this, &weaponId) )
    {
        (void)actor;
        if ( weaponId > 0 )
            return _world->GetWeaponsProtos().at(weaponId).mortar_barrage_radius;
    }

    weaponId = ypabact_GetMountedMortarWeaponId(this);
    if ( weaponId > 0 )
        return _world->GetWeaponsProtos().at(weaponId).mortar_barrage_radius;

    return 0.0f;
}

float NC_STACK_ypabact::GetMortarReadinessRatio()
{
    if ( !_world )
        return 0.0f;

    int weaponId = 0;
    NC_STACK_ypabact *actor = ypabact_GetManualMortarActor(this, &weaponId);
    if ( !actor || weaponId <= 0 || (size_t)weaponId >= _world->GetWeaponsProtos().size() )
        return IsMortarPlatform() ? 1.0f : 0.0f;

    if ( actor->_mortar_barrage_active && actor->_mortar_shots_remaining > 0 )
        return 1.0f;

    const World::TWeapProto &wproto = _world->GetWeaponsProtos().at(weaponId);
    int cooldown = wproto.mortar_barrage_cooldown > 0 ? wproto.mortar_barrage_cooldown : 10000;
    if ( cooldown <= 0 || actor->_clock >= actor->_mortar_next_activation_time )
        return 1.0f;

    int remaining = actor->_mortar_next_activation_time - actor->_clock;
    float ready = 1.0f - ((float)remaining / (float)cooldown);
    if ( ready < 0.0f )
        ready = 0.0f;
    if ( ready > 1.0f )
        ready = 1.0f;

    return ready;
}

static bool ypabact_CanUseMortar(NC_STACK_ypabact *unit)
{
    if ( !unit || !unit->getBACT_pWorld() )
        return false;

    if ( unit->_owner == World::OWNER_0 )
        return false;

    if ( unit->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( unit->_status == BACT_STATUS_DEAD ||
         unit->_status == BACT_STATUS_CREATE ||
         unit->_status == BACT_STATUS_BEAM )
        return false;

    if ( unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER) )
        return false;

    // Dummy attachments and spectator helpers never auto-fire mortars.
    if ( unit->_isDummy )
        return false;

    if ( unit->getBACT_pWorld()->IsSpectatorBact(unit) )
        return false;

    return true;
}

// OpenUA custom: the actual ballistic launch direction of a shell aimed at
// targetCenter. Matches NC_STACK_ypamissile::UpdateMortarBallistic(): the shell
// follows pos(t)=lerp(start,target) horizontally with a parabolic vertical arc
// pos.y = baseY - arcHeight*4*t*(1-t). Differentiating at t=0 gives the initial
// velocity below (engine convention: +Y is DOWN, so -4*arcHeight points upward).
// The barrel is aimed along this vector so it visibly points up into the arc.
static vec3d ypabact_GetMortarLaunchDir(NC_STACK_ypabact *unit, const World::TWeapProto &wproto, const vec3d &targetCenter)
{
    vec3d delta = targetCenter - unit->_position;

    vec3d dir;
    dir.x = delta.x;
    dir.z = delta.z;
    dir.y = delta.y - 4.0f * wproto.mortar_arc_height;

    return dir; // caller normalises
}

static float ypabact_ClampMortarAimDelta(float delta, float maxRot)
{
    if ( maxRot <= 0.0f )
        return delta;

    if ( delta > maxRot )
        return maxRot;

    if ( delta < -maxRot )
        return -maxRot;

    return delta;
}

static void ypabact_AimMortarLauncherVisual(NC_STACK_ypabact *unit, const World::TWeapProto &wproto,
                                            const vec3d &targetCenter, int frameTime, bool instant)
{
    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(unit);
    if ( !gun || gun->_isDummy )
        return;

    if ( gun->_gunBasis.length() <= 0.001f || gun->_gunRott.length() <= 0.001f )
        return;

    vec3d vTgt = ypabact_GetMortarLaunchDir(unit, wproto, targetCenter);
    float dist = vTgt.length();
    if ( dist <= 0.001f )
        return;

    vTgt /= dist;

    unit->_target_vec = targetCenter - unit->_position;
    unit->_target_dir = vTgt;

    float maxRot = instant ? 0.0f : unit->_maxrot * ((float)frameTime / 1000.0f);

    float xzAngle = 0.0f;
    float xzWanted = 0.0f;
    vec3d lx = gun->_gunRott * gun->_gunBasis;

    vec2d xzRot( unit->_rotation.AxisX().dot(lx),
                 unit->_rotation.AxisX().dot(gun->_gunBasis) );

    if ( xzRot.normalise() > 0.001f )
        xzAngle = xzRot.xyAngle();

    vec2d xzWant( vTgt.dot(gun->_gunBasis),
                  vTgt.dot(-lx) );

    if ( xzWant.normalise() > 0.001f )
        xzWanted = xzWant.xyAngle();
    else
        xzWanted = xzAngle;

    if ( gun->_gunMaxSide <= 3.1f )
    {
        if ( xzWanted < -gun->_gunMaxSide )
            xzWanted = -gun->_gunMaxSide;

        if ( xzWanted > gun->_gunMaxSide )
            xzWanted = gun->_gunMaxSide;
    }

    float xzDelta = xzWanted - xzAngle;

    if ( gun->_gunMaxSide > 3.1f )
    {
        if ( fabs(xzDelta) > C_PI )
        {
            if ( xzDelta < -C_PI )
                xzDelta += C_2PI;

            if ( xzDelta > C_PI )
                xzDelta -= C_2PI;
        }
    }

    xzDelta = ypabact_ClampMortarAimDelta(xzDelta, maxRot);

    if ( fabs(xzDelta) > 0.001f )
        unit->_rotation *= mat3x3(gun->_gunRott, xzDelta);

    vec3d invRed = -gun->_gunRott;
    float yAngle = clp_asin( invRed.dot(unit->_rotation.AxisZ()) );
    float yWant = clp_asin( invRed.dot(vTgt) );

    // OpenUA custom: mortars are high-angle artillery. Guarantee a generous upward
    // elevation envelope even if the gun model's gun_up_angle is small, so the
    // barrel convincingly points up into the ballistic arc.
    const float MORTAR_MIN_MAX_UP = 1.30f; // ~74 degrees
    float gunMaxUp = gun->_gunMaxUp > MORTAR_MIN_MAX_UP ? gun->_gunMaxUp : MORTAR_MIN_MAX_UP;

    if ( yWant > gunMaxUp )
        yWant = gunMaxUp;

    if ( yWant < -gun->_gunMaxDown )
        yWant = -gun->_gunMaxDown;

    float yDelta = yWant - yAngle;
    yDelta = ypabact_ClampMortarAimDelta(yDelta, maxRot);

    // No extra damping: yDelta is already speed-limited by maxRot when not instant,
    // so the barrel reaches the wanted elevation instead of stalling short of it.
    if ( fabs(yDelta) > 0.001f )
        unit->_rotation = mat3x3::RotateX(yDelta) * unit->_rotation;

    unit->_viewer_rotation = unit->_rotation;
}

static vec3d ypabact_GetMortarLaunchPosition(NC_STACK_ypabact *unit)
{
    if ( !unit )
        return vec3d(0.0, 0.0, 0.0);

    return unit->_position + unit->_rotation.Transpose().Transform(unit->_fire_pos);
}

static void ypabact_TriggerMortarFireVisual(NC_STACK_ypabact *unit)
{
    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>(unit);
    if ( !gun || gun->_isDummy )
        return;

    int fireTime = gun->_gunFireTime > 0 ? gun->_gunFireTime : 100;
    if ( gun->_gunFireCount < fireTime )
        gun->_gunFireCount = fireTime;

    if ( !(unit->_status_flg & BACT_STFLAG_FIRE) )
    {
        setState_msg state;
        state.unsetFlags = 0;
        state.newStatus = BACT_STATUS_NOPE;
        state.setFlags = BACT_STFLAG_FIRE;

        unit->SetState(&state);
    }
}

// A candidate is a valid mortar target only if it is a real, living enemy actor.
static bool ypabact_IsMortarEnemy(NC_STACK_ypabact *unit, NC_STACK_ypabact *cand)
{
    if ( !cand || cand == unit )
        return false;

    if ( cand->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( cand->_status == BACT_STATUS_DEAD ||
         cand->_status == BACT_STATUS_CREATE ||
         cand->_status == BACT_STATUS_BEAM )
        return false;

    if ( cand->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_NORENDER | BACT_STFLAG_CLEAN) )
        return false;

    if ( cand->_owner == World::OWNER_0 || cand->_owner == unit->_owner )
        return false;

    if ( cand->_isDummy ) // dummy attachments must never be preferred targets
        return false;

    NC_STACK_ypaworld *world = unit->getBACT_pWorld();
    if ( world && world->IsSpectatorBact(cand) )
        return false;

    return true;
}

// V1 target selection: pick the nearest valid enemy within [mortar_min_range,
// mortar_max_range] and use its position as the barrage center.
//
// onlyHostStation: when true, only enemy Host Stations (robo) are considered (used for
// the mortar_prefer_host_station priority pass). The radar requirement is always
// honoured in both passes: priority only chooses among radar-visible enemies.
static bool ypabact_ScanMortarTarget(NC_STACK_ypabact *unit, const World::TWeapProto &wproto,
                                     bool onlyHostStation, vec3d *out)
{
    NC_STACK_ypaworld *world = unit->getBACT_pWorld();
    if ( !world )
        return false;

    float maxRange = wproto.mortar_max_range;
    if ( maxRange <= 0.0 )
        return false; // no max range: mortar cannot auto-fire

    float minRange = wproto.mortar_min_range > 0.0 ? wproto.mortar_min_range : 0.0;
    float effRangeSq = maxRange * maxRange;
    float minRangeSq = minRange * minRange;

    int sectorRadius = (int)(maxRange / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(unit->_position);

    NC_STACK_ypabact *best = NULL;
    float bestDistSq = 0.0;

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);
            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            // Radar requirement: the target sector must be visible to our faction.
            // Always enforced, including the Host-Station priority pass.
            if ( wproto.mortar_requires_radar && !cell.IsCanSee(unit->_owner) )
                continue;

            for (NC_STACK_ypabact *cand : cell.unitsList)
            {
                if ( !ypabact_IsMortarEnemy(unit, cand) )
                    continue;

                if ( onlyHostStation && cand->_bact_type != BACT_TYPES_ROBO )
                    continue;

                float distSq = (cand->_position.XZ() - unit->_position.XZ()).square();
                if ( distSq < minRangeSq || distSq > effRangeSq )
                    continue;

                if ( !best || distSq < bestDistSq )
                {
                    best = cand;
                    bestDistSq = distSq;
                }
            }
        }
    }

    if ( !best )
        return false;

    *out = best->_position;
    return true;
}

static bool ypabact_FindMortarTargetZone(NC_STACK_ypabact *unit, const World::TWeapProto &wproto, vec3d *out)
{
    // Priority pass: among radar-visible enemies, prefer an enemy Host Station (robo)
    // (mortar_prefer_host_station). Still honours mortar_requires_radar.
    if ( wproto.mortar_prefer_host_station && ypabact_ScanMortarTarget(unit, wproto, true, out) )
        return true;

    // Normal pass: nearest valid enemy, honouring the radar requirement.
    return ypabact_ScanMortarTarget(unit, wproto, false, out);
}

// Fire a single ballistic mortar shell at the target zone (with per-shell spread).
// The shell is tracked in the firing unit's own missile list with the unit as
// emitter, exactly like a normal fired missile, so Die()'s reparent/cleanup keeps
// pointers valid.
static bool ypabact_FireMortarShell(NC_STACK_ypabact *unit, int weaponId, const World::TWeapProto &wproto, const vec3d &targetCenter)
{
    NC_STACK_ypaworld *world = unit->getBACT_pWorld();
    if ( !world || weaponId <= 0 || (size_t)weaponId >= world->GetWeaponsProtos().size() )
        return false;

    // Per-shell landing point: uniform random scatter inside spread radius.
    vec3d landing = targetCenter;
    if ( wproto.mortar_spread_radius > 0.0 )
    {
        float ang = ((float)rand() / (float)RAND_MAX) * 2.0 * C_PI;
        float r   = sqrt((float)rand() / (float)RAND_MAX) * wproto.mortar_spread_radius;
        landing.x += cos(ang) * r;
        landing.z += sin(ang) * r;
    }

    // Ground-burst mode (mortar_airburst = 0): snap the impact height to the real
    // terrain at THIS shell's own landing point (spread included), so it explodes on
    // contact with the ground instead of at the nominal target/arc height (airburst).
    if ( !wproto.mortar_airburst )
    {
        ypaworld_arg136 gnd;
        gnd.stPos = vec3d(landing.x, -30000.0, landing.z);
        gnd.vect  = vec3d(0.0, 50000.0, 0.0);
        gnd.flags = 0;

        world->ypaworld_func136(&gnd);

        if ( gnd.isect )
            landing.y = gnd.isectPos.y;
    }

    ypabact_AimMortarLauncherVisual(unit, wproto, targetCenter, 0, true);
    ypabact_TriggerMortarFireVisual(unit);

    vec3d launchPos = ypabact_GetMortarLaunchPosition(unit);

    ypaworld_arg146 arg147;
    arg147.vehicle_id = weaponId;
    arg147.pos = launchPos;

    NC_STACK_ypamissile *shell = world->ypaworld_func147(&arg147);
    if ( !shell )
        return false;

    shell->SetLauncherBact(unit);
    shell->_owner = unit->_owner;
    shell->_host_station = unit->_host_station;
    shell->SetStartHeight(launchPos.y);

    // Consistent downward impulse magnitude when the shell lands.
    shell->_fly_dir_length = wproto.start_speed;

    // Optional per-shell in-flight drift direction (fades to zero at landing).
    vec3d driftVec(0.0, 0.0, 0.0);
    if ( wproto.mortar_inflight_drift > 0.0 )
    {
        float ang = ((float)rand() / (float)RAND_MAX) * 2.0 * C_PI;
        driftVec.x = cos(ang) * wproto.mortar_inflight_drift;
        driftVec.z = sin(ang) * wproto.mortar_inflight_drift;
    }

    // Use the same guarded flight time the shell clamps to internally, so the
    // trajectory and the lifetime/marker below agree even if mortar_flight_time <= 0.
    int flight = wproto.mortar_flight_time > 0 ? wproto.mortar_flight_time : 2500;
    shell->SetupMortarShell(launchPos, landing, flight, wproto.mortar_arc_height, driftVec);

    shell->_kidRef.Detach();
    shell->_parent = NULL;
    unit->_missiles_list.push_back(shell);

    if ( wproto.vp_launch > 0 )
        world->SpawnTransientVP(wproto.vp_launch, shell->_position, shell->_rotation, 1000);

    SFXEngine::SFXe.startSound(&shell->_soundcarrier, 1);

    // Make sure the shell outlives its scheduled flight time (reuses guarded flight above).
    if ( shell->GetLifeTime() < flight + 1000 )
        shell->SetLifeTime(flight + 1000);

    // OpenUA custom: register/refresh the bombardment marker for this barrage zone.
    // Each shell refreshes it so the ring stays up until the last shell has landed.
    if ( wproto.mortar_minimap_marker && wproto.mortar_barrage_radius > 0.0 )
        world->AddMortarMarker(targetCenter, wproto.mortar_barrage_radius, unit->_owner, flight + 2000);

    return true;
}

// Fire one shell at the unit's current mortar target, then advance the shared shot
// budget. When the budget is spent the barrage ends and the cooldown starts. This
// single place owns the budget/cooldown bookkeeping for both the AI and manual paths.
static void ypabact_FireMortarShotAndAdvance(NC_STACK_ypabact *unit, int weaponId,
                                             const World::TWeapProto &wproto)
{
    int delay    = wproto.mortar_barrage_shot_delay > 0 ? wproto.mortar_barrage_shot_delay : 0;
    int cooldown = wproto.mortar_barrage_cooldown  > 0 ? wproto.mortar_barrage_cooldown  : 10000;

    if ( !ypabact_FireMortarShell(unit, weaponId, wproto, unit->_mortar_target_center) )
    {
        // Do not spend barrage budget if the projectile could not be created. Retry
        // after the normal shot delay instead of silently eating a shell.
        unit->_mortar_next_shot_time = unit->_clock + delay;
        return;
    }

    unit->_mortar_shots_remaining--;

    if ( unit->_mortar_shots_remaining <= 0 )
    {
        unit->_mortar_shots_remaining = 0;
        unit->_mortar_barrage_active = false;
        unit->_mortar_next_activation_time = unit->_clock + cooldown; // cooldown starts now
    }
    else
    {
        unit->_mortar_next_shot_time = unit->_clock + delay;
    }
}

void NC_STACK_ypabact::UpdateMortar(update_msg *arg)
{
    if ( !_world )
        return;

    // V1 is single-player/local only; unsynchronised network projectiles are unsafe.
    if ( _world->_isNetGame )
    {
        _mortar_barrage_active = false;
        return;
    }

    int weaponId = ypabact_GetMortarWeaponId(this);
    if ( weaponId <= 0 || !ypabact_CanUseMortar(this) )
    {
        _mortar_barrage_active = false;
        return;
    }

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(weaponId);

    int shots = wproto.mortar_barrage_shots;
    if ( shots <= 0 )
    {
        _mortar_barrage_active = false;
        return;
    }

    // Active barrage: fire one shell per shot-delay until the shared budget runs out.
    if ( _mortar_barrage_active )
    {
        ypabact_AimMortarLauncherVisual(this, wproto, _mortar_target_center, arg ? arg->frameTime : 0, false);

        if ( _clock < _mortar_next_shot_time )
            return;

        ypabact_FireMortarShotAndAdvance(this, weaponId, wproto);
        return;
    }

    // Pending manual order (queued while on cooldown): keep its azure zone ring alive
    // and fire it the instant the cooldown elapses. It takes priority over auto-scan
    // but never fires early, so it cannot bypass the cooldown.
    if ( _mortar_has_pending )
    {
        if ( wproto.mortar_minimap_marker && wproto.mortar_barrage_radius > 0.0 )
            _world->AddMortarMarker(_mortar_pending_target, wproto.mortar_barrage_radius, _owner, 1000);

        if ( _clock >= _mortar_next_activation_time )
        {
            vec3d pending = _mortar_pending_target;
            _mortar_has_pending = false;
            StartMortarBarrage(pending);
        }

        return;
    }

    // Manual-only mode: the auto AI is disabled. Active barrages and queued manual
    // orders (handled above) still run; we just never auto-acquire a target here.
    if ( wproto.mortar_manual_mode_only )
        return;

    // Cooldown gate between barrages.
    if ( _clock < _mortar_next_activation_time )
        return;

    // Throttle the mortar_max_range target scan so it never runs every frame while
    // the mortar is ready but has no valid target in range.
    if ( _clock < _mortar_next_scan_time )
        return;
    _mortar_next_scan_time = _clock + 500;

    vec3d targetCenter;
    if ( !ypabact_FindMortarTargetZone(this, wproto, &targetCenter) )
        return;

    StartMortarBarrage(targetCenter);
}

// Begin OR redirect a barrage aimed at targetCenter. Shared by the automatic AI and
// the manual call. Returns false if not eligible.
//
// Anti-exploit: the barrage's shots are a shared per-cycle budget. Redirecting an
// active barrage only re-aims it and keeps spending the SAME budget (no refill), so
// rapidly re-targeting can never dodge the cooldown. The budget refills (and the
// cooldown clears) only after the cooldown has fully elapsed.
bool NC_STACK_ypabact::StartMortarBarrage(const vec3d &targetCenter)
{
    if ( !_world )
        return false;

    int weaponId = ypabact_GetMortarWeaponId(this);
    if ( weaponId <= 0 )
    {
        if ( NC_STACK_ypabact *mountedMortar = ypabact_GetMountedMortarActor(this) )
            return mountedMortar->StartMortarBarrage(targetCenter);
        return false;
    }

    if ( !ypabact_CanUseMortar(this) )
        return false;

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(weaponId);

    int shots = wproto.mortar_barrage_shots;
    if ( shots <= 0 )
        return false;

    // Redirect an in-progress barrage: keep the remaining budget, just re-aim. The
    // already-scheduled next shot lands on the new area. No refill => no exploit.
    if ( _mortar_barrage_active && _mortar_shots_remaining > 0 )
    {
        _mortar_target_center = targetCenter;
        return true;
    }

    // Fresh barrage: only allowed once the cooldown has fully elapsed.
    if ( _clock < _mortar_next_activation_time )
        return false;

    _mortar_barrage_active = true;
    _mortar_shots_remaining = shots; // refill the cycle budget
    _mortar_target_center = targetCenter;

    // Fire the first shell immediately (this also spends one budget shot and, if the
    // budget is just 1, ends the barrage and starts the cooldown right away).
    ypabact_FireMortarShotAndAdvance(this, weaponId, wproto);

    return true;
}

// Check whether this unit can ACCEPT a manual bombardment call against targetPos.
// Returns true when the target is valid (range and radar; manual control is always
// enabled for mortars). It does NOT reject by cooldown/in-progress barrage: instead,
// *outReadyNow tells the caller whether the strike can fire immediately (redirect an
// active barrage, or start a fresh one off cooldown) or must be QUEUED until the
// cooldown elapses.
bool NC_STACK_ypabact::CanManualMortar(const vec3d &targetPos, int *outWeaponId, bool *outReadyNow)
{
    if ( outReadyNow )
        *outReadyNow = false;

    if ( !_world || _world->_isNetGame )
        return false;

    int weaponId = ypabact_GetMortarWeaponId(this);
    if ( weaponId <= 0 )
    {
        if ( NC_STACK_ypabact *mountedMortar = ypabact_GetMountedMortarActor(this) )
            return mountedMortar->CanManualMortar(targetPos, outWeaponId, outReadyNow);
        return false;
    }

    if ( !ypabact_CanUseMortar(this) )
        return false;

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(weaponId);

    // Manual map-click control is always available for mortars (no opt-in flag).

    if ( wproto.mortar_barrage_shots <= 0 )
        return false;

    // Range from this unit to the requested target point. mortar_max_range is the
    // single authoritative maximum range for both manual fire and auto-search.
    float maxRange = wproto.mortar_max_range;
    if ( maxRange <= 0.0 )
        return false;

    float minRange = wproto.mortar_min_range > 0.0 ? wproto.mortar_min_range : 0.0;
    float distSq = (targetPos.XZ() - _position.XZ()).square();
    if ( distSq < minRange * minRange || distSq > maxRange * maxRange )
        return false;

    // Radar: the target sector must be visible to our faction. This is the only
    // visibility gate on a manual strike.
    if ( wproto.mortar_requires_radar )
    {
        Common::Point cellId = World::PositionToSectorID(targetPos);
        if ( !_world->IsSector(cellId) || !_world->SectorAt(cellId).IsCanSee(_owner) )
            return false;
    }

    // Ready now if we can redirect an active barrage (spending its remaining shared
    // budget) or start a fresh one (idle and off cooldown). Otherwise the order is
    // still accepted, but queued: it fires when the cooldown elapses. A manual call
    // NEVER bypasses the cooldown once the shot budget is spent (anti-exploit).
    if ( outReadyNow )
    {
        bool canRedirect = _mortar_barrage_active && _mortar_shots_remaining > 0;
        bool canStartNew = !_mortar_barrage_active && _clock >= _mortar_next_activation_time;
        *outReadyNow = canRedirect || canStartNew;
    }

    if ( outWeaponId )
        *outWeaponId = weaponId;

    return true;
}

// Queue a manual strike to fire once the cooldown elapses (used when the target is
// valid but the mortar is mid-cooldown). The azure zone ring is shown immediately
// by UpdateMortar while the order is pending. Never fires early (anti-exploit).
void NC_STACK_ypabact::QueueManualMortar(const vec3d &targetPos)
{
    if ( ypabact_GetMortarWeaponId(this) <= 0 )
    {
        if ( NC_STACK_ypabact *mountedMortar = ypabact_GetMountedMortarActor(this) )
        {
            mountedMortar->QueueManualMortar(targetPos);
            return;
        }
    }

    _mortar_pending_target = targetPos;
    _mortar_has_pending = true;
}

static NC_STACK_ypabact *ypabact_GetSeekAndExplodePayloadListOwner(NC_STACK_ypabact *unit)
{
    if ( unit->_parent && unit->_parent->_status != BACT_STATUS_DEAD )
        return unit->_parent;

    if ( unit->_host_station && unit->_host_station->_status != BACT_STATUS_DEAD )
        return unit->_host_station;

    if ( unit->getBACT_pWorld() )
    {
        NC_STACK_ypabact *userHost = unit->getBACT_pWorld()->getYW_userHostStation();
        if ( userHost && userHost != unit && userHost->_status != BACT_STATUS_DEAD )
            return userHost;
    }

    return unit;
}

// ============================ OpenUA custom: model = laser ============================
// A laser is a targeted-class weapon that, instead of spawning a projectile, fires a
// continuous aimed hitscan beam. The beam is always visible while firing (even into
// empty space); when it crosses a valid target it applies static tick damage using
// energy as the base tick amount. All state is transient and lives on the firing unit
// (see the _laser_* members). The firing paths only register a per-frame request via
// RequestLaserFire(); UpdateLaser() does all the real work.

// Beam reach (engine units) derived from the weapon's life_time, so a modder controls
// the laser length with the familiar life_time knob. Clamped to a sane span.
static float ypabact_LaserRange(const World::TWeapProto &wproto)
{
    float range = (float)wproto.life_time;          // life_time (ms) reused as beam length
    if ( range <= 0.0f )
        range = World::CVSectorLength * 3.0f;       // safe default ~3 sectors

    float minR = World::CVSectorLength * 0.5f;
    float maxR = World::CVSectorLength * 10.0f;
    if ( range < minR ) range = minR;
    if ( range > maxR ) range = maxR;
    return range;
}

static int ypabact_LaserDamageInterval(const World::TWeapProto &wproto, bool playerControlled)
{
    int interval = playerControlled && wproto.laser_energy_tick_time_user > 0
                 ? wproto.laser_energy_tick_time_user
                 : wproto.laser_energy_tick_time;
    if ( interval <= 0 )
        interval = 250;
    if ( interval < 1 )
        interval = 1;
    return interval;
}

static float ypabact_LaserEnergyScale(const World::TWeapProto &wproto, NC_STACK_ypabact *target)
{
    if ( !target )
        return 1.0f;

    switch ( target->_bact_type )
    {
    case BACT_TYPES_BACT:
        return wproto.energy_heli;

    case BACT_TYPES_TANK:
    case BACT_TYPES_CAR:
        return wproto.energy_tank;

    case BACT_TYPES_FLYER:
    case BACT_TYPES_UFO:
        return wproto.energy_flyer;

    case BACT_TYPES_ROBO:
        return wproto.energy_robo;

    default:
        return 1.0f;
    }
}

static int ypabact_LaserTickDamage(const World::TWeapProto &wproto, NC_STACK_ypabact *target,
                                   int connectedTicks, float damageMult = 1.0f)
{
    if ( !target )
        return 0;

    float baseEnergy = (float)wproto.energy;
    if ( wproto.laser_energy_increment_rate > 0.0f && connectedTicks > 0 )
        baseEnergy += (float)connectedTicks * wproto.laser_energy_increment_rate;
    if ( wproto.laser_max_energy > 0.0f && baseEnergy > wproto.laser_max_energy )
        baseEnergy = wproto.laser_max_energy;
    if ( baseEnergy <= 0.0f )
        return 0;

    if ( damageMult <= 0.0f )
        damageMult = 1.0f;
    baseEnergy *= damageMult;

    float damage = baseEnergy * ypabact_LaserEnergyScale(wproto, target);
    if ( damage <= 0.0f )
        return 0;

    float shield = target->GetEffectiveShield();
    if ( shield >= 100.0f )
        return 0;

    float divisor = ( target->getBACT_inputting() || target->getBACT_viewer() ) ? 250.0f : 100.0f;
    int tickDamage = (int)ceil(damage * (100.0f - shield) / divisor);
    return tickDamage > 0 ? tickDamage : 0;
}

static bool ypabact_CanApplyLaserDamage(NC_STACK_ypabact *shooter)
{
    if ( !shooter || !shooter->getBACT_pWorld() )
        return false;

    NC_STACK_ypaworld *world = shooter->getBACT_pWorld();
    NC_STACK_ypabact *userHost = world->getYW_userHostStation();
    return !world->_isNetGame || (userHost && userHost->_owner == shooter->_owner);
}

static void ypabact_ApplyLaserUnitTick(NC_STACK_ypabact *shooter, World::TWeapProto &wproto,
                                       NC_STACK_ypabact *target,
                                       NC_STACK_ypabact::TLaserBeamRuntime &beam,
                                       bool playerControlled, float damageMult)
{
    if ( !shooter || !target )
        return;

    if ( beam.next_damage_time > 0 && shooter->_clock < beam.next_damage_time )
        return;

    int applyNow = ypabact_LaserTickDamage(wproto, target, beam.energy_ticks, damageMult);

    if ( applyNow > 0 && ypabact_CanApplyLaserDamage(shooter) )
    {
        bact_arg84 dmg;
        dmg.energy = -applyNow;
        dmg.unit = shooter;
        target->ModifyEnergy(&dmg);

        if ( wproto.debuff.allow && target->_energy > 0 && target->_status != BACT_STATUS_DEAD )
            target->ApplyWeaponDebuff(wproto.debuff, shooter);
    }

    beam.energy_ticks++;
    beam.next_damage_time = shooter->_clock + ypabact_LaserDamageInterval(wproto, playerControlled);
}

struct TLaserWorldHit
{
    Common::Point cellId;
    int bldX = 0;
    int bldY = 0;
    vec3d damagePos;
};

struct TLaserUnitHit
{
    NC_STACK_ypabact *target = NULL;
    vec3d hitPoint;
    float along = 0.0f;
};

static bool ypabact_LaserGetSectorHit(NC_STACK_ypaworld *world, const vec3d &pos, TLaserWorldHit *outHit)
{
    if ( !world )
        return false;

    Common::Point sec = World::PositionToSectorID(pos);
    if ( !world->IsSector(sec) )
        return false;

    cellArea &cell = world->SectorAt(sec);

    if ( !cell.IsGamePlaySector() || cell.PurposeType == cellArea::PT_CONSTRUCTING )
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

    if ( outHit )
    {
        outHit->cellId = sec;
        outHit->bldX = outX;
        outHit->bldY = outY;
        outHit->damagePos = pos;
    }

    return true;
}

static int32_t ypabact_LaserSectorTargetId(const TLaserWorldHit &hit)
{
    int slot = hit.bldY * 3 + hit.bldX;
    int32_t id = (int32_t)((hit.cellId.y * 1024 + hit.cellId.x) * 9 + slot + 1);
    return id > 0 ? -id : -1;
}

static int ypabact_LaserTickSectorEnergy(const World::TWeapProto &wproto, int connectedTicks)
{
    float baseEnergy = (float)wproto.energy;
    if ( wproto.laser_energy_increment_rate > 0.0f && connectedTicks > 0 )
        baseEnergy += (float)connectedTicks * wproto.laser_energy_increment_rate;
    if ( wproto.laser_max_energy > 0.0f && baseEnergy > wproto.laser_max_energy )
        baseEnergy = wproto.laser_max_energy;

    // Keep the same public/internal energy scale as normal weapons and laser unit
    // damage: energy = 1000 means "10" in script/HUD terms. The sector/building
    // path applies its own vanilla conversion inside ypaworld_func129().
    return baseEnergy > 0.0f ? (int)ceil(baseEnergy) : 0;
}

// Hitscan along the forward beam: returns the nearest valid damage target whose
// body the beam passes through, within range. This is the final damage trace, so
// it intentionally includes allies/friendly fire; AI target selection is filtered
// separately by ypabact_LaserAutoTarget().
static bool ypabact_LaserHitscan(NC_STACK_ypabact *shooter, const World::TWeapProto &wproto,
                                 const vec3d &origin, const vec3d &dir, float range,
                                 TLaserUnitHit *outHit)
{
    if ( outHit )
        *outHit = TLaserUnitHit();

    NC_STACK_ypaworld *world = shooter->getBACT_pWorld();
    if ( !world )
        return false;

    // Scan the whole square of sectors covering the beam, so off-axis bodies are found.
    int sectorRadius = (int)(range / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(origin);

    NC_STACK_ypabact *best = NULL;
    float bestAlong = range + 1.0f;

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);
            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            for (NC_STACK_ypabact *bct : cell.unitsList)
            {
                if ( !ypabact_IsLaserDamageTarget(shooter, bct) )
                    continue;

                vec3d to = bct->_position - origin;
                float along = to.dot(dir);
                if ( along <= 0.0f || along > range )
                    continue;

                vec3d closest = origin + dir * along;
                float perp = (bct->_position - closest).length();

                // Clean hit thickness: unit body radius + weapon radius only. F10
                // debug displays the weapon radius, so do not add hidden distance
                // based tolerance here.
                float weaponRadius = wproto.radius > 0.0f ? wproto.radius : 1.0f;
                float hitRadius = bct->_radius + weaponRadius;

                if ( perp > hitRadius )
                    continue;

                if ( along < bestAlong )
                {
                    bestAlong = along;
                    best = bct;
                }
            }
        }
    }

    if ( !best )
        return false;

    if ( outHit )
    {
        outHit->target = best;
        outHit->hitPoint = best->_position;
        outHit->along = bestAlong;
    }

    return true;
}

// AI auto-lock: nearest enemy in a generous forward arc within range. Used for AI-driven
// lasers so they reliably connect even if the body is not perfectly aligned; the beam is
// then bent toward the returned target. Player fire keeps the requested firing path and
// falls back to forward hitscan when no explicit target was provided.
static NC_STACK_ypabact *ypabact_LaserAutoTarget(NC_STACK_ypabact *shooter, const vec3d &origin,
                                                 const vec3d &dir, float range)
{
    NC_STACK_ypaworld *world = shooter->getBACT_pWorld();
    if ( !world )
        return NULL;

    int sectorRadius = (int)(range / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(origin);

    NC_STACK_ypabact *best = NULL;
    float bestDist = range + 1.0f;

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);
            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            for (NC_STACK_ypabact *bct : cell.unitsList)
            {
                if ( !ypabact_IsLaserAimTarget(shooter, bct) )
                    continue;

                vec3d to = bct->_position - origin;
                float dist = to.length();
                if ( dist < 0.001f || dist > range )
                    continue;
                if ( to.dot(dir) / dist < 0.4f )   // ~66 deg forward arc
                    continue;

                if ( dist < bestDist )
                {
                    bestDist = dist;
                    best = bct;
                }
            }
        }
    }

    return best;
}

static bool ypabact_LaserTargetInList(NC_STACK_ypabact *target, const std::vector<NC_STACK_ypabact *> &targets)
{
    return std::find(targets.begin(), targets.end(), target) != targets.end();
}

static bool ypabact_IsLaserFriendlyToShooter(NC_STACK_ypabact *shooter, NC_STACK_ypabact *unit)
{
    return shooter && unit && unit->_owner != World::OWNER_0 && unit->_owner == shooter->_owner;
}

static bool ypabact_IsLaserEnemyToShooter(NC_STACK_ypabact *shooter, NC_STACK_ypabact *unit)
{
    return shooter && unit && unit->_owner != World::OWNER_0 && unit->_owner != shooter->_owner;
}

// Shared owner/validity filter for laser secondary targets. Used both for direct
// multi-target beams and for chain jumps (identical logic); "friendly" selects
// same-owner vs enemy-owner targets.
static bool ypabact_IsLaserSecondaryTargetCandidate(NC_STACK_ypabact *shooter, NC_STACK_ypabact *unit,
                                                    bool friendly)
{
    if ( !ypabact_IsLaserDamageTarget(shooter, unit) )
        return false;

    if ( !shooter || unit->_owner == World::OWNER_0 )
        return false;

    if ( friendly )
        return ypabact_IsLaserFriendlyToShooter(shooter, unit);

    return ypabact_IsLaserEnemyToShooter(shooter, unit);
}

static NC_STACK_ypabact *ypabact_FindNearestLaserMultiTarget(NC_STACK_ypabact *shooter, const vec3d &origin,
                                                            const vec3d &aimDir, float range,
                                                            bool friendlyTargets,
                                                            const std::vector<NC_STACK_ypabact *> &excluded)
{
    if ( !shooter || !shooter->getBACT_pWorld() || range <= 0.0f )
        return NULL;

    vec3d forward = aimDir;
    bool useForwardFilter = forward.normalise() >= 0.001f;

    NC_STACK_ypaworld *world = shooter->getBACT_pWorld();
    int sectorRadius = (int)(range / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(origin);

    NC_STACK_ypabact *best = NULL;
    float bestDist = range + 1.0f;

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);
            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            for (NC_STACK_ypabact *bct : cell.unitsList)
            {
                if ( !ypabact_IsLaserSecondaryTargetCandidate(shooter, bct, friendlyTargets) )
                    continue;
                if ( ypabact_LaserTargetInList(bct, excluded) )
                    continue;

                vec3d to = bct->_position - origin;
                float dist = to.length();
                if ( dist < 0.001f || dist > range )
                    continue;
                if ( useForwardFilter && to.dot(forward) / dist < 0.4f )
                    continue;

                if ( dist < bestDist )
                {
                    bestDist = dist;
                    best = bct;
                }
            }
        }
    }

    return best;
}

static NC_STACK_ypabact *ypabact_FindNearestLaserChainUnit(NC_STACK_ypabact *shooter, const vec3d &origin,
                                                          float range, bool friendlyChain,
                                                          const std::vector<NC_STACK_ypabact *> &excluded)
{
    if ( !shooter || !shooter->getBACT_pWorld() || range <= 0.0f )
        return NULL;

    NC_STACK_ypaworld *world = shooter->getBACT_pWorld();
    int sectorRadius = (int)(range / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(origin);

    NC_STACK_ypabact *best = NULL;
    float bestDist = range + 1.0f;

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);
            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            for (NC_STACK_ypabact *bct : cell.unitsList)
            {
                if ( !ypabact_IsLaserSecondaryTargetCandidate(shooter, bct, friendlyChain) )
                    continue;
                if ( ypabact_LaserTargetInList(bct, excluded) )
                    continue;

                vec3d to = bct->_position - origin;
                float dist = to.length();
                if ( dist < 0.001f || dist > range )
                    continue;

                if ( dist < bestDist )
                {
                    bestDist = dist;
                    best = bct;
                }
            }
        }
    }

    return best;
}

static void ypabact_AddLaserMultiTargetRequests(NC_STACK_ypabact *shooter, const World::TWeapProto &wproto,
                                                std::vector<NC_STACK_ypabact::TLaserBeamRequest> *requests,
                                                float range, bool friendlyTargets, bool playerControlled)
{
    if ( !shooter || !requests || requests->empty() || wproto.laser_multi_target <= 1 )
        return;
    if ( friendlyTargets && !playerControlled )
        return;

    size_t originalRequestCount = requests->size();
    if ( originalRequestCount >= (size_t)wproto.laser_multi_target )
        return;

    std::vector<NC_STACK_ypabact *> selectedTargets;
    selectedTargets.reserve((size_t)wproto.laser_multi_target);

    for (const NC_STACK_ypabact::TLaserBeamRequest &request : *requests)
    {
        if ( request.target && ypabact_IsLaserSecondaryTargetCandidate(shooter, request.target, friendlyTargets) &&
             !ypabact_LaserTargetInList(request.target, selectedTargets) )
            selectedTargets.push_back(request.target);
    }

    while ( requests->size() < (size_t)wproto.laser_multi_target )
    {
        size_t sourceIndex = (requests->size() - originalRequestCount) % originalRequestCount;
        const NC_STACK_ypabact::TLaserBeamRequest &source = requests->at(sourceIndex);
        vec3d primaryAim = requests->front().dir;
        if ( primaryAim.normalise() < 0.001f )
            primaryAim = shooter->_rotation.AxisZ();

        NC_STACK_ypabact *target = ypabact_FindNearestLaserMultiTarget(shooter, source.start, primaryAim,
                                                                       range, friendlyTargets, selectedTargets);
        if ( !target )
            break;

        NC_STACK_ypabact::TLaserBeamRequest request;
        request.target = target;
        request.start = source.start;
        request.dir = target->_position - request.start;
        if ( request.dir.normalise() < 0.001f )
            request.dir = source.dir;
        if ( request.dir.normalise() < 0.001f )
            request.dir = shooter->_rotation.AxisZ();

        requests->push_back(request);
        selectedTargets.push_back(target);
    }
}

static void ypabact_ApplyLaserLoopNormalFX(TSoundSource &snd, World::TWeapProto &wproto)
{
    World::TVhclSound &normalFx = wproto.sndFXes[World::TWeapProto::SND_NORMAL];

    if ( normalFx.sndPrm.slot )
    {
        snd.PPFx = &normalFx.sndPrm;
        snd.SetPFx(true);
    }
    else
    {
        snd.PPFx = NULL;
        snd.SetPFx(false);
    }

    if ( normalFx.sndPrm_shk.slot )
    {
        snd.PShkFx = &normalFx.sndPrm_shk;
        snd.SetShk(true);
    }
    else
    {
        snd.PShkFx = NULL;
        snd.SetShk(false);
    }
}

static void ypabact_StoreHUDLaserMultiLockTargets(NC_STACK_ypabact *shooter,
                                                  const std::vector<NC_STACK_ypabact::TLaserBeamRequest> &requests)
{
    if ( !shooter || !shooter->getBACT_pWorld() || !(shooter->_oflags & BACT_OFLAG_USERINPT) )
        return;

    std::vector<NC_STACK_ypabact *> targets;
    targets.reserve(requests.size());

    for (const NC_STACK_ypabact::TLaserBeamRequest &request : requests)
    {
        if ( request.target && ypabact_IsLaserAimTarget(shooter, request.target) &&
             !ypabact_LaserTargetInList(request.target, targets) )
            targets.push_back(request.target);
    }

    ypabact_StoreHUDMissileMultiLockTargets(shooter, targets);
}

static vec3d ypabact_LaserSourceOrigin(NC_STACK_ypabact *bact);

static void ypabact_UpdateHUDLaserMultiLockTargets(NC_STACK_ypabact *shooter, const bact_arg79 *arg,
                                                   const World::TWeapProto &wproto)
{
    if ( !shooter || !arg || !shooter->getBACT_pWorld() || !(shooter->_oflags & BACT_OFLAG_USERINPT) )
        return;

    if ( !wproto.IsLaser() || wproto.laser_multi_target <= 1 )
    {
        shooter->getBACT_pWorld()->_hudMissileMultiLockTargets.clear();
        return;
    }

    if ( arg->tgType != BACT_TGT_TYPE_UNIT || !ypabact_IsLaserAimTarget(shooter, arg->target.pbact) )
    {
        shooter->getBACT_pWorld()->_hudMissileMultiLockTargets.clear();
        return;
    }

    std::vector<NC_STACK_ypabact::TLaserBeamRequest> requests;
    requests.resize(1);
    requests[0].target = arg->target.pbact;
    requests[0].start = ypabact_LaserSourceOrigin(shooter);
    requests[0].dir = arg->target.pbact->_position - requests[0].start;

    if ( requests[0].dir.normalise() < 0.001f )
        requests[0].dir = shooter->_rotation.AxisZ();

    ypabact_AddLaserMultiTargetRequests(shooter, wproto, &requests, ypabact_LaserRange(wproto), false, true);
    ypabact_StoreHUDLaserMultiLockTargets(shooter, requests);
}

static NC_STACK_ypabact *ypabact_FindLaserChainTarget(NC_STACK_ypabact *shooter, NC_STACK_ypabact *from,
                                                      float radius, bool friendlyChain,
                                                      const std::vector<NC_STACK_ypabact *> &hitHistory)
{
    if ( !from || radius <= 0.0f )
        return NULL;

    return ypabact_FindNearestLaserChainUnit(shooter, from->_position, radius, friendlyChain, hitHistory);
}

static vec3d ypabact_LaserSourceOrigin(NC_STACK_ypabact *bact)
{
    if ( !bact )
        return vec3d(0.0, 0.0, 0.0);

    vec3d localOffset = bact->_fire_pos;

    // First-person/viewer fire originates from the actual view point plus the single
    // configured fire_x/fire_y/fire_z muzzle offset. num_weapons never expands this
    // into multiple laser sources.
    if ( bact->getBACT_viewer() )
        return bact->_position + bact->_rotation.Transpose().Transform(bact->_viewer_position + localOffset);

    return bact->_position + bact->_rotation.Transpose().Transform(localOffset);
}

static bool ypabact_LaserWorldHit(NC_STACK_ypabact *shooter, const vec3d &origin,
                                      const vec3d &dir, float range, vec3d *outHitPoint)
{
    if ( !shooter || !shooter->getBACT_pWorld() || range <= 1.0f )
        return false;

    vec3d rayDir = dir;
    if ( rayDir.normalise() < 0.001f )
        return false;

    // ypaworld_func136() is reliable for short ray spans, but it only checks a tiny
    // collision set derived from the segment start/end grid cells. A full laser beam
    // can be several sectors long, so one single long ray may miss terrain/buildings
    // for many frames and make vp_megadeth appear only sporadically. Walk the beam
    // in short segments and return the first real world contact.
    const float maxSegmentLen = 240.0f;
    float travelled = 0.0f;

    while ( travelled < range )
    {
        float segmentLen = range - travelled;
        if ( segmentLen > maxSegmentLen )
            segmentLen = maxSegmentLen;

        ypaworld_arg136 ray;
        ray.stPos = origin + rayDir * travelled;
        ray.vect = rayDir * segmentLen;
        ray.flags = 0;

        shooter->getBACT_pWorld()->ypaworld_func136(&ray);

        if ( ray.isect )
        {
            vec3d toHit = ray.isectPos - origin;
            float along = toHit.dot(rayDir);

            // Ignore bogus/backward hits and keep the contact inside the requested beam range.
            if ( along >= 0.0f && along <= range + 1.0f )
            {
                if ( outHitPoint )
                    *outHitPoint = ray.isectPos;

                return true;
            }
        }

        travelled += segmentLen;
    }

    return false;
}

static mat3x3 ypabact_LaserRotationFromDir(const vec3d &beamDir, const mat3x3 &fallback)
{
    vec3d z = beamDir;
    if ( z.normalise() < 0.001 )
        return fallback;

    vec3d x = fallback.AxisX();
    x -= z * x.dot(z);

    if ( x.normalise() < 0.001 )
    {
        x = (fabs(z.y) < 0.9) ? vec3d(0.0, 1.0, 0.0) : vec3d(1.0, 0.0, 0.0);
        x = x * z;
        if ( x.normalise() < 0.001 )
            return fallback;
    }

    vec3d y = z * x;
    if ( y.normalise() < 0.001 )
        return fallback;

    mat3x3 rot;
    rot.SetX(x);
    rot.SetY(y);
    rot.SetZ(z);
    return rot;
}

static float ypabact_LaserClampVPSpacing(float spacing)
{
    if ( spacing <= 0.0f )
        spacing = 40.0f;
    if ( spacing < 20.0f )
        spacing = 20.0f;
    if ( spacing > 500.0f )
        spacing = 500.0f;
    return spacing;
}

static float ypabact_LaserVisualScale(const World::TWeapProto &wproto)
{
    if ( wproto.visual_scale_mode == World::VISUAL_SCALE_AXIS )
        return 1.0f;

    return wproto.visual_scale > 0.0f ? wproto.visual_scale : 1.0f;
}

static vec3d ypabact_LaserVisualAxisScale(const World::TWeapProto &wproto)
{
    if ( wproto.visual_scale_mode != World::VISUAL_SCALE_AXIS )
        return vec3d(1.0, 1.0, 1.0);

    return vec3d(wproto.visual_scale_axis.x > 0.0f ? wproto.visual_scale_axis.x : 1.0f,
                 wproto.visual_scale_axis.y > 0.0f ? wproto.visual_scale_axis.y : 1.0f,
                 wproto.visual_scale_axis.z > 0.0f ? wproto.visual_scale_axis.z : 1.0f);
}

static void ypabact_SpawnLaserBeamVPs(NC_STACK_ypabact *bact, const World::TWeapProto &wproto,
                                      const vec3d &beamStart, const vec3d &beamEnd)
{
    if ( !bact || wproto.vp_normal <= 0 )
        return;

    NC_STACK_ypaworld *world = bact->getBACT_pWorld();
    if ( !world )
        return;

    vec3d span = beamEnd - beamStart;
    float len = span.length();
    if ( len < 1.0f )
        return;

    vec3d dir = span / len;
    mat3x3 rot = ypabact_LaserRotationFromDir(dir, bact->_rotation);

    float scale = ypabact_LaserVisualScale(wproto);
    vec3d axisScale = ypabact_LaserVisualAxisScale(wproto);

    // Visual-only density control: damage timing stays controlled only by
    // laser_energy_tick_time, while radius remains the gameplay hit thickness.
    float spacing = ypabact_LaserClampVPSpacing(wproto.laser_vp_spacing);

    vec3d visualStart = beamStart;
    if ( bact->getBACT_viewer() || bact->getBACT_inputting() )
    {
        float lead = spacing * 0.5f;
        if ( lead < 16.0f )
            lead = 16.0f;
        if ( lead > len * 0.25f )
            lead = len * 0.25f;

        visualStart += dir * lead;
        span = beamEnd - visualStart;
        len = span.length();
        if ( len < 1.0f )
            return;
    }

    int count = (int)(len / spacing) + 2;
    if ( count < 2 ) count = 2;
    if ( count > 320 ) count = 320;

    for (int i = 0; i < count; i++)
    {
        float t = ((float)i + 0.5f) / (float)count;
        vec3d pos = visualStart + span * t;
        // OpenUA custom: the laser beam body uses vp_normal, so honour the weapon's
        // visual_tint here (same eligible prototype as projectiles). Impact/launch FX
        // below deliberately stay untinted.
        world->SpawnTransientVP(wproto.vp_normal, pos, rot, 45, scale, wproto.visual_tint, axisScale);
    }
}

void NC_STACK_ypabact::RequestLaserFire(int weaponId, bact_arg79 *arg)
{
    _laser_weapon = weaponId;
    _laser_fire_request = true;

    TLaserBeamRequest request;
    request.target = (arg->tgType == BACT_TGT_TYPE_UNIT) ? arg->target.pbact : NULL;

    // Stable laser source: model=laser ignores num_weapons as a beam count, but
    // still honours fire_x/fire_y/fire_z as one muzzle/source offset.
    request.start = ypabact_LaserSourceOrigin(this);

    request.dir = arg->direction;

    if ( request.dir.normalise() < 0.001 )
    {
        if ( arg->tgType == BACT_TGT_TYPE_UNIT && arg->target.pbact )
            request.dir = arg->target.pbact->_position - request.start;
        else if ( arg->tgType == BACT_TGT_TYPE_CELL )
            request.dir = arg->tgt_pos - request.start;
        else if ( arg->tgType == BACT_TGT_TYPE_DRCT )
            request.dir = arg->tgt_pos;
    }

    if ( request.dir.normalise() < 0.001 )
        request.dir = _rotation.AxisZ();

    _laser_requests.push_back(request);

    // Compatibility mirror for older single-beam debug/UI code paths.
    _laser_target = request.target;
    _laser_request_start = request.start;
    _laser_request_dir = request.dir;
}

void NC_STACK_ypabact::StopLaser()
{
    // Disconnect: stop the loop sound (only if running), drop the beam and reset tick state.
    if ( !_laser_soundcarrier.Sounds.empty() && _laser_soundcarrier.Sounds[0].IsEnabled() )
        SFXEngine::SFXe.StopCarrier(&_laser_soundcarrier);

    _laser_active = false;
    _laser_fire_request = false;
    _laser_weapon = -1;
    _laser_energy_ticks = 0;
    _laser_target_gid = 0;
    _laser_target = NULL;
    _laser_request_start = vec3d(0.0, 0.0, 0.0);
    _laser_request_dir = vec3d(0.0, 0.0, 0.0);
    _laser_beam_start = vec3d(0.0, 0.0, 0.0);
    _laser_beam_end = vec3d(0.0, 0.0, 0.0);
    _laser_next_damage_time = 0;
    _laser_next_fx_time = 0;
    _laser_next_beam_vp_time = 0;
    _laser_requests.clear();
    _laser_beams.clear();
}

void NC_STACK_ypabact::UpdateLaser(update_msg *arg)
{
    (void)arg;

    bool requested = _laser_fire_request;
    std::vector<TLaserBeamRequest> requests = _laser_requests;
    _laser_fire_request = false;
    _laser_target = NULL;
    _laser_requests.clear();

    // Not firing this frame, or weapon invalid => beam off.
    if ( !requested || requests.empty() ||
         !_world || _laser_weapon < 0 || (size_t)_laser_weapon >= _world->GetWeaponsProtos().size() )
    {
        StopLaser();
        return;
    }

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(_laser_weapon);
    if ( !wproto.IsLaser() )
    {
        StopLaser();
        return;
    }

    float range = ypabact_LaserRange(wproto);
    bool playerControlled = getBACT_inputting() || getBACT_viewer();

    bool wasActive = _laser_active;
    size_t oldBeamCount = _laser_beams.size();
    _laser_active = true;
    if ( _laser_beams.size() < requests.size() )
        _laser_beams.resize(requests.size());
    size_t activeBeamCount = requests.size();
    NC_STACK_ypabact *primaryHitTarget = NULL;
    NC_STACK_ypabact *primaryChainGroupTarget = NULL;
    std::vector<NC_STACK_ypabact *> directHitTargets;
    directHitTargets.reserve(requests.size());

    bool spawnBeamVPs = (_laser_next_beam_vp_time <= 0 || _clock >= _laser_next_beam_vp_time);

    for (size_t i = 0; i < requests.size(); i++)
    {
        TLaserBeamRequest &request = requests[i];
        if ( _laser_beams.size() <= i )
            _laser_beams.resize(i + 1);
        TLaserBeamRuntime &beam = _laser_beams[i];
        bool beamWasActive = wasActive && i < oldBeamCount;

        // Forward aim direction. Its reach comes from the weapon's life_time.
        vec3d dir = request.dir;
        if ( dir.normalise() < 0.001f )
            dir = _rotation.AxisZ();

        // Aiming model:
        //   * Explicit vanilla target -> use it only to choose the beam direction.
        //   * Player free fire        -> keep requested/manual direction.
        //   * AI free fire            -> auto-pick an enemy and bend the beam toward it.
        // The final damage trace below is always run along the final direction and can
        // hit allies/friendly units if they physically stand in front of the aimed target.
        if ( request.target && ypabact_IsLaserDamageTarget(this, request.target) )
        {
            vec3d toT = request.target->_position - request.start;
            float l = toT.length();
            if ( l > 0.001f && l <= range )
                dir = toT / l;
        }
        else if ( !playerControlled )
        {
            NC_STACK_ypabact *aimTarget = ypabact_LaserAutoTarget(this, request.start, dir, range);
            if ( aimTarget )
            {
                vec3d toT = aimTarget->_position - request.start;
                float l = toT.length();
                if ( l > 0.001f )
                    dir = toT / l;
            }
        }

        TLaserUnitHit unitHit;
        bool hasUnitHit = ypabact_LaserHitscan(this, wproto, request.start, dir, range, &unitHit);

        vec3d worldHitPoint;
        bool worldHit = ypabact_LaserWorldHit(this, request.start, dir, range, &worldHitPoint);
        float worldAlong = range + 1.0f;
        if ( worldHit )
        {
            worldAlong = (worldHitPoint - request.start).dot(dir);
            if ( worldAlong < 0.0f )
                worldHit = false;
        }

        bool useWorldHit = worldHit && (!hasUnitHit || worldAlong <= unitHit.along);
        NC_STACK_ypabact *target = (!useWorldHit && hasUnitHit) ? unitHit.target : NULL;
        vec3d hitPoint = target ? unitHit.hitPoint : vec3d(0.0, 0.0, 0.0);

        TLaserWorldHit sectorHit;
        bool hasSectorHit = false;

        beam.start = request.start;
        if ( target )
            beam.end = hitPoint;
        else if ( useWorldHit )
            beam.end = worldHitPoint;
        else
            beam.end = request.start + dir * range;

        if ( i == 0 )
        {
            NC_STACK_ypabact *requestedGroupTarget =
                (request.target && request.target->_owner != World::OWNER_0 &&
                 ypabact_IsLaserDamageTarget(this, request.target))
                    ? request.target
                    : NULL;

            _laser_beam_start = beam.start;
            _laser_beam_end = beam.end;
            _laser_request_start = request.start;
            _laser_request_dir = dir;
            _laser_target = target;
            primaryHitTarget = target;
            primaryChainGroupTarget = requestedGroupTarget ? requestedGroupTarget : target;
            request.target = target;

            if ( primaryChainGroupTarget && primaryChainGroupTarget->_owner != World::OWNER_0 )
            {
                bool friendlyMultiTargets = ypabact_IsLaserFriendlyToShooter(this, primaryChainGroupTarget);
                ypabact_AddLaserMultiTargetRequests(this, wproto, &requests, range,
                                                    friendlyMultiTargets, playerControlled);
            }
            ypabact_StoreHUDLaserMultiLockTargets(this, requests);
        }

        if ( useWorldHit )
        {
            // Step a little beyond the rendered contact, matching the direct-hit missile
            // path. This reliably selects the sector/building slot under the collision point.
            hasSectorHit = ypabact_LaserGetSectorHit(_world, worldHitPoint + dir * 5.0f, &sectorHit);
            if ( !hasSectorHit )
                hasSectorHit = ypabact_LaserGetSectorHit(_world, worldHitPoint, &sectorHit);
        }

        if ( !beamWasActive && wproto.vp_launch > 0 )
            _world->SpawnTransientVP(wproto.vp_launch, beam.start, ypabact_LaserRotationFromDir(dir, _rotation), 90);

        if ( spawnBeamVPs )
            ypabact_SpawnLaserBeamVPs(this, wproto, beam.start, beam.end);

        if ( target )
        {
            // Fresh contact or target switch => reset tick timers and optional ramp.
            if ( !beamWasActive || target->_gid != beam.target_gid )
            {
                beam.energy_ticks = 0;
                beam.next_damage_time = 0;
                beam.next_fx_time = 0;
            }
            beam.target_gid = target->_gid;

            if ( target->_owner != World::OWNER_0 && ypabact_IsLaserDamageTarget(this, target) &&
                 !ypabact_LaserTargetInList(target, directHitTargets) )
                directHitTargets.push_back(target);

            ypabact_ApplyLaserUnitTick(this, wproto, target, beam, playerControlled, 1.0f);

            // ---- Throttled impact/contact FX ----
            if ( _clock >= beam.next_fx_time )
            {
                int impactVP = wproto.vp_dead > 0 ? wproto.vp_dead : wproto.vp_megadeth;
                if ( impactVP > 0 )
                    _world->SpawnTransientVP(impactVP, beam.end, ypabact_LaserRotationFromDir(dir, _rotation), 90);
                beam.next_fx_time = _clock + 160;
            }
        }
        else
        {
            if ( hasSectorHit )
            {
                int32_t sectorTargetId = ypabact_LaserSectorTargetId(sectorHit);
                if ( !beamWasActive || sectorTargetId != beam.target_gid )
                {
                    beam.energy_ticks = 0;
                    beam.next_damage_time = 0;
                    beam.next_fx_time = 0;
                }
                beam.target_gid = sectorTargetId;

                if ( beam.next_damage_time <= 0 || _clock >= beam.next_damage_time )
                {
                    int applyNow = ypabact_LaserTickSectorEnergy(wproto, beam.energy_ticks);

                    NC_STACK_ypabact *userHost = _world->getYW_userHostStation();
                    bool canApplyDamage = !_world->_isNetGame || (userHost && userHost->_owner == _owner);

                    if ( applyNow > 0 && canApplyDamage )
                    {
                        yw_arg129 dmg;
                        dmg.field_0 = 0;
                        dmg.pos = sectorHit.damagePos;
                        dmg.field_10 = applyNow;
                        dmg.unit = this;
                        ChangeSectorEnergy(&dmg);
                    }

                    beam.energy_ticks++;
                    beam.next_damage_time = _clock + ypabact_LaserDamageInterval(wproto, playerControlled);
                }
            }
            else
            {
                // No damage contact: the beam still shows forward, but the ramp resets.
                beam.energy_ticks = 0;
                beam.target_gid = 0;
                beam.next_damage_time = 0;
            }

            // Terrain/building/world contact FX. This is the laser equivalent of a
            // projectile megadeth impact: firing into the ground must show vp_megadeth
            // even when no unit was hit. If vp_megadeth is not configured, fall back to
            // vp_dead so old test weapons still show something instead of nothing.
            if ( useWorldHit && _clock >= beam.next_fx_time )
            {
                int impactVP = wproto.vp_megadeth > 0 ? wproto.vp_megadeth : wproto.vp_dead;
                if ( impactVP > 0 )
                    _world->SpawnTransientVP(impactVP, beam.end, ypabact_LaserRotationFromDir(dir, _rotation), 90);
                beam.next_fx_time = _clock + 160;
            }
        }
    }

    activeBeamCount = requests.size();

    if ( wproto.laser_chain_allow && wproto.laser_chain_max_jumps > 0 &&
         wproto.laser_chain_radius > 0.0f && primaryHitTarget &&
         primaryHitTarget->_owner != World::OWNER_0 &&
         ypabact_IsLaserDamageTarget(this, primaryHitTarget) )
    {
        NC_STACK_ypabact *chainGroupTarget = primaryChainGroupTarget ? primaryChainGroupTarget : primaryHitTarget;
        bool friendlyChain = ypabact_IsLaserFriendlyToShooter(this, chainGroupTarget);
        bool canRunChain = (!friendlyChain || playerControlled) &&
                           ypabact_IsLaserSecondaryTargetCandidate(this, primaryHitTarget, friendlyChain);

        if ( canRunChain )
        {
            std::vector<NC_STACK_ypabact *> chainHits;
            chainHits.reserve(directHitTargets.size() + (size_t)wproto.laser_chain_max_jumps + 1);
            chainHits = directHitTargets;
            if ( !ypabact_LaserTargetInList(primaryHitTarget, chainHits) )
                chainHits.push_back(primaryHitTarget);

            NC_STACK_ypabact *from = primaryHitTarget;
            float chainDamageMult = 1.0f;
            float perJumpMult = wproto.laser_chain_damage_mult > 0.0f ? wproto.laser_chain_damage_mult : 1.0f;

            for (int jump = 0; jump < wproto.laser_chain_max_jumps; jump++)
            {
                NC_STACK_ypabact *target = ypabact_FindLaserChainTarget(this, from, wproto.laser_chain_radius,
                                                                        friendlyChain, chainHits);
                if ( !target )
                    break;

                size_t beamIndex = activeBeamCount++;
                if ( _laser_beams.size() <= beamIndex )
                    _laser_beams.resize(beamIndex + 1);

                TLaserBeamRuntime &beam = _laser_beams[beamIndex];
                bool beamWasActive = wasActive && beamIndex < oldBeamCount;

                beam.start = from->_position;
                beam.end = target->_position;

                vec3d dir = beam.end - beam.start;
                if ( dir.normalise() < 0.001f )
                    dir = _rotation.AxisZ();

                if ( !beamWasActive || target->_gid != beam.target_gid )
                {
                    beam.energy_ticks = 0;
                    beam.next_damage_time = 0;
                    beam.next_fx_time = 0;
                }
                beam.target_gid = target->_gid;

                chainDamageMult *= perJumpMult;

                if ( spawnBeamVPs )
                    ypabact_SpawnLaserBeamVPs(this, wproto, beam.start, beam.end);

                ypabact_ApplyLaserUnitTick(this, wproto, target, beam, playerControlled, chainDamageMult);

                if ( _clock >= beam.next_fx_time )
                {
                    int impactVP = wproto.vp_dead > 0 ? wproto.vp_dead : wproto.vp_megadeth;
                    if ( impactVP > 0 )
                        _world->SpawnTransientVP(impactVP, beam.end, ypabact_LaserRotationFromDir(dir, _rotation), 90);
                    beam.next_fx_time = _clock + 160;
                }

                chainHits.push_back(target);
                from = target;
            }
        }
    }

    if ( _laser_beams.size() > activeBeamCount )
        _laser_beams.resize(activeBeamCount);

    if ( spawnBeamVPs )
    {
        // Refresh as fast as the engine clock allows. Keep this at 1ms: 0 would be
        // treated as invalid in several timing paths and would risk duplicate spam in
        // the same update tick.
        _laser_next_beam_vp_time = _clock + 1;
    }

    if ( !_laser_beams.empty() )
    {
        _laser_target_gid = _laser_beams[0].target_gid;
        _laser_energy_ticks = _laser_beams[0].energy_ticks;
        _laser_next_damage_time = _laser_beams[0].next_damage_time;
        _laser_next_fx_time = _laser_beams[0].next_fx_time;
    }

    // ---- Loop sound: laser loops by default while the trigger is held ----
    wproto.snd_loop.LoadSamples(); // idempotent: real load happens only once

    TSampleData *psample = wproto.snd_loop.MainSample.Sample
                         ? wproto.snd_loop.MainSample.Sample->GetSampleData()
                         : NULL;

    if ( psample )
    {
        if ( _laser_soundcarrier.Sounds.empty() )
            _laser_soundcarrier.Resize(1);

        TSoundSource &snd = _laser_soundcarrier.Sounds[0];
        snd.PSample = psample;
        snd.SampleVariants.clear();
        snd.SampleVariants.push_back(psample);
        snd.Volume = wproto.snd_loop.volume ? wproto.snd_loop.volume : 120;
        snd.Pitch = wproto.snd_loop.pitch;
        snd.PriorityBias = 0;
        snd.SetLoop(true);
        snd.SetFragmented(false);
        ypabact_ApplyLaserLoopNormalFX(snd, wproto);

        _laser_soundcarrier.Position = _laser_beam_start;
        _laser_soundcarrier.Vector = _fly_dir * _fly_dir_length;

        if ( !snd.IsEnabled() )
            SFXEngine::SFXe.startSound(&_laser_soundcarrier, 0);

        SFXEngine::SFXe.UpdateSoundCarrier(&_laser_soundcarrier);
    }
}

static float ypabact_VerticalLaserFireRadius(const World::TWeapProto &wproto)
{
    return wproto.vertical_laser_fire_radius > 0.0f ? wproto.vertical_laser_fire_radius : 300.0f;
}

static vec3d ypabact_VerticalLaserSourceOrigin(NC_STACK_ypabact *bact, const bact_arg79 *arg)
{
    if ( !bact )
        return vec3d(0.0, 0.0, 0.0);

    vec3d localOffset = arg ? arg->start_point : bact->_fire_pos;
    return bact->_position + bact->_rotation.Transpose().Transform(localOffset);
}

static bool ypabact_IsVerticalLaserMultiCandidate(NC_STACK_ypabact *shooter, NC_STACK_ypabact *unit,
                                                  bool friendlyTargets, const vec3d &origin,
                                                  float range, float fireRadius,
                                                  const std::vector<NC_STACK_ypabact *> &excluded)
{
    if ( !ypabact_IsLaserSecondaryTargetCandidate(shooter, unit, friendlyTargets) )
        return false;
    if ( ypabact_LaserTargetInList(unit, excluded) )
        return false;

    vec3d to = unit->_position - origin;
    if ( to.y < 0.0f || to.y > range )
        return false;

    return to.XZ().length() <= fireRadius;
}

static NC_STACK_ypabact *ypabact_FindNearestVerticalLaserTarget(NC_STACK_ypabact *shooter,
                                                               const vec3d &origin,
                                                               float range, float fireRadius,
                                                               bool friendlyTargets,
                                                               const std::vector<NC_STACK_ypabact *> &excluded)
{
    if ( !shooter || !shooter->getBACT_pWorld() || range <= 0.0f || fireRadius <= 0.0f )
        return NULL;

    NC_STACK_ypaworld *world = shooter->getBACT_pWorld();
    int sectorRadius = (int)(fireRadius / World::CVSectorLength) + 2;
    Common::Point center = World::PositionToSectorID(origin);

    NC_STACK_ypabact *best = NULL;
    float bestDist = fireRadius + 1.0f;

    for (int y = center.y - sectorRadius; y <= center.y + sectorRadius; y++)
    {
        for (int x = center.x - sectorRadius; x <= center.x + sectorRadius; x++)
        {
            Common::Point cellId(x, y);
            if ( !world->IsSector(cellId) )
                continue;

            cellArea &cell = world->SectorAt(cellId);

            for (NC_STACK_ypabact *bct : cell.unitsList)
            {
                if ( !ypabact_IsVerticalLaserMultiCandidate(shooter, bct, friendlyTargets,
                                                            origin, range, fireRadius, excluded) )
                    continue;

                float dist = (bct->_position.XZ() - origin.XZ()).length();
                if ( dist < bestDist )
                {
                    bestDist = dist;
                    best = bct;
                }
            }
        }
    }

    return best;
}

static NC_STACK_ypabact *ypabact_UpdateVerticalLaserBeam(NC_STACK_ypabact *shooter,
                                                        World::TWeapProto &wproto,
                                                        NC_STACK_ypabact::TLaserBeamRuntime &beam,
                                                        const vec3d &start, const vec3d &down,
                                                        float range, bool beamWasActive,
                                                        bool spawnBeamVPs, bool playerControlled,
                                                        float damageMult)
{
    NC_STACK_ypaworld *world = shooter ? shooter->getBACT_pWorld() : NULL;
    if ( !world )
        return NULL;

    TLaserUnitHit unitHit;
    bool hasUnitHit = ypabact_LaserHitscan(shooter, wproto, start, down, range, &unitHit);

    vec3d worldHitPoint;
    bool worldHit = ypabact_LaserWorldHit(shooter, start, down, range, &worldHitPoint);
    float worldAlong = range + 1.0f;
    if ( worldHit )
    {
        worldAlong = (worldHitPoint - start).dot(down);
        if ( worldAlong < 0.0f )
            worldHit = false;
    }

    bool useWorldHit = worldHit && (!hasUnitHit || worldAlong <= unitHit.along);
    NC_STACK_ypabact *target = (!useWorldHit && hasUnitHit) ? unitHit.target : NULL;

    beam.start = start;
    if ( target )
        beam.end = unitHit.hitPoint;
    else if ( useWorldHit )
        beam.end = worldHitPoint;
    else
        beam.end = start + down * range;

    if ( !beamWasActive && wproto.vp_launch > 0 )
        world->SpawnTransientVP(wproto.vp_launch, beam.start,
                                ypabact_LaserRotationFromDir(down, shooter->_rotation), 90);

    if ( spawnBeamVPs )
        ypabact_SpawnLaserBeamVPs(shooter, wproto, beam.start, beam.end);

    if ( target )
    {
        if ( !beamWasActive || target->_gid != beam.target_gid )
        {
            beam.energy_ticks = 0;
            beam.next_damage_time = 0;
            beam.next_fx_time = 0;
        }

        beam.target_gid = target->_gid;
        ypabact_ApplyLaserUnitTick(shooter, wproto, target, beam, playerControlled, damageMult);

        if ( shooter->_clock >= beam.next_fx_time )
        {
            int impactVP = wproto.vp_dead > 0 ? wproto.vp_dead : wproto.vp_megadeth;
            if ( impactVP > 0 )
                world->SpawnTransientVP(impactVP, beam.end,
                                        ypabact_LaserRotationFromDir(down, shooter->_rotation), 90);
            beam.next_fx_time = shooter->_clock + 160;
        }
    }
    else
    {
        TLaserWorldHit sectorHit;
        bool hasSectorHit = false;

        if ( useWorldHit )
        {
            hasSectorHit = ypabact_LaserGetSectorHit(world, worldHitPoint + down * 5.0f, &sectorHit);
            if ( !hasSectorHit )
                hasSectorHit = ypabact_LaserGetSectorHit(world, worldHitPoint, &sectorHit);
        }

        if ( hasSectorHit )
        {
            int32_t sectorTargetId = ypabact_LaserSectorTargetId(sectorHit);
            if ( !beamWasActive || sectorTargetId != beam.target_gid )
            {
                beam.energy_ticks = 0;
                beam.next_damage_time = 0;
                beam.next_fx_time = 0;
            }
            beam.target_gid = sectorTargetId;

            if ( beam.next_damage_time <= 0 || shooter->_clock >= beam.next_damage_time )
            {
                int applyNow = ypabact_LaserTickSectorEnergy(wproto, beam.energy_ticks);

                NC_STACK_ypabact *userHost = world->getYW_userHostStation();
                bool canApplyDamage = !world->_isNetGame ||
                                      (userHost && userHost->_owner == shooter->_owner);

                if ( applyNow > 0 && canApplyDamage )
                {
                    yw_arg129 dmg;
                    dmg.field_0 = 0;
                    dmg.pos = sectorHit.damagePos;
                    dmg.field_10 = applyNow;
                    dmg.unit = shooter;
                    shooter->ChangeSectorEnergy(&dmg);
                }

                beam.energy_ticks++;
                beam.next_damage_time = shooter->_clock + ypabact_LaserDamageInterval(wproto, playerControlled);
            }
        }
        else
        {
            beam.energy_ticks = 0;
            beam.target_gid = 0;
            beam.next_damage_time = 0;
        }

        if ( useWorldHit && shooter->_clock >= beam.next_fx_time )
        {
            int impactVP = wproto.vp_megadeth > 0 ? wproto.vp_megadeth : wproto.vp_dead;
            if ( impactVP > 0 )
                world->SpawnTransientVP(impactVP, beam.end,
                                        ypabact_LaserRotationFromDir(down, shooter->_rotation), 90);
            beam.next_fx_time = shooter->_clock + 160;
        }
    }

    return target;
}

void NC_STACK_ypabact::RequestVerticalLaserFire(int weaponId, bact_arg79 *arg)
{
    _vertical_laser_weapon = weaponId;
    _vertical_laser_fire_request = true;
    _vertical_laser_request_target = (arg && arg->tgType == BACT_TGT_TYPE_UNIT) ? arg->target.pbact : NULL;
    _vertical_laser_request_start = ypabact_VerticalLaserSourceOrigin(this, arg);
}

void NC_STACK_ypabact::StopVerticalLaser()
{
    if ( !_vertical_laser_soundcarrier.Sounds.empty() &&
         _vertical_laser_soundcarrier.Sounds[0].IsEnabled() )
    {
        SFXEngine::SFXe.StopCarrier(&_vertical_laser_soundcarrier);
    }

    _vertical_laser_active = false;
    _vertical_laser_fire_request = false;
    _vertical_laser_weapon = -1;
    _vertical_laser_request_target = NULL;
    _vertical_laser_request_start = vec3d(0.0, 0.0, 0.0);
    _vertical_laser_next_beam_vp_time = 0;
    _vertical_laser_beam = TLaserBeamRuntime();
    _vertical_laser_beams.clear();
}

void NC_STACK_ypabact::UpdateVerticalLaser(update_msg *arg)
{
    (void)arg;

    bool requested = _vertical_laser_fire_request;
    _vertical_laser_fire_request = false;

    if ( !requested || !_world || _vertical_laser_weapon < 0 ||
         (size_t)_vertical_laser_weapon >= _world->GetWeaponsProtos().size() )
    {
        StopVerticalLaser();
        return;
    }

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(_vertical_laser_weapon);
    if ( !wproto.IsVerticalLaser() )
    {
        StopVerticalLaser();
        return;
    }

    // UA world coordinates use +Y as "down" toward terrain/buildings.
    const vec3d down(0.0, 1.0, 0.0);
    float range = ypabact_LaserRange(wproto);
    float fireRadius = ypabact_VerticalLaserFireRadius(wproto);
    bool playerControlled = getBACT_inputting() || getBACT_viewer();
    bool wasActive = _vertical_laser_active;
    size_t oldBeamCount = _vertical_laser_beams.size();
    bool spawnBeamVPs = (_vertical_laser_next_beam_vp_time <= 0 ||
                         _clock >= _vertical_laser_next_beam_vp_time);

    _vertical_laser_active = true;

    if ( _vertical_laser_beams.empty() )
        _vertical_laser_beams.resize(1);

    size_t activeBeamCount = 0;
    std::vector<NC_STACK_ypabact *> directHitTargets;
    directHitTargets.reserve((size_t)wproto.laser_multi_target + 1);

    NC_STACK_ypabact *primaryHitTarget =
        ypabact_UpdateVerticalLaserBeam(this, wproto, _vertical_laser_beams[0],
                                        _vertical_laser_request_start, down, range,
                                        wasActive && oldBeamCount > 0,
                                        spawnBeamVPs, playerControlled, 1.0f);
    activeBeamCount = 1;

    NC_STACK_ypabact *primaryChainGroupTarget =
        (_vertical_laser_request_target && _vertical_laser_request_target->_owner != World::OWNER_0 &&
         ypabact_IsLaserDamageTarget(this, _vertical_laser_request_target))
            ? _vertical_laser_request_target
            : primaryHitTarget;

    if ( primaryHitTarget && primaryHitTarget->_owner != World::OWNER_0 &&
         ypabact_IsLaserDamageTarget(this, primaryHitTarget) )
    {
        directHitTargets.push_back(primaryHitTarget);
    }

    bool friendlyMultiTargets = ypabact_IsLaserFriendlyToShooter(this, primaryChainGroupTarget);
    bool allowFriendlyExtraTargets = !friendlyMultiTargets || playerControlled;

    if ( primaryChainGroupTarget && primaryChainGroupTarget->_owner != World::OWNER_0 &&
         wproto.laser_multi_target > 1 && allowFriendlyExtraTargets )
    {
        std::vector<NC_STACK_ypabact *> selectedTargets = directHitTargets;

        while ( activeBeamCount < (size_t)wproto.laser_multi_target )
        {
            NC_STACK_ypabact *extraTarget =
                ypabact_FindNearestVerticalLaserTarget(this, _vertical_laser_request_start,
                                                       range, fireRadius, friendlyMultiTargets,
                                                       selectedTargets);
            if ( !extraTarget )
                break;

            if ( _vertical_laser_beams.size() <= activeBeamCount )
                _vertical_laser_beams.resize(activeBeamCount + 1);

            vec3d extraDir = extraTarget->_position - _vertical_laser_request_start;
            if ( extraDir.normalise() < 0.001f )
                extraDir = down;

            NC_STACK_ypabact *hitTarget =
                ypabact_UpdateVerticalLaserBeam(this, wproto, _vertical_laser_beams[activeBeamCount],
                                                _vertical_laser_request_start, extraDir, range,
                                                wasActive && activeBeamCount < oldBeamCount,
                                                spawnBeamVPs, playerControlled, 1.0f);

            selectedTargets.push_back(extraTarget);
            if ( hitTarget && hitTarget->_owner != World::OWNER_0 &&
                 ypabact_IsLaserDamageTarget(this, hitTarget) &&
                 !ypabact_LaserTargetInList(hitTarget, directHitTargets) )
            {
                directHitTargets.push_back(hitTarget);
            }

            activeBeamCount++;
        }
    }

    if ( wproto.laser_chain_allow && wproto.laser_chain_max_jumps > 0 &&
         wproto.laser_chain_radius > 0.0f && primaryHitTarget &&
         primaryHitTarget->_owner != World::OWNER_0 &&
         ypabact_IsLaserDamageTarget(this, primaryHitTarget) )
    {
        NC_STACK_ypabact *chainGroupTarget = primaryChainGroupTarget ? primaryChainGroupTarget : primaryHitTarget;
        bool friendlyChain = ypabact_IsLaserFriendlyToShooter(this, chainGroupTarget);
        bool canRunChain = (!friendlyChain || playerControlled) &&
                           ypabact_IsLaserSecondaryTargetCandidate(this, primaryHitTarget, friendlyChain);

        if ( canRunChain )
        {
            std::vector<NC_STACK_ypabact *> chainHits = directHitTargets;
            if ( !ypabact_LaserTargetInList(primaryHitTarget, chainHits) )
                chainHits.push_back(primaryHitTarget);

            NC_STACK_ypabact *from = primaryHitTarget;
            float chainDamageMult = 1.0f;
            float perJumpMult = wproto.laser_chain_damage_mult > 0.0f ? wproto.laser_chain_damage_mult : 1.0f;

            for (int jump = 0; jump < wproto.laser_chain_max_jumps; jump++)
            {
                NC_STACK_ypabact *chainTarget =
                    ypabact_FindLaserChainTarget(this, from, wproto.laser_chain_radius,
                                                 friendlyChain, chainHits);
                if ( !chainTarget )
                    break;

                if ( _vertical_laser_beams.size() <= activeBeamCount )
                    _vertical_laser_beams.resize(activeBeamCount + 1);

                TLaserBeamRuntime &chainBeam = _vertical_laser_beams[activeBeamCount];
                bool beamWasActive = wasActive && activeBeamCount < oldBeamCount;

                chainBeam.start = from->_position;
                chainBeam.end = chainTarget->_position;

                vec3d chainDir = chainBeam.end - chainBeam.start;
                if ( chainDir.normalise() < 0.001f )
                    chainDir = _rotation.AxisZ();

                if ( !beamWasActive || chainTarget->_gid != chainBeam.target_gid )
                {
                    chainBeam.energy_ticks = 0;
                    chainBeam.next_damage_time = 0;
                    chainBeam.next_fx_time = 0;
                }
                chainBeam.target_gid = chainTarget->_gid;

                chainDamageMult *= perJumpMult;

                if ( spawnBeamVPs )
                    ypabact_SpawnLaserBeamVPs(this, wproto, chainBeam.start, chainBeam.end);

                ypabact_ApplyLaserUnitTick(this, wproto, chainTarget, chainBeam,
                                           playerControlled, chainDamageMult);

                if ( _clock >= chainBeam.next_fx_time )
                {
                    int impactVP = wproto.vp_dead > 0 ? wproto.vp_dead : wproto.vp_megadeth;
                    if ( impactVP > 0 )
                        _world->SpawnTransientVP(impactVP, chainBeam.end,
                                                 ypabact_LaserRotationFromDir(chainDir, _rotation), 90);
                    chainBeam.next_fx_time = _clock + 160;
                }

                chainHits.push_back(chainTarget);
                from = chainTarget;
                activeBeamCount++;
            }
        }
    }

    if ( _vertical_laser_beams.size() > activeBeamCount )
        _vertical_laser_beams.resize(activeBeamCount);

    if ( !_vertical_laser_beams.empty() )
        _vertical_laser_beam = _vertical_laser_beams[0];

    if ( spawnBeamVPs )
        _vertical_laser_next_beam_vp_time = _clock + 1;

    wproto.snd_loop.LoadSamples();

    TSampleData *psample = wproto.snd_loop.MainSample.Sample
                         ? wproto.snd_loop.MainSample.Sample->GetSampleData()
                         : NULL;

    if ( psample )
    {
        if ( _vertical_laser_soundcarrier.Sounds.empty() )
            _vertical_laser_soundcarrier.Resize(1);

        TSoundSource &snd = _vertical_laser_soundcarrier.Sounds[0];
        snd.PSample = psample;
        snd.SampleVariants.clear();
        snd.SampleVariants.push_back(psample);
        snd.Volume = wproto.snd_loop.volume ? wproto.snd_loop.volume : 120;
        snd.Pitch = wproto.snd_loop.pitch;
        snd.PriorityBias = 0;
        snd.SetLoop(true);
        snd.SetFragmented(false);
        ypabact_ApplyLaserLoopNormalFX(snd, wproto);

        _vertical_laser_soundcarrier.Position = _vertical_laser_beams.empty()
                                              ? _vertical_laser_request_start
                                              : _vertical_laser_beams[0].start;
        _vertical_laser_soundcarrier.Vector = down;

        if ( !snd.IsEnabled() )
            SFXEngine::SFXe.startSound(&_vertical_laser_soundcarrier, 0);

        SFXEngine::SFXe.UpdateSoundCarrier(&_vertical_laser_soundcarrier);
    }
}

void NC_STACK_ypabact::UpdateSeekAndExplode(update_msg *)
{
    NC_STACK_ypabact *target = ypabact_FindSeekAndExplodeContactTarget(this);
    if ( !target )
        return;

    _seek_and_explode_triggered = true;

    if ( _seek_and_explode_weapon > 0 )
    {
        ypaworld_arg146 arg147;
        arg147.vehicle_id = _seek_and_explode_weapon;
        arg147.pos = _position;

        NC_STACK_ypamissile *payload = _world->ypaworld_func147(&arg147);
        if ( !payload )
        {
            _seek_and_explode_triggered = false;
            return;
        }

        vec3d payloadDir = _rotation.AxisZ();
        if ( payloadDir.normalise() <= 0.001 )
            payloadDir = vec3d::OZ(1.0);

        payload->SetLauncherBact(this);
        // Seek-and-explode detonates at the carrier position. If the payload is
        // model=bomb, the normal drop-height filter would skip nearby ground
        // units that sit slightly below the carrier.
        payload->SetStartHeight(std::numeric_limits<float>::lowest());
        payload->_owner = _owner;
        payload->_host_station = _host_station;
        payload->_fly_dir = payloadDir;
        payload->_fly_dir_length = 0.0;
        payload->_rotation.SetZ(payload->_fly_dir);
        payload->_rotation.SetX(_rotation.AxisX());
        payload->_rotation.SetY(payload->_rotation.AxisZ() * payload->_rotation.AxisX());
        payload->_kidRef.Detach();
        payload->_parent = NULL;
        ypabact_GetSeekAndExplodePayloadListOwner(this)->_missiles_list.push_back(payload);

        payload->DetonateSeekAndExplodePayload(target);
    }

    bool wasInvulnerable = _invulnerable;
    _invulnerable = false;

    int suicideDamage = _energy_max > 0 ? _energy_max : (_energy > 0 ? _energy : 1);
    if ( suicideDamage > std::numeric_limits<int>::max() / 2 )
        suicideDamage = std::numeric_limits<int>::max() / 2;

    bact_arg84 arg84;
    arg84.unit = this;
    arg84.energy = -2 * suicideDamage;

    ModifyEnergy(&arg84);

    if ( _status != BACT_STATUS_DEAD )
    {
        _energy = 0;

        setState_msg arg78;
        arg78.unsetFlags = 0;
        arg78.setFlags = 0;
        arg78.newStatus = BACT_STATUS_DEAD;

        SetState(&arg78);
        Die();
    }

    _invulnerable = wasInvulnerable;
}

size_t NC_STACK_ypabact::LaunchMissile(bact_arg79 *arg)
{
    if ( _world && _world->IsSpectatorBact(this) )
        return 0;

    NC_STACK_ypamissile *wobj = NULL;

    bool useLowHPWeapon = arg->weapon == _weapon && ypabact_IsLowHPWeaponActive(this);
    int slots[4];
    int slotCount = ypabact_GetPrimaryWeaponSlots(this, slots);
    bool useRandomSlots = !useLowHPWeapon && arg->weapon == _weapon && _weapon_switch_mode == 1 && slotCount > 1;

    int selectedWeapon = -1;
    int cooldownWeapon = -1;

    if ( useLowHPWeapon )
    {
        selectedWeapon = _lowhp_weapon;
        cooldownWeapon = selectedWeapon;
    }
    else if ( useRandomSlots )
    {
        cooldownWeapon = ypabact_IsValidWeaponId(this, _current_weapon_id) ? _current_weapon_id : slots[0];
    }
    else
    {
        selectedWeapon = ypabact_SelectPrimaryWeaponSlot(this, arg->weapon);

        cooldownWeapon = selectedWeapon;
    }

    if ( cooldownWeapon == -1 || !ypabact_IsValidWeaponId(this, cooldownWeapon) )
        return 0;

    World::TWeapProto &cooldownProto = _world->GetWeaponsProtos().at(cooldownWeapon);

    // OpenUA custom: a laser weapon does not spawn projectiles and is not rate-limited
    // by shot_time. It registers a per-frame fire request that UpdateLaser() turns into
    // a continuous beam (static tick damage + VP beam visual + loop sound). Bail out here,
    // before the cooldown gate and the normal missile-spawn path.
    if ( cooldownProto.IsLaser() )
    {
        RequestLaserFire(cooldownWeapon, arg);
        return 0;
    }

    if ( cooldownProto.IsVerticalLaser() )
    {
        RequestVerticalLaserFire(cooldownWeapon, arg);
        return 0;
    }

    if ( _weapon_time )
    {
        int v4;

        if ( _oflags & BACT_OFLAG_USERINPT )
            v4 = cooldownProto.shot_time_user;
        else
            v4 = cooldownProto.shot_time;

        if ( cooldownProto.salve_shots )
        {
            if ( cooldownProto.salve_shots <= _salve_counter )
                v4 = cooldownProto.salve_delay;
        }

        // OpenUA Black Sect clone balance: imperfect grey clones (owner 5) fire a
        // little slower. We stretch the *effective* cooldown (shot_time * malus%) only;
        // the weapon prototype's shot_time is never modified.
        if ( World::CloneBalance::IsCloneActor(this) )
            v4 = (int)((float)v4 * World::CloneBalance::AttackTimeFactor());

        if ( arg->g_time - _weapon_time < v4 )
            return 0;
    }

    if ( useRandomSlots )
        selectedWeapon = slots[rand() % slotCount];

    if ( selectedWeapon == -1 || !ypabact_IsValidWeaponId(this, selectedWeapon) )
        return 0;

    World::TWeapProto &wproto = _world->GetWeaponsProtos().at(selectedWeapon);

    // OpenUA custom: mortar weapons are driven exclusively by UpdateMortar()'s
    // barrage AI. Never fire them through the normal direct/missile path.
    if ( wproto.IsMortar() )
        return 0;

    if ( _salve_counter < wproto.salve_shots )
        _salve_counter += 1;
    else
        _salve_counter = 1;

    if ( _oflags & BACT_OFLAG_USERINPT )
    {
        yw_arg180 v26;

        if ( wproto._weaponFlags & 2 )
            v26.effects_type = 0;
        else if ( wproto._weaponFlags & 0x10 )
            v26.effects_type = 1;
        else
            v26.effects_type = 2;

        _world->ypaworld_func180(&v26);
    }

    int v13;

    if ( _num_weapons <= 1 )
        v13 = 1;
    else
        v13 = _num_weapons;

    std::vector<NC_STACK_ypabact *> weaponTargets;
    int maxTargets = ypabact_GetMissileMultiTargetLimit(wproto, v13);
    bool missileMultiTarget = maxTargets > 1;
    bool homingBomb = ypabact_IsHomingBombWeapon(wproto);
    if ( missileMultiTarget )
    {
        weaponTargets = ypabact_CollectMissileMultiTargets(this, arg, wproto, maxTargets);
    }

    bool bombMultiTarget = false;
    if ( !missileMultiTarget )
    {
        maxTargets = ypabact_GetBombMultiTargetLimit(wproto, v13);
        bombMultiTarget = maxTargets > 1;
        if ( bombMultiTarget )
            weaponTargets = ypabact_CollectHomingBombTargets(this, arg, wproto, maxTargets);
        else if ( homingBomb )
            weaponTargets = ypabact_CollectHomingBombTargets(this, arg, wproto, 1);
    }

    ypabact_StoreHUDMissileMultiLockTargets(this, missileMultiTarget ? weaponTargets : std::vector<NC_STACK_ypabact *>());

    for (int i = 0; i < v13; i++)
    {
        float v37;
        bact_arg79 missileArg = *arg;
        bool distributeTargets = missileMultiTarget || bombMultiTarget || homingBomb;
        NC_STACK_ypabact *multiTarget = distributeTargets ? ypabact_GetDistributedMissileTarget(weaponTargets, i) : NULL;

        if ( v13 == 1 )
            v37 = arg->start_point.x;
        else
        {
            float v14 = fabs(arg->start_point.x);
            v37 = (i * 2) * v14 / (v13 - 1) - v14;
        }

        ypaworld_arg146 arg147;
        arg147.vehicle_id = selectedWeapon;
        arg147.pos = _position + _rotation.Transpose().Transform( vec3d(v37, arg->start_point.y, arg->start_point.z) );

        wobj = _world->ypaworld_func147(&arg147);

        if ( !wobj )
            return 0;

        wobj->SetLauncherBact(this);

        wobj->SetStartHeight(arg147.pos.y);

        wobj->_owner = _owner;

        if ( multiTarget )
        {
            missileArg.tgType = BACT_TGT_TYPE_UNIT;
            missileArg.target.pbact = multiTarget;
            missileArg.tgt_pos = multiTarget->_position;

            vec3d targetDir = multiTarget->_position - arg147.pos;
            float targetDirLen = targetDir.length();
            if ( targetDirLen > 0.001 )
                missileArg.direction = targetDir / targetDirLen;
        }
        else if ( missileMultiTarget && missileArg.tgType == BACT_TGT_TYPE_UNIT && !ypabact_IsValidMissileMultiTarget(this, missileArg.target.pbact) )
        {
            missileArg.tgType = BACT_TGT_TYPE_DRCT;
        }

        if ( _bact_type != BACT_TYPES_GUN && !_invulnerable )
            _energy -= wobj->_energy / 300;

        if ( missileArg.direction.x != 0.0 || missileArg.direction.y != 0.0 || missileArg.direction.z != 0.0 )
        {
            wobj->_fly_dir = missileArg.direction;
        }
        else
        {
            wobj->_fly_dir = _rotation.AxisZ();
        }

        bool userInput = (_oflags & BACT_OFLAG_USERINPT);
        float weaponSpreadX = _weapon_spread_x;
        float weaponSpreadY = _weapon_spread_y;

        if ( userInput )
        {
            if ( _weapon_spread_x_user_set )
                weaponSpreadX = _weapon_spread_x_user;

            if ( _weapon_spread_y_user_set )
                weaponSpreadY = _weapon_spread_y_user;
        }

        if ( weaponSpreadX > 0.0 || weaponSpreadY > 0.0 )
            wobj->_fly_dir = ypabact_ApplyDirectionalSpread(_rotation, wobj->_fly_dir, weaponSpreadX, weaponSpreadY);

        wobj->_fly_dir_length = _fly_dir_length + wproto.start_speed;

        if ( !(wproto._weaponFlags & 0x12) )
            wobj->_fly_dir_length *= 0.2;

        wobj->_rotation.SetZ( wobj->_fly_dir );

        wobj->_rotation.SetX( _rotation.AxisX() );

        wobj->_rotation.SetY( wobj->_rotation.AxisZ() * wobj->_rotation.AxisX() );

        if ( i == 0 )
        {
            if ( arg->flags & 1 )
                wobj->_position = wobj->_position - wobj->_rotation.AxisZ() * 30.0;
        }

        if ( wproto.vp_launch > 0 )
            _world->SpawnTransientVP(wproto.vp_launch, wobj->_position, wobj->_rotation, 1000);

        /** Missiles will be stored in another list
         *  so kidref will be not attached to anything.
         *  Looks it's somehow related to mentioned problem with dead cache.
        **/
        
        wobj->_kidRef.Detach();
        wobj->_parent = NULL;

        _missiles_list.push_back(wobj);

        int v42 = wobj->GetMissileType();
        if ( v42 == NC_STACK_ypamissile::MISL_TARGETED || homingBomb )
        {
            setTarget_msg arg67;

            arg67.tgt = missileArg.target;
            arg67.tgt_type = missileArg.tgType;
            arg67.priority = 0;
            arg67.tgt_pos = missileArg.tgt_pos;

            wobj->SetTarget(&arg67);

            if ( missileArg.flags & 2 )
            {
                if ( missileArg.tgType == BACT_TGT_TYPE_CELL )
                    wobj->_primTpos.y = missileArg.tgt_pos.y;
            }
        }

        uamessage_newWeapon wpnMsg;
        wpnMsg.targetPos = missileArg.tgt_pos;

        if ( v42 == 2 )
        {
            wobj->_primTtype = BACT_TGT_TYPE_DRCT;
            wobj->_target_dir = wobj->_fly_dir;
        }

        wobj->_host_station = _host_station;
        _weapon_time = arg->g_time;

        SFXEngine::SFXe.startSound(&wobj->_soundcarrier, 1);

        if ( _world->_isNetGame )
        {
            wobj->_gid |= _owner << 24;

            wpnMsg.msgID = UAMSG_NEWWEAPON;
            wpnMsg.owner = _owner;
            wpnMsg.id = wobj->_gid;
            wpnMsg.launcher = _gid;
            wpnMsg.type = selectedWeapon;
            wpnMsg.pos = arg147.pos;
            wpnMsg.flags = 0;
            wpnMsg.dir = wobj->_fly_dir * wobj->_fly_dir_length;
            wpnMsg.targetType = wobj->_primTtype;

            if ( wobj->_primTtype == BACT_TGT_TYPE_UNIT )
            {
                wpnMsg.target = wobj->_primT.pbact->_gid;
                wpnMsg.targetOwner = wobj->_primT.pbact->_owner;
            }

            _world->NetBroadcastMessage(&wpnMsg, sizeof(wpnMsg), true);
        }

        if ( missileArg.flags & 1 )
        {
            if ( i == 0 )
            {
                if ( _oflags & BACT_OFLAG_VIEWER )
                {
                    setBACT_viewer(false);
                    wobj->setBACT_viewer(true);
                }
            }
        }
        
        if ( missileArg.flags & 4 )
            wobj->SetIgnoreBuilds(1);
            

        if ( missileArg.tgType != BACT_TGT_TYPE_UNIT )
        {
            int life_time_nt = wproto.life_time_nt;

            if ( life_time_nt )
                wobj->SetLifeTime(life_time_nt);
        }
    }

    if ( _kill_after_shot )
    {
        if ( _oflags & BACT_OFLAG_USERINPT )
        {
            setBACT_viewer(false);
            wobj->setBACT_viewer(true);
        }

        bact_arg84 arg84;
        arg84.unit = _parent;
        arg84.energy = -2 * _energy_max;

        ModifyEnergy(&arg84);
    }

    if ( !useLowHPWeapon )
        ypabact_AdvancePrimaryWeaponSlot(this, arg->weapon);
    _current_weapon_id = GetCurrentWeaponId();

    if ( !useLowHPWeapon && _weapon_switch_mode == 1 )
        _current_weapon_id = selectedWeapon;

    return 1;
}

size_t NC_STACK_ypabact::SetPosition(bact_arg80 *arg)
{
    yw_130arg sect_info;

    sect_info.pos_x = arg->pos.x;
    sect_info.pos_z = arg->pos.z;
    if (!_world->GetSectorInfo(&sect_info))
        return 0;

    if ( _pSector )
        _cellRef.Detach();

    _cellRef = sect_info.pcell->unitsList.push_back(this);

    _pSector = sect_info.pcell;
    _old_pos = arg->pos;
    _position = arg->pos;
    _cellId = sect_info.CellId;

    if ( !(arg->field_C & 2) )
        CorrectPositionInLevelBox(NULL);

    return 1;
}

void NC_STACK_ypabact::GetSummary(bact_arg81 *arg)
{
    for ( NC_STACK_ypabact* &node : _kidList )
        node->GetSummary(arg);

    if ( _status != BACT_STATUS_DEAD )
    {
        switch ( arg->enrg_type )
        {
        case 1:
            arg->enrg_sum += _energy;
            break;

        case 3:
            arg->enrg_sum++;
            break;

        case 2:
            arg->enrg_sum += _shield;
            break;

        case 4:
            arg->enrg_sum += _energy_max;
            break;

        case 5:
        {
            arg->enrg_sum += _attackersList.size();
        }
        break;

        default:
            break;
        }
    }
}

// Update bact energy
void NC_STACK_ypabact::EnergyInteract(update_msg *arg)
{
    if ( _status != BACT_STATUS_DEAD )
    {
        int v16 = _clock - _energy_time;

        if ( v16 >= 1500 )
        {
            _energy_time = _clock;

            yw_arg176 arg176;
            arg176.owner = _pSector->owner;

            _world->ypaworld_func176(&arg176);

            float v14 = v16 / 1000.0;

            float denerg = 2.0 * _energy_max * v14 * _pSector->energy_power * arg176.field_4 / 7000.0;

            if ( _owner == _pSector->owner )
                _energy += denerg;
            else if ( !_invulnerable )
                _energy -= denerg;

            TMobilePowerInfluence mobilePower = _world->FindMobilePowerInfluenceForUnit(this);
            float mobileDelta = 2.0 * _energy_max * v14 * (mobilePower.AlliedEnergyPower - mobilePower.EnemyEnergyPower) / 7000.0;

            if ( mobileDelta >= 0.0 || !_invulnerable )
                _energy += mobileDelta;

            if ( _energy < 0 )
                _energy = 0;

            if ( _energy > _energy_max )
                _energy = _energy_max;
        }
    }
}

void NC_STACK_ypabact::ApplyImpulse(bact_arg83 *arg)
{
    float v81 = 50.0 / _mass;
    float v79 = arg->energ * 0.0004;

    vec3d v60 = _position - arg->pos;

    float distance = v60.length();

    if ( distance <= _radius )
    {
        vec3d v63 = (arg->pos2 * (2.5 * arg->mass * arg->force) + _fly_dir * _mass * _fly_dir_length) / (_mass + arg->mass);

        _fly_dir_length = v63.normalise();

        if ( _fly_dir_length > 0.0 )
            _fly_dir = v63;

        v60 = arg->pos2;

        distance = 1.0;
    }
    else
    {
        v60 /= distance;

        vec3d v63 = _fly_dir * _fly_dir_length + (v60 * v81 * v79) / distance;

        _fly_dir_length = v63.normalise();

        if ( _fly_dir_length > 0.0 )
            _fly_dir = v63;
    }

    CorrectPositionInLevelBox(NULL);

    _status_flg &= ~BACT_STFLAG_LAND;

    float angle = v81 * 0.01 * v79 / distance;

    float cos_len = v60.dot(_rotation.AxisZ());

    // cos(45) == 0.7071
    if ( fabs(cos_len) > 0.7071 )
    {
        if ( cos_len > 0.7071 )
            _rotation = mat3x3::RotateX(-angle) * _rotation;
        else
            _rotation = mat3x3::RotateX(angle) * _rotation;
    }
    else
    {
        if ( v60.XZ().cross( _rotation.AxisZ().XZ() ) >= 0.0 )
            _rotation = mat3x3::RotateZ(angle) * _rotation;
        else
            _rotation = mat3x3::RotateZ(-angle) * _rotation;
    }
}

float NC_STACK_ypabact::GetEffectiveShieldWithAdditionalMalus(float additionalMalus) const
{
    float shield = (float)_shield;
    float mult = 1.0f;

    if ( _active_debuff.active )
        mult *= ypabact_DebuffMalusToMult(_active_debuff.shield_malus);

    mult *= ypabact_DebuffMalusToMult(additionalMalus);

    // OpenUA Black Sect clone balance: imperfect grey clones (owner 5) have a
    // slightly lower effective defense. This scales the *effective* shield only;
    // the stored _shield (and the shared prototype) are never modified, so the
    // malus is recomputed each call and never compounds on save/load/respawn.
    if ( World::CloneBalance::IsCloneActor(this) )
        mult *= World::CloneBalance::DownFactor();

    if ( mult < 0.0f )
        mult = 0.0f;

    return shield * mult;
}

float NC_STACK_ypabact::GetEffectiveShield() const
{
    return GetEffectiveShieldWithAdditionalMalus(0.0f);
}

void NC_STACK_ypabact::ModifyEnergy(bact_arg84 *arg)
{
    // OpenUA Black Sect clone balance: when the *attacker* is an imperfect grey
    // clone (owner 5), its outgoing final damage is reduced by the malus. This is
    // the single choke point every damage source funnels through (direct weapons,
    // missiles, lasers, guns, AoE...), so the malus is applied exactly once per hit
    // and never compounds. Only actual damage (negative delta) from a real attacker
    // is scaled; healing/energy transfer and prototype values stay untouched.
    if ( arg->energy < 0 && World::CloneBalance::IsCloneActor(arg->unit) )
    {
        int scaled = (int)((float)arg->energy * World::CloneBalance::DownFactor());
        // Never let rounding turn a real hit into zero damage.
        if ( scaled == 0 )
            scaled = -1;
        arg->energy = scaled;
    }

    if ( _invulnerable && arg->energy < 0 )
        return;

    if (_world && (_oflags & BACT_OFLAG_VIEWER))
    {
        if (_world->getYW_invulnerable() && arg->energy > -1000000)
            return;
    }
    
    bool isNetGame = false;
    if (_world && _world->_isNetGame)
        isNetGame = true;

    // ---- OpenUA: protective dummy damage absorption (single-player only) ----
    // Route incoming damage to an active protective dummy module before it
    // reaches the parent. If the dummy survives, the parent takes nothing; if
    // the hit destroys the dummy, only the leftover passes through. Net games
    // keep vanilla routing to avoid desync.
    if ( arg->energy < 0 && !_isDummy && !isNetGame && !_unitDummies.empty() )
    {
        NC_STACK_ypabact *prot = SelectProtectiveDummy(arg->unit);
        if ( prot && prot != this )
        {
            int incoming = -arg->energy;   // positive damage amount
            int dummyHP  = prot->_energy;  // remaining dummy health

            if ( incoming <= dummyHP )
            {
                // Dummy absorbs the whole hit; parent untouched.
                bact_arg84 dmgArg;
                dmgArg.energy = arg->energy;
                dmgArg.unit   = arg->unit;
                prot->ModifyEnergy(&dmgArg);
                return;
            }

            // Dummy is destroyed; only the leftover damage passes to the parent.
            bact_arg84 dmgArg;
            dmgArg.energy = -dummyHP;
            dmgArg.unit   = arg->unit;
            prot->ModifyEnergy(&dmgArg);

            arg->energy += dummyHP;        // reduce parent damage by absorbed part
            if ( arg->energy >= 0 )
                return;
        }
    }

    bool friendlyFire = false;
    if (!arg->unit || _owner == arg->unit->_owner)
        friendlyFire = true;

    if ( isNetGame && friendlyFire == false )
    {
        uamessage_vhclEnergy vhclEnrgy;
        vhclEnrgy.msgID = UAMSG_VHCLENERGY;
        vhclEnrgy.owner = _owner;
        vhclEnrgy.id = _gid;
        vhclEnrgy.energy = arg->energy;

        if ( arg->unit )
        {
            vhclEnrgy.killer = arg->unit->_gid;
            vhclEnrgy.killerOwner = arg->unit->_owner;
        }
        else
        {
            vhclEnrgy.killer = 0;
            vhclEnrgy.killerOwner = 0;
        }

        _world->NetBroadcastMessage(&vhclEnrgy, sizeof(vhclEnrgy), true);
    }
    else
    {
        _energy += arg->energy;

        if ( _energy <= 0 )
        {
            if ( arg->unit )
                _killer_owner = arg->unit->_owner;
            else
                _killer_owner = 0;

            _killer = arg->unit;
            _status_flg &= ~BACT_STFLAG_LAND;

            setState_msg v16;
            v16.newStatus = BACT_STATUS_DEAD;
            v16.unsetFlags = 0;
            v16.setFlags = 0;

            SetState(&v16);

            Die();
        }
    }
}

bool NC_STACK_ypabact::ypabact_func85(vec3d *arg)
{
    float tmp2 = arg->dot( _fly_dir * _fly_dir_length );

    if ( fabs(tmp2) > 15.0 )
        return true;

    return false;
}


void CrashOrLand__sub1(NC_STACK_ypabact *bact)
{
    if ( bact->_fly_dir.x < 0.0 )
        bact->_fly_dir.x -= 7.0;

    if ( bact->_fly_dir.z < 0.0 )
        bact->_fly_dir.z -= 7.0;

    if ( bact->_fly_dir.x >= 0.0 )
        bact->_fly_dir.x += 7.0;

    if ( bact->_fly_dir.z >= 0.0 )
        bact->_fly_dir.z += 7.0;

    if ( bact->_fly_dir_length < 15.0 )
        bact->_fly_dir_length = 15.0;

    float v4 = bact->_fly_dir.length();

    if ( v4 <= 0.001 )
        bact->_fly_dir = vec3d(0.0, 1.0, 0.0);
    else
        bact->_fly_dir /= v4;
}

void sub_48AB14(NC_STACK_ypabact *bact, const vec3d &vec)
{
    vec3d vaxis = bact->_rotation.AxisY() * vec;

    if ( vaxis.normalise() != 0.0 )
    {
        float angle = clp_acos( vec.dot(bact->_rotation.AxisY()) );

        if ( angle > 0.001 )
            bact->_rotation *= mat3x3::AxisAngle(vaxis, angle);
    }
}

void CrashOrLand__sub0(NC_STACK_ypabact *bact, int a2)
{
    bact->_status_flg |= BACT_STFLAG_SCALE;

    if ( bact->_scale_duration > bact->_scale_pos )
    {
        float v5 = bact->_maxrot * a2 / 1000.0;


        bact->_scale_speed += bact->_scale_accel * a2 / 1000.0;
        bact->_scale_start += bact->_scale_speed * (a2 / 1000.0);

        bact->_scale = vec3d(bact->_scale_start);

        bact->_rotation = mat3x3::RotateY(v5) * bact->_rotation;

        int v14 = 0;
        for (int i = 0; i < 32; i++)
        {
            if ( bact->_vp_fx_models[i] )
                v14++;
        }

        if ( v14 )
        {
            int v15 = bact->_scale_pos * v14 / bact->_scale_duration;

            bact->SetVP(bact->_vp_fx_models[v15]);
        }

        bact->_scale_pos += a2;
    }
    else
    {
        bact->_yls_time = -1;
        bact->Release();
    }
}

size_t NC_STACK_ypabact::CrashOrLand(bact_arg86 *arg)
{
    yw_137col v58[10];

    int v85 = 0;

    if ( _status_flg & BACT_STFLAG_SEFFECT )
    {
        CrashOrLand__sub0(this, arg->field_two);
    }
    else
    {
        float v84;
        float v90;

        if ( _oflags & BACT_OFLAG_VIEWER )
        {
            v84 = _viewer_radius;
            v90 = _viewer_overeof;
        }
        else
        {
            v84 = _radius;
            v90 = _overeof;
        }

        if ( _bact_type == BACT_TYPES_ROBO )
            v90 = 60.0;

        vec3d vaxis = vec3d( -_rotation.m12, 0.0, _rotation.m10 );

        float v94 = arg->field_two / 1000.0;

        if ( vaxis.normalise() > 0.001 && !(arg->field_one & 1) )
        {
            float angle = clp_acos( _rotation.m11 );
            float maxrot = _maxrot * v94;

            if ( angle > maxrot )
                angle = maxrot;

            if ( fabs(angle) > BACT_MIN_ANGLE )
            {
                _rotation *= mat3x3::AxisAngle(vaxis, angle);
            }
        }

        if ( arg->field_one & 2 )
        {
            float v18 = fabs(_fly_dir_length) * v94 * 0.08;

            _rotation = mat3x3::RotateZ(v18) * _rotation;
        }

        if ( !(_status_flg & BACT_STFLAG_LAND) )
        {
            if ( arg->field_one & 1 )
                _airconst = 0;
            else
                _airconst = 500.0;

            for (int i = 0; i < 3; i++)
            {

                move_msg v66;

                v66.field_0 = v94;
                v66.flag = 1;

                Move(&v66);

                int v20 = 0;

                if ( _oflags & BACT_OFLAG_BACTCOLL )
                {
                    if ( CollisionWithBact(arg->field_two) )
                    {
                        if ( _bact_type == BACT_TYPES_TANK || _bact_type == BACT_TYPES_CAR )
                        {
                            CrashOrLand__sub1(this);
                            return 0;
                        }

                        return 0;
                    }
                }

                if ( _oflags & BACT_OFLAG_VIEWER )
                {
                    ypaworld_arg137 arg137;
                    arg137.pos = _fly_dir * _fly_dir_length * v94 * 6.0 + _position;
                    arg137.pos2 = _fly_dir;
                    arg137.radius = v84;
                    arg137.collisions = v58;
                    arg137.field_30 = 0;
                    arg137.coll_max = 10;

                    _world->ypaworld_func137(&arg137);

                    if ( arg137.coll_count )
                    {
                        int v24 = 0;
                        v85 = 1;

                        vec3d v98;

                        for (int j = arg137.coll_count - 1; j >= 0; j--)
                        {
                            yw_137col *v25 = &arg137.collisions[ j ];

                            v98 += v25->pos2;

                            if ( v98.y > 0.6 )
                                v24 = 1;
                        }

                        bact_arg88 arg88;
                        vec3d a2a;

                        float lnn = v98.length();

                        if ( lnn != 0.0 )
                        {
                            arg88.pos1 = v98 / lnn;

                            a2a = arg88.pos1;
                        }
                        else
                        {
                            a2a = _fly_dir;
                            arg88.pos1 = _fly_dir;
                        }

                        if ( arg->field_one & 1 )
                        {
                            if ( !_invulnerable )
                                _energy -= fabs(_fly_dir_length) * 10.0;

                            if ( _energy <= 0 || (GetVP() == _vp_dead && _status == BACT_STATUS_DEAD) )
                            {
                                setState_msg arg78;
                                arg78.setFlags = BACT_STFLAG_DEATH2;
                                arg78.unsetFlags = 0;
                                arg78.newStatus = BACT_STATUS_NOPE;

                                SetState(&arg78);
                            }

                            if ( _oflags & BACT_OFLAG_USERINPT )
                            {
                                if ( fabs(_fly_dir_length) > 7.0 )
                                    SFXEngine::SFXe.startSound(&_soundcarrier, 5);

                                yw_arg180 arg180_1;

                                arg180_1.effects_type = 5;
                                arg180_1.field_4 = 1.0;
                                arg180_1.field_8 = v98.x * 10.0 + _position.x;
                                arg180_1.field_C = v98.z * 10.0 + _position.z;

                                _world->ypaworld_func180(&arg180_1);
                            }

                            if ( v98.y >= 0.6 && v24 )
                            {
                                _position.y = _old_pos.y;

                                _status_flg |= BACT_STFLAG_LAND;

                                _fly_dir_length *= _fly_dir.XZ().length();

                                sub_48AB14(this, a2a);

                                _reb_count = 0;
                            }
                            else
                            {
                                Recoil(&arg88);

                                _reb_count++;

                                v20 = 1;

                                if ( _reb_count > 50 )
                                {
                                    if ( !_invulnerable )
                                        _energy = -10000;

                                    _status_flg |= BACT_STFLAG_LAND;
                                }
                            }
                        }
                        else if ( v98.y < 0.6 )
                        {
                            Recoil(&arg88);

                            v20 = 1;
                        }
                        else
                        {
                            _position.y = _old_pos.y;
                            _fly_dir_length = 0;
                            _reb_count = 0;
                            _status_flg |= BACT_STFLAG_LAND;
                        }
                    }
                }

                if ( !v85 )
                {
                    ypaworld_arg136 arg136;
                    arg136.stPos = _old_pos;
                    arg136.vect = _position - _old_pos + vec3d(0.0, v90, 0.0);
                    arg136.flags = 0;

                    _world->ypaworld_func136(&arg136);

                    if ( arg136.isect )
                    {
                        bact_arg88 arg88;

                        arg88.pos1 = arg136.skel->polygons[arg136.polyID].Normal();

                        vec3d a2a = arg88.pos1;

                        if ( arg->field_one & 1 )
                        {
                            if ( !_invulnerable )
                                _energy -= fabs(_fly_dir_length) * 10.0;

                            if ( _energy <= 0 || (GetVP() == _vp_dead && _status == BACT_STATUS_DEAD) )
                            {
                                setState_msg arg78;
                                arg78.setFlags = BACT_STFLAG_DEATH2;
                                arg78.unsetFlags = 0;
                                arg78.newStatus = BACT_STATUS_NOPE;

                                SetState(&arg78);
                            }

                            if ( _oflags & BACT_OFLAG_USERINPT )
                            {
                                if ( fabs(_fly_dir_length) > 7.0 )
                                    SFXEngine::SFXe.startSound(&_soundcarrier, 5);

                                yw_arg180 arg180;

                                arg180.effects_type = 5;
                                arg180.field_4 = 1.0;
                                arg180.field_8 = a2a.x * 10.0 + _position.x;
                                arg180.field_C = a2a.z * 10.0 + _position.z;

                                _world->ypaworld_func180(&arg180);
                            }

                            if ( arg136.skel->polygons[arg136.polyID].B < 0.6 )
                            {
                                Recoil(&arg88);

                                _reb_count++;

                                v20 = 1;

                                if ( _reb_count > 50 )
                                {
                                    _energy = -10000;
                                    _status_flg |= BACT_STFLAG_LAND;
                                }
                            }
                            else
                            {
                                _position = arg136.isectPos - vec3d(0.0, v90, 0.0);

                                _status_flg |= BACT_STFLAG_LAND;

                                _fly_dir_length *= _fly_dir.XZ().length();

                                sub_48AB14(this, a2a);

                                _reb_count = 0;
                            }
                        }
                        else if ( arg136.skel->polygons[arg136.polyID].B < 0.6 )
                        {
                            Recoil(&arg88);

                            v20 = 1;
                        }
                        else
                        {
                            _position.y = arg136.isectPos.y - v90;

                            _fly_dir_length = 0;
                            _reb_count = 0;
                            _status_flg |= BACT_STFLAG_LAND;
                        }
                    }
                }

                if ( !v20 ) // Alternative exit from loop
                    break;
            }

        }
        if ( _status_flg & BACT_STFLAG_LAND )
            return 1;
    }
    return 0;
}


void CollisionWithBact__sub0(NC_STACK_ypabact *bact, NC_STACK_ypabact *a2)
{
    int v2 = (int)((float)a2->_energy_max * 0.7);

    if ( v2 < 10000 )
        v2 = 10000;

    if ( v2 > 25000 )
        v2 = 25000;

    int v3 = (float)a2->_scale_time * 0.2 / (float)v2 * (float)a2->_energy_max;

    if ( bact->_energy + v3 > bact->_energy_max )
    {
        NC_STACK_yparobo *robo = bact->_host_station;

        int v10 = v3 - (bact->_energy_max - bact->_energy);

        bact->_energy = bact->_energy_max;

        if ( robo->_energy + v10 > robo->_energy_max )
        {
            int v14 = v10 - (robo->_energy_max - robo->_energy);

            robo->_energy = robo->_energy_max;

            if ( robo->_roboEnergyLife + v14 >= robo->_energy_max )
            {
                robo->_roboEnergyMove += v14 - (robo->_energy_max - robo->_roboEnergyLife);

                robo->_roboEnergyLife = robo->_energy_max;

                if ( robo->_roboEnergyMove > robo->_energy_max )
                    robo->_roboEnergyMove = robo->_energy_max;
            }
            else
            {
                robo->_roboEnergyLife += v14;
            }
        }
        else
        {
            robo->_energy += v10;
        }
    }
    else
    {
        bact->_energy += v3;
    }
}

size_t NC_STACK_ypabact::CollisionWithBact(int arg)
{
    bool isViewer = getBACT_viewer();

    float trad;
    if ( isViewer )
        trad = _viewer_radius;
    else
        trad = _radius;


    int v49 = 0;

    World::rbcolls *v46 = getBACT_collNodes();

    if ( _fly_dir_length == 0.0 )
        return 0;

    vec3d stru_5150E8(0.0, 0.0, 0.0);

    int v45 = 0;

    World::rbcolls *v55;

    for ( NC_STACK_ypabact* &bnode : _pSector->unitsList )
    {
        int v53 = bnode->_status == BACT_STATUS_DEAD && (bnode->_vp_extra[0].flags & 1) && (_oflags & BACT_OFLAG_USERINPT) && bnode->_scale_time > 0 ;

        if ( bnode != this && bnode->_bact_type != BACT_TYPES_MISSLE && (!bnode->IsDestroyed() || v53) )
        {

            v55 = bnode->getBACT_collNodes();

            int v9;

            if ( v55 )
            {
                v9 = v55->roboColls.size();
                v49 = 1;
            }
            else
            {
                v9 = 1;
            }

            for (int i = v9 - 1; i >= 0; i--)
            {
                float ttrad;
                vec3d v41;

                if (!v55)
                {
                    ttrad = trad;
                    v41 = bnode->_position;
                }
                else
                {
                    World::TRoboColl *v10 = &v55->roboColls[i];
                    ttrad = v10->robo_coll_radius;

                    v41 = bnode->_position + bnode->_rotation.Transpose().Transform(v10->coll_pos);

                    if ( ttrad < 0.01 )
                        continue;
                }

                if ( (_position - v41).length() <= trad + ttrad )
                {
                    if ( !v53 )
                    {
                        stru_5150E8 += v41;

                        v45++;
                    }
                    else
                    {
                        CollisionWithBact__sub0(this, bnode);

                        bnode->_scale_time = -1;

                        if ( _world->_GameShell )
                            SFXEngine::SFXe.startSound(&_world->_GameShell->samples1_info, World::SOUND_ID_PLASMA);

                        if ( _world->_isNetGame )
                        {
                            uamessage_endPlasma epMsg;
                            epMsg.msgID = UAMSG_ENDPLASMA;
                            epMsg.owner = bnode->_owner;
                            epMsg.id = bnode->_gid;

                            _world->NetBroadcastMessage(&epMsg, sizeof(epMsg), true);

                            if ( bnode->_owner != _owner )
                            {
                                bnode->_vp_extra[0].flags = 0;
                                bnode->_vp_extra[0].SetVP((NC_STACK_base::Instance *)NULL);// = NULL;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    if ( !v45 || (v46 && !v49) )
    {
        _status_flg &= ~BACT_STFLAG_BCRASH;
        return 0;
    }

    stru_5150E8 /= (double)v45;

    vec3d stru_5150F4 = stru_5150E8 - _position;

    float v26 = stru_5150F4.length();

    if ( v26 < 0.0001)
        return 0;

    bact_arg88 v33;
    v33.pos1 = stru_5150F4 / v26;

    // FIX MY MATH
    // stru_5150F4 should be normalised?
    // May be replace it with "dot < 0.0" ?
    // Because cos of 1.0...0 is 0..PI/2 and 0...-1.0 is PI/2..PI
    if ( clp_acos( stru_5150F4.dot( _fly_dir ) ) > C_PI_2 )
        return 0;

    if ( !(_status_flg & BACT_STFLAG_BCRASH) )
    {
        if ( isViewer )
        {
            SFXEngine::SFXe.startSound(&_soundcarrier, 6);

            _status_flg |= BACT_STFLAG_BCRASH;

            yw_arg180 v40;
            v40.field_4 = 1.0;
            v40.field_8 = stru_5150E8.x;
            v40.field_C = stru_5150E8.z;
            v40.effects_type = 5;

            _world->ypaworld_func180(&v40);
        }
    }

    if ( fabs(_fly_dir_length) < 0.1 )
        _fly_dir_length = 1.0;

    Recoil(&v33);

    _target_vec = _fly_dir;

    _AI_time1 = _clock;
    _AI_time2 = _clock;

    return 1;
}

void NC_STACK_ypabact::Recoil(bact_arg88 *arg)
{
    if ( !(_status_flg & BACT_STFLAG_LAND) )
    {
        if ( _fly_dir.dot(arg->pos1) >= 0.0 )
        {
            if ( _fly_dir_length != 0.0 )
            {
                _position = _old_pos;

                float v4 = _fly_dir.dot(arg->pos1) * 2.0;

                _fly_dir -= arg->pos1 * v4;

                _fly_dir_length *= 25.0 / (fabs(_fly_dir_length) + 10.0);
            }
        }
    }
}

void NC_STACK_ypabact::ypabact_func89(IDVPair *arg)
{
    dprintf("MAKE ME %s\n","ypabact_func89");
    //call_parent(zis, obj, 89, arg);
}


bool NC_STACK_ypabact::IsAnyKidWithoutSecondUnitTarget() const
{
    for ( NC_STACK_ypabact* node : _kidList )
    {
        if ( node->_secndTtype != BACT_TGT_TYPE_UNIT )
            return true;
    }
    return false;
}

NC_STACK_ypabact * NC_STACK_ypabact::GetEnemyCandidateInSector(const cellArea &cell, float *radius, char *job) const
{
    NC_STACK_ypabact *lastSelectedUnit = NULL;

    const World::TVhclProto &proto = _world->GetVhclProtos().at( _vehicleID );

    for( NC_STACK_ypabact* cel_unit : cell.unitsList )
    {
        // Do not target missile or dead
        if ( cel_unit->_bact_type == BACT_TYPES_MISSLE ||
             cel_unit->_status == BACT_STATUS_DEAD )
            continue;

        // OpenUA: dummy modules are armor/decoration, never independent AI targets
        if ( cel_unit->_isDummy )
            continue;

        // Do not target same fraction unit or owner == 0
        if ( cel_unit->_owner == _owner || cel_unit->_owner == World::OWNER_0 )
            continue;
            
        int jobLevel;
        
        switch ( cel_unit->_bact_type )
        {
        case BACT_TYPES_BACT:
            jobLevel = proto.job_fighthelicopter;
            break;

        case BACT_TYPES_TANK:
        case BACT_TYPES_CAR:
            jobLevel = proto.job_fighttank;
            break;

        case BACT_TYPES_FLYER:
        case BACT_TYPES_UFO:
            jobLevel = proto.job_fightflyer;
            break;

        case BACT_TYPES_ROBO:
            jobLevel = proto.job_fightrobo;
            break;

        default:
            jobLevel = 5;
            break;
        }

        // do not target if job for this unit is less of previous
        if ( jobLevel < *job )
            continue;
        
        float radivs = (_position - cel_unit->_position).length();
        
        // do not target if distance more than for old selected unit
        if ( radivs > *radius && !cel_unit->getBACT_viewer() )
            continue;

        // If own unit is not gun or robo do additional checks for distance
        if ( _bact_type != BACT_TYPES_GUN && _bact_type != BACT_TYPES_ROBO )
        {
            vec3d ownTargetPos;
            bool isLeader;

            if ( IsParentMyRobo() )
            {
                if ( _primTtype == BACT_TGT_TYPE_CELL )
                {
                    ownTargetPos = _primTpos;

                }
                else if ( _primTtype == BACT_TGT_TYPE_UNIT )
                {
                    ownTargetPos = _primT.pbact->_position;
                }
                else
                {
                    ownTargetPos = _position;
                }

                isLeader = true;
            }
            else
            {
                if ( _parent->_primTtype == BACT_TGT_TYPE_CELL )
                {
                    ownTargetPos = _parent->_primTpos;
                }
                else if ( _parent->_primTtype == BACT_TGT_TYPE_UNIT )
                {
                    ownTargetPos = _parent->_primT.pbact->_position;
                }
                else
                {
                    ownTargetPos = _position;
                }

                isLeader = false;
            }

            // if primary/secondary squad target distance is more than 3 sector length
            // do additional check
            if ( (ownTargetPos.XZ() - _position.XZ()).length() > World::CVUnitFarSecDist )
            {
                int countOwnAttackers = 0;

                for ( const TBactAttacker &ainf : cel_unit->_attackersList )
                {
                    if ( ainf.attacker->_secndTtype == BACT_TGT_TYPE_UNIT &&
                         ainf.attacker->_secndT.pbact == cel_unit && 
                         ainf.attacker->_owner == _owner )
                        countOwnAttackers++;

                    if ( countOwnAttackers > 1 ) // if more than 1 do break already
                        break;
                }

                // If current unit already attacked by more than 1 another units - skip it
                if ( countOwnAttackers > 1 )
                    continue;
                
                // If we is leader and if some of us kids do not has second target unit
                // let's skip this unit and leave it for targeting by kid
                if (isLeader && IsAnyKidWithoutSecondUnitTarget() )
                    continue;
            }
        }
        
        // If test of sector below of unit is OK then make it current candidate
        // and do tests for next units in this sector
        if ( TestTargetSector(cel_unit) )
        {
            *radius = radivs;
            *job = jobLevel;
            lastSelectedUnit = cel_unit;
        }
    }

    return lastSelectedUnit;
}

NC_STACK_ypabact * NC_STACK_ypabact::GetSectorTarget(Common::Point CellId) const
{
    NC_STACK_ypabact *enemy = NULL;

    if ( _world->IsSector(CellId) )
    {
        float rad = 1800.0;
        char job = 0;

        for (int x = -1; x < 2; x++)
        {
            for (int y = -1; y < 2; y++)
            {
                Common::Point pt = CellId + Common::Point(x, y);
                NC_STACK_ypabact *unit = GetEnemyCandidateInSector( _world->SectorAt(pt), &rad, &job);

                if ( unit ) enemy = unit;
            }
        }
    }
    return enemy;
}

void NC_STACK_ypabact::GetBestSectorPart(vec3d *arg)
{
    yw_130arg arg130;
    arg130.pos_x = arg->x;
    arg130.pos_z = arg->z;

    _world->GetSectorInfo(&arg130);

    vec2d ttmp = World::SectorIDToCenterPos2( arg130.CellId );

    arg->x = ttmp.x;
    arg->z = ttmp.y;

    if ( arg130.pcell->SectorType != 1 )
    {
        int v7 = 0;

        for (int y = 0; y < 3; y++)
        {
            for (int x = 0; x < 3; x++)
            {
                if ( arg130.pcell->buildings_health.At(x, y) > v7 )
                {
                    arg->z = 300.0 * (-1 + y) + ttmp.y;
                    arg->x = 300.0 * (-1 + x) + ttmp.x;

                    v7 = arg130.pcell->buildings_health.At(x, y);
                }
            }
        }
    }
}

void NC_STACK_ypabact::GetForcesRatio(bact_arg92 *arg)
{
    yw_130arg arg130;

    arg->energ1 = 0;
    arg->energ2 = 0;

    if ( arg->field_14 & 1 )
    {
        arg130.pos_x = _position.x;
        arg130.pos_z = _position.z;
    }
    else
    {
        arg130.pos_x = arg->pos.x;
        arg130.pos_z = arg->pos.z;
    }

    if ( _world->GetSectorInfo(&arg130) )
    {
        cellArea *cell = arg130.pcell;
        Common::Point pt = cell->CellId;

        if ( arg130.CellId.x != 0 && arg130.CellId.y != 0 )
        {
            // left-up
            cellArea &tcell = _world->SectorAt(pt.x - 1, pt.y - 1);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( arg130.CellId.y )
        {
            // up
            cellArea &tcell = _world->SectorAt(pt.x, pt.y - 1);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( arg130.CellId.x < _wrldSectors.x - 1 && arg130.CellId.y )
        {
            // right-up
            cellArea &tcell = _world->SectorAt(pt.x + 1, pt.y - 1);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( arg130.CellId.x )
        {
            // left
            cellArea &tcell = _world->SectorAt(pt.x - 1, pt.y);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        // center
        if ( cell->IsCanSee(_owner) )
        {
            for (NC_STACK_ypabact* &cl_unit : cell->unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
        }

        if ( arg130.CellId.x < _wrldSectors.x - 1 )
        {
            // right
            cellArea &tcell = _world->SectorAt(pt.x + 1, pt.y);

            if ( tcell.IsCanSee(_owner) )
            {
               for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( arg130.CellId.x != 0 && arg130.CellId.y < _wrldSectors.y - 1 )
        {
            // left-down
            cellArea &tcell = _world->SectorAt(pt.x - 1, pt.y + 1);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( arg130.CellId.y < _wrldSectors.y - 1  )
        {
            // down
            cellArea &tcell = _world->SectorAt(pt.x, pt.y + 1);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( arg130.CellId.x < _wrldSectors.x - 1 && arg130.CellId.y < _wrldSectors.y - 1 )
        {
            // down-right
            cellArea &tcell = _world->SectorAt(pt.x + 1, pt.y + 1);

            if ( tcell.IsCanSee(_owner) )
            {
                for (NC_STACK_ypabact* &cl_unit : tcell.unitsList)
                {
                    if ( cl_unit->_owner )
                    {
                        if ( cl_unit->_status != BACT_STATUS_DEAD && 
                            (cl_unit->_bact_type != BACT_TYPES_ROBO || cl_unit->_owner != _owner) && 
                             cl_unit->_bact_type != BACT_TYPES_MISSLE )
                        {
                            if ( cl_unit->_owner == _owner )
                                arg->energ1 += cl_unit->_energy;
                            else
                                arg->energ2 += cl_unit->_energy;
                        }
                    }
                }
            }
        }

        if ( !(arg->field_14 & 2) )
        {
            int v33 = 0;

            if ( cell->SectorType == 1 )
            {
                v33 = cell->buildings_health.At(0, 0);
            }
            else
            {
                for (auto helth : cell->buildings_health)
                    v33 += helth;

                v33 /= cell->buildings_health.size();
            }

            if ( cell->owner == _owner )
            {
                if ( arg->field_14 & 4 )
                    arg->energ1 += v33 * 120;
            }
            else
            {
                arg->energ2 += v33 * 120;
            }
        }
    }
}

void NC_STACK_ypabact::ypabact_func93(IDVPair *arg)
{
    dprintf("MAKE ME %s\n","ypabact_func93");
//    call_parent(zis, obj, 93, arg);
}

void NC_STACK_ypabact::GetFormationPosition(bact_arg94 *arg)
{
    vec3d v2d = _rotation.AxisZ().X0Z();
    v2d.normalise();

    arg->pos1 = _position - v2d * ( (arg->field_0 / 3 + 1) * 150.0 );

    int v6 = arg->field_0 % 3;

    if ( v6 == 0 )
    {
        arg->pos1.x += 100.0 * v2d.z;
        arg->pos1.z += -100.0 * v2d.x;
    }
    else if ( v6 == 2 )
    {
        arg->pos1.x += -100.0 * v2d.z;
        arg->pos1.z += 100.0 * v2d.x;
    }

    // With y = 0
    //arg->pos2 = vec3d::X0Z( arg->pos1.XZ() - _position.XZ() );
}

void NC_STACK_ypabact::ypabact_func95(IDVPair *arg)
{
    dprintf("MAKE ME %s\n","ypabact_func95");
//    call_parent(zis, obj, 95, arg);
}

// Reset
void NC_STACK_ypabact::Renew()
{
    _oflags = BACT_OFLAG_EXACTCOLL;
    _status_flg = 0;
    _host_station = NULL;
    _yls_time = 3000;
    _primTtype = BACT_TGT_TYPE_NONE;

    _secndTtype = BACT_TGT_TYPE_NONE;
    _primT_cmdID = 0;

    _wrldSectors = _world->GetMapSize();

    _wrldSize = World::SectorIDToPos2( _wrldSectors );

    _commandID = 0;
//    bact->field_3D1 = 1;
    _killer = NULL;
    _brkfr_time = 0;
    _brkfr_time2 = 0;
    _mpos.x = 0;
    _mpos.y = 0;
    _mpos.z = 0;
    _gun_leftright = 0.0;
    _scale_time = 0;
    _clock = 0;
    _AI_time1 = 0;
    _AI_time2 = 0;
//    bact->field_921 = 0;
//    bact->field_925 = 0;
    _search_time1 = 0;
    _search_time2 = 0;
    _slider_time = 0;
//    bact->field_951 = 0;
    _mgun_time = 0;
    _weapon_time = 0;
    _extra_weapons = {0, 0, 0};
    _weapon_switch_mode = 0;
    _weapon_slot_index = 0;
    _current_weapon_id = -1;
    _lowhp_weapon_enable = 0;
    _lowhp_threshold = 0.30;
    _lowhp_weapon = 0;
    _num_mguns = 1;
    _mgun_fire_x = 0.0;
    _spawn_units = 0;
    _spawn_vehicle = 0;
    _spawn_interval = 5000;
    _spawn_trigger_radius = 0.0;
    _spawn_random_pos = 0.0;
    _spawn_max_active = 0;
    _spawn_count = 1;
    _spawn_instant = 0;
    _spawn_last_time = 0;
    _spawn_at_death_units = 0;
    _spawn_at_death_vehicle = 0;
    _spawn_at_death_count = 1;
    _spawn_at_death_random_pos = 0.0;
    _spawn_at_death_instant = 0;
    _spawn_at_death_immunity_time = 0;
    _spawn_at_death_done = false;
    _spawn_at_death_protection_end_time = 0;
    _spawn_at_death_restore_vulnerable = false;
    _carrier_spawn_root_gid = 0;
    _carrier_spawn_root_vehicle = 0;
    _carrier_spawned_gids.clear();
    _proximity_defense_enable = 0;
    _proximity_defense_weapon = 0;
    _proximity_defense_trigger_radius = 0.0;
    _proximity_defense_interval = 1000;
    _proximity_defense_shots = 12;
    _proximity_defense_fire_pos = vec3d(0.0, 0.0, 0.0);
    _proximity_defense_vp_launch = -1;
    _proximity_defense_fire_mode = 0;
    _proximity_defense_sequence_delay = 100;
    _proximity_defense_at_death = 0;
    _proximity_defense_random_yaw_set = false;
    _proximity_defense_random_yaw_min = 0.0;
    _proximity_defense_random_yaw_max = 360.0;
    _proximity_defense_random_pitch_set = false;
    _proximity_defense_random_pitch_min = -10.0;
    _proximity_defense_random_pitch_max = 45.0;
    _proximity_defense_sequence_active = false;
    _proximity_defense_sequence_shots_fired = 0;
    _proximity_defense_next_shot_time = 0;
    _proximity_defense_next_activation_time = 0;
    _proximity_defense_at_death_done = false;
    _mortar_barrage_active = false;
    _mortar_shots_remaining = 0;
    _mortar_next_shot_time = 0;
    _mortar_next_activation_time = 0;
    _mortar_next_scan_time = 0;
    _mortar_target_center = vec3d(0.0, 0.0, 0.0);
    _mortar_has_pending = false;
    _mortar_pending_target = vec3d(0.0, 0.0, 0.0);
    StopLaser();
    StopVerticalLaser();
    _seek_and_explode = 0;
    _seek_and_explode_weapon = 0;
    _seek_and_explode_trigger_radius = 0.0;
    _seek_and_explode_triggered = false;
    _newtarget_time = 0;
    _assess_time = 0;
    _scale_pos = 0;
    _scale_delay = 0;
    _beam_time = 0;
    _energy_time = 0;
    ypabact_ResetDamagedFX(this);
    ClearActiveDebuff();
    _fe_time = -45000;
    _salve_counter = 0;
    _kill_after_shot = 0;
    
    Common::DeleteAndNull(&_current_vp);

    _vp_active = 0;
    _volume = 0; //_soundcarrier.Sounds[0].Volume;
    _pitch = 0; //_soundcarrier.Sounds[0].Pitch;

    _m_cmdID = 0;
    _gun_angle_user = _gun_angle;
    _oflags |= BACT_OFLAG_LANDONWAIT;

    for (World::DestFX &x : _destroyFX)
        x = World::DestFX();
    
    _extDestroyFX.clear();
    _chainFX.clear();

    for (extra_vproto &vp : _vp_extra)
        vp = extra_vproto();
    
    _current_waypoint = 0;

    _attackersList.clear();
    _kidList.clear();
    _missiles_list.clear();
}

void NC_STACK_ypabact::HandBrake(update_msg *arg)
{
    _thraction = _mass * 9.77665;

    float v53 = arg->frameTime * 0.001;

    vec3d vaxis = _rotation.AxisY() * vec3d(0.0, 1.0, 0.0);

    if ( vaxis.normalise() > 0.001 )
    {
        float v62 = clp_acos( _rotation.AxisY().dot( vec3d(0.0, 1.0, 0.0) ) );
        float v11 = _maxrot * v53;

        if ( v62 > v11 )
            v62 = (v62 * 1.5) * v11;

        if ( fabs(v62) <= 0.0015 )
        {
            _rotation.SetY( vec3d(0.0, 1.0, 0.0) );

            vec3d axisX = _rotation.AxisX().X0Z();
            axisX.normalise();

            _rotation.SetX( axisX );

            vec3d axisZ = _rotation.AxisZ().X0Z();
            axisZ.normalise();

            _rotation.SetZ( axisZ );

            if ( fabs(_fly_dir_length) < 0.1 )
            {
                _fly_dir = vec3d(0.0, 1.0, 0.0);

                _fly_dir_length = 0;
            }
        }
        else
        {
            _rotation *= mat3x3::AxisAngle(vaxis, v62);
        }
    }

    _fly_dir_length *= 0.8;
}

void NC_STACK_ypabact::ypabact_func98(IDVPair *arg)
{
    dprintf("MAKE ME %s\n","ypabact_func98");
//    call_parent(zis, obj, 98, arg);
}

void NC_STACK_ypabact::CreationTimeUpdate(update_msg *arg)
{
    _scale_time -= arg->frameTime;

    float v30 = arg->frameTime / 1000.0;

    if ( _scale_time > 0 )
    {
        _status_flg |= BACT_STFLAG_SCALE;

        if ( _scale_time < 0 )
            _scale = vec3d(1.0);
        else
            _scale = vec3d( 0.9 / ((float)_scale_time / 1000.0 + 0.9) + 0.1 );

        _rotation = mat3x3::RotateY( 2.5 / _scale.x * v30 ) * _rotation;
    }
    else
    {
        setState_msg v25;
        v25.newStatus = BACT_STATUS_NORMAL;
        v25.setFlags = 0;
        v25.unsetFlags = 0;

        SetState(&v25);

        _status_flg &= ~BACT_STFLAG_SCALE;

        bact_arg80 v24;

        v24.pos = _position;
        v24.field_C = 0;

        SetPosition(&v24);

        NC_STACK_ypabact *a4 = _world->getYW_userHostStation();

        if ( _host_station == a4 )
        {

            if ( IsParentMyRobo() )
            {
                robo_arg134 v23;
                v23.unit = this;
                v23.field_4 = 14;
                v23.field_8 = 0;
                v23.field_C = 0;
                v23.field_10 = 0;
                v23.field_14 = 26;

                _host_station->placeMessage(&v23);
            }
        }

        if ( _host_station )
        {
            if ( _bact_type != BACT_TYPES_GUN )
            {
                _fly_dir = v24.pos - _host_station->_position;

                float fly_len = _fly_dir.length();

                if ( fly_len > 0.001 )
                    _fly_dir /= fly_len;

                _fly_dir_length = 20.0;
            }
        }
    }
}

size_t NC_STACK_ypabact::IsDestroyed()
{
    return (GetVP() == _vp_dead || GetVP() == _vp_genesis || GetVP() == _vp_megadeth) && _status == BACT_STATUS_DEAD;
}

size_t NC_STACK_ypabact::CheckFireAI(bact_arg101 *arg)
{
    vec3d tmp;

    if ( arg->unkn == 2 )
        tmp = arg->pos - _position;
    else
        tmp = arg->pos.X0Z() - _position.X0Z() + vec3d::OY(_height);

    float len = tmp.normalise();

    if ( len == 0.0 )
        return 0;

    World::TWeapProto *v8 = NULL;

    int v36;

    if ( _weapon != -1 )
    {
        v8 = &_world->GetWeaponsProtos().at( _weapon );


        if ( v8->_weaponFlags & World::TWeapProto::WEAPON_FLAG_PROJECTILE )
            v36 = v8->GetFireControlFlags();
        else
            v8 = NULL;
    }

    if ( !v8 )
    {
        if ( _mgun == -1 )
            return 0;

        v36 = 2;
    }

    if ( v8 && v8->IsVerticalLaser() )
    {
        vec3d fireOrigin = _position + _rotation.Transpose().Transform(_fire_pos);
        if ( arg->pos.y < fireOrigin.y )
            return 0;

        return (arg->pos.XZ() - fireOrigin.XZ()).length() <= ypabact_VerticalLaserFireRadius(*v8);
    }

    if ( arg->unkn == 2 )
    {
        float v32;

        if ( v8 )
        {

            float v38 = arg->radius * 0.8 + v8->radius;

            if ( v38 >= 40.0 )
            {
                v32 = v38;
            }
            else
            {
                v32 = 3.0625;
            }
        }
        else
        {
            float v41 = arg->radius * 0.8;

            if ( v41 >= 40.0 )
                v32 = v41;
            else
                v32 = 40.0;
        }

        if ( v36 )
        {
            if ( v36 == 16 )
            {
                if ( len < World::CVSectorLength && tmp.XZ().dot( _rotation.AxisZ().XZ() ) > 0.93 )
                    return 1;
            }
            else
            {
                vec3d tmp2 = tmp * _rotation.AxisZ();

                if ( len < World::CVSectorLength && (tmp.dot( _rotation.AxisZ() ) > 0.0) && v32 / len > tmp2.length() )
                    return 1;
            }
        }
        else
        {
            if ( (arg->pos.XZ() - _position.XZ()).length() < v32 && arg->pos.y > _position.y )
                return 1;
        }
    }
    else if ( v8 )
    {
        if ( v36 )
        {
            if ( v36 == 16 )
            {
                if ( len < World::CVSectorLength && tmp.XZ().dot( _rotation.AxisZ().XZ() ) > 0.91 )
                    return 1;
            }
            else if ( len < World::CVSectorLength && tmp.dot( _rotation.AxisZ() ) > 0.91 )
            {
                return 1;
            }
        }
        else
        {
            if ( (arg->pos.XZ() - _position.XZ()).length() < v8->radius )
                return 1;
        }
    }
    return 0;
}

void NC_STACK_ypabact::MarkSectorsForView()
{
    /* Missle does not have kids, if else it's a BUG or must be another missle */
    if ( _bact_type == BACT_TYPES_MISSLE )
        return;
 
    /* Unit already dead also must do not have any kids */
    if ( _status == BACT_STATUS_DEAD || _status == BACT_STATUS_CREATE )
        return;
    
    if ( !_parent || _cellId != _parent->_cellId || 
        (_radar > _parent->_radar || _unhideRadar > _parent->_unhideRadar) )
    {
        if ( _owner < 8 )
        {
            for (int i = -_radar; i <= _radar; i++)
            {
                int yy = _cellId.y + i;

                if ( _radar == 1 )
                {
                    if ( yy > 0 && yy < _wrldSectors.y - 1 )
                    {
                        if (_unhideRadar > 0)
                        {
                            if ( _cellId.x > 1 )
                            {
                                _world->SectorAt(_cellId.x - 1, yy).AddToViewMask(_owner);
                                _world->SectorAt(_cellId.x - 1, yy).AddUnhideMask(_owner);
                            }

                            _world->SectorAt(_cellId.x, yy).AddToViewMask(_owner);
                            _world->SectorAt(_cellId.x, yy).AddUnhideMask(_owner);

                            if ( _cellId.x + 1 < _wrldSectors.x - 1 )
                            {
                                _world->SectorAt(_cellId.x + 1, yy).AddToViewMask(_owner);
                                _world->SectorAt(_cellId.x + 1, yy).AddUnhideMask(_owner);
                            }
                        }
                        else
                        {
                            if ( _cellId.x > 1 )
                                _world->SectorAt(_cellId.x - 1, yy).AddToViewMask(_owner);

                            _world->SectorAt(_cellId.x, yy).AddToViewMask(_owner);

                            if ( _cellId.x + 1 < _wrldSectors.x - 1 )
                                _world->SectorAt(_cellId.x + 1, yy).AddToViewMask(_owner);
                        }
                    }
                }
                else
                {
                    float vtmp = POW2((float)_radar) - POW2((float)i);

                    if (vtmp < 0.0)
                        vtmp = 0.0;

                    int tmp = dround( sqrt(vtmp) );

                    if (_unhideRadar > 0 && Common::ABS(i) < _unhideRadar)
                    {
                        for (int j = -tmp; j <= tmp; j++)
                        {
                            Common::Point d(_cellId.x + j, yy);

                            if ( _world->IsGamePlaySector(d) )
                            {
                                _world->SectorAt(d).AddToViewMask(_owner);
                                if (Common::ABS(j) < _unhideRadar)
                                    _world->SectorAt(d).AddUnhideMask(_owner);
                            }
                        }
                    }
                    else
                    {
                        for (int j = -tmp; j <= tmp; j++)
                        {
                            Common::Point d(_cellId.x + j, yy);

                            if ( _world->IsGamePlaySector(d) )
                                _world->SectorAt(d).AddToViewMask(_owner);
                        }
                    }
                }
            }
        }
    }

    for( NC_STACK_ypabact* &kid : _kidList )
        kid->MarkSectorsForView();
}

void NC_STACK_ypabact::ypabact_func103(IDVPair *arg)
{
    dprintf("MAKE ME %s\n","ypabact_func103");
//    call_parent(zis, obj, 103, arg);
}

void NC_STACK_ypabact::StuckFree(update_msg *arg)
{
//    if ( bact->field_93D > 0 )
//        bact->field_93D -= arg->field_4;

//    if ( bact->field_93D < 0 )
//        bact->field_93D = 0;

    if ( _bflags & BACT_OFLAG_BACTCOLL )
    {
//        if ( !bact->field_93D )
        _oflags |= BACT_OFLAG_BACTCOLL;
    }

    if ( _status != BACT_STATUS_NORMAL || _oflags & BACT_OFLAG_USERINPT )
    {
        _mpos = _position;
        _brkfr_time2 = _clock;
    }
    else
    {
        vec3d tmp = _mpos - _position;

        if (tmp.length() >= 12.0)
        {
            _mpos = _position;
            _brkfr_time2 = _clock;
        }
        else
        {
            if ( _oflags & BACT_OFLAG_BACTCOLL )
                _bflags |= BACT_OFLAG_BACTCOLL;

            if ( _clock - _brkfr_time2 > 10000 )
            {
                if ( (_bact_type == BACT_TYPES_TANK || _bact_type == BACT_TYPES_CAR) && !(_status_flg & BACT_STFLAG_ATTACK) )
                {
                    _old_pos = _position;

                    _position += -_rotation.AxisZ() * 10.0;

                    CorrectPositionInLevelBox(NULL);

                    _rotation = mat3x3::RotateY(0.1) * _rotation;

                    ypaworld_arg136 arg136;
                    arg136.stPos = _old_pos;
                    arg136.vect = _position - _old_pos;
                    arg136.flags = 1;

                    _world->ypaworld_func136(&arg136);

                    if ( arg136.isect )
                    {
                        _position = arg136.isectPos - vec3d::OY(5.0);
                    }
                }
            }
        }
    }
}

static vec3d ypabact_ApplyDirectionalSpread(const mat3x3 &rotation, const vec3d &direction, float spreadX, float spreadY)
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

static float ypabact_GetMgunOffset(const NC_STACK_ypabact *bact, int shotId, int shotCount)
{
    if ( shotCount <= 1 )
        return 0.0;

    // Matches primary num_weapons/fire_x distribution: -abs(x) ... +abs(x).
    float sideSpan = fabs(bact->_mgun_fire_x);
    return (shotId * 2) * sideSpan / (shotCount - 1) - sideSpan;
}

size_t NC_STACK_ypabact::FireMinigun(bact_arg105 *arg)
{
    if ( _world && _world->IsSpectatorBact(this) )
        return 0;

    int a5 = 0;

    if ( _world->_isNetGame )
        a5 = 1;

    if ( _mgun == -1 )
        return 0;

    World::TWeapProto &mgunProto = _world->GetWeaponsProtos().at(_mgun);
    int mgunShots = _num_mguns > 0 ? _num_mguns : 1;

    int v107 = 0;
    if ( _bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( this );
        int a4 = gun->IsRoboGun();

        if ( a4 )
            v107 = 1;
    }
    else if ( !_invulnerable )
    {
        _energy -= _gun_power * arg->field_C / 300.0;
    }

    int v88 = getBACT_inputting();
    bool spawnVisual = false;

    if ( (v88 || _world->ypaworld_func145(this)) && !a5 )
    {
        int v45;

        if ( v88 )
        {
            int v43 = mgunProto.shot_time_user;
            float v42 = arg->field_C * 1000.0;

            if ( v43 <= v42 )
                v45 = v42;
            else
                v45 = v43;
        }
        else
        {
            int v47 = mgunProto.shot_time;
            float v46 = arg->field_C * 1000.0;

            if ( v47 <= v46 )
                v45 = v46;
            else
                v45 = v47;
        }

        // OpenUA Black Sect clone balance: imperfect grey clones (owner 5) fire their
        // machine gun slower. The effective shot_time (cooldown between visible shots)
        // is stretched by the malus % (game.black_sect_clone_malus_percent), exactly
        // like the main weapon. Runtime-only; the weapon prototype's shot_time is never
        // modified, and only owner-5 combat units are affected.
        if ( World::CloneBalance::IsCloneActor(this) )
            v45 = (int)((float)v45 * World::CloneBalance::AttackTimeFactor());

        if ( arg->field_10 - _mgun_time > v45 )
        {
            _mgun_time = arg->field_10;
            spawnVisual = true;
        }
    }

    for (int shotId = 0; shotId < mgunShots; shotId++)
    {
        float mgunOffset = ypabact_GetMgunOffset(this, shotId, mgunShots);
        vec3d sideOffset = _rotation.Transpose().Transform(vec3d(mgunOffset, 0.0, 0.0));
        vec3d shotPos = _position + sideOffset;
        vec3d shotOldPos = _old_pos + sideOffset;
        bool userInput = (_oflags & BACT_OFLAG_USERINPT);
        float spreadX = _mgun_spread_x;
        float spreadY = _mgun_spread_y;

        if ( userInput )
        {
            if ( _mgun_spread_x_user_set )
                spreadX = _mgun_spread_x_user;

            if ( _mgun_spread_y_user_set )
                spreadY = _mgun_spread_y_user;
        }
        vec3d shotDir = ypabact_ApplyDirectionalSpread(_rotation, arg->field_0, spreadX, spreadY);

        NC_STACK_ypabact *v108 = NULL;
        float v123 = 0.0;
        float v121 = 0.0;
        vec3d v66;

        yw_130arg arg130;
        arg130.pos_x = shotPos.x;
        arg130.pos_z = shotPos.z;

        vec2d tmp = shotPos.XZ() + shotDir.XZ() * 1000.0;

        if ( !_world->GetSectorInfo(&arg130) )
            continue;

        cellArea *pCells[3];
        pCells[0] = arg130.pcell;

        arg130.pos_x = tmp.x;
        arg130.pos_z = tmp.y;

        if ( !_world->GetSectorInfo(&arg130) )
            continue;

        pCells[2] = arg130.pcell;

        if ( arg130.pcell == pCells[0] )
        {
            pCells[1] = pCells[0];
        }
        else
        {
            vec2d tmp2 = shotPos.XZ() + (tmp - shotPos.XZ()) * 0.5;
            arg130.pos_x = tmp2.x;
            arg130.pos_z = tmp2.y;

            if ( !_world->GetSectorInfo(&arg130) )
                continue;

            pCells[1] = arg130.pcell;
        }

        for(int i = 0; i < 3; i++)
        {
            if ( i <= 0 || pCells[ i ] != pCells[ i - 1 ] )
            {
                for ( NC_STACK_ypabact* &cellUnit : pCells[ i ]->unitsList )
                {
                    if ( cellUnit != this && cellUnit->_bact_type != BACT_TYPES_MISSLE && cellUnit->_status != BACT_STATUS_DEAD )
                    {
                        int v89 = 0;
                        if (cellUnit->_bact_type == BACT_TYPES_GUN)
                        {
                            NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( cellUnit );
                            v89 = gun->IsRoboGun();
                        }

                        if ( cellUnit->_bact_type != BACT_TYPES_GUN || !v89 || cellUnit->GetEffectiveShield() < 100.0f )
                        {
                            if ( (_oflags & BACT_OFLAG_USERINPT || cellUnit->_owner != _owner) && (!v107 || cellUnit != _host_station) )
                            {

                                World::rbcolls *v93 = cellUnit->getBACT_collNodes();

                                int v109;
                                if ( v93 )
                                    v109 = v93->roboColls.size();
                                else
                                    v109 = 1;

                                int v22 = 0;

                                for (int j = v109 - 1; j >= 0; j-- )
                                {
                                    vec3d v77;
                                    float v27;

                                    if ( v93 )
                                    {
                                        v77 = cellUnit->_position + cellUnit->_rotation.Transpose().Transform( v93->roboColls[j].coll_pos );

                                        v27 = v93->roboColls[j].robo_coll_radius;
                                    }
                                    else
                                    {
                                        v77 = cellUnit->_position;

                                        v27 = cellUnit->_radius;
                                    }

                                    if ( !v93 || v27 >= 0.01 )
                                    {
                                        v121 = v27;

                                        vec3d v63 = v77 - shotOldPos;

                                        if ( v63.dot( shotDir ) >= 0.3 )
                                        {
                                            vec3d v33 = shotDir * v63;

                                            float v111 = v63.length();
                                            float v110 = v33.length();

                                            float v37 = v27 + _gun_radius;

                                            if ( v37 > v110 )
                                            {
                                                if ( sqrt( POW2(v110) + 1000000.0 ) > v111 )
                                                {
                                                    if ( !v22 )
                                                    {
                                                        int energ;

                                                        if ( cellUnit->getBACT_inputting() || cellUnit->getBACT_viewer() )
                                                        {
                                                            float v39 = (_gun_power * arg->field_C) * (100.0 - cellUnit->GetEffectiveShield());
                                                            energ = (v39 * 0.004);
                                                        }
                                                        else
                                                        {

                                                            float v41 = (_gun_power * arg->field_C) * (100.0 - cellUnit->GetEffectiveShield());
                                                            energ = v41 / 100;
                                                        }

                                                        bact_arg84 v86;
                                                        v86.unit = this;
                                                        v86.energy = -energ;

                                                        if ( energ )
                                                            cellUnit->ModifyEnergy(&v86);
                                                    }

                                                    v22 = 1;

                                                    if ( !v108 || v123 > v111 )
                                                    {
                                                        v108 = cellUnit;
                                                        v123 = v111;

                                                        v66 = cellUnit->_position;
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

        if ( spawnVisual )
        {
            int v55 = 0;
            int v96 = 0;

            ypaworld_arg136 v59;

            vec3d v80;

            if ( v108 )
            {
                v55 = 1;
                v96 = 0;

                if (isnormal(v123)) // Not NULL, NAN, INF
                    v80 = v66 - (v66 - shotPos) * (v121 * 0.7) / v123;
                else
                    v80 = v66;
            }
            else
            {
                v59.stPos = shotPos;
                v59.vect = shotDir * 1000.0;
                v59.flags = 0;

                _world->ypaworld_func149(&v59);

                if ( v59.isect )
                {
                    v80 = v59.isectPos;

                    v96 = 1;
                    v55 = 1;
                }
                else
                {
                    v55 = 0;
                }
            }

            if ( v55 )
            {
                ypaworld_arg146 arg147;
                arg147.pos = v80;
                arg147.vehicle_id = _mgun;

                NC_STACK_ypamissile *gunFireBact = _world->ypaworld_func147(&arg147);

                if ( gunFireBact )
                {
                    gunFireBact->_owner = _owner;

                    gunFireBact->_kidRef.Detach();
                    gunFireBact->_parent = NULL;

                    _missiles_list.push_back(gunFireBact);

                    setState_msg v69;
                    v69.newStatus = BACT_STATUS_DEAD;
                    v69.setFlags = 0;
                    v69.unsetFlags = 0;

                    gunFireBact->SetStateInternal(&v69);

                    if ( v96 )
                    {
                        v69.setFlags = BACT_STFLAG_DEATH2;
                        v69.newStatus = BACT_STATUS_NOPE;
                        v69.unsetFlags = 0;
                        gunFireBact->SetStateInternal(&v69);

                        gunFireBact->AlignMissileByNormal( v59.skel->polygons[ v59.polyID ].Normal() );
                    }
                }
            }
        }
    }

    return 1;
}


void NC_STACK_ypabact::sub_4843BC(NC_STACK_ypabact *bact2, int a3)
{
    bact_hudi hudi;

    float v23;
    float v24;

    if ( bact2 )
    {
        vec3d v17 = bact2->_position - _position;

        mat3x3 corrected = _rotation;
        GFX::Engine.matrixAspectCorrection(corrected, false);

        vec3d v20 = corrected.Transform(v17);

        if ( v20.z != 0.0 )
        {
            v23 = v20.x / v20.z;
            v24 = v20.y / v20.z;
        }
        else
        {
            v24 = 0.0;
            v23 = 0.0;
        }

        hudi.field_18 = bact2;
    }
    else
    {
        v23 = -_gun_leftright;
        v24 = -_gun_angle_user;

        hudi.field_18 = NULL;
    }

    if ( _mgun == -1 )
    {
        hudi.field_0 = 0;
    }
    else
    {
        hudi.field_0 = 1;
        hudi.field_8 = -_gun_leftright;
        hudi.field_C = -_gun_angle_user;
    }

    if ( _weapon == -1 || a3 )
    {
        hudi.field_4 = 0;
    }
    else
    {
        if ( _weapon_flags & 4 )
        {
            hudi.field_4 = 4;
            hudi.field_10 = v23;
            hudi.field_14 = v24;
        }
        else
        {
            if ( (_weapon_flags & 4) || !(_weapon_flags & 2) )
                hudi.field_4 = 2;
            else
                hudi.field_4 = 3;

            hudi.field_10 = -_gun_leftright;
            hudi.field_14 = -_gun_angle_user;
        }
    }

    _world->ypaworld_func153(&hudi);
}

size_t NC_STACK_ypabact::UserTargeting(bact_arg106 *arg)
{
    NC_STACK_ypabact *targeto = 0;
    float v56 = 0.0;

    float v55;

    if ( _weapon == -1 )
        v55 = 0.0;
    else
        v55 = _world->GetWeaponsProtos().at(_weapon).radius;

    bool homingBomb = _weapon != -1 && _world->GetWeaponsProtos().at(_weapon).IsHomingBomb();
    int a3a = !(_weapon_flags & 2) && !(_weapon_flags & 0x10);
    bool searchWeaponTarget = !a3a || homingBomb;

    if ( _weapon != -1 && searchWeaponTarget )
    {
        yw_130arg arg130;
        arg130.pos_x = _position.x;
        arg130.pos_z = _position.z;

        _world->GetSectorInfo(&arg130);

        vec2d tmp = _rotation.AxisZ().XZ() * World::CVSectorLength + _position.XZ();

        cellArea *pCells[3];

        pCells[0] = arg130.pcell;

        arg130.pos_x = tmp.x;
        arg130.pos_z = tmp.y;

        _world->GetSectorInfo(&arg130);

        pCells[2] = arg130.pcell;

        if ( arg130.pcell == pCells[0] )
        {
            pCells[1] = pCells[0];
        }
        else
        {
            vec2d tmp2 = _position.XZ() + (tmp - _position.XZ()) * 0.5;
            arg130.pos_x = tmp2.x;
            arg130.pos_z = tmp2.y;

            _world->GetSectorInfo(&arg130);

            pCells[1] = arg130.pcell;
        }

        for (int i = 0; i < 3; i++)
        {
            if ( i <= 0 || pCells[i] != pCells[i - 1] )
            {
                if ( pCells[i] )
                {
                    for ( NC_STACK_ypabact* &bct : pCells[i]->unitsList )
                    {
                        if ( bct != this )
                        {
                            if ( bct->_bact_type != BACT_TYPES_MISSLE && bct->_status != BACT_STATUS_DEAD )
                            {
                                int v53 = 0;
                                if (bct->_bact_type == BACT_TYPES_GUN)
                                {
                                    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( bct );
                                    v53 = gun->IsRoboGun() && bct->GetEffectiveShield() >= 100.0f;
                                }

                                if ( !v53 )
                                {
                                    if ( arg->field_0 & 2 || bct->_owner != _owner )
                                    {
                                        if ( arg->field_0 & 1 || bct->_owner == _owner || !bct->_owner )
                                        {
                                            if ( arg->field_0 & 4 || bct->_owner )
                                            {
                                                vec3d mv = bct->_position - _old_pos;

                                                if ( mv.dot( _rotation.AxisZ() ) >= 0.0 )
                                                {
                                                    float mv_len = mv.length();

                                                    vec3d mvd = arg->field_4 * mv;

                                                    float v59 = mv_len * 1000.0 * 0.0005 + 20.0;
                                                    float mvd_len = mvd.length();

                                                    if ( ((mvd_len < v59 && (_weapon_flags & 4)) || (bct->_radius + v55 > mvd_len && !(_weapon_flags & 4)) )
                                                            && mv_len < 2000.0
                                                            && (v56 > mvd_len || !targeto) )
                                                    {
                                                        targeto = bct;
                                                        v56 = mvd_len;
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

    if ( targeto )
    {
        sub_4843BC(targeto, a3a);

        if ( _weapon != -1 && !a3a )
        {
            bact_arg79 previewArg = {};
            previewArg.direction = vec3d(0.0, 0.0, 0.0);
            previewArg.tgType = BACT_TGT_TYPE_UNIT;
            previewArg.target.pbact = targeto;
            previewArg.tgt_pos = targeto->_position;
            previewArg.weapon = _weapon;

            World::TWeapProto &previewProto = _world->GetWeaponsProtos().at(_weapon);
            if ( previewProto.IsLaser() )
            {
                ypabact_UpdateHUDLaserMultiLockTargets(this, &previewArg, previewProto);
            }
            else
            {
                int weaponCount = _num_weapons <= 1 ? 1 : _num_weapons;
                ypabact_UpdateHUDMissileMultiLockTargets(this, &previewArg, previewProto, weaponCount);
            }
        }
        else if ( _oflags & BACT_OFLAG_USERINPT )
        {
            _world->_hudMissileMultiLockTargets.clear();
        }

        setTarget_msg arg67;
        arg67.tgt_type = BACT_TGT_TYPE_UNIT;
        arg67.priority = 1;
        arg67.tgt.pbact = targeto;

        SetTarget(&arg67);

        arg->ret_bact = targeto;
        return 1;
    }

    sub_4843BC(NULL, a3a);
    arg->ret_bact = NULL;

    return 0;
}

void NC_STACK_ypabact::HandleVisChildrens(int *arg)
{
    NC_STACK_base *vps[] {
    _vp_normal,
    _vp_dead,
    _vp_fire,
    _vp_genesis,
    _vp_wait,
    _vp_megadeth};

    for ( NC_STACK_base *vp : vps )
    {
        for( NC_STACK_base *kd : vp->GetKidList())
        {
            if ( *arg == 1 )
            {
                kd->SetParentFollow(true);

                kd->SetPosition( kd->GetPos() - _position );
            }
            else if ( *arg == 2 )
            {
                kd->SetParentFollow(true);

                kd->SetPosition( kd->GetPos() + _position );
            }
        }
    }
}

bool NC_STACK_ypabact::GetFightMotivation(float *arg)
{
    if ( _aggr == 100 )
        return true;

    if ( _aggr == 0 )
        return false;

    bact_arg81 arg81;
    arg81.enrg_sum = 0;
    arg81.enrg_type = 1;

    GetSummary(&arg81);

    float v11 = arg81.enrg_sum;

    arg81.enrg_sum = 0;
    arg81.enrg_type = 4;

    GetSummary(&arg81);

    if (arg81.enrg_sum == 0) // Possible devision by zero
        arg81.enrg_sum = 1;

    v11 = v11 / (float)arg81.enrg_sum;

    if ( arg )
        *arg = v11;

    if ( (_status_flg & BACT_STFLAG_ESCAPE) && v11 > 0.5 )
    {
        return true;
    }
    else if ( v11 > 0.2 )
    {
        return true;
    }
    return false;
}

NC_STACK_ypabact *sb_0x493984__sub1(NC_STACK_ypabact *bact)
{
    vec3d v12;

    if ( bact->_primTtype == BACT_TGT_TYPE_CELL )
        v12 = bact->_primTpos;
    else if ( bact->_primTtype == BACT_TGT_TYPE_UNIT )
        v12 = bact->_primT.pbact->_position;
    else
        return NULL;

    float v14 = 215040.0;

    NC_STACK_ypabact *new_leader = NULL;

    for ( NC_STACK_ypabact* &kid_unit : bact->_kidList )
    {
        if ( kid_unit->_status != BACT_STATUS_DEAD && !kid_unit->ShouldHideFromStrategicUI() )
        {
            int a4 = kid_unit->getBACT_inputting();

            if ( !a4 )
            {

                float v17 = (v12.XZ() - kid_unit->_position.XZ()).length();

                if ( !new_leader || (kid_unit->_bact_type != BACT_TYPES_UFO && v14 > v17) || (new_leader->_bact_type == BACT_TYPES_UFO && (kid_unit->_bact_type != BACT_TYPES_UFO || v14 > v17 )) )
                {
                    new_leader = kid_unit;
                    v14 = v17;
                }
            }
        }
    }
    return new_leader;
}

NC_STACK_ypabact *sb_0x493984__sub0(NC_STACK_ypabact *bact)
{
    float tmp = 0.0;
    NC_STACK_ypabact *new_leader = NULL;

    for ( NC_STACK_ypabact* &kid_unit : bact->_kidList )
    {
        if ( kid_unit->_status != BACT_STATUS_DEAD && !kid_unit->ShouldHideFromStrategicUI() )
        {
            float v10;
            if ( kid_unit->_bact_type == BACT_TYPES_UFO )
            {
                v10 = 0.0;
            }
            else
            {
                float v8 = 1.0 - ( (bact->_position.XZ() - kid_unit->_position.XZ()).length() / 110400.0);
                v10 = (float)kid_unit->_energy / (float)kid_unit->_energy_max + v8;
            }

            if ( !new_leader || tmp < v10 )
            {
                new_leader = kid_unit;
                tmp = v10;
            }
        }
    }

    return new_leader;
}

NC_STACK_ypabact *sb_0x493984(NC_STACK_ypabact *bact, int a2)
{
    if ( !bact->_kidList.empty() )
    {
        NC_STACK_ypabact *new_leader = NULL;

        if (a2)
            new_leader = sb_0x493984__sub1(bact);
        else
            new_leader = sb_0x493984__sub0(bact);

        if (!new_leader)
            return NULL;

        if (new_leader->_bact_type != BACT_TYPES_UFO || bact->_bact_type == BACT_TYPES_UFO)
        {
            bact->_host_station->AddSubject(new_leader);

            new_leader->CopyTargetOf(bact);

            for ( World::RefBactList::iterator it = bact->_kidList.begin(); it != bact->_kidList.end(); )
            {
                NC_STACK_ypabact *kid_unit = *it;
                it++;

                if ( kid_unit->ShouldHideFromStrategicUI() )
                    continue;

                new_leader->AddSubject(kid_unit);

                kid_unit->CopyTargetOf(new_leader);
            }
            new_leader->_commandID = bact->_commandID;
            return new_leader;
        }

    }
    return NULL;
}

void NC_STACK_ypabact::sub_493480(NC_STACK_ypabact *bact2, int mode)
{
    if ( _world->_isNetGame )
    {
        static uamessage_reorder ordMsg;

        ordMsg.comm = bact2->_gid;
        ordMsg.num = 0;
        ordMsg.commID = bact2->_commandID;

        for ( NC_STACK_ypabact* &bct : bact2->_kidList )
        {
            if ( bct->ShouldHideFromStrategicUI() )
                continue;

            if ( ordMsg.num < 500 )
            {
                ordMsg.units[ordMsg.num] = bct->_gid;
                ordMsg.num++;
            }
        }

        ordMsg.owner = _owner;
        ordMsg.sz = (char *)&ordMsg.units[ordMsg.num] - (char *)&ordMsg;
        ordMsg.mode = mode;
        ordMsg.msgID = UAMSG_REORDER;

        _world->NetBroadcastMessage(&ordMsg, ordMsg.sz, true);
    }
}

void NC_STACK_ypabact::ReorganizeGroup(bact_arg109 *arg)
{
    if ( arg->field_4 && arg->field_4->ShouldHideFromStrategicUI() )
        return;

    switch ( arg->field_0 )
    {
    case 1:
        if ( arg->field_4 )
        {
            if ( arg->field_4->_status == BACT_STATUS_DEAD )
            {
                ypa_log_out("ORG_NEWCHIEF: Dead master\n");
            }
            else if ( arg->field_4 != _parent && arg->field_4 != this )
            {
                _commandID = arg->field_4->_commandID;
                _aggr = arg->field_4->_aggr;

                arg->field_4->AddSubject(this);

                for ( World::RefBactList::iterator it = _kidList.begin(); it != _kidList.end(); )
                {
                    NC_STACK_ypabact *kid = *it;
                    it++;

                    if ( kid->ShouldHideFromStrategicUI() )
                        continue;

                    kid->_aggr = arg->field_4->_aggr;
                    kid->_commandID = arg->field_4->_commandID;

                    arg->field_4->AddSubject(kid);
                    
                    kid->CopyTargetOf(arg->field_4);
                }

                CopyTargetOf(arg->field_4);
                sub_493480(arg->field_4, 1);
            }
        }
        break;

    case 2:
        if ( _host_station != _parent || arg->field_4 != this )
        {
            if ( _status == BACT_STATUS_DEAD )
            {
                ypa_log_out("ORG_BECOMECHIEF dead vehicle\n");
            }
            else
            {
                if ( _host_station != _parent && _host_station )
                    _host_station->AddSubject(this);

                if ( arg->field_4 )
                {
                    CopyTargetOf(arg->field_4);

                    _aggr = arg->field_4->_aggr;
                    _commandID = arg->field_4->_commandID;

                    AddSubject(arg->field_4);

                    for ( World::RefBactList::iterator it = arg->field_4->_kidList.begin(); it != arg->field_4->_kidList.end(); )
                    {
                        NC_STACK_ypabact *kid = *it;
                        it++;

                        if ( kid->ShouldHideFromStrategicUI() )
                            continue;

                        AddSubject(kid);
                        kid->_aggr = arg->field_4->_aggr;

                        kid->CopyTargetOf(this);
                    }

                    _commandID = arg->field_4->_commandID;
                    sub_493480(this, 2);
                }
                else
                {
                    if ( _host_station != _parent && _host_station )
                    {
                        int a4 = _host_station->getROBO_commCount();

                        _commandID = a4;

                        _host_station->setROBO_commCount(a4 + 1);
                    }
                    sub_493480(this, 2);
                }
            }
        }
        break;

    case 3:
        if ( _status == BACT_STATUS_DEAD )
        {
            ypa_log_out("ORG_NEWCOMMAND: dead vehicle\n");
        }
        else if (_host_station)
        {

            if ( _host_station == _parent )
            {
                NC_STACK_ypabact *v14 = sb_0x493984(this, 0);

                if ( v14 )
                    sub_493480(v14, 13);
            }
            else
            {
                _host_station->AddSubject(this);
            }

            int a4 = _host_station->getROBO_commCount();
            _commandID = a4;

            if (_world->_isNetGame)
                _commandID |= _owner << 24;

            _host_station->setROBO_commCount(a4 + 1);
            sub_493480(this, 3);
        }
        break;

    case 4:
        if ( arg->field_4->ShouldHideFromStrategicUI() )
            break;

        if ( arg->field_4->IsParentMyRobo() )
        {
            NC_STACK_ypabact *v19 = sb_0x493984(arg->field_4, 0);

            if ( v19 )
                sub_493480(v19, 14);
        }

        AddSubject(arg->field_4);

        arg->field_4->_commandID = _commandID;

        arg->field_4->CopyTargetOf(this);
        sub_493480(this, 4);
        break;

    case 6:
    {
        int a4 = getBACT_inputting();

        if ( !a4 )
        {
            NC_STACK_ypabact *v21 = sb_0x493984(this, 1);

            if ( v21 )
            {
                v21->AddSubject(this);
                v21->_commandID = _commandID;

                sub_493480(v21, 6);
            }
        }
    }
    break;

    default:
        break;
    }
}

void NC_STACK_ypabact::DoTargetWaypoint()
{
    if ( ( _position.XZ() - _primTpos.XZ() ).length() >= 300.0 )
        return;

    if ( !(_status_flg & BACT_STFLAG_WAYPOINTCCL) )
    {
        _current_waypoint++;

        setTarget_msg arg67;

        if ( _waypoints_count > 1 )
        {
            arg67.tgt_type = BACT_TGT_TYPE_CELL_IND;
            arg67.priority = 0;
            arg67.tgt_pos = _waypoints[ _current_waypoint ];

            SetTarget(&arg67);
        }

        if ( _current_waypoint >= _waypoints_count - 1 )
        {
            if ( _m_cmdID )
            {
                NC_STACK_ypabact *v9 = _world->FindBactByCmdOwn(_m_cmdID, _m_owner);

                if ( v9 )
                {
                    if ( v9->_pSector->IsCanSee(_owner) )
                    {
                        arg67.tgt.pbact = v9;
                        arg67.tgt_type = BACT_TGT_TYPE_UNIT;
                        arg67.priority = 0;

                        SetTarget(&arg67);
                    }
                }
            }

            _m_owner = 0;
            _m_cmdID = 0;
            _status_flg &= ~(BACT_STFLAG_WAYPOINT | BACT_STFLAG_WAYPOINTCCL);
        }
    }
    else
    {

        _current_waypoint++;

        int v5 = _current_waypoint;

        if ( _current_waypoint >= _waypoints_count )
        {
            _current_waypoint = 0;
            v5 = 0;
        }

        setTarget_msg arg67;

        arg67.tgt_type = BACT_TGT_TYPE_CELL_IND;
        arg67.priority = 0;
        arg67.tgt_pos = _waypoints[ v5 ];

        SetTarget(&arg67);
    }
}

size_t NC_STACK_ypabact::TargetAssess(bact_arg110 *arg)
{
    bool primTgtDone = false;
    bool primTgtNear = false;

    if ( arg->tgType == BACT_TGT_TYPE_FRMT 
         && 
        (_primTtype == BACT_TGT_TYPE_FRMT || _secndTtype == BACT_TGT_TYPE_FRMT) )
        return TA_MOVE;

    if ( arg->tgType == BACT_TGT_TYPE_NONE)
        return TA_IGNORE;

    if ( _primTtype == BACT_TGT_TYPE_CELL )
    {
        if ( (_position.XZ() - _primTpos.XZ()).length() < 1800.0 )
            primTgtNear = true;

        if ( _owner == _primT.pcell->owner )
            primTgtDone = true;
    }

    if ( _primTtype == BACT_TGT_TYPE_UNIT )
    {
        if ( (_position.XZ() - _primT.pbact->_position.XZ()).length() < 1800.0 )
            primTgtNear = true;

        if ( _owner == _primT.pbact->_owner )
            primTgtDone = true;
    }

    if ( arg->tgType == BACT_TGT_TYPE_UNIT )
    {
        NC_STACK_ypabact *enemy = NULL;
        bool isSecTgt = false;
        int aggr = 0;

        if ( arg->priority == 1 )
        {
            enemy = _secndT.pbact;
            isSecTgt = true;
            aggr = 50;
        }
        else if ( arg->priority == 0)
        {
            enemy = _primT.pbact;
            isSecTgt = false;
            aggr = 25;
        }

        if ( enemy )
        {
            if ( _world->IsSpectatorBact(enemy) )
                return TA_CANCEL;

            float enemyDistance = (enemy->_position.XZ() - _position.XZ()).length();

            if ( !enemy->_pSector->IsCanSee(_owner) )
                return TA_CANCEL;

            if ( _aggr >= 100 )
            {
                if ( isSecTgt && enemyDistance > 2160.0 )
                    return TA_CANCEL;

                return TA_FIGHT;
            }

            if ( enemy->_owner == 0 || enemy->_owner == _owner )
            {
                if ( enemyDistance < 300.0 )
                    return TA_IGNORE;

                return TA_MOVE;
            }

            if ( _status_flg & BACT_STFLAG_ESCAPE )
            {
                if ( !primTgtNear )
                    return TA_CANCEL;

                return TA_FIGHT;
            }

            if ( _aggr < aggr )
            {
                if ( primTgtNear && primTgtDone )
                    return TA_FIGHT;

                return TA_CANCEL;
            }
            
            if ( !isSecTgt || _bact_type == BACT_TYPES_GUN )
                return TA_FIGHT;

            if ( enemyDistance > 2160.0 )
                return TA_CANCEL;

            vec3d tgtPos;

            if ( IsParentMyRobo() )
            {

                if ( _primTtype == BACT_TGT_TYPE_CELL )
                    tgtPos = _primTpos;
                else if ( _primTtype == BACT_TGT_TYPE_UNIT )
                    tgtPos = _primT.pbact->_position;
                else
                    tgtPos = _position;
            }
            else if ( _parent )
            {
                if ( _parent->_primTtype == BACT_TGT_TYPE_CELL )
                    tgtPos = _parent->_primTpos;
                else if ( _parent->_primTtype == BACT_TGT_TYPE_UNIT )
                    tgtPos = _parent->_primT.pbact->_position;
                else
                    tgtPos = _position;
            }

            if ( (tgtPos.XZ() - _position.XZ()).length() > 3600.0 )
            {
                int v28 = 0;

                for( const TBactAttacker &ainf : _secndT.pbact->_attackersList )
                {
                    if ( ainf.attacker->_secndTtype == BACT_TGT_TYPE_UNIT &&
                         ainf.attacker->_secndT.pbact == _secndT.pbact &&
                         ainf.attacker->_owner == _owner )
                        v28++;

                    if ( v28 > 2 )
                        break;
                }

                if ( v28 > 2 )
                    return TA_CANCEL;
            }
            return TA_FIGHT;
        }
    }
    else if ( arg->tgType == BACT_TGT_TYPE_CELL )
    {
        cellArea *pCell = NULL;
        vec2d cellPos;
        bool isSecTgt = false;
        int aggr = 0;

        if ( _secndTtype == BACT_TGT_TYPE_CELL )
        {
            pCell = _secndT.pcell;
            cellPos = _sencdTpos.XZ();

            aggr = 75;
            isSecTgt = true;
        }
        else if ( _primTtype == BACT_TGT_TYPE_CELL )
        {
            pCell = _primT.pcell;
            cellPos = _primTpos.XZ();

            aggr = 25;
            isSecTgt = false;
        }

        if ( (_status_flg & BACT_STFLAG_WAYPOINT) && !isSecTgt )
        {
            DoTargetWaypoint();
            return TA_MOVE;
        }

        if ( !pCell )
            return TA_IGNORE;

        int cellEnergy = pCell->GetEnergy();

        float cellDistance = (_position.XZ() - cellPos).length();

        if ( _aggr >= 100 )
        {
            if ( cellEnergy <= 0 && pCell->owner == _owner )
            {
                if ( cellDistance < 300.0 )
                    return TA_IGNORE;

                return TA_MOVE;
            }

            return TA_FIGHT;
        }

        if ( cellDistance >= 300.0 )
        {
            if ( _owner != pCell->owner )
            {
                if ( (_status_flg & BACT_STFLAG_ESCAPE) || _aggr < aggr )
                    return TA_CANCEL;

                return TA_FIGHT;
            }

            if ( isSecTgt )
                return TA_CANCEL;

            return TA_MOVE;
        }

        if ( _owner == pCell->owner )
        {
            if ( isSecTgt )
                return TA_CANCEL;

            return TA_IGNORE;
        }

        if ( (_status_flg & BACT_STFLAG_ESCAPE) || _aggr < aggr )
            return TA_CANCEL;

        return TA_FIGHT;
    }

    return TA_IGNORE;
}

void NC_STACK_ypabact::BeamingTimeUpdate(update_msg *arg)
{
    float v14 = 0.66;

    if ( _scale_delay <= 0 )
    {
        if ( _scale_time >= 1980.0 )
        {
            if ( _scale_time >= 3000 )
            {
                _world->ypaworld_func168(this);

                _status_flg |= BACT_STFLAG_CLEAN;

                Die();

                if ( _oflags & BACT_OFLAG_USERINPT )
                    _status_flg |= BACT_STFLAG_NORENDER;
                else
                    Release();

                _status_flg &= ~BACT_STFLAG_SCALE;
            }
            else
            {
                _status_flg |= BACT_STFLAG_SCALE;

                _scale = vec3d(1.0, 30.0, 1.0) - vec3d::OY( (_scale_time - 1980.0) * 30.0 / 1020.0 );
            }
        }
        else
        {
            if ( GetVP() != _vp_genesis )
            {
                setState_msg arg78;
                arg78.newStatus = BACT_STATUS_BEAM;
                arg78.setFlags = 0;
                arg78.unsetFlags = 0;

                SetState(&arg78);
            }

            _status_flg |= BACT_STFLAG_SCALE;

            _scale = vec3d(1.0, 0.0, 1.0) + vec3d::OY( (30 * _scale_time)/ (v14 * 3000.0) );
        }

        _scale_time += arg->frameTime;
    }
    else
    {
        _scale_delay -= arg->frameTime;
    }
}

void NC_STACK_ypabact::StartDestFX(const World::DestFX &fx)
{
    ypaworld_arg146 arg146;

    arg146.pos = _position;
    arg146.vehicle_id = fx.ModelID;

    if ( _radius > 31.0 )    // 31.0
    {
        float len = fx.Pos.length();

        if ( len > 0.1 )
        {
            vec3d pos = fx.Pos / len * _radius;

            arg146.pos += _rotation.Transform(pos);
        }
    }

    NC_STACK_ypabact *bah = _world->ypaworld_func146(&arg146);

    if ( bah )
    {
        _world->ypaworld_func134(bah);

        setState_msg v18;
        v18.newStatus = BACT_STATUS_DEAD;
        v18.setFlags = 0;
        v18.unsetFlags = 0;

        bah->SetStateInternal(&v18);

        bah->_fly_dir = _rotation.Transform(fx.Pos);

        if ( fx.Accel )
            bah->_fly_dir += _fly_dir * _fly_dir_length;

        float len = bah->_fly_dir.length();

        if ( len > 0.001 )
        {
            bah->_fly_dir /= len;
            bah->_fly_dir_length = len;
        }

    }
}

void NC_STACK_ypabact::StartDestFXByType(uint8_t type)
{
    if ( _world->ypaworld_func145(this) )
    {
        size_t a4 = _world->getYW_destroyFX();

        if (a4 > _destroyFX.size())
            a4 = _destroyFX.size();

        for (size_t i = 0; i < a4; i++)
        {
            if ( _destroyFX[i].ModelID )
            {
                const World::DestFX &fx = _destroyFX[i];

                if ( fx.Type == type )
                    StartDestFX(fx);
            }
        }
        
        for (const World::DestFX &x : _extDestroyFX)
        {
            if (x.ModelID != 0 && x.Type == type)
                StartDestFX(x);
        }
    }
}

bool NC_STACK_ypabact::StartChainFXByTrigger(uint8_t trigger)
{
    if ( !_world || !_world->ypaworld_func145(this) || _chainFX.empty() )
        return false;

    bool spawned = false;
    for (const World::TChainFXConfig &fx : _chainFX)
    {
        if ( fx.trigger != trigger )
            continue;

        _world->SpawnChainFX(fx, _position, _rotation);
        spawned = true;
    }

    return spawned;
}

void NC_STACK_ypabact::CorrectPositionOnLand()
{
    float radius;
    if ( _viewer_radius >= 32.0 )
        radius = _viewer_radius;
    else
        radius = 32.0;

    yw_137col coltmp[10];

    ypaworld_arg137 arg137;
    arg137.pos = _position;
    arg137.pos2 = _rotation.AxisX();
    arg137.coll_max = 10;
    arg137.radius = radius;
    arg137.field_30 = 0;
    arg137.collisions = coltmp;

    _world->ypaworld_func137(&arg137);

    vec3d tmp(0.0, 0.0, 0.0);

    float trad = 0.0;

    for (int i = arg137.coll_count - 1; i >= 0; i-- )
    {
        yw_137col *clsn = &arg137.collisions[i];

        if ( clsn->pos2.y < 0.6 )
        {
            vec3d tmp2 = _position - clsn->pos1;

            tmp += clsn->pos2;

            float v36 = radius - tmp2.length();

            if ( trad == 0.0 || trad < v36 )
                trad = v36;
        }
    }

    if ( _viewer_radius >= 32.0 )
        radius = _viewer_radius;
    else
        radius = 32.0;

    arg137.pos = _position;
    arg137.pos2 = -_rotation.AxisX();
    arg137.coll_max = 10;
    arg137.radius = radius;
    arg137.field_30 = 0;
    arg137.collisions = coltmp;

    _world->ypaworld_func137(&arg137);

    for (int i = arg137.coll_count - 1; i >= 0; i-- )
    {
        yw_137col *clsn = &arg137.collisions[i];

        if ( clsn->pos2.y < 0.6 )
        {
            vec3d tmp2 = _position - clsn->pos1;

            tmp += clsn->pos2;

            float v36 = radius - tmp2.length();

            if ( trad == 0.0 || trad < v36 )
                trad = v36;
        }
    }

    float v25 = tmp.length();

    if ( v25 > 0.0001 )
        tmp /= v25;

    _position -= tmp * trad;
}


void NC_STACK_ypabact::CorrectPositionInLevelBox(void *)
{
    int v4 = 0;
    
    constexpr float CurSectrLen = World::CVSectorLength + 10.0;

    if ( _position.x > _wrldSize.x - CurSectrLen )
    {
        v4 = 1;
        _position.x = _wrldSize.x - CurSectrLen;
    }

    if ( _position.x < CurSectrLen )
    {
        v4 = 1;
        _position.x = CurSectrLen;
    }

    if ( _position.z > -CurSectrLen )
    {
        v4 = 1;
        _position.z = -CurSectrLen;
    }

    if ( _position.z < _wrldSize.y + CurSectrLen )
    {
        v4 = 1;
        _position.z = _wrldSize.y + CurSectrLen;
    }

    if ( _oflags & BACT_OFLAG_VIEWER )
    {
        if ( v4 )
        {
            if ( _bact_type != BACT_TYPES_TANK && _bact_type != BACT_TYPES_CAR )
            {
                ypaworld_arg136 arg136;

                arg136.stPos = _position - vec3d::OY(100.0);

                arg136.vect = vec3d::OY(_viewer_overeof + 100.0);
                arg136.flags = 0;

                _world->ypaworld_func136(&arg136);

                if ( arg136.isect )
                    _position.y = arg136.isectPos.y - _viewer_overeof;
            }
        }
    }
}

void ypabact_NetUpdate_VPHACKS(NC_STACK_ypabact *bact, update_msg *upd)
{
    if ( bact->_vp_extra_mode == 1 )
    {
        int engy = bact->_energy_max * 0.7;

        if ( engy < 10000 )
            engy = 10000;

        if ( engy > 25000 )
            engy = 25000;

        sb_0x4874c4(bact, engy, upd->frameTime, 0.75);
        bact->_scale_time -= upd->frameTime;

        if ( bact->_scale_time < 0 )
            bact->_vp_extra[0].SetVP((NC_STACK_base::Instance *)NULL);
    }

    if ( bact->_vp_extra_mode == 2 )
    {
        NC_STACK_yparobo *roboo = dynamic_cast<NC_STACK_yparobo *>(bact);

        if (roboo)
        {
            roboo->_roboBeamTimePre -= upd->frameTime;
            if ( roboo->_roboBeamTimePre <= 0 )
            {
                roboo->_roboBeamTimePre = 0;
                SFXEngine::SFXe.startSound(&bact->_soundcarrier, 10);

                roboo->_roboState &= ~NC_STACK_yparobo::ROBOSTATE_MOVE;
                bact->_vp_extra[0].flags = 0;
                bact->_vp_extra[1].flags = 0;
            }
            else
            {
                if ( roboo->_roboBeamFXTime <= 0 )
                {
                    if ( bact->_vp_extra[0].flags & EVPROTO_FLAG_ACTIVE )
                    {
                        roboo->_roboBeamFXTime = roboo->_roboBeamTimePre / 10;
                        bact->_vp_extra[0].flags &= ~EVPROTO_FLAG_ACTIVE;
                    }
                    else
                    {
                        roboo->_roboBeamFXTime = (1500 - roboo->_roboBeamTimePre) / 10;
                        bact->_vp_extra[0].pos = bact->_position;
                        bact->_vp_extra[0].rotate = bact->_rotation;;
                        bact->_vp_extra[0].flags = 3;
                        bact->_vp_extra[0].scale = 1.25;
                        bact->_vp_extra[0].SetVP(bact->_vp_genesis);
                    }

                    if ( roboo->_vp_extra[1].flags & EVPROTO_FLAG_ACTIVE )
                    {
                        roboo->_roboBeamFXTime = roboo->_roboBeamTimePre / 10;
                        bact->_vp_extra[1].flags &= ~EVPROTO_FLAG_ACTIVE;
                    }
                    else
                    {
                        roboo->_roboBeamFXTime = (1500 - roboo->_roboBeamTimePre) / 10;
                        bact->_vp_extra[1].pos = roboo->_roboBeamPos;
                        bact->_vp_extra[1].rotate = bact->_rotation;
                        bact->_vp_extra[1].flags = 1;
                        bact->_vp_extra[1].SetVP(bact->_vp_genesis);
                    }
                }
                roboo->_roboBeamFXTime -= upd->frameTime;
            }

        }
    }
}

void NC_STACK_ypabact::NetUpdate(update_msg *upd)
{
    ypabact_NetUpdate_VPHACKS(this, upd);

    yw_130arg arg130;
    arg130.pos_x = _position.x;
    arg130.pos_z = _position.z;
    if ( !_world->GetSectorInfo(&arg130) )
    {
        FixBeyondTheWorld();

        arg130.pos_x = _position.x;
        arg130.pos_z = _position.z;
        _world->GetSectorInfo(&arg130);
    }

    cellArea *oldSect = _pSector;

    _cellId = arg130.CellId;
    _pSector = arg130.pcell;

    if ( oldSect != arg130.pcell )
    {
        _cellRef.Detach();
        _cellRef = _pSector->unitsList.push_back(this);
    }

    _clock += upd->frameTime;
    ypabact_UpdateSpawnAtDeathProtection(this);

    UpdateActiveDebuff(upd);
    UpdateDamageFX(upd);
    UpdateDecorationFX(upd);

    ypabact_func117(upd);

    for ( NC_STACK_ypamissile* misl : Utils::IterateListCopy<NC_STACK_ypamissile *>(_missiles_list) )
    {
        misl->SetLauncherBact(this);
        misl->Update(upd);
    }

    sub_481F94(this);

    _tForm.Pos = _position;

    if ( _status_flg & BACT_STFLAG_SCALE )
        _tForm.SclRot = _rotation.Transpose() * mat3x3::Scale(_scale);
    else
        _tForm.SclRot = _rotation.Transpose();

    ypabact_ApplyDamagedSoundPitch(this);

    int units_cnt = upd->units_count;

    upd->units_count = 0;

    for (NC_STACK_ypabact* bct : _kidList.safe_iter())
    {
        bct->NetUpdate(upd);
        upd->units_count++;
    }

    upd->units_count = units_cnt;

    _soundcarrier.Position = _position;
    _soundcarrier.Vector = _fly_dir * _fly_dir_length;
    BeforeSoundCarrierUpdate();

    SFXEngine::SFXe.UpdateSoundCarrier(&_soundcarrier);
    ypabact_UpdateStatusSoundCarrier(this, &_debuff_soundcarrier);
    ypabact_UpdateStatusSoundCarrier(this, &_damaged_shake_carrier);
}

void NC_STACK_ypabact::ypabact_func117(update_msg *upd)
{
    if (_world->_netInterpolate)
        ypabact_func122(upd);
    else
        ypabact_func123(upd);
}

void NC_STACK_ypabact::Release()
{
    if ( _owner )
    {
        if ( _world->_isNetGame )
        {
            if ( _bact_type != BACT_TYPES_MISSLE )
            {
                uamessage_destroyVhcl destrMsg;

                destrMsg.msgID = UAMSG_DESTROYVHCL;
                destrMsg.owner = _owner;
                destrMsg.id = _gid;
                destrMsg.type = _bact_type;

                _world->NetBroadcastMessage(&destrMsg, sizeof(destrMsg), true);
            }
        }
    }

    _world->ypaworld_func144(this);
}

size_t NC_STACK_ypabact::SetStateInternal(setState_msg *arg)
{
    int result = 0;

    if ( arg->newStatus )
        _status = arg->newStatus;

    if ( arg->setFlags )
        _status_flg |= arg->setFlags;

    if ( arg->unsetFlags )
        _status_flg &= ~arg->unsetFlags;

    if ( arg->newStatus == BACT_STATUS_DEAD && (_vp_active != 2 && _vp_active != 3) )
    {
        _energy = -10000;

        SetVP(_vp_dead);

        _vp_active = 2;

        if ( _soundFlags & 2 )
        {
            if ( _oflags & BACT_OFLAG_USERINPT )
            {
                yw_arg180 v43;
                v43.effects_type = 4;

                _world->ypaworld_func180(&v43);
            }

            SFXEngine::SFXe.sub_424000(&_soundcarrier, 1);
            _soundFlags &= ~2;
        }

        if ( _oflags & BACT_OFLAG_USERINPT )
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 8);

        if ( _soundFlags & 1 )
        {
            _soundFlags &= ~1;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 0);
        }

        if ( _soundFlags & 8 )
        {
            _soundFlags &= ~8;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 3);
        }

        if ( _soundFlags & 4 )
        {
            _soundFlags &= ~4;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 2);
        }

        SFXEngine::SFXe.startSound(&_soundcarrier, 7);

        _soundFlags |= 0x80;

        if ( !StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_DESTROYED) )
            StartDestFXByType(World::DestFX::FX_DEATH);

        result = 1;
    }

    if ( arg->newStatus == BACT_STATUS_NORMAL && 1 != _vp_active )
    {
        SetVP(_vp_normal);

        _vp_active = 1;

        if ( _soundFlags & 8 )
        {
            _soundFlags &= ~8;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 3);
        }

        if ( _soundFlags & 4 )
        {
            _soundFlags &= ~4;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 2);
        }

        if ( _soundFlags & 0x80 )
        {
            _soundFlags &= ~0x80;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 7);
        }

        if ( !(_soundFlags & 1) )
        {
            _soundFlags |= 1;
            SFXEngine::SFXe.startSound(&_soundcarrier, 0);
        }

        result = 1;
    }

    if ( arg->newStatus == BACT_STATUS_BEAM && 5 != _vp_active )
    {
        _vp_active = 5;
        SetVP(_vp_genesis);

        if ( _soundFlags & 8 )
        {
            _soundFlags &= ~8;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 3);
        }

        if ( _soundFlags & 4 )
        {
            _soundFlags &= ~4;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 2);
        }

        if ( _soundFlags & 0x80 )
        {
            _soundFlags &= ~0x80;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 7);
        }

        if ( !(_soundFlags & 0x200) )
        {
            _soundFlags |= 0x200;
            SFXEngine::SFXe.startSound(&_soundcarrier, 9);
        }

        StartDestFXByType(World::DestFX::FX_BEAM);

        result = 1;
    }

    if ( arg->newStatus == BACT_STATUS_IDLE && _vp_active != 6 )
    {
        SetVP(_vp_wait);
        _vp_active = 6;

        if ( _soundFlags & 1 )
        {
            _soundFlags &= ~1;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 0);
        }

        if ( _soundFlags & 8 )
        {
            _soundFlags &= ~8;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 3);
        }

        if ( _soundFlags & 0x80 )
        {
            _soundFlags &= ~0x80;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 7);
        }

        if ( !(_soundFlags & 4) )
        {
            _soundFlags |= 4;
            SFXEngine::SFXe.startSound(&_soundcarrier, 2);
        }

        result = 1;
    }

    if ( arg->newStatus == BACT_STATUS_CREATE && 4 != _vp_active )
    {
        _vp_active = arg->newStatus;
        SetVP(_vp_genesis);

        if ( _soundFlags & 2 )
        {
            if ( _oflags & BACT_OFLAG_USERINPT )
            {
                yw_arg180 v46;
                v46.effects_type = 4;

                _world->ypaworld_func180(&v46);
            }

            SFXEngine::SFXe.sub_424000(&_soundcarrier, 1);
            _soundFlags &= ~2;
        }

        if ( _soundFlags & 1 )
        {
            _soundFlags &= ~1;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 0);
        }

        if ( _soundFlags & 4 )
        {
            _soundFlags &= ~4;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 2);
        }

        if ( _soundFlags & 0x80 )
        {
            _soundFlags &= ~0x80;
            SFXEngine::SFXe.sub_424000(&_soundcarrier, 7);
        }

        if ( !(_soundFlags & 8) )
        {
            _soundFlags |= 8;
            SFXEngine::SFXe.startSound(&_soundcarrier, 3);
        }

        StartDestFXByType(World::DestFX::FX_CREATE);

        result = 1;
    }

    if ( arg->unsetFlags == BACT_STFLAG_FIRE && _vp_active == 7 )
    {
        if ( _oflags & BACT_OFLAG_USERINPT )
        {
            yw_arg180 v45;
            v45.effects_type = 4;
            _world->ypaworld_func180(&v45);
        }

        SetVP(_vp_normal);
        _vp_active = 1;

        SFXEngine::SFXe.sub_424000(&_soundcarrier, 1);

        _soundFlags &= ~2;

        result = 1;
    }

    if ( arg->unsetFlags == BACT_STFLAG_DEATH2 && _vp_active == 3 )
    {
        _vp_active = 1;
        SetVP(_vp_normal);

        result = 1;
    }

    if ( arg->setFlags == BACT_STFLAG_FIRE && _vp_active != 7 )
    {
        _vp_active = 7;
        SetVP(_vp_fire);

        if ( !(_soundFlags & 2) )
        {
            if ( _oflags & BACT_OFLAG_USERINPT )
            {
                yw_arg180 v42;
                v42.effects_type = 3;
                _world->ypaworld_func180(&v42);
            }

            _soundFlags |= 2;
            SFXEngine::SFXe.startSound(&_soundcarrier, 1);
        }
        result = 1;
    }

    if ( arg->setFlags == BACT_STFLAG_DEATH2 )
    {
        _status = BACT_STATUS_DEAD;

        if ( _vp_active != 3 )
        {
            SetVP(_vp_megadeth);
            _vp_active = 3;

            if ( _soundFlags & 2 )
            {
                if ( _oflags & BACT_OFLAG_USERINPT )
                {
                    yw_arg180 v44;
                    v44.effects_type = 4;
                    _world->ypaworld_func180(&v44);
                }

                SFXEngine::SFXe.sub_424000(&_soundcarrier, 1);
                _soundFlags &= ~2;
            }

            if ( _oflags & BACT_OFLAG_USERINPT )
                SFXEngine::SFXe.sub_424000(&_soundcarrier, 8);

            if ( _soundFlags & 1 )
            {
                _soundFlags &= ~2;
                SFXEngine::SFXe.sub_424000(&_soundcarrier, 0);
            }

            if ( _soundFlags & 8 )
            {
                _soundFlags &= ~8;
                SFXEngine::SFXe.sub_424000(&_soundcarrier, 3);
            }

            if ( _soundFlags & 4 )
            {
                _soundFlags &= ~4;
                SFXEngine::SFXe.sub_424000(&_soundcarrier, 2);
            }

            if ( _soundFlags & 0x80 )
            {
                _soundFlags &= ~0x80;
                SFXEngine::SFXe.sub_424000(&_soundcarrier, 7);
            }

            SFXEngine::SFXe.startSound(&_soundcarrier, 4);

            if ( !StartChainFXByTrigger(World::TChainFXConfig::TRIGGER_CRASH) )
                StartDestFXByType(World::DestFX::FX_MEGADETH);

            _fly_dir_length = 0;

            result = 1;
        }
    }
    return result;
}

void NC_STACK_ypabact::ChangeSectorEnergy(yw_arg129 *arg)
{
    if ( _world && _world->IsSpectatorBact(this) )
        return;

    arg->OwnerID = World::OWNER_RECALC;

    _world->ypaworld_func129(arg);

    yw_130arg arg130;
    arg130.pos_x = arg->pos.x;
    arg130.pos_z = arg->pos.z;

    int v5;

    if ( _world->GetSectorInfo(&arg130) )
        v5 = arg130.pcell->owner;
    else
        v5 = 0;

    if ( _world->_isNetGame )
    {
        uamessage_sectorEnergy seMsg;
        seMsg.msgID = UAMSG_SECTORENERGY;
        seMsg.owner = _owner;
        seMsg.pos = arg->pos;
        seMsg.energy = arg->field_10;
        seMsg.sectOwner = v5;

        if ( arg->unit )
            seMsg.whoHit = arg->unit->_gid;
        else
            seMsg.whoHit = 0;

        _world->NetBroadcastMessage(&seMsg, sizeof(seMsg), true);
    }
}

void sb_0x4874c4(NC_STACK_ypabact *bact, int a2, int a3, float a4)
{
    if (a2 == 0)
        a2 = 1;

    bact->_vp_extra[0].scale = sqrt( (float)bact->_scale_time / (float)a2 ) * a4;

    if ( bact->_vp_extra[0].scale < 0.0 )
        bact->_vp_extra[0].scale = 0;

    bact->_vp_extra[0].rotate = mat3x3::RotateY(bact->_maxrot * 2.0 * (float)a3 * 0.001) * bact->_vp_extra[0].rotate;
}

void NC_STACK_ypabact::DeadTimeUpdate(update_msg *arg)
{
    if ( _status_flg & BACT_STFLAG_LAND || (_clock - _dead_time > 5000 && _status_flg & BACT_STFLAG_DEATH1 ) )
    {
        if ( !(_status_flg & BACT_STFLAG_DEATH2) )
        {
            setState_msg arg78;
            arg78.newStatus = BACT_STATUS_NOPE;
            arg78.unsetFlags = 0;
            arg78.setFlags = BACT_STFLAG_DEATH2;

            SetState(&arg78);
        }

        _status_flg |= BACT_STFLAG_LAND;

        if ( _owner && _bact_type != BACT_TYPES_MISSLE && _vp_genesis )
        {
            int a2 = _energy_max * 0.7;

            if ( a2 < 10000 )
                a2 = 10000;

            if ( a2 > 25000 )
                a2 = 25000;

            if ( _vp_extra[0].flags & EVPROTO_FLAG_ACTIVE )
            {
                _scale_time -= arg->frameTime;

                if ( _scale_time <= 0 )
                {
                    _vp_extra[0].SetVP((NC_STACK_base::Instance *)NULL);

                    if ( _yls_time <= 0 )
                    {

                        if ( _oflags & BACT_OFLAG_USERINPT )
                            _status_flg |= BACT_STFLAG_NORENDER;
                        else
                            Release();

                    }
                }
                else
                {
                    sb_0x4874c4(this, a2, arg->frameTime, 0.75);

                    if ( _yls_time <= 0 )
                        _status_flg |= BACT_STFLAG_NORENDER;
                }
            }
            else
            {
                _scale_time = a2;
                _vp_extra[0].scale = 0.75;
                _vp_extra[0].pos = _position;
                _vp_extra[0].rotate = _rotation;
                _vp_extra[0].SetVP(_vp_genesis);
                _vp_extra[0].flags |= (EVPROTO_FLAG_ACTIVE | EVPROTO_FLAG_SCALE);

                if ( _world->_isNetGame )
                {
                    uamessage_startPlasma splMsg;
                    splMsg.msgID = UAMSG_STARTPLASMA;
                    splMsg.owner = _owner;
                    splMsg.scale = 0.75;
                    splMsg.time = a2;
                    splMsg.id = _gid;
                    splMsg.pos = _position;
                    splMsg.dir = _rotation;

                    _world->NetBroadcastMessage(&splMsg, sizeof(splMsg), true);
                }
            }
        }
        else if ( _yls_time <= 0 )
        {
            if ( _oflags & BACT_OFLAG_USERINPT )
                _status_flg |= BACT_STFLAG_NORENDER;
            else
                Release();
        }
    }
    else
    {
        bact_arg86 arg86;
        arg86.field_one = 3;
        arg86.field_two = arg->frameTime;

        CrashOrLand(&arg86);
    }
}

void NC_STACK_ypabact::ypabact_func122(update_msg *upd)
{
    float ftime = upd->frameTime * 0.001;

    if ( 0.001 * (upd->gTime - _lastFrmStamp) > 0.0 )
    {
        // Interpolate rotation
        _rotation += _netDRot * ftime;

        vec3d axis = _rotation.AxisX();

        if (axis.normalise() > 0.0001)
            _rotation.SetX( axis );
        else
            _rotation.SetX( vec3d::OX(1.0) );

        axis = _rotation.AxisY();

        if (axis.normalise() > 0.0001)
            _rotation.SetY( axis );
        else
            _rotation.SetY( vec3d::OY(1.0) );

        // Get "90 - angle" between interpolated X and Y
        float as = C_PI_2 - clp_acos( _rotation.AxisX().dot( _rotation.AxisY() ));

        // Calculate correction axis
        vec3d axs = _rotation.AxisX() * _rotation.AxisY();

        // FIX MY MATH ?
        // axs must be 1.0 normalised?

        // Rotate Y axis for 90" between X and Y
        vec3d newY = mat3x3::AxisAngle(axs, -as).Transform( _rotation.AxisY() );

        _rotation.SetY( newY );

        _rotation.SetZ( _rotation.AxisX() * _rotation.AxisY() );

        _position += _fly_dir * (_fly_dir_length * ftime * 6.0);

        CorrectPositionInLevelBox(NULL);
    }
}

void NC_STACK_ypabact::ypabact_func123(update_msg *upd)
{
    float ftime = upd->frameTime * 0.001;
    float stupd = (upd->gTime - _lastFrmStamp) * 0.001;

    if ( stupd > 0.0 )
    {
        _rotation += _netDRot * ftime;

        vec3d axis = _rotation.AxisX();

        if (axis.normalise() > 0.0001)
            _rotation.SetX( axis );
        else
            _rotation.SetX( vec3d::OX(1.0) );

        axis = _rotation.AxisY();

        if (axis.normalise() > 0.0001)
            _rotation.SetY( axis );
        else
            _rotation.SetY( vec3d::OY(1.0) );

        axis = _rotation.AxisZ();

        if (axis.normalise() > 0.0001)
            _rotation.SetZ( axis );
        else
            _rotation.SetZ( vec3d::OZ(1.0) );

        vec3d spd = _fly_dir * _fly_dir_length + _netDSpeed * stupd;

        bool hgun = false;
        if (_bact_type == BACT_TYPES_GUN)
        {
            NC_STACK_ypagun *guno = dynamic_cast<NC_STACK_ypagun *>(this);
            if (guno)
            {
                hgun = guno->IsRoboGun();
            }
        }

        if (_bact_type != BACT_TYPES_GUN || hgun)
            _position = spd * ftime * 6.0;

        CorrectPositionInLevelBox(NULL);

        if ( _status_flg & BACT_STFLAG_LAND )
        {
            ypaworld_arg136 arg136;
            arg136.stPos = _position;
            arg136.vect = _rotation.AxisY() * 200.0;
            arg136.flags = 0;

            _world->ypaworld_func136(&arg136);

            if ( arg136.isect )
                _position = arg136.isectPos - _rotation.AxisY() * _overeof;
        }
    }
}

size_t NC_STACK_ypabact::PathFinder(bact_arg124 *arg)
{
    //path find for ground units (tank & car)
    int maxsteps = arg->steps_cnt;

    for (cellArea &cll : _world->_cells)
    {
        cll.pf_flags = 0;
        cll.cost_to_this = 0;
        cll.cost_to_target = 0;
        cll.pf_treeup= NULL;
    }

    cellArea *target_pcell = _world->GetSector( World::PositionToSectorID( arg->to ) );
    cellArea *start_pcell = _world->GetSector( World::PositionToSectorID( arg->from ) );

    if ( target_pcell == start_pcell )
    {
        arg->steps_cnt = 1;
        arg->waypoints[0].x = arg->to.x;
        arg->waypoints[0].z = arg->to.y;
        return 1;
    }

    std::list<cellArea *> openList;

    start_pcell->pf_flags |= CELL_PFLAGS_IN_CLST;

    cellArea *current_pcell = start_pcell;

    int v23 = Common::ABS(target_pcell->CellId.x - current_pcell->CellId.x);
    int v24 = Common::ABS(target_pcell->CellId.y - current_pcell->CellId.y);

    float sq2 = sqrt(2.0);

    current_pcell->cost_to_target = Common::MIN(v23, v24) * sq2 + Common::ABS(v23 - v24);
    current_pcell->cost_to_this = 0;
    while ( 1 )
    {

        for(int dx = -1; dx <= 1; dx++)
        {
            for(int dz = -1; dz <= 1; dz++)
            {
                if ( dx == 0.0 && dz == 0.0 )
                    continue;

                Common::Point currentSec = current_pcell->CellId;
                Common::Point t = currentSec + Common::Point(dx, dz);

                if ( _world->IsGamePlaySector(t) )
                {
                    cellArea *cell_tzx = _world->GetSector(t);

                    if ( cell_tzx->pf_flags & CELL_PFLAGS_IN_CLST )
                        continue;

                    if ( cell_tzx->addit_cost >= 100 )
                        continue;

                    if (fabs(current_pcell->height - cell_tzx->height) >= 500.0 )
                        continue;

                    if (cell_tzx->SectorType == 1 && cell_tzx != target_pcell)
                    {
                        int32_t hlth = _world->GetLegoBld(cell_tzx, 0, 0);

                        if (_world->_legoArray[hlth].UseCollisionSkelet != _world->_legoArray[hlth].CollisionSkelet)
                            continue;
                    }

                    if ( dx != 0 && dz != 0)
                    {
                        cellArea *cell_tz = _world->GetSector(currentSec.x, t.y);
                        cellArea *cell_tx = _world->GetSector(t.x, currentSec.y);

                        if ( fabs(current_pcell->height - cell_tzx->height) > 300.0
                                || fabs(current_pcell->height - cell_tz->height) > 300.0
                                || fabs(current_pcell->height - cell_tx->height) > 300.0
                                || fabs(cell_tz->height - cell_tx->height) > 300.0
                                || fabs(cell_tzx->height - cell_tz->height) > 300.0
                                || fabs(cell_tzx->height - cell_tx->height) > 300.0)
                            continue;
                    }

                    float new_cost_to_this = sqrt(POW2(dx) + POW2(dz)) + cell_tzx->addit_cost + current_pcell->cost_to_this;

                    int v40 = Common::ABS(target_pcell->CellId.x - t.x);
                    int v41 = Common::ABS(target_pcell->CellId.y - t.y);

                    float new_cost_to_target = Common::MIN(v40, v41) * sq2 + Common::ABS(v40 - v41);

                    if ( (cell_tzx->pf_flags & CELL_PFLAGS_IN_OLST)
                            && new_cost_to_this + new_cost_to_target > cell_tzx->cost_to_this + cell_tzx->cost_to_target )
                        continue;

                    cell_tzx->cost_to_this = new_cost_to_this;
                    cell_tzx->cost_to_target = new_cost_to_target;

                    if ( !(cell_tzx->pf_flags & CELL_PFLAGS_IN_OLST) )
                        openList.push_back(cell_tzx);

                    cell_tzx->pf_treeup = current_pcell;
                    cell_tzx->pf_flags |= CELL_PFLAGS_IN_OLST;
                }
            }
        }



        if ( openList.empty() )
        {
            arg->steps_cnt = 0;
            return 0;
        }

        std::list<cellArea *>::iterator it = openList.begin();
        
        std::list<cellArea *>::iterator selected = it;
        float selected_value = (*selected)->cost_to_this + (*selected)->cost_to_target;
        
        for(it++; it != openList.end(); it++)
        {
            float v49 = (*it)->cost_to_this + (*it)->cost_to_target;

            if ( v49 < selected_value )
            {
                selected = it;
                selected_value = v49;
            }
        }
        
        current_pcell = *selected;

        openList.erase(selected); // Remove OLIST

        current_pcell->pf_flags &= ~CELL_PFLAGS_IN_OLST;
        current_pcell->pf_flags |= CELL_PFLAGS_IN_CLST;

        if ( current_pcell == target_pcell )
            break;
    }

    std::stack<cellArea *> pathCells;

    cellArea *iter_cell = target_pcell;

    while(iter_cell)
    {
        pathCells.push(iter_cell);
        iter_cell = iter_cell->pf_treeup;
    }

    cellArea *curcell = pathCells.top();
    pathCells.pop();
    
    cellArea *nextcell = pathCells.top();

    int v61 = nextcell->CellId.x - curcell->CellId.x;
    int v62 = nextcell->CellId.y - curcell->CellId.y;

    int step_id = 0;

    while ( !pathCells.empty() )
    {
        if ( maxsteps <= 1 || nextcell == target_pcell)
        {
            arg->waypoints[ step_id ].x = arg->to.x;
            arg->waypoints[ step_id ].z = arg->to.y;
            break;
        }

        curcell = nextcell;
        
        pathCells.pop();
        nextcell = pathCells.top();

        if ( nextcell->CellId.x - curcell->CellId.x != v61 || nextcell->CellId.y - curcell->CellId.y != v62 )
        {
            float tx, tz;

            if ( Common::ABS(v61) < Common::ABS(v62) )
            {
                if ( v61 > 0 )
                {
                    tz = 0.0;
                    tx = -200.0;
                }
                else
                {
                    tz = 0.0;
                    tx = 200.0;
                }
            }
            else
            {
                if ( v62 > 0 )
                {
                    tz = 200.0;
                    tx = 0.0;
                }
                else
                {
                    tz = -200.0;
                    tx = 0.0;
                }
            }

            v61 = nextcell->CellId.x - curcell->CellId.x;
            v62 = nextcell->CellId.y - curcell->CellId.y;
            
            arg->waypoints[ step_id ] = World::SectorIDToCenterPos3(curcell->CellId) + vec3d(tx, 0.0, tz);
            maxsteps--;
            step_id++;
        }
    }

    arg->steps_cnt = step_id + 1;
    return 1;
}

void NC_STACK_ypabact::SetKidsPath(int beginWp)
{
    for (NC_STACK_ypabact* &kidunit : _kidList)
    {
        if ( kidunit->ShouldHideFromStrategicUI() )
            continue;

        kidunit->_waypoints_count = _waypoints_count;
        kidunit->_current_waypoint = beginWp;

        kidunit->_status_flg |= BACT_STFLAG_WAYPOINT;

        if ( _status_flg & BACT_STFLAG_WAYPOINTCCL )
            kidunit->_status_flg |= BACT_STFLAG_WAYPOINTCCL;
        else
            kidunit->_status_flg &= ~BACT_STFLAG_WAYPOINTCCL;

        for (int i = 0; i < 32; i++)
        {
            kidunit->_waypoints[i] = _waypoints[i];
        }
    }
}

size_t NC_STACK_ypabact::SetPath(bact_arg124 *arg)
{
    // path find caller for ground squads
    int maxsteps = arg->steps_cnt;

    if ( arg->field_12 >= 2 || arg->field_12 != 1 )
        return 0; //may be 1   CHECK IT

    if ( !PathFinder(arg) )
        return 0;

    setTarget_msg arg67;
    if ( arg->steps_cnt <= 1 )
    {
        arg67.tgt_pos.x = arg->to.x;
        arg67.tgt_pos.z = arg->to.y;
    }
    else
    {
        for (int i = 0; i < arg->steps_cnt; i++)
        {
            _waypoints[i] = arg->waypoints[i];
        }

        _status_flg |= BACT_STFLAG_WAYPOINT;

        _current_waypoint = 0;
        _waypoints_count = arg->steps_cnt;

        SetKidsPath(0);

        arg67.tgt_pos.x = arg->waypoints[0].x;
        arg67.tgt_pos.z = arg->waypoints[0].z;
    }

    arg67.tgt_type = BACT_TGT_TYPE_CELL;
    arg67.priority = 0;
    SetTarget(&arg67);

    for (NC_STACK_ypabact* &kidunit : _kidList)
    {
        if ( kidunit->ShouldHideFromStrategicUI() )
            continue;

        if ( (kidunit->_bact_type == BACT_TYPES_CAR || kidunit->_bact_type == BACT_TYPES_TANK) && _pSector != kidunit->_pSector )
        {
            bact_arg124 arg125;
            arg125.steps_cnt = maxsteps;
            arg125.from = kidunit->_position.XZ();
            arg125.to = arg->to;
            arg125.field_12 = arg->field_12;

            kidunit->SetPath(&arg125);
        }
    }

    return 1;
}



void NC_STACK_ypabact::setBACT_viewer(bool vwr)
{
    uamessage_viewer viewMsg;

    if ( vwr )
    {
        if ( _world && !_world->CanControlUnitInSpectatorMode(this) )
            return;

        // OpenUA custom: mortar platforms are map-only artillery; never let the
        // camera/viewer enter one (that is what made it look possessed in 1st person).
        if ( _world && IsMortarPlatform() )
            return;

        if (_world->_viewerBact)
        {
            if ( _world->_viewerBact->_bact_type != BACT_TYPES_MISSLE )
                _salve_counter = 0;
        }

        _world->ypaworld_func131(this); //Set current bact

        _oflags |= BACT_OFLAG_VIEWER;

        if ( _world->_isNetGame )
            viewMsg.view = 1;

        if ( _bact_type == BACT_TYPES_BACT && !(_status_flg & BACT_STFLAG_LAND) && _status == BACT_STATUS_NORMAL )
            _thraction = _force;

        SFXEngine::SFXe.startSound(&_soundcarrier, 8);
    }
    else
    {
        _oflags &= ~BACT_OFLAG_VIEWER;

        if ( _world->_isNetGame )
            viewMsg.view = 0;

        SFXEngine::SFXe.sub_424000(&_soundcarrier, 8);

        if ( _bact_type != BACT_TYPES_MISSLE && _bact_type != BACT_TYPES_ROBO && _status != BACT_STATUS_DEAD )
        {
            if ( _host_station == _parent )
            {
                if ( !(_status_flg & BACT_STFLAG_WAYPOINT) || !(_status_flg & BACT_STFLAG_WAYPOINTCCL) )
                {
                    for (NC_STACK_ypabact* &node : _kidList)
                        node->CopyTargetOf(this);
                }
            }
            else
            {
                if ( !(_status_flg & BACT_STFLAG_WAYPOINT) || !(_status_flg & BACT_STFLAG_WAYPOINTCCL) )
                    CopyTargetOf(_parent);
            }
        }
    }

    if ( _world->_isNetGame ) // Network message send routine?
    {
        viewMsg.msgID = UAMSG_VIEWER;
        viewMsg.owner = _owner;
        viewMsg.classID = _bact_type;
        viewMsg.id = _gid;

        if ( viewMsg.classID == BACT_TYPES_MISSLE )
        {
            NC_STACK_ypamissile *miss = dynamic_cast<NC_STACK_ypamissile *>(this);
            viewMsg.launcher = miss->GetLauncherBact()->_gid;
        }

        _world->NetBroadcastMessage(&viewMsg, sizeof(viewMsg), true);
    }
}

void NC_STACK_ypabact::setBACT_inputting(bool inpt)
{
    if ( inpt )
    {
        if ( _world && !_world->CanControlUnitInSpectatorMode(this) )
            return;

        // OpenUA custom: mortar platforms are artillery used only from the 2D
        // strategic map. Never let the player take first-person control of them.
        if ( _world && IsMortarPlatform() )
            return;

        _oflags |= BACT_OFLAG_USERINPT;
        _world->setYW_userVehicle(this);

        if ( _bact_type != BACT_TYPES_GUN )
            CorrectPositionOnLand();
    }
    else
    {
        _oflags &= ~BACT_OFLAG_USERINPT;
    }
}

void NC_STACK_ypabact::setBACT_exactCollisions(bool col)
{
    if ( col )
        _oflags |= BACT_OFLAG_EXACTCOLL;
    else
        _oflags &= ~BACT_OFLAG_EXACTCOLL;
}

void NC_STACK_ypabact::setBACT_bactCollisions(bool col)
{
    if ( col )
        _oflags |= BACT_OFLAG_BACTCOLL;
    else
        _oflags &= ~BACT_OFLAG_BACTCOLL;
}

void NC_STACK_ypabact::setBACT_airconst(int air)
{
    _airconst = air;
    _airconst_static = air;
}

void NC_STACK_ypabact::setBACT_landingOnWait(bool lnding)
{
    if ( lnding )
        _oflags |= BACT_OFLAG_LANDONWAIT;
    else
        _oflags &= ~BACT_OFLAG_LANDONWAIT;
}

void NC_STACK_ypabact::setBACT_yourLastSeconds(int ls)
{
    _yls_time = ls;
}

void NC_STACK_ypabact::SetVP(NC_STACK_base *vp)
{
    Common::DeleteAndNull(&_current_vp);
    if (vp)
        _current_vp = vp->GenRenderInstance();
}

void NC_STACK_ypabact::setBACT_aggression(int aggr)
{
    _aggr = aggr;
    
    for (NC_STACK_ypabact* &nod : _kidList)
        nod->_aggr = aggr;
}

void NC_STACK_ypabact::setBACT_extraViewer(bool vwr)
{
    if ( vwr )
        _oflags |= BACT_OFLAG_EXTRAVIEW;
    else
        _oflags &= ~BACT_OFLAG_EXTRAVIEW;
}

void NC_STACK_ypabact::setBACT_alwaysRender(bool rndr)
{
    if ( rndr )
        _oflags |= BACT_OFLAG_ALWAYSREND;
    else
        _oflags &= ~BACT_OFLAG_ALWAYSREND;
}





bool NC_STACK_ypabact::IsNeedsWaypoints() const
{
    if (IsGroundUnit())
        return true;
    
    for (NC_STACK_ypabact* const &unit : _kidList)
    {
        if (unit->IsGroundUnit())
            return true;
    }
    
    return false;
}

void NC_STACK_ypabact::CleanAttackersTarget()
{
    for(auto it = _attackersList.begin();
        it != _attackersList.end();
        it = _attackersList.erase(it))
    {
        NC_STACK_ypabact *attacker = it->attacker;

        if ( attacker->_primTtype == BACT_TGT_TYPE_UNIT &&
             attacker->_primT.pbact == this )
        {
            attacker->_primT.pbact = NULL;
            attacker->_primTtype = BACT_TGT_TYPE_NONE;
            attacker->_assess_time = 0;
        }
        
        if ( attacker->_secndTtype == BACT_TGT_TYPE_UNIT && 
             attacker->_secndT.pbact == this )
        {
            attacker->_secndT.pbact = NULL;
            attacker->_secndTtype = BACT_TGT_TYPE_NONE;
            attacker->_assess_time = 0;
        }
    }
}

void NC_STACK_ypabact::DeleteAttacker(NC_STACK_ypabact *bact, int tgtType)
{
    _attackersList.remove( TBactAttacker(tgtType, bact) );
}

void NC_STACK_ypabact::AddAttacker(NC_STACK_ypabact *bact, int tgtType)
{
    _attackersList.push_back(TBactAttacker(tgtType, bact));
}

bool NC_STACK_ypabact::IsParentMyRobo() const
{
    return (_host_station) && (_parent) && (_host_station == _parent);
}

void NC_STACK_ypabact::ChangeEscapeFlag(bool escape)
{
    if ( escape )
        _status_flg |= BACT_STFLAG_ESCAPE;
    else
        _status_flg &= ~BACT_STFLAG_ESCAPE;

    // May be do it in recursion?
    for( NC_STACK_ypabact* &node : _kidList ) 
    {
        if ( escape )
            node->_status_flg |= BACT_STFLAG_ESCAPE;
        else
            node->_status_flg &= ~BACT_STFLAG_ESCAPE;
    }
}

bool NC_STACK_ypabact::IsHidden() const 
{
    if (_hidden)
        return true;
    
    if (_world && _world->IsHidden(_owner))
        return true;
    
    return false;
}

bool NC_STACK_ypabact::IsHiddenFor(uint8_t owner) const
{
    if (owner == _owner) // Own unit can be seen
        return false;
    
    if (_pSector && _pSector->IsUnhideFor(owner))
        return false;
    
    if (_hidden)
        return true;
    
    if (_world && _world->IsHidden(_owner))
        return true;
    
    return false;
}
