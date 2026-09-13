#ifndef YMISSILE_H_INCLUDED
#define YMISSILE_H_INCLUDED

#include <array>
#include <deque>
#include <vector>

#include "nucleas.h"
#include "ypabact.h"

class NC_STACK_ypamissile: public NC_STACK_ypabact
{
protected:
    // Bomb rotation speed
    const double BOMB_MIN_ANGLE = 0.001;

public:

    enum FLAG_MISL
    {
        FLAG_MISL_VIEW          = (1 << 0),
        FLAG_MISL_COUNTDELAY    = (1 << 1),
        FLAG_MISL_IGNOREBUILDS  = (1 << 2),
    };

    enum MISL_TYPE
    {
        MISL_BOMB       = 1, // Drop down
        MISL_DIRECT     = 2, // Simple missile
        MISL_TARGETED   = 3, // Targeted missile
        MISL_GRENADE    = 4, // Gravity affected
        MISL_OBSAVOID   = 5, // Obstacle avoiding
        MISL_INTERNAL   = 6, // Only for internal use
        MISL_ARC_GRENADE= 7, // OpenNeoUA: dedicated ballistic arc grenade
    };

public:
    virtual size_t Init(IDVList &stak) override;
    virtual size_t Deinit() override;
    virtual size_t SetParameters(IDVList &stak) override;
    virtual void AI_layer1(update_msg *arg) override;
    virtual void AI_layer2(update_msg *arg) override;
    virtual void AI_layer3(update_msg *arg) override;
    virtual void User_layer(update_msg *arg) override;
    virtual void Move(move_msg *arg) override;
    virtual void Render(baseRender_msg *arg) override;
    virtual void SetState(setState_msg *arg) override;
    virtual void Renew() override;
    virtual size_t SetStateInternal(setState_msg *arg) override;

    virtual void ResetViewing(); // Detach camera
    void ConfigureSuicideViewerReturn(const std::vector<int32_t> &squadGids, int32_t hostGid);
    virtual void Impact(); // Apply impulse to all in sector

    // OpenNeoUA custom artillery shell: arm this projectile as a ballistic barrage shell.
    // Once armed it follows a parametric arc (start->target) at the authored artillery
    // speed and force-impacts when the computed flight timer expires, reusing Impact()/AoE/FX.
    int SetupArtilleryShell(const vec3d &startPos, const vec3d &targetPos,
                            float arcHeight, float shellSpeed, bool impactOnSurface);
    int SetupArtilleryShellVerticalBarrage(const vec3d &startPos, const vec3d &targetPos,
                                           int fallDelay, float fallHeight, float verticalSpeed,
                                           float spreadX, float spreadZ, bool impactOnSurface);
    virtual void DetonateAtContact(NC_STACK_ypabact *directHit);
    virtual void DetonateKamikazePayload(NC_STACK_ypabact *directHit);
    virtual void AlignMissile(float dtime = 0.0);
    virtual void AlignMissileByNormal(const vec3d &normal);

    NC_STACK_ypamissile();
    virtual ~NC_STACK_ypamissile() {};

    virtual const std::string ClassName() const {
        return __ClassName;
    };

    enum MISS_ATT
    {
        MISS_ATT_LAUNCHER = 0x80002000,
        MISS_ATT_TYPE = 0x80002002,
        MISS_ATT_LIFETIME = 0x80002004,
        MISS_ATT_DELAY = 0x80002005,
        MISS_ATT_DRIVETIME = 0x80002006,
        MISS_ATT_IGNOREBUILDS = 0x80002007,
        MISS_ATT_POW_HELI = 0x80002008,
        MISS_ATT_POW_TANK = 0x80002009,
        MISS_ATT_POW_FLYER = 0x8000200A,
        MISS_ATT_POW_ROBO = 0x8000200B,
        MISS_ATT_RAD_HELI = 0x8000200C,
        MISS_ATT_RAD_TANK = 0x8000200D,
        MISS_ATT_RAD_FLYER = 0x8000200E,
        MISS_ATT_RAD_ROBO = 0x8000200F,
        MISS_ATT_STHEIGHT = 0x80002010
    };

    virtual void setBACT_viewer(bool) override;

    virtual void SetLauncherBact(NC_STACK_ypabact *);
    void ConfigureSpecificEnergyMultipliers(const World::TWeapProto &proto);
    virtual void SetMissileType(int);
    virtual void SetLifeTime(int);
    virtual void SetDelay(int);
    virtual void SetDriveTime(int);
    virtual void SetIgnoreBuilds(int);
    virtual void SetPowerHeli(int);
    virtual void SetPowerTank(int);
    virtual void SetPowerFlyer(int);
    virtual void SetPowerRobo(int);
    virtual void SetRadiusHeli(float);
    virtual void SetRadiusTank(float);
    virtual void SetRadiusFlyer(float);
    virtual void SetRadiusRobo(float);
    virtual void SetAreaDamage(float unitRadius, int unitEnergy, float buildingRadius, int buildingEnergy,
                               float sectorRadius, int sectorEnergy, int falloff);
    virtual void SetAoeUnitPush(float push); // public continuous intensity 0..10
    virtual void SetDirectPush(float push);  // public continuous intensity 0..10
    virtual void SetArmorPenetrationTargets(int targets);
    virtual void SetStartHeight(float);
    // OpenNeoUA Arc Grenade: dedicated ballistic launch/runtime. The launch
    // method applies elevation once; the network method restores an already
    // resolved velocity without applying the angle twice.
    void SetupArcGrenadeLaunch(float angleDegrees, float gravity, float startSpeed);
    void SetupArcGrenadeVelocity(const vec3d &velocity, float gravity);
    void ConfigureSpawnedProjectileBase(NC_STACK_ypabact *launcher,
                                        NC_STACK_yparobo *hostStation,
                                        uint8_t owner,
                                        const vec3d &direction);
    virtual void SetClusterSpawnedChild(bool child);
    void ConfigureWeaponTracer(const World::TWeaponTracerConfig &config,
                               bool supported);
    void StartWeaponTracer();

    virtual NC_STACK_ypabact *GetLauncherBact();
    virtual int GetMissileType();
    virtual int GetLifeTime();
    virtual int GetDelay();
    virtual int GetDriveTime();
    virtual int GetIgnoreBuilds();
    virtual int GetPowerHeli();
    virtual int GetPowerTank();
    virtual int GetPowerFlyer();
    virtual int GetPowerRobo();
    virtual float GetRadiusHeli();
    virtual float GetRadiusTank();
    virtual float GetRadiusFlyer();
    virtual float GetRadiusRobo();
    virtual float GetStartHeight();

    vec3d CalcForceVector();
    bool TubeCollisionTest(bool applyDirectDamage = true, NC_STACK_ypabact **hitTarget = NULL);
    void SetWeaponSoundEventsEnabled(bool enabled)
    { _weaponSoundEventsEnabled = enabled; }
    bool WeaponSoundEventsEnabled() const
    { return _weaponSoundEventsEnabled; }

protected:
    bool TryGetSpecificEnergyForTarget(NC_STACK_ypabact *bct, float *outEnergy) const;
    int CalcDamageForBact(NC_STACK_ypabact *bct, int baseEnergy);
    int ApplyDamageToBact(NC_STACK_ypabact *bct, int baseEnergy);
    void ApplyDirectHitToBact(NC_STACK_ypabact *bct, bool applyDamage = true);
    bool ApplyDirectPushToBact(NC_STACK_ypabact *bct, vec3d *appliedDir = NULL,
                               float *appliedStrength = NULL, bool enqueue = true,
                               NC_STACK_ypabact *directionTarget = NULL);
    const char *GetAreaDamageSkipReason(NC_STACK_ypabact *bct, bool allowFriendly) const;
    const char *GetAreaPushSkipReason(NC_STACK_ypabact *bct) const;
    bool CanCollideWithWeapon(NC_STACK_ypamissile *other);
    void DetonateWeaponCollision(NC_STACK_ypamissile *other);
    bool IsDirectHitUnit(NC_STACK_ypabact *bct) const;
    void RememberDirectHitUnit(NC_STACK_ypabact *bct);
    bool IsDirectPushRecipient(NC_STACK_ypabact *bct) const;
    void RememberDirectPushRecipient(NC_STACK_ypabact *bct);
    bool IsArmorPenetratedTarget(NC_STACK_ypabact *bct) const;
    bool ShouldArmorPenetrateTarget(NC_STACK_ypabact *bct) const;
    void RememberArmorPenetratedTarget(NC_STACK_ypabact *bct);
    void ApplyArmorPenetrationUnitImpactFX();
    vec3d GetBuildingSlotCenter(const cellArea &cell, int bldX, int bldY) const;
    bool GetBuildingSlotAtPosition(const vec3d &pos, Common::Point *cellId, int *bldX, int *bldY) const;
    const char *GetAreaBuildingSkipReason(const cellArea &cell, int bldX, int bldY) const;
    bool IsDirectHitBuilding(const Common::Point &cellId, int bldX, int bldY) const;
    void RememberDirectHitBuildingAt(const vec3d &pos);
    bool GetSectorSlotAtPosition(const vec3d &pos, Common::Point *cellId, int *bldX, int *bldY) const;
    const char *GetAreaSectorSkipReason(const cellArea &cell, int bldX, int bldY) const;
    bool IsDirectHitSector(const Common::Point &cellId, int bldX, int bldY) const;
    void RememberDirectHitSectorAt(const vec3d &pos);
    void ApplyAreaDamage();
    void ApplyBuildingAreaDamage();
    void ApplySectorAreaDamage();
    void AttachDelayedDetonationToTarget(NC_STACK_ypabact *target);
    NC_STACK_ypabact *FindAttachedTarget();
    void UpdateAttachedDetonationPosition();
    void ApplyAttachedDirectHitDamage();
    void SteerHomingBombDirection(float dtime);
    void UpdateArcGrenadeBallistic(float dtime);
    bool TryClusterSplit(bool impactOrDetonation = false);
    bool CanChainToTarget(NC_STACK_ypabact *target, NC_STACK_ypabact *currentHit) const;
    NC_STACK_ypabact *FindNextChainTarget(NC_STACK_ypabact *currentHit) const;
    bool SpawnChainProjectile(const vec3d &originPos, float originRadius, NC_STACK_ypabact *nextTarget, int childEnergy);
    void TrySpawnChainProjectile(NC_STACK_ypabact *currentHit, int appliedDamage);
    void UpdatePendingChainJump(update_msg *arg);
    void RememberChainHit(NC_STACK_ypabact *target);
    bool IsChainHit(NC_STACK_ypabact *target) const;
    void UpdateArtilleryShellBallistic(update_msg *arg); // OpenNeoUA custom: ballistic shell flight + timed impact
    vec3d GetWeaponTracerPosition() const;
    void UpdateWeaponTracer();
    void RenderWeaponTracer(baseRender_msg *arg);

    struct TWeaponTracerPoint
    {
        vec3d pos;
        int32_t time = 0;
    };

    struct TBuildingHitRef
    {
        Common::Point cellId;
        int bldX = 0;
        int bldY = 0;
    };

    //Data
public:
    static constexpr const char * __ClassName = "ypamissile.class";

protected:
    int _mislType = 0;
    NC_STACK_ypabact *_mislEmitter = NULL;
    int _mislLifeTime   = 0;
    int _mislDriveTime  = 0;
    int _mislDelayTime  = 0;
    int _mislFlags      = 0;
    float _mislStartHeight  = 0.0;
    float _mislEnergyHeli   = 0.0;
    float _mislEnergyTank   = 0.0;
    float _mislEnergyFlyer  = 0.0;
    float _mislEnergyRobo   = 0.0;
    // Fine-grained target multipliers are snapshotted at projectile creation,
    // matching the legacy energy_* copy semantics even if a GEM modifies the
    // Weapon prototype while this projectile is already in flight. Enum-indexed
    // storage reuses the shared class resolver instead of maintaining a second
    // class-to-field switch inside the missile runtime.
    std::array<float, World::VEHICLE_COMBAT_CLASS_COUNT> _mislSpecificEnergy = {};
    std::array<bool, World::VEHICLE_COMBAT_CLASS_COUNT> _mislSpecificEnergyDefined = {};
    float _mislRadiusHeli   = 0.0;
    float _mislRadiusTank   = 0.0;
    float _mislRadiusFlyer  = 0.0;
    float _mislRadiusRobo   = 0.0;
    float _mislAoeUnitRadius    = 0.0;
    int _mislAoeUnitEnergy      = 0;
    float _mislAoeBuildingRadius = 0.0;
    int _mislAoeBuildingEnergy   = 0;
    float _mislAoeSectorRadius   = 0.0;
    int _mislAoeSectorEnergy     = 0;
    int _mislAoeFalloff          = 0;
    float _mislAoeUnitPush       = 0.0f; // configured continuous intensity 0..10
    float _mislDirectPush        = 0.0f; // configured continuous intensity 0..10
    int _mislArmorPenetrationRemaining = 0;
    int _mislClusterAge          = 0;
    // One authoritative lifetime flag for the complete Weapon sound package.
    // Factory sets it before BACT_STATUS_NORMAL can start SND_NORMAL.
    bool _weaponSoundEventsEnabled = true;
    // Minimal previous transform state needed to sweep local compound spheres.
    mat3x3 _collisionOldRotation;
    bool _collisionOldRotationValid = false;
    int _mislClusterGeneration   = 0;
    bool _mislClusterDone        = false;
    bool _mislClusterChild       = false;
    int  _mislChainDepth         = 0;
    int  _mislChainEnergy        = 0;
    bool _mislChainSpawned       = false;
    bool _mislChainAllowFriendly = false;
    bool _mislChainPending       = false;
    int  _mislChainPendingElapsed = 0;
    int  _mislChainPendingDelay  = 0;
    int32_t _mislChainPendingTargetGid = 0;
    int  _mislChainPendingEnergy = 0;
    vec3d _mislChainPendingOrigin;
    float _mislChainPendingOriginRadius = 0.0;
    bool _mislAttachedToTarget   = false;
    int32_t _mislAttachTargetGid = 0;
    vec3d _mislAttachOffset;
    vec3d _mislLastAttachedPosition;
    TSndCarrier _mislClusterSoundCarrier;
    std::vector<int32_t> _mislChainHitGids;
    std::vector<int32_t> _mislArmorPenetratedGids;
    std::vector<int32_t> _mislDirectPushRecipientGids;
    std::vector<int32_t> _suicideViewerReturnGids;
    int32_t _suicideViewerHostGid = 0;
    std::vector<NC_STACK_ypabact *> _mislDirectHitUnits;
    std::vector<TBuildingHitRef> _mislDirectHitBuildings;
    std::vector<TBuildingHitRef> _mislDirectHitSectors;
    // OpenNeoUA Arc Grenade: explicit world-space ballistic velocity. +Y is
    // downward in Urban Assault, so positive gravity increases velocity.y.
    vec3d _arcGrenadeVelocity = vec3d(0.0, 0.0, 0.0);
    float _arcGrenadeGravity = 9.80665f;

    // OpenNeoUA custom artillery shell state (only meaningful when _isArtilleryShellProjectile).
    bool  _isArtilleryShellProjectile = false;
    vec3d _artilleryShellStartPos;
    vec3d _artilleryShellTargetPos;
    int   _artilleryShellElapsed    = 0;
    int   _artilleryShellFlightTime = 0;
    float _artilleryShellArcHeight  = 0.0;
    bool  _artilleryShellImpactOnSurface = false;
    bool  _artilleryShellVerticalBarrage = false;
    bool  _artilleryShellVerticalTransferred = false;
    vec3d _artilleryShellVerticalApexOffset;
    int   _artilleryShellVerticalAscentTime = 0;
    int   _artilleryShellVerticalFallStartTime = 0;
    int   _artilleryShellVerticalDescentTime = 0;
    World::TWeaponTracerConfig _weaponTracer;
    bool _weaponTracerStarted = false;
    uint32_t _weaponTracerVisualSeed = 0;
    std::deque<TWeaponTracerPoint> _weaponTracerPoints;
};

#endif // YMISSILE_H_INCLUDED
