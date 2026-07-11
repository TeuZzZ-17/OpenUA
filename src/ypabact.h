#ifndef YBACT_H_INCLUDED
#define YBACT_H_INCLUDED

#include "nucleas.h"
#include "system/gfx.h"
#include "base.h"
#include "listnode.h"
#include "types.h"
#include "world/protos.h"

// !!!! if period is small, then this never happen
#define BACT_MIN_ANGLE 0.0002

class NC_STACK_ypabact;
class NC_STACK_yparobo;
class NC_STACK_ypamissile;

class NC_STACK_ypaworld;

struct yw_arg129;

struct cellArea;





enum EVPROTO_FLAG
{
    EVPROTO_FLAG_ACTIVE = 1,
    EVPROTO_FLAG_SCALE  = 2
};

struct extra_vproto
{
    float scale = 0.0;
    vec3d pos;
    mat3x3 rotate;
    int flags = 0;
    NC_STACK_base::Instance *vp = NULL;
    
    ~extra_vproto()
    {
        if (vp)
            delete vp;
    }
    
    void SetVP(NC_STACK_base::Instance *newVP)
    {
        Common::DeleteAndNull(&vp);
        vp = newVP;
    }
    
    void SetVP(NC_STACK_base *bas)
    {
        Common::DeleteAndNull(&vp);
        
        if (bas)
            vp = bas->GenRenderInstance();
        else
            vp = NULL;
    }
};

struct TActiveDebuffState
{
    bool active = false;
    std::string name;
    std::string icon;
    int damage = 0;
    float damage_percent = 0.0;
    int tick_time = 1000;
    int expire_time = 0;
    int next_tick_time = 0;
    float force_malus = 0.0;
    float maxrot_malus = 0.0;
    float shield_malus = 0.0;
    float snd_pitch_mult = 1.0;
    std::vector<int16_t> fx_vps;
    float fx_random_pos = 0.0;
    TSampleData *snd_sample = NULL;
    int snd_volume = 120;
    int snd_pitch = 0;
    int32_t source_gid = 0;

    void Clear()
    {
        active = false;
        name.clear();
        icon.clear();
        damage = 0;
        damage_percent = 0.0;
        tick_time = 1000;
        expire_time = 0;
        next_tick_time = 0;
        force_malus = 0.0;
        maxrot_malus = 0.0;
        shield_malus = 0.0;
        snd_pitch_mult = 1.0;
        fx_vps.clear();
        fx_random_pos = 0.0;
        snd_sample = NULL;
        snd_volume = 120;
        snd_pitch = 0;
        source_gid = 0;
    }
};

enum BACT_TGT_TYPE
{
    BACT_TGT_TYPE_NONE = 0,
    BACT_TGT_TYPE_CELL = 1,
    BACT_TGT_TYPE_UNIT = 2,
    BACT_TGT_TYPE_FRMT = 3,
    BACT_TGT_TYPE_DRCT = 4,
    BACT_TGT_TYPE_CELL_IND = 5,
    BACT_TGT_TYPE_UNIT_IND = 6
};

enum BACT_STATUS
{
    BACT_STATUS_NOPE = 0,
    BACT_STATUS_NORMAL = 1,
    BACT_STATUS_DEAD = 2,
    BACT_STATUS_IDLE = 3,
    BACT_STATUS_CREATE = 4,
    BACT_STATUS_BEAM = 5
};

enum BACT_STFLAG
{
    BACT_STFLAG_FIGHT_P     = 1, // Primary target fight
    BACT_STFLAG_FIGHT_S     = 2, // Secondary target fight
    BACT_STFLAG_FORMATION   = 4,
    //BACT_STFLAG_DODGE = 8,          //Unused flag
    BACT_STFLAG_DODGE_LEFT  = 0x10,
    BACT_STFLAG_DODGE_RIGHT = 0x20,
    BACT_STFLAG_MOVE        = 0x40,
    BACT_STFLAG_UPWRD       = 0x80,
    BACT_STFLAG_FIRE        = 0x100,
    BACT_STFLAG_LAND        = 0x200,
    BACT_STFLAG_DEATH1      = 0x400,
    BACT_STFLAG_DEATH2      = 0x800,
    //BACT_STFLAG_AKKU        = 0x1000,  //Unused flag
    BACT_STFLAG_APPROACH    = 0x2000,
    BACT_STFLAG_ESCAPE      = 0x4000,
    BACT_STFLAG_XLEFT       = 0x8000,
    BACT_STFLAG_YUP         = 0x10000,
    BACT_STFLAG_BCRASH      = 0x20000,
    BACT_STFLAG_LCRASH      = 0x40000,
    BACT_STFLAG_UNUSE       = 0x80000,
    BACT_STFLAG_SCALE       = 0x100000,
    BACT_STFLAG_SHAKE       = 0x200000,
    BACT_STFLAG_NORENDER    = 0x400000,
    BACT_STFLAG_ISVIEW      = 0x800000,
    BACT_STFLAG_SEFFECT     = 0x1000000,
    BACT_STFLAG_CLEAN       = 0x2000000,
    BACT_STFLAG_WAYPOINT    = 0x4000000,
    BACT_STFLAG_WAYPOINTCCL = 0x8000000,
    BACT_STFLAG_NOMSG       = 0x10000000,
    BACT_STFLAG_DSETTED     = 0x20000000,
    BACT_STFLAG_ATTACK      = 0x40000000
};

enum BACT_OFLAG
{
    BACT_OFLAG_VIEWER     = 1,
    BACT_OFLAG_USERINPT   = 2,
    BACT_OFLAG_EXACTCOLL  = 4,
    BACT_OFLAG_BACTCOLL   = 8,
    BACT_OFLAG_LANDONWAIT = 0x10,
    BACT_OFLAG_EXTRAVIEW  = 0x20,
    BACT_OFLAG_ALWAYSREND = 0x40
};

union BactTarget
{
    NC_STACK_ypabact *pbact;
    cellArea *pcell;
};




enum BACT_TYPES
{
    BACT_TYPES_NOPE = 0,
    BACT_TYPES_BACT = 1,
    BACT_TYPES_TANK = 2,
    BACT_TYPES_ROBO = 3,
    BACT_TYPES_MISSLE = 4,
    BACT_TYPES_ZEPP = 5,  //No real class
    BACT_TYPES_FLYER = 6,
    BACT_TYPES_UFO = 7,
    BACT_TYPES_CAR = 8,
    BACT_TYPES_GUN = 9,
    BACT_TYPES_HOVER = 10 //No real class
};

struct newMaster_msg
{
    NC_STACK_ypabact *bact;
    World::RefBactList *list;
};

struct bact_arg80
{
    vec3d pos;
    int field_C;
};

struct setState_msg
{
    int newStatus;
    int setFlags;
    int unsetFlags;
    
    setState_msg()
    {
        newStatus = 0;
        setFlags = 0;
        unsetFlags = 0;
    }
};

struct update_msg
{
    int gTime = 0;
    int frameTime = 0;
    TInputState *inpt = NULL;
    int units_count = 0;
    int user_action = 0;
    int protoID = 0;
    NC_STACK_ypabact *selectBact = NULL;
    cellArea *target_Sect = NULL;
    vec3d target_point;
    NC_STACK_ypabact *target_Bact = NULL;
    int energy = 0;
};

struct setTarget_msg
{
    uint8_t tgt_type;
    uint32_t priority;
    BactTarget tgt;
    vec3d tgt_pos;
};

struct bact_arg124
{
    vec2d from;
    vec2d to;
    int16_t steps_cnt;
    int16_t field_12;
    vec3d waypoints[32];
};

struct bact_arg84
{
    int energy;
    NC_STACK_ypabact *unit;
};

struct bact_arg88
{
    vec3d pos1;
};

struct bact_arg94
{
    int field_0;
    vec3d pos1;
    //vec3d pos2;
};

struct move_msg
{
    float field_0;
    vec3d vec;
    int flag;
};

struct bact_arg81
{
    int enrg_type;
    int enrg_sum;
};

struct bact_arg109
{
    int field_0;
    NC_STACK_ypabact *field_4;
};

struct bact_arg92
{
    vec3d pos;
    int energ1;
    int energ2;
    int field_14;
};

struct bact_hudi
{
    int field_0;
    int field_4;
    float field_8;
    float field_C;
    float field_10;
    float field_14;
    NC_STACK_ypabact *field_18;

    bact_hudi()
    {
        field_0 = 0;
        field_4 = 0;
        field_8 = 0.0;
        field_C = 0.0;
        field_10 = 0.0;
        field_14 = 0.0;
        field_18 = NULL;
    }
};

struct bact_arg105
{
    vec3d field_0;
    float field_C;
    int field_10;
};

struct bact_arg106
{
    int field_0;
    vec3d field_4;
    NC_STACK_ypabact *ret_bact;
};

struct bact_arg79
{
    vec3d direction;
    vec3d start_point;
    int tgType;
    BactTarget target;
    vec3d tgt_pos;
    int weapon;
    int g_time;
    int flags;
};

// OpenUA custom: internal LaunchMissile() flag.  When the player is in
// first-person/viewer control and holds the handbrake while firing, weapon
// recoil is reduced without touching push_resistance, push or ApplyImpulse().
static const int BACT_ARG79_FLAG_RECOIL_BRAKE_HELD = 0x100;

struct bact_arg75
{
    vec3d pos;
    BactTarget target;
    int prio;
    float fperiod;
    int g_time;
};

struct bact_arg110
{
    int tgType;
    int priority;
};

struct bact_arg86
{
    int field_one;
    int field_two;
};

struct bact_arg101
{
    vec3d pos;
    int unkn;
    float radius;
};

struct bact_arg83
{
    int energ;
    vec3d pos;
    vec3d pos2;
    float force;
    float mass;
};

struct TBactAttacker
{
	int type = 0; // primary / secondary
	NC_STACK_ypabact *attacker = NULL;

    TBactAttacker(int t = 0, NC_STACK_ypabact *bact = NULL)
    : type(t), attacker(bact) {};

    inline bool operator==(const TBactAttacker &b)
    {
        if (type != b.type)
            return false;
        
        if (attacker != b.attacker)
            return false;
        return true;
    }
};


class NC_STACK_ypabact: public NC_STACK_nucleus
{
public:
    enum TA
    {
        TA_CANCEL,
        TA_MOVE,
        TA_FIGHT,
        TA_IGNORE
    };
public:
    virtual size_t Init(IDVList &stak);
    virtual size_t Deinit();
    virtual size_t SetParameters(IDVList &stak);
    virtual void Update(update_msg *arg);
    virtual void Render(baseRender_msg *arg);
    virtual void SetTarget(setTarget_msg *arg);
    virtual void AI_layer1(update_msg *arg);
    virtual void AI_layer2(update_msg *arg);
    virtual void AI_layer3(update_msg *arg);
    virtual void User_layer(update_msg *arg);
    virtual void AddSubject(NC_STACK_ypabact *kid);
    virtual void SetNewMaster(newMaster_msg *arg);
    virtual void Move(move_msg *arg);
    virtual void FightWithBact(bact_arg75 *arg);
    virtual void FightWithSect(bact_arg75 *arg);
    virtual void Die();
    virtual void SetState(setState_msg *arg);
    virtual size_t LaunchMissile(bact_arg79 *arg);
    virtual size_t SetPosition(bact_arg80 *arg);
    virtual void GetSummary(bact_arg81 *arg);
    virtual void EnergyInteract(update_msg *arg);
    virtual void BeforeSoundCarrierUpdate();
    void UpdateCarrierSpawn(update_msg *arg);
    void UpdateProximityDefense(update_msg *arg);
    void UpdateMortar(update_msg *arg); // OpenUA custom: radar-guided mortar barrage AI
    bool StartMortarBarrage(const vec3d &targetCenter); // OpenUA custom: begin a barrage at a point
    bool CanManualMortar(const vec3d &targetPos, int *outWeaponId, bool *outReadyNow = nullptr); // OpenUA custom: manual-call validity (+ ready-now flag)
    void QueueManualMortar(const vec3d &targetPos); // OpenUA custom: queue a manual strike during cooldown
    bool IsMortarPlatform(); // OpenUA custom: true if any weapon slot is a mortar (blocks first-person entry)
    bool IsManualMortarPlatform(); // OpenUA custom: mortar platform that opted into manual map-click control
    float GetMortarBarrageRadius(); // OpenUA custom: bombardment zone radius of this unit's mortar (0 if none)
    float GetMortarReadinessRatio(); // OpenUA custom: 0..1 cooldown readiness for UI bars
    void UpdateSeekAndExplode(update_msg *arg);
    bool ApplySeekAndExplodeRammingGuidance(bool clearAvoidanceFlags);
    // OpenUA custom: continuous laser beam ("model = laser"). UpdateLaser drives the
    // static tick damage, beam state and loop sound each frame; the firing paths only
    // register a per-frame request via RequestLaserFire().
    void UpdateLaser(update_msg *arg);
    void RequestLaserFire(int weaponId, bact_arg79 *arg);
    void StopLaser(); // disconnect: reset tick state, stop loop sound, hide beam
    void UpdateVerticalLaser(update_msg *arg); // OpenUA custom: model = vertical_laser downward beam
    void RequestVerticalLaserFire(int weaponId, bact_arg79 *arg);
    void StopVerticalLaser();
    void UpdateDamageFX(update_msg *arg);
    void UpdateDecorationFX(update_msg *arg);
    void AddAoePush(const vec3d &dir, float distance); // queue aoe_unit_push knockback
    void ApplyWeaponRecoil(const vec3d &dir, float recoil);
    bool ApplyUnitCollisionEffects(NC_STACK_ypabact *target, const vec3d &dirToTarget, float impactSpeed);
    void UpdateWeaponRecoilPush(update_msg *arg);      // integrate/decay weapon recoil push or visual offset
    void UpdateAoePush(update_msg *arg);               // integrate/decay it per frame
    void ApplyWeaponDebuff(World::TWeaponDebuffConfig &debuff, NC_STACK_ypabact *source);
    void UpdateActiveDebuff(update_msg *arg);
    void ClearActiveDebuff();
    virtual void ApplyImpulse(bact_arg83 *arg);
    virtual void ModifyEnergy(bact_arg84 *arg);
    float GetEffectiveShield() const;
    float GetEffectiveShieldWithAdditionalMalus(float additionalMalus) const;
    virtual bool ypabact_func85(vec3d *arg);
    virtual size_t CrashOrLand(bact_arg86 *arg);
    virtual size_t CollisionWithBact(int arg);
    virtual void Recoil(bact_arg88 *arg);
    virtual void ypabact_func89(IDVPair *arg);
    virtual NC_STACK_ypabact *GetSectorTarget(Common::Point CellId) const;
    virtual void GetBestSectorPart(vec3d *arg);
    virtual void GetForcesRatio(bact_arg92 *arg);
    virtual void ypabact_func93(IDVPair *arg);
    virtual void GetFormationPosition(bact_arg94 *arg);
    virtual void ypabact_func95(IDVPair *arg);
    virtual void Renew();
    virtual void HandBrake(update_msg *arg);
    virtual void ypabact_func98(IDVPair *arg);
    virtual void CreationTimeUpdate(update_msg *arg);
    virtual size_t IsDestroyed();
    virtual size_t CheckFireAI(bact_arg101 *arg);
    virtual void MarkSectorsForView();
    virtual void ypabact_func103(IDVPair *arg);
    virtual void StuckFree(update_msg *arg);
    virtual size_t FireMinigun(bact_arg105 *arg);
    virtual size_t UserTargeting(bact_arg106 *arg);
    virtual void HandleVisChildrens(int *arg);
    virtual bool GetFightMotivation(float *arg);
    virtual void ReorganizeGroup(bact_arg109 *arg);
    virtual size_t TargetAssess(bact_arg110 *arg);
    virtual bool TestTargetSector(const NC_STACK_ypabact *) const { return true; };
    virtual void BeamingTimeUpdate(update_msg *arg);
    virtual void StartDestFXByType(uint8_t arg);
    virtual void CorrectPositionOnLand();
    virtual void CorrectPositionInLevelBox(void *);
    virtual void NetUpdate(update_msg *arg);
    virtual void ypabact_func117(update_msg *arg);
    virtual void Release();
    virtual size_t SetStateInternal(setState_msg *arg);
    virtual void ChangeSectorEnergy(yw_arg129 *arg);
    virtual void DeadTimeUpdate(update_msg *arg);
    virtual void ypabact_func122(update_msg *arg);
    virtual void ypabact_func123(update_msg *arg);
    virtual size_t PathFinder(bact_arg124 *arg);
    virtual size_t SetPath(bact_arg124 *arg);
    
    virtual bool IsHidden() const;
    virtual bool IsHiddenFor(uint8_t owner) const;
    bool ShouldHideFromStrategicUI() const;

    // OpenUA custom: vehicle-only "invisible" stealth-until-first-attack.
    // IsInvisibleUnrevealed()  -> true while the unit is still cloaked (no render,
    //                             radar/map/UI, sound, decoration FX, AI targeting).
    // CanBeSeenByAIOrRadar()   -> convenience inverse used by AI/radar candidate filters.
    // RevealInvisibleOnAttack()-> permanently reveals this unit (and, for attached
    //                             unit-gun/dummy children, their carrier) the moment it
    //                             performs a real attack. No-op once revealed/normal.
    bool IsInvisibleUnrevealed() const { return _invisibleUnrevealed; }
    bool CanBeSeenByAIOrRadar() const { return !_invisibleUnrevealed; }
    void RevealInvisibleOnAttack();
    bool IsCockpitCameraAvailable() const;
    bool IsCockpitCameraActive() const;
    bool IsPlayerFirstPersonCameraActive() const;
    bool ShouldRenderCockpitCameraBody() const;
    vec3d GetCockpitCameraPosition() const;
    void ToggleCockpitCameraMode();
    bool HasMinigun() const;
    bool UsesVehicleMinigunTiming() const { return !_mgun_set && _mgun_shot_time > 0; }
    int GetMinigunShotTime(bool userControlled, int frameDeltaMs) const;
    float GetMinigunPower() const { return _mgun_power_set ? _mgun_power : _gun_power; }
    float GetMinigunAngle() const { return _mgun_angle_set ? _mgun_angle : _gun_angle; }

    NC_STACK_ypabact();
    virtual ~NC_STACK_ypabact();
    
    virtual const std::string ClassName() const {
        return __ClassName;
    };

    enum BACT_ATT
    {
        BACT_ATT_WORLD = 0x80001001,
        BACT_ATT_PTRANSFORM = 0x80001002,
        BACT_ATT_VIEWER = 0x80001004,       // bool
        BACT_ATT_INPUTTING = 0x80001005,    // bool
        BACT_ATT_EXACTCOLL = 0x80001006,    // bool
        BACT_ATT_BACTCOLL = 0x80001007,     // bool
        BACT_ATT_ATTACKLIST = 0x80001008,
        BACT_ATT_AIRCONST = 0x80001009,
        BACT_ATT_LANDINGONWAIT = 0x8000100A, // bool
        BACT_ATT_YOURLS = 0x8000100B,
        BACT_ATT_VISPROT = 0x8000100C,
        BACT_ATT_AGGRESSION = 0x8000100D,
        BACT_ATT_COLLNODES = 0x8000100E,
        //BACT_ATT_VPTRANSFORM = 0x8000100F,
        BACT_ATT_EXTRAVIEWER = 0x80001010,  // bool
        BACT_ATT_P_ATTACKNODE = 0x80001011,
        BACT_ATT_S_ATTACKNODE = 0x80001012,
        BACT_ATT_ALWAYSRENDER = 0x80001013  // bool
    };

    virtual void setBACT_viewer(bool);
    virtual void setBACT_inputting(bool);
    virtual void setBACT_exactCollisions(bool);
    virtual void setBACT_bactCollisions(bool);
    virtual void setBACT_airconst(int);
    virtual void setBACT_landingOnWait(bool);
    virtual void setBACT_yourLastSeconds(int);
    virtual void SetVP(NC_STACK_base *vp);
    virtual void setBACT_aggression(int);
    virtual void setBACT_extraViewer(bool);
    virtual void setBACT_alwaysRender(bool);

    NC_STACK_ypabact * GetEnemyCandidateInSector(const cellArea &cell, float *radius, char *job) const;

    virtual NC_STACK_ypaworld *getBACT_pWorld()
    { return _world; }

    int GetCurrentWeaponId();
    
    virtual TF::TForm3D *getBACT_pTransform()
    { return &_tForm; }
    
    virtual bool getBACT_viewer() const 
    { return (_oflags & BACT_OFLAG_VIEWER) != 0; }
    
    virtual bool getBACT_inputting() const 
    { return (_oflags & BACT_OFLAG_USERINPT) != 0; }

    virtual bool getBACT_exactCollisions() const 
    { return (_oflags & BACT_OFLAG_EXACTCOLL) != 0; }
    
    virtual bool getBACT_bactCollisions() const 
    { return (_oflags & BACT_OFLAG_BACTCOLL) != 0; }
    
    virtual bool getBACT_landingOnWait() const 
    { return (_oflags & BACT_OFLAG_LANDONWAIT) != 0; }
    
    virtual int getBACT_yourLastSeconds() const 
    { return _yls_time; }
    
    virtual NC_STACK_base *GetVP()
    {
        if (_current_vp)
            return _current_vp->Bas;

        return NULL;
    }
    
    virtual int getBACT_aggression() const
    { return _aggr; }
    
    virtual World::rbcolls *getBACT_collNodes()
    { return _collNodes.roboColls.empty() ? NULL : &_collNodes; } // OpenUA: universal compound spheres

    bool UsesAutoCollisionSpheres() const
    { return _autoCollisionSpheres; }

    void GetClosestCollisionBodySphere(const vec3d &target, vec3d *center, float *radius) const;

    virtual bool getBACT_extraViewer() const
    { return (_oflags & BACT_OFLAG_EXTRAVIEW) != 0; }    
    
    virtual bool getBACT_alwaysRender() const
    { return (_oflags & BACT_OFLAG_ALWAYSREND) != 0; }
    
    
    void ChangeEscapeFlag(bool escape);
    
    virtual bool IsGroundUnit() const { return false; };
    
    bool IsNeedsWaypoints() const;
    bool IsAnyKidWithoutSecondUnitTarget() const;
    
    void sub_4843BC(NC_STACK_ypabact *bact2, int a3);
    void sub_493480(NC_STACK_ypabact *bact2, int mode);
    void StartDestFX(const World::DestFX &fx);
    bool StartChainFXByTrigger(uint8_t trigger);

    void DoTargetWaypoint();
    void FixSectorFall();
    void FixBeyondTheWorld();
    void CleanAttackersTarget();
    void SetUnitGuns(const std::vector<World::TRoboGun> &guns);
    void UpdateUnitGuns(update_msg *arg);
    void CleanupUnitGuns(bool releaseGuns, bool parentDying = false);
    void ClearUnitGunPointer(NC_STACK_ypabact *gun);

    // OpenUA custom: modular dummy attachments (mirrors the unit-gun machinery)
    void SetUnitDummies(const std::vector<World::TUnitDummy> &dummies);
    void UpdateUnitDummies(update_msg *arg);
    void CleanupUnitDummies(bool releaseDummies, bool parentDying = false);
    void ClearUnitDummyPointer(NC_STACK_ypabact *dummy);
    NC_STACK_ypabact *SelectProtectiveDummy(NC_STACK_ypabact *attacker);

    void DeleteAttacker(NC_STACK_ypabact *bact, int tgtType);
    void AddAttacker(NC_STACK_ypabact *bact, int tgtType);
    
    void CopyTargetOf(NC_STACK_ypabact *commander);
    
    bool IsParentMyRobo() const;
    
    void CopyWaypointsStuff(NC_STACK_ypabact *bact);
    
    World::RefBactList &GetKidList() { return _kidList; }

    // static methods for return correspond for reflist  kid ref node
    static World::RefBactList::Node& GetCellRefNode(NC_STACK_ypabact *&bact)
    {
        return bact->_cellRef;
    }
    
    static World::RefBactList::Node& GetKidRefNode(NC_STACK_ypabact *&bact)
    {
        return bact->_kidRef;
    }
 
protected:
    void SetKidsPath(int beginWp);

    //Data
public:
    static constexpr const char * __ClassName = "ypabact.class";
public:    
    
    World::RefBactList::Node _cellRef; 
    
    Common::Point _cellId;
    cellArea *_pSector = NULL;
    
    vec2d _wrldSize;
    
    Common::Point _wrldSectors;
 
    int _bact_type;
    uint32_t _gid = 0; // global bact id
    uint8_t _vehicleID; // vehicle id, from scr files
    uint8_t _mimic_disguise_vehicleID; // OpenUA: copied proto for model = mimic runtime behavior
    uint8_t _bflags;
    uint32_t _commandID = 0;
    NC_STACK_yparobo *_host_station; // parent robo?
    NC_STACK_ypabact *_parent;
    World::RefBactList _kidList;
    World::RefBactList::Node _kidRef;
    TSndCarrier _soundcarrier;
    int _soundFlags;
    int _volume;
    int _pitch;
    float _pitch_max;
    int _base_snd_normal_pitch;
    int _base_snd_fire_pitch;
    int _base_snd_wait_pitch;
    int _energy;
    int _energy_max;
    bool _invulnerable;
    int _reload_const;
//    int16_t field_3CE;
    uint8_t _shield;
//    char field_3D1;
    uint8_t _radar; // num sectors view
    uint8_t _owner;
    uint8_t _aggr;
    uint8_t _status;
//    uint64_t paddiong;
    int _status_flg; //Additional status flags
//    int field_3DA;
    uint8_t _primTtype;
    uint8_t _secndTtype;
    uint32_t _primT_cmdID;
    uint32_t _secndT_cmdID;
    BactTarget _primT;
    vec3d _primTpos;
    BactTarget _secndT;
    vec3d _sencdTpos;

    float _adist_sector;
    float _adist_bact;
    float _sdist_sector;
    float _sdist_bact;
    vec3d _waypoints[32]; //waypoints
    int16_t _current_waypoint;
    int16_t _waypoints_count;
    int _m_cmdID;
    uint8_t _m_owner;
    uint32_t _fe_cmdID; // found enemy group ID
    int _fe_time; //
    float _mass;
    float _base_force;
    float _base_maxrot;
    float _force;
    float _airconst;
    float _airconst_static;
    float _maxrot;

    vec3d _viewer_position;
    mat3x3 _viewer_rotation;
    float _viewer_horiz_angle;
    float _viewer_vert_angle;
    float _viewer_max_up;
    float _viewer_max_down;
    float _viewer_max_side;

    float _thraction;
    vec3d _fly_dir;
    float _fly_dir_length;
    int _weaponRecoilVisualEndTime = 0; // OpenUA: render-only tank firing tilt, does not affect physics
    int _weaponRecoilVisualDuration = 0;
    float _weaponRecoilVisualPitch = 0.0f;
    vec3d _weaponRecoilVisualOffset = vec3d(0.0, 0.0, 0.0); // OpenUA: render-only AI tank recoil translation
    int _weaponRecoilAiRecoveryEndTime = 0; // OpenUA: short AI tank forward-thrust pause after fake recoil
    int _weaponRecoilPlayerRecoveryEndTime = 0; // OpenUA: short player tank forward-input damping after fake recoil
    vec3d _weaponRecoilPushVel = vec3d(0.0, 0.0, 0.0);

    // OpenUA aoe_unit_push: residual knockback velocity, integrated and decayed
    // every frame by UpdateAoePush() so shockwaves shove units smoothly.
    vec3d _aoePushVel = vec3d(0.0, 0.0, 0.0);

    vec3d _position; //Current pos
    vec3d _old_pos; //Prev pos
    vec3d _target_vec; //Vector to target
    vec3d _target_dir; //Target 1-vector direction
    mat3x3 _rotation;

    float _height;
    float _height_max_user;
    vec3d _scale;
    vec3d _vp_scale = vec3d(1.0, 1.0, 1.0);
    World::TVisualTint _vp_tint; // OpenUA custom: main VP visual-only RGBA tint multiplier
    vec3d _vp_orientation = vec3d(0.0, 0.0, 0.0);
    vec3d _vp_spin_speed = vec3d(0.0, 0.0, 0.0);
    vec3d _vp_trail_scale = vec3d(1.0, 1.0, 1.0);
    World::TVisualTint _vp_trail_tint; // OpenUA custom: weapon embedded particle/trail tint
    vec3d _vp_trail_spin_speed = vec3d(0.0, 0.0, 0.0);
    NC_STACK_base *_vp_normal;
    NC_STACK_base *_vp_fire;
    NC_STACK_base *_vp_wait;
    NC_STACK_base *_vp_dead;
    NC_STACK_base *_vp_megadeth;
    NC_STACK_base *_vp_genesis;
    World::TDamagedFXConfig _damaged_fx;
    int32_t _damaged_fx_next_time;
    World::TDecorationFXConfig _decoration_fx;
    int32_t _decoration_fx_next_time = 0;
    int32_t _decoration_fx_persistent_id = 0;
    float _damaged_force_malus;
    float _damaged_maxrot_malus;
    float _damaged_snd_pitch_mult;
    bool _damaged_fx_active;
    TActiveDebuffState _active_debuff;
    TSndCarrier _debuff_soundcarrier;
    TSndCarrier _damaged_shake_carrier;
    TSndCarrier _laser_soundcarrier; // OpenUA custom: managed loop sound for model = laser
    TSndCarrier _vertical_laser_soundcarrier; // OpenUA custom: managed loop sound for model = vertical_laser
    TSndCarrier _mgun_soundcarrier; // OpenUA custom: one-shot pulse sound for vehicle-controlled MG
    TSndCarrier _mimic_soundcarrier; // OpenUA custom: persistent loop for model = mimic shell
    int _mgun_sound_index;
    int _mgun_vp_fire_end_time;
    int _vp_active;
    extra_vproto _vp_extra[3];
    int _vp_extra_mode;
    std::vector<World::DestFX> _destroyFX;    // dest_fx
    std::vector<World::DestFX> _extDestroyFX; // ext_dest_fx
    std::vector<World::TChainFXConfig> _chainFX;
    float _radius;
    float _viewer_radius;
    float _overeof;
    float _viewer_overeof;
    bool _cockpit_camera_enable;
    vec3d _cockpit_camera_offset;
    bool _mgun_pov_fx_enable;
    int16_t _mgun_pov_fx_vp;
    int16_t _mgun_pov_num_mguns_fx;
    float _mgun_pov_fx_scale;
    vec3d _mgun_pov_fx_offset;
    vec3d _mgun_pov_fx_rot;
//    float pos_x_cntr;
//
//    float pos_y_cntr;

    TF::TForm3D _tForm;
    int _clock;           // local time
    int _AI_time1;
    int _AI_time2;
//    int field_921;
//    int field_925;
    int _search_time1;
    int _search_time2;
    int _scale_time;
    int _brkfr_time;
    int _brkfr_time2;
//    int field_93D;
    int _newtarget_time;
    int _assess_time;
    int _waitCol_time; //Used in tank
    int _slider_time;
//    int field_951;
    int _dead_time;
    int _beam_time;
    int _energy_time;
    vec3d _mpos;
    int _weapon;
    std::array<int16_t, 3> _extra_weapons;
    int _weapon_switch_mode;
    int _weapon_slot_index;
    int _current_weapon_id;
    int _lowhp_weapon_enable;
    float _lowhp_threshold;
    int _lowhp_weapon;
    uint8_t _weapon_flags;
    int _mgun;
    bool _mgun_set;
    int _num_mguns;
    int _mgun_shot_time;
    int _mgun_shot_time_user;
    int _mgun_vp_dead;
    int _mgun_vp_megadeth;
    float _mgun_power;
    float _mgun_angle;
    bool _mgun_power_set;
    bool _mgun_angle_set;
    float _mgun_ai_range;
    float _mgun_ai_fire_alignment;
    bool _mgun_damage_sectors;
    float _mgun_sector_damage_accum;
    float _weapon_spread_x;
    float _weapon_spread_y;
    float _mgun_spread_x;
    float _mgun_spread_y;
    float _weapon_spread_x_user;
    float _weapon_spread_y_user;
    bool _weapon_spread_x_user_set;
    bool _weapon_spread_y_user_set;
    uint8_t _num_weapons;

    World::MissileList _missiles_list;
    int _weapon_time;
    vec3d _fire_pos;
    float _gun_angle;
    float _gun_angle_user;
    float _gun_leftright;
    float _gun_radius;
    float _gun_power;
    int _mgun_time;
    int _salve_counter;
    int _kill_after_shot;
    int _spawn_units;
    int _spawn_vehicle;
    int _spawn_interval;
    float _spawn_trigger_radius;
    float _spawn_random_pos;
    int _spawn_max_active;
    int _spawn_count;
    int _spawn_instant;
    int _spawn_last_time;
    int _spawn_at_death_units;
    int _spawn_at_death_vehicle;
    int _spawn_at_death_count;
    float _spawn_at_death_random_pos;
    int _spawn_at_death_instant;
    int _spawn_at_death_immunity_time;
    bool _spawn_at_death_done;
    int _spawn_at_death_protection_end_time;
    bool _spawn_at_death_restore_vulnerable;
    int _death_damage;
    bool _death_damage_applied_dead;
    bool _death_damage_applied_megadeth;
    int _carrier_spawn_root_gid;
    int _carrier_spawn_root_vehicle;
    std::vector<int32_t> _carrier_spawned_gids;
    int _proximity_defense_enable;
    int _proximity_defense_weapon;
    float _proximity_defense_trigger_radius;
    int _proximity_defense_interval;
    int _proximity_defense_shots;
    vec3d _proximity_defense_fire_pos;
    int _proximity_defense_vp_launch;
    int _proximity_defense_fire_mode;
    int _proximity_defense_sequence_delay;
    int _proximity_defense_at_death;
    bool _proximity_defense_random_yaw_set;
    float _proximity_defense_random_yaw_min;
    float _proximity_defense_random_yaw_max;
    bool _proximity_defense_random_pitch_set;
    float _proximity_defense_random_pitch_min;
    float _proximity_defense_random_pitch_max;
    bool _proximity_defense_sequence_active;
    int _proximity_defense_sequence_shots_fired;
    int _proximity_defense_next_shot_time;
    int _proximity_defense_next_activation_time;
    bool _proximity_defense_at_death_done;
    // OpenUA custom: mortar barrage runtime state (transient, not saved per instance)
    bool _mortar_barrage_active = false;
    // Shots left in the CURRENT firing cycle. A cycle's shots are a shared budget:
    // redirecting the barrage (manual or auto) spends from it and never refills it.
    // Only the cooldown (after the budget is spent) refills it. This stops the
    // "infinite barrage" exploit of re-aiming to dodge the cooldown.
    int _mortar_shots_remaining = 0;
    int _mortar_next_shot_time = 0;
    int _mortar_next_activation_time = 0;
    int _mortar_next_scan_time = 0;
    vec3d _mortar_target_center;
    // Manual order queued while on cooldown: the strike is accepted now (azure ring
    // shown) but only fires once the cooldown has elapsed. Never bypasses cooldown.
    bool _mortar_has_pending = false;
    vec3d _mortar_pending_target;
    struct TLaserBeamRequest
    {
        NC_STACK_ypabact *target = NULL;
        vec3d start;
        vec3d dir;
    };

    struct TLaserBeamRuntime
    {
        vec3d start;
        vec3d end;
        int32_t target_gid = 0;
        int energy_ticks = 0;
        int next_damage_time = 0;
        int next_fx_time = 0;
    };

    // OpenUA custom: laser beam runtime state (transient, per shooter/weapon/target;
    // never saved per instance). Supports direct laser_multi_target beams and chain
    // segments, while keeping the first beam in the legacy fields for older debug/UI paths.
    bool _laser_active = false;            // beam currently firing/visible
    bool _laser_fire_request = false;      // set by RequestLaserFire() each firing frame, consumed by UpdateLaser()
    int  _laser_weapon = -1;               // weapon id of the active/requested laser
    int32_t _laser_target_gid = 0;         // gid of the locked target (0 = none / ground)
    int _laser_energy_ticks = 0;           // connected damage ticks applied to the current target
    vec3d _laser_beam_start;               // world muzzle/fire point (for the beam visual)
    vec3d _laser_beam_end;                 // world contact point (target center)
    NC_STACK_ypabact *_laser_target = NULL;  // requested target this frame (valid only within the firing frame)
    vec3d _laser_request_start;            // requested muzzle this frame
    vec3d _laser_request_dir;              // requested forward beam direction this frame (normalized)
    int _laser_next_damage_time = 0;       // next _clock at which static tick damage may apply
    int _laser_next_fx_time = 0;           // next _clock at which a throttled impact VP may spawn
    int _laser_next_beam_vp_time = 0;      // next _clock at which the VP beam body may be refreshed
    std::vector<TLaserBeamRequest> _laser_requests;
    std::vector<TLaserBeamRuntime> _laser_beams;
    // OpenUA custom: separate downward beam runtime for model = vertical_laser.
    bool _vertical_laser_active = false;
    bool _vertical_laser_fire_request = false;
    int _vertical_laser_weapon = -1;
    NC_STACK_ypabact *_vertical_laser_request_target = NULL;
    vec3d _vertical_laser_request_start;
    int _vertical_laser_next_beam_vp_time = 0;
    TLaserBeamRuntime _vertical_laser_beam;
    std::vector<TLaserBeamRuntime> _vertical_laser_beams;
    int _seek_and_explode;
    int _seek_and_explode_weapon;
    float _seek_and_explode_trigger_radius;
    bool _seek_and_explode_triggered;
    std::vector<World::TRoboGun> _unitGuns;
    std::string _gunDisplayName;
    mat3x3 _unitGunsParentRotation;
    bool _unitGunsSpawned;
    bool _unitGunsHaveParentRotation;
    bool _isUnitGunChild;
    // OpenUA custom: dummy modular attachments
    std::vector<World::TUnitDummy> _unitDummies;
    mat3x3 _unitDummiesParentRotation;
    bool _unitDummiesSpawned;
    bool _unitDummiesHaveParentRotation;
    bool _isDummy;
    // OpenUA custom: universal compound collision spheres for non-robo vehicles
    World::rbcolls _collNodes;
    bool _autoCollisionSpheres = false;
    float _heading_speed;
    NC_STACK_ypabact *_killer;
    int16_t _killer_owner;
    int16_t _reb_count;
    int _atk_ret;
    uint32_t _lastFrmStamp;
    mat3x3 _netDRot;
    mat3x3 _netRotation;
    vec3d _netDSpeed;
    float _scale_start;
    float _scale_speed;
    float _scale_accel;
    int _scale_duration;
    int _scale_pos;
    int _scale_delay;
    NC_STACK_base *_vp_fx_models[32];

    int _oflags;
    NC_STACK_ypaworld *_yw;
    NC_STACK_base::Instance *_current_vp = NULL;
    std::list<TBactAttacker> _attackersList;
    int _yls_time;  
    
    bool _hidden = false;
    int8_t _unhideRadar = 0;

    // OpenUA custom: per-instance "invisible" stealth state. Seeded from the vehicle
    // prototype's `invisible` flag at spawn; cleared permanently by the first real
    // attack via RevealInvisibleOnAttack(). Gameplay/physics/control stay active while set.
    bool _invisibleUnrevealed = false;
    int16_t _invisible_reveal_vp = 0;

protected:
    NC_STACK_ypaworld *_world;
};

void sb_0x4874c4(NC_STACK_ypabact *bact, int a2, int a3, float a4);

#endif // YBACT_H_INCLUDED
