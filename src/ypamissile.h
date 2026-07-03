#ifndef YMISSILE_H_INCLUDED
#define YMISSILE_H_INCLUDED

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
    virtual void SetState(setState_msg *arg) override;
    virtual void Renew() override;
    virtual size_t SetStateInternal(setState_msg *arg) override;
    
    virtual void ResetViewing(); // Detach camera
    virtual void Impact(); // Apply impulse to all in sector

    // OpenUA custom mortar shell: arm this projectile as a ballistic barrage shell.
    // Once armed it follows a parametric arc (start->target) and force-impacts when
    // its flight timer expires, reusing the normal Impact()/AoE/FX path.
    void SetupMortarShell(const vec3d &startPos, const vec3d &targetPos,
                          int flightTime, float arcHeight, const vec3d &driftVec, bool impactOnSurface);
    virtual void DetonateAtContact(NC_STACK_ypabact *directHit);
    virtual void DetonateSeekAndExplodePayload(NC_STACK_ypabact *directHit);
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
    virtual void SetMissileType(int);
    virtual void SetLifeTime(int);
    virtual void SetDelay(int);
    virtual void SetDriveTime(int);
    virtual void SetIgnoreBuilds(int);
    virtual void SetPowerHeli(int);
    virtual void SetPowerTank(int);
    virtual void SetPowerFlyer(int);
    virtual void SetPowerRobo(int);
    virtual void SetAreaDamage(float unitRadius, int unitEnergy, float buildingRadius, int buildingEnergy,
                               float sectorRadius, int sectorEnergy, int falloff);
    virtual void SetAoeUnitPush(int push);
    virtual void SetAoeUnitPushAtDeath(int push);
    virtual void SetDirectPush(int push);
    virtual void SetPushAtDeath(int push);
    // Per-class weapon radius API: inactive (always set to 0.0) but kept for ABI/object-system
    // compatibility. Collision uses weapon.radius only. Do not remove. See GetRadius* below.
    virtual void SetRadiusHeli(float);
    virtual void SetRadiusTank(float);
    virtual void SetRadiusFlyer(float);
    virtual void SetRadiusRobo(float);
    virtual void SetStartHeight(float);
    virtual void SetClusterSpawnedChild(bool child);

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
    // Inactive per-class weapon radius accessors (currently no callers); kept for
    // ABI/object-system compatibility alongside SetRadius* above. Do not remove.
    virtual float GetRadiusHeli();
    virtual float GetRadiusTank();
    virtual float GetRadiusFlyer();
    virtual float GetRadiusRobo();
    virtual float GetStartHeight();
    
    vec3d CalcForceVector();
    bool TubeCollisionTest(bool applyDirectDamage = true, NC_STACK_ypabact **hitTarget = NULL);

protected:
    int CalcDamageForBact(NC_STACK_ypabact *bct, int baseEnergy);
    int ApplyDamageToBact(NC_STACK_ypabact *bct, int baseEnergy);
    void ApplyDirectHitToBact(NC_STACK_ypabact *bct);
    bool ApplyDirectPushToBact(NC_STACK_ypabact *bct, vec3d *appliedDir = NULL, float *appliedStrength = NULL, bool enqueue = true);
    bool IsNewDeath1ForPushAtDeath(NC_STACK_ypabact *bct, bool wasAlive) const;
    void ApplyPushAtDeath(NC_STACK_ypabact *bct, const vec3d &fallbackDir);
    void ApplyAoePushAtDeath(NC_STACK_ypabact *bct, const vec3d &pushDir, float distance);
    const char *GetAreaDamageSkipReason(NC_STACK_ypabact *bct, bool allowFriendly) const;
    const char *GetAreaPushSkipReason(NC_STACK_ypabact *bct, bool allowFriendly) const;
    bool CanCollideWithWeapon(NC_STACK_ypamissile *other) const;
    void DetonateWeaponCollision(NC_STACK_ypamissile *other);
    bool IsDirectHitUnit(NC_STACK_ypabact *bct) const;
    void RememberDirectHitUnit(NC_STACK_ypabact *bct);
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
    bool TryClusterSplit();
    bool CanChainToTarget(NC_STACK_ypabact *target, NC_STACK_ypabact *currentHit) const;
    NC_STACK_ypabact *FindNextChainTarget(NC_STACK_ypabact *currentHit) const;
    bool SpawnChainProjectile(const vec3d &originPos, float originRadius, NC_STACK_ypabact *nextTarget, int childEnergy);
    void TrySpawnChainProjectile(NC_STACK_ypabact *currentHit, int appliedDamage);
    void UpdatePendingChainJump(update_msg *arg);
    void RememberChainHit(NC_STACK_ypabact *target);
    bool IsChainHit(NC_STACK_ypabact *target) const;
    void UpdateMortarBallistic(update_msg *arg); // OpenUA custom: ballistic shell flight + timed impact

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
    float _mislAoeUnitRadius    = 0.0;
    int _mislAoeUnitEnergy      = 0;
    float _mislAoeBuildingRadius = 0.0;
    int _mislAoeBuildingEnergy   = 0;
    float _mislAoeSectorRadius   = 0.0;
    int _mislAoeSectorEnergy     = 0;
    int _mislAoeFalloff          = 0;
    int _mislAoeUnitPush         = 0;
    int _mislAoeUnitPushAtDeath  = 0;
    int _mislDirectPush          = 0;
    int _mislPushAtDeath = 0;
    int _mislClusterAge          = 0;
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
    std::vector<NC_STACK_ypabact *> _mislDirectHitUnits;
    std::vector<TBuildingHitRef> _mislDirectHitBuildings;
    std::vector<TBuildingHitRef> _mislDirectHitSectors;
    // Legacy/deprecated: kept for script/API compatibility, ignored for gameplay.
    float _mislRadiusHeli   = 0.0;
    float _mislRadiusTank   = 0.0;
    float _mislRadiusFlyer  = 0.0;
    float _mislRadiusRobo   = 0.0;

    // OpenUA custom mortar shell state (only meaningful when _isMortarProjectile).
    bool  _isMortarProjectile = false;
    vec3d _mortarStartPos;
    vec3d _mortarTargetPos;
    vec3d _mortarDriftVec;
    int   _mortarElapsed    = 0;
    int   _mortarFlightTime = 0;
    float _mortarArcHeight  = 0.0;
    bool  _mortarImpactOnSurface = false;
};

#endif // YMISSILE_H_INCLUDED
