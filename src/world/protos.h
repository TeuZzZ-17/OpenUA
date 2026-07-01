#ifndef WORLD_PROTOS_H_INCLUDED
#define WORLD_PROTOS_H_INCLUDED

#include "../system/sound.h"
#include "../nucleas.h"
#include "../sample.h"
#include "../skeleton.h"

class NC_STACK_ypabact;

namespace World
{
struct TRoboProto;

enum DecorationFXMode
{
    DECORATION_FX_PERIODIC = 0,
    DECORATION_FX_PERSISTENT = 1
};

// OpenUA custom: RGBA tint multiplier (see visual_tint / wireframe_tint script params).
// Stored as normalized 0..1 float multipliers. Neutral default = no change.
struct TVisualTint
{
    float r = 1.0;
    float g = 1.0;
    float b = 1.0;
    float a = 1.0;

    bool IsNeutral() const
    {
        return r == 1.0 && g == 1.0 && b == 1.0 && a == 1.0;
    }

    void Clamp()
    {
        auto cl = [](float v) -> float { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        r = cl(r);
        g = cl(g);
        b = cl(b);
        a = cl(a);
    }
};

struct TDecorationFXConfig
{
    uint8_t mode = DECORATION_FX_PERIODIC;
    int16_t vp = 0;
    int interval_min = 0;
    int interval_max = 0;
    int count_min = 0;
    int count_max = 0;
    int duration = 1000;
    float random_pos = 0.0;
    float scale = 1.0;
    vec3d offset;
    bool has_tint = false;
    TVisualTint tint;
};

struct TChainFXVPModel
{
    int16_t model = 0;
    bool has_tint = false;
    TVisualTint tint;
};

enum VisualScaleMode
{
    VISUAL_SCALE_FIXED = 0,
    VISUAL_SCALE_RANDOM = 1,
    VISUAL_SCALE_AXIS = 2
};

struct DestFX
{
    enum FXTYPES {
        FX_NONE = 0,
        FX_DEATH,    // "death"
        FX_MEGADETH, // "megadeth"
        FX_CREATE,   // "create"
        FX_BEAM      // "beam"
    };
    
    uint8_t Type = FX_NONE;
    int ModelID  = 0; // Model id. >= 0
    vec3d Pos;
    bool Accel   = false;

    static uint8_t ParseTypeName(const std::string &in);
};

struct TChainFXConfig
{
    enum Mode
    {
        MODE_VISUAL = 0,
        MODE_PHYSICAL
    };

    enum Trigger
    {
        TRIGGER_NONE = 0,
        TRIGGER_DESTROYED,
        TRIGGER_CRASH,
        TRIGGER_DETONATE,
        TRIGGER_IMPACT_WORLD
    };

    uint8_t mode = MODE_VISUAL;
    uint8_t trigger = TRIGGER_NONE;
    vec3d offset;
    float start_size = 1.0;
    float end_size = 0.0;
    int duration = 0;
    std::vector<TChainFXVPModel> vp_models;
    int physical_vehicle = 0;
    bool inherit_velocity = false;
};

struct TRoboColl
{
    float robo_coll_radius = 0.0;
    vec3d coll_pos;
    vec3d field_10;
};

struct rbcolls
{
    int field_0 = 0;
    std::vector<TRoboColl> roboColls;
    
    rbcolls() {};
    
    rbcolls(const rbcolls &b)
    {
        operator =(b);
    }
    
    rbcolls(rbcolls &&b)
    {
        field_0 = b.field_0;
        roboColls = std::move(b.roboColls);
    }
    
    rbcolls &operator=(const rbcolls &b)
    {
        field_0 = b.field_0;
        roboColls = b.roboColls;
        return *this;
    }
};
    
struct TVhclSound
{
    struct TSndSample
    {
        std::string Name;
        NC_STACK_sample *Sample = NULL;

        void ClearLoaded()
        {
            if (Sample)
            {
                Sample->Delete();
                Sample = NULL;
            }
        }
        
        ~TSndSample()
        {
            ClearLoaded();
        }
    };
    
    TSndSample MainSample;
    std::vector<TSndSample> MainSampleVariants;
    
    std::vector<TSndSample> ExtSamples;
    int16_t volume = 0;
    int16_t pitch = 0;
    TSndFXParam sndPrm;
    TSndFxPosParam sndPrm_shk;
    std::vector<TSampleParams> extS;
    
    void SetMainSampleVariant(size_t variant, const std::string &name);
    void LoadSamples();
    void ClearSounds();
};

constexpr int DAMAGED_FX_SLOT_COUNT = 8;
constexpr size_t ROBO_GUN_MAX_COUNT = 20;
constexpr size_t UNIT_DUMMY_MAX_COUNT = 20; // OpenUA: max dummy attachment slots per parent vehicle
constexpr size_t UNIT_COLL_MAX_COUNT = 32;  // OpenUA: max compound collision spheres per vehicle

struct TDamagedFXConfig
{
    std::vector<int16_t> vps = {0};
    float threshold = 0.0;
    int interval_min = 0;
    int interval_max = 0;
    float random_pos = 0.0;
    TSndFxPosParam shake;

    TDamagedFXConfig()
    {
        shake.mag0 = 1.0;
        shake.time = 1000;
        shake.mute = 0.02;
        shake.pos.x = 0.2;
        shake.pos.y = 0.2;
        shake.pos.z = 0.2;
    }
};

struct TWeaponDebuffConfig
{
    bool allow = false;
    std::string name;
    std::string icon;
    int damage = 0;
    float damage_percent = 0.0;
    bool mindcontrol = false;
    int tick_time = 1000;
    int duration = 5000;
    float force_malus = 0.0;
    float maxrot_malus = 0.0;
    float shield_malus = 0.0;
    float snd_pitch_mult = 1.0;
    std::vector<int16_t> fx_vps;
    float fx_random_pos = 0.0;
    TVhclSound tick_snd;
};

struct TWeaponClusterConfig
{
    bool enable = false;
    int generations = 0;
    int count = 0;
    int16_t weapon_id = 0;
    int trigger_time = 0;
    float spread_x = 0.0;
    float spread_y = 0.0;
    int16_t vp = 0;
    TVhclSound snd;
};

struct TWeaponChainConfig
{
    bool allow = false;
    int max_jumps = 0;
    float radius = 0.0;
    float damage_mult = 1.0;
    int jump_delay = 0;
};

struct TRoboGun
{
    vec3d pos;
    vec3d dir;
    NC_STACK_ypabact *gun_obj = NULL;
    std::string robo_gun_name;
    std::string icon;
    uint8_t robo_gun_type = 0;

    TRoboGun()
    {}

    TRoboGun(const TRoboGun &b)
    {
        operator =(b);
    }

    TRoboGun(TRoboGun &&b)
    {
        pos = b.pos;
        dir = b.dir;
        gun_obj = b.gun_obj;
        robo_gun_name = std::move(b.robo_gun_name);
        icon = std::move(b.icon);
        robo_gun_type = b.robo_gun_type;
    }

    TRoboGun& operator=(const TRoboGun &b)
    {
        pos = b.pos;
        dir = b.dir;
        gun_obj = b.gun_obj;
        robo_gun_name = b.robo_gun_name;
        icon = b.icon;
        robo_gun_type = b.robo_gun_type;
        return *this;
    }
};

// OpenUA custom: one modular dummy attachment slot on a parent vehicle.
// The dummy stats/visuals come from a normal vehicle prototype (model = dummy)
// referenced by vehicle_id; this struct only describes the mount point/options.
struct TUnitDummy
{
    int vehicle_id = 0;            // unit_dummy_vehicle: proto ID of the dummy module
    vec3d pos = vec3d(0.0, 0.0, 0.0);   // unit_dummy_pos_*: local offset from parent
    vec3d dir = vec3d(0.0, 0.0, 1.0);   // unit_dummy_dir_*: local initial orientation
    int protect = 0;               // unit_dummy_protect: absorb damage before parent
    int destroy_with_parent = 1;   // unit_dummy_destroy_with_parent
    int hide_when_destroyed = 1;   // unit_dummy_hide_when_destroyed
    NC_STACK_ypabact *dummy_obj = NULL; // runtime child instance (not parsed)

    TUnitDummy() {}

    TUnitDummy(const TUnitDummy &b)
    {
        operator =(b);
    }

    TUnitDummy& operator=(const TUnitDummy &b)
    {
        vehicle_id = b.vehicle_id;
        pos = b.pos;
        dir = b.dir;
        protect = b.protect;
        destroy_with_parent = b.destroy_with_parent;
        hide_when_destroyed = b.hide_when_destroyed;
        dummy_obj = b.dummy_obj;
        return *this;
    }
};

struct TVhclProto
{
    enum
    {
        SND_NORMAL  = 0,
        SND_FIRE    = 1,
        SND_WAIT    = 2,
        SND_GENESIS = 3,
        SND_EXPLODE = 4,
        SND_CRSHLND = 5,
        SND_CRSHVHCL= 6,
        SND_GODOWN  = 7,
        SND_COCKPIT = 8,
        SND_BEAMIN  = 9,
        SND_BEAMOUT = 10,
        SND_BUILD   = 11,
        SND_AIREXPLODE = 12,
        
        SND_MAX     = 13
    };
    
    inline static bool IsLoopingSnd(int i)
    {
        switch (i)
        {
            default:
                return false;
            
            case SND_NORMAL:    
            case SND_FIRE:
            case SND_WAIT:
            case SND_GENESIS:
            case SND_GODOWN:
            case SND_COCKPIT:
                return true;                      
        }
        return false;
    }
    
    int32_t Index = -1;
    int model_id = 0;
    uint8_t disable_enable_bitmask = 0;
    int8_t weapon = 0;
    std::array<int16_t, 3> extra_weapons = {0, 0, 0};
    int weapon_switch_mode = 0; // 0 sequence, 1 random
    int lowhp_weapon_enable = 0;
    float lowhp_threshold = 0.30;
    int16_t lowhp_weapon = 0;
    int field_4 = 0;
    int8_t mgun = 0;
    bool mgun_set = false;
    int16_t num_mguns = 1;
    int mgun_shot_time = 0;
    int mgun_shot_time_user = 0;
    int16_t mgun_vp_dead = 0;
    int16_t mgun_vp_megadeth = 0;
    float mgun_power = 0.0;
    float mgun_angle = 0.0;
    bool mgun_power_set = false;
    bool mgun_angle_set = false;
    float weapon_spread_x = 0.0;
    float weapon_spread_y = 0.0;
    float mgun_spread_x = 0.0;
    float mgun_spread_y = 0.0;
    float weapon_spread_x_user = 0.0;
    float weapon_spread_y_user = 0.0;
    bool weapon_spread_x_user_set = false;
    bool weapon_spread_y_user_set = false;
    uint8_t type_icon = 0;
    std::string name;
    int16_t vp_normal = 0;
    int16_t vp_fire = 0;
    int16_t vp_dead = 0;
    int16_t vp_wait = 0;
    int16_t vp_megadeth = 0;
    int16_t vp_genesis = 0;
    float visual_scale = 1.0;
    uint8_t visual_scale_mode = VISUAL_SCALE_FIXED;
    float visual_scale_random_min = 1.0;
    float visual_scale_random_max = 1.0;
    vec3d visual_scale_axis = vec3d(1.0, 1.0, 1.0);
    TVisualTint visual_tint; // OpenUA custom: visual-only RGBA tint multiplier
    TVisualTint wireframe_tint; // OpenUA custom: UI wireframe-only RGBA tint multiplier
    vec3d visual_rotation = vec3d(0.0, 0.0, 0.0); // OpenUA custom: visual-only local rotation, degrees
    TDamagedFXConfig damaged_fx;
    TDecorationFXConfig decoration_fx;
    std::string damaged_icon;
    std::string regen_icon;
    std::string drain_icon;
    std::string spawn_icon;
    std::string radar_icon;
    std::string unit_gun_icon;
    std::string power_icon;
    std::string seek_and_explode_icon;
    int power = 0;
    float power_radius = 0.0;
    float damaged_force_malus = 0.0;
    float damaged_maxrot_malus = 0.0;
    float damaged_snd_pitch_mult = 1.0;
    int spawn_units = 0;
    int16_t spawn_vehicle = 0;
    int spawn_interval = 5000;
    float spawn_trigger_radius = 0.0;
    float spawn_random_pos = 0.0;
    int spawn_max_active = 0;
    int spawn_count = 1;
    int spawn_instant = 0;
    int spawn_at_death_units = 0;
    int16_t spawn_at_death_vehicle = 0;
    int spawn_at_death_count = 1;
    float spawn_at_death_random_pos = 0.0;
    int spawn_at_death_instant = 0;
    int spawn_at_death_immunity_time = 0;
    int death_damage = 0; // OpenUA custom: direct radius damage applied on death phases
    int proximity_defense_enable = 0;
    int proximity_defense_weapon = 0;
    float proximity_defense_trigger_radius = 0.0;
    int proximity_defense_interval = 1000;
    int proximity_defense_shots = 12;
    vec3d proximity_defense_fire_pos;
    int proximity_defense_vp_launch = -1;
    int proximity_defense_fire_mode = 0;
    int proximity_defense_sequence_delay = 100;
    int proximity_defense_at_death = 0;
    bool proximity_defense_random_yaw_set = false;
    float proximity_defense_random_yaw_min = 0.0;
    float proximity_defense_random_yaw_max = 360.0;
    bool proximity_defense_random_pitch_set = false;
    float proximity_defense_random_pitch_min = -10.0;
    float proximity_defense_random_pitch_max = 45.0;
    int seek_and_explode = 0;
    int seek_and_explode_weapon = 0;
    float seek_and_explode_trigger_radius = 0.0;
    int ai_max_active_at_once = 0;
    std::vector<DestFX> dest_fx;      // dest_fx
    std::vector<DestFX>    ExtDestroyFX; // ext_dest_fx
    std::array<TVhclSound, SND_MAX> sndFX;
    int vo_type = 0;
    std::string voicepack;
    float max_pitch = 0.0;
    int16_t field_1D6D = 0;
    int16_t field_1D6F = 0;
    int shield = 0;
    int energy = 0;
    bool invulnerable = false;
    int field_1D79 = 0;
    float adist_sector = 0.0;
    float adist_bact = 0.0;
    float sdist_sector = 0.0;
    float sdist_bact = 0.0;
    int8_t radar = 0;
    float mass = 0.0;
    float force = 0.0;
    float airconst = 0.0;
    float maxrot = 0.0;
    float height = 0.0;
    float radius = 0.0;
    float overeof = 0.0;
    float vwr_radius = 0.0;
    float vwr_overeof = 0.0;
    float gun_angle = 0.0;
    float fire_x = 0.0;
    float fire_y = 0.0;
    float fire_z = 0.0;
    int16_t num_weapons = 0;
    float gun_power = 0.0;
    float gun_radius = 0.0;
    int kill_after_shot = 0;
    std::vector<TChainFXConfig> chain_fx;
    float scale_fx_p0 = 0.0;
    float scale_fx_p1 = 0.0;
    float scale_fx_p2 = 0.0;
    int scale_fx_p3 = 0;
    std::array<int16_t, 32> scale_fx_pXX;
    int8_t job_fighttank = 0;
    int8_t job_fighthelicopter = 0;
    int8_t job_fightflyer = 0;
    int8_t job_fightrobo = 0;
    int8_t job_conquer = 0;
    int8_t job_reconnoitre = 0;
    NC_STACK_skeleton *wireframe = NULL;
    NC_STACK_skeleton *hud_wireframe = NULL;
    NC_STACK_skeleton *mg_wireframe = NULL;
    NC_STACK_skeleton *wpn_wireframe_1 = NULL;
    NC_STACK_skeleton *wpn_wireframe_2 = NULL;
    IDVList initParams;
    
    bool hidden = false;
    int8_t unhideRadar = 0;

    // OpenUA custom: vehicle-only "invisible" stealth. When true every new instance
    // spawns fully cloaked (no render/radar/UI/sound/AI-target) until its first real
    // attack, after which it is permanently revealed. Independent from the legacy
    // owner-based `hidden`/`unhide_radar` system above. Default off.
    bool invisible = false;
    int16_t invisible_reveal_vp = 0;

    TRoboProto *RoboProto = NULL;
    std::vector<TRoboGun> unit_guns;

    int is_dummy = 0;                       // OpenUA: model = dummy (attachable module proto)
    std::vector<TUnitDummy> unit_dummies;   // OpenUA: dummy attachment slots (parent side)

    rbcolls coll;                           // OpenUA: universal compound collision spheres (coll_*)

    ~TVhclProto();
};

struct TWeapProto
{
    enum
    {
        SND_NORMAL = 0,
        SND_LAUNCH = 1,
        SND_HIT    = 2,
        
        SND_MAX    = 3
    };

    enum
    {
        WEAPON_FLAG_PROJECTILE = 1,
        WEAPON_FLAG_DIRECT = 2,
        WEAPON_FLAG_TARGETED = 4,
        WEAPON_FLAG_OBSAVOID = 8,
        WEAPON_FLAG_GRENADE = 16,
        WEAPON_FLAG_HOMING_BOMB = 32,
        WEAPON_FLAG_MORTAR = 64, // OpenUA custom: radar-guided ballistic barrage
        WEAPON_FLAG_LASER = 128, // OpenUA custom: continuous targeted beam weapon
        WEAPON_FLAG_VERTICAL_LASER = 256, // OpenUA custom: downward continuous beam weapon

        WEAPON_FLAGS_BOMB = WEAPON_FLAG_PROJECTILE,
        WEAPON_FLAGS_ROCKET = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT,
        WEAPON_FLAGS_MISSILE = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT | WEAPON_FLAG_TARGETED,
        WEAPON_FLAGS_OBSAVOID = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT | WEAPON_FLAG_OBSAVOID,
        WEAPON_FLAGS_GRENADE = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_GRENADE,
        WEAPON_FLAGS_HOMING_BOMB = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_HOMING_BOMB,
        WEAPON_FLAGS_MORTAR = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_MORTAR,
        // Laser keeps PROJECTILE|DIRECT|TARGETED so the AI/aim/lock logic treats it
        // like a normal targeted weapon, but the LASER bit reroutes firing to the
        // continuous-beam path (UpdateLaser) instead of spawning a projectile.
        WEAPON_FLAGS_LASER = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT | WEAPON_FLAG_TARGETED | WEAPON_FLAG_LASER,
        WEAPON_FLAGS_VERTICAL_LASER = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_VERTICAL_LASER
    };

    int8_t unitID = 0;
    uint8_t enable_mask = 0;
    int16_t _weaponFlags = 0;

    bool IsHomingBomb() const
    {
        return _weaponFlags == WEAPON_FLAGS_HOMING_BOMB;
    }

    // OpenUA custom: true only for weapons declared as "model = mortar".
    bool IsMortar() const
    {
        return (_weaponFlags & WEAPON_FLAG_MORTAR) != 0;
    }

    // OpenUA custom: true only for weapons declared as "model = laser".
    bool IsLaser() const
    {
        return (_weaponFlags & WEAPON_FLAG_LASER) != 0;
    }

    // OpenUA custom: true only for weapons declared as "model = vertical_laser".
    bool IsVerticalLaser() const
    {
        return (_weaponFlags & WEAPON_FLAG_VERTICAL_LASER) != 0;
    }

    bool IsBombLike() const
    {
        return _weaponFlags == WEAPON_FLAGS_BOMB || IsHomingBomb() || IsVerticalLaser();
    }

    int GetFireControlFlags() const
    {
        return IsBombLike() ? 0 : (_weaponFlags & ~WEAPON_FLAG_PROJECTILE);
    }

    uint8_t type_icon = 0;
    std::string name;
    int16_t vp_normal = 0;
    int16_t vp_fire = 0;
    int16_t vp_dead = 0;
    int16_t vp_wait = 0;
    int16_t vp_megadeth = 0;
    int16_t vp_genesis = 0;
    int16_t vp_launch = 0;
    float visual_scale = 1.0;
    uint8_t visual_scale_mode = VISUAL_SCALE_FIXED;
    float visual_scale_random_min = 1.0;
    float visual_scale_random_max = 1.0;
    vec3d visual_scale_axis = vec3d(1.0, 1.0, 1.0);
    TVisualTint visual_tint; // OpenUA custom: visual-only RGBA tint multiplier
    TVisualTint wireframe_tint; // OpenUA custom: UI wireframe-only RGBA tint multiplier
    vec3d visual_rotation = vec3d(0.0, 0.0, 0.0); // OpenUA custom: visual-only local rotation, degrees
    vec3d projectile_spin_speed = vec3d(0.0, 0.0, 0.0); // OpenUA custom: visual-only spin speed, degrees per second, local X/Y/Z
    std::vector<DestFX> dfx;
    std::vector<DestFX> ExtDestroyFX; // ext_dest_fx
    std::array<TVhclSound, SND_MAX> sndFXes;
    TWeaponDebuffConfig debuff;
    TWeaponClusterConfig cluster;
    TWeaponChainConfig chain;
    TDecorationFXConfig decoration_fx;
//    int field_870 = 0;
//    int field_874 = 0;
    int energy = 0;
    int aoe_unit_energy = 0;
    int aoe_building_energy = 0;
    int aoe_sector_energy = 0;
    int aoe_falloff = 0;
    int aoe_unit_push = 0;
    // OpenUA custom: direct-hit single-target knockback. Same movement model as aoe_unit_push,
    // but only for the primary/direct-hit unit. If both push and aoe_unit_push are set,
    // the direct-hit unit receives only push; nearby units receive aoe_unit_push.
    int push = 0;
//    int field_87C = 0;
    int life_time = 0;
    int life_time_nt = 0;
    int drive_time = 0;
    int delay_time = 0;
    float adistSector = 0;
    float adistBact = 0;
    int shot_time = 0;
    int shot_time_user = 0;
    int salve_shots = 0;
    int salve_delay = 0;
    int missile_multi_target = 0;
    int homing_bomb_multi_target = 0; // OpenUA: multi-target spread, only on model = homing_bomb
    // OpenUA custom: shared continuous beam parameters for model = laser and
    // model = vertical_laser. "energy" is static base damage per tick; the class
    // multipliers below (energy_heli/tank/flyer/robo) are applied like normal weapons.
    int   laser_energy_tick_time = 250;        // ms between damage ticks for AI/non-player fire
    int   laser_energy_tick_time_user = 150;   // ms between damage ticks for player-controlled fire
    float laser_energy_increment_rate = 0.0;   // extra base damage added after each connected tick
    float laser_max_energy = 0.0;              // max base damage per tick (<=0 => no clamp)
    float laser_vp_spacing = 40.0;             // visual-only distance between vp_normal beam instances
    int   laser_chain_allow = 0;               // 1 = primary laser hit may chain to nearby enemy units
    int   laser_chain_max_jumps = 0;           // max unit-to-unit chain segments after the primary hit
    float laser_chain_radius = 0.0;            // search radius around the last chained unit
    float laser_chain_damage_mult = 1.0;       // cumulative damage multiplier per chain jump
    int   laser_multi_target = 1;              // total direct shooter-to-target laser beams (<=1 = off)
    float vertical_laser_fire_radius = 300.0;  // X/Z distance required before AI fires downward
    float energy_heli = 0.0;
    float energy_tank = 0.0;
    float energy_flyer = 0.0;
    float energy_robo = 0.0;
    // Legacy/deprecated: parsed for old SCR compatibility, ignored for gameplay.
    // Unit collision always uses radius.
    float radius_heli = 0.0;
    float radius_tank = 0.0;
    float radius_flyer = 0.0;
    float radius_robo = 0.0;
    float mass = 0.0;
    float force = 0.0;
    float airconst = 0.0;
    float maxrot = 0.0;
    float heightStd = 0;
    // radius is direct projectile collision. AoE has separate unit/building/sector values.
    // visual_scale never affects any gameplay radius.
    float radius = 0.0;
    float aoe_unit_radius = 0.0;
    float aoe_building_radius = 0.0;
    float aoe_sector_radius = 0.0;
    float overeof = 0.0;
    float vwr_radius = 0.0;
    float vwr_overeof = 0.0;
    float start_speed = 0.0;
    // OpenUA custom: dedicated mortar barrage weapon ("model = mortar").
    // All defaults are vanilla-safe: with mortar_barrage_shots <= 0 / no max range,
    // a mortar weapon simply never fires.
    float mortar_min_range = 0.0;          // min distance from mortar to target zone
    float mortar_max_range = 0.0;          // max distance for manual fire and automatic target search (<=0 = disabled)
    int   mortar_requires_radar = 1;       // 1 = target sector must be visible to the owner faction
    int   mortar_manual_mode_only = 0;     // 1 = disable the auto AI; the mortar only fires via manual map-click
    int   mortar_prefer_host_station = 0;  // 1 = auto AI prefers an enemy Host Station (robo) among radar-visible enemies (still honours mortar_requires_radar)
    float mortar_barrage_radius = 0.0;     // bombardment zone radius (marker size)
    int   mortar_barrage_shots = 0;        // shells per barrage (<=0 = no barrage)
    int   mortar_barrage_shot_delay = 250; // ms between shells in the same barrage
    int   mortar_barrage_cooldown = 10000; // ms cooldown after a barrage ends
    float mortar_arc_height = 2500.0;      // extra ballistic arc height (engine units)
    int   mortar_flight_time = 2500;       // ms from launch to impact (<=0 => 2500 default)
    float mortar_spread_radius = 0.0;      // per-shell random landing scatter radius
    float mortar_inflight_drift = 0.0;     // optional small horizontal drift during flight
    int   mortar_airburst = 1;             // 1 = explode at the timed arc apex/target height (airburst); 0 = land on the real terrain height at the shell's own impact point
    int   mortar_minimap_marker = 0;       // 1 = show bombardment zone on the 2D strategic map
    // OpenUA custom: looped beam sound for model = laser (snd_loop_sample/volume/pitch).
    // Loaded lazily on first laser activation; vanilla weapons never touch it.
    TVhclSound snd_loop;
    NC_STACK_skeleton *wireframe = NULL;
    IDVList initParams;
    std::vector<TChainFXConfig> chain_fx;

    ~TWeapProto();
};



struct TBuildingProto
{
    struct TGun
    {
        int32_t VhclID = 0;
        vec3d Pos;
        vec3d Dir;
    };
    
    int32_t Index = -1;
    uint8_t SecType = 0;
    uint8_t EnableMask = 0;
    uint8_t ModelID = 0;
    uint8_t Power = 0;
    uint8_t TypeIcon = 0;
    std::string Name;
    int Energy = 0;
    TDecorationFXConfig DecorationFX;
    TVhclSound SndFX;
    std::vector<TGun> Guns;
    int spawn_units = 0;
    int16_t spawn_vehicle = 0;
    int spawn_interval = 0;
    float spawn_trigger_radius = 0.0;
    int spawn_max_active = 0;
    int spawn_count = 1;
    int spawn_instant = 0;
    std::string spawn_icon;
};

struct TRoboProto
{
    vec3d viewer;
    mat3x3 matrix;
    int field_30 = 0;
    int field_34 = 0;
    float robo_viewer_max_up = 0.0;
    float robo_viewer_max_down = 0.0;
    float robo_viewer_max_side = 0.0;
    std::vector<TRoboGun> guns;
    vec3d dock;
    rbcolls coll;

    TRoboProto()
    {}
    
    TRoboProto(const TRoboProto &b)
    {
        operator =(b);
    }
    
    TRoboProto(TRoboProto &&b)
    {
        viewer = b.viewer;
        matrix = b.matrix;
        field_30 = b.field_30;
        field_34 = b.field_34;
        robo_viewer_max_up = b.robo_viewer_max_up;
        robo_viewer_max_down = b.robo_viewer_max_down;
        robo_viewer_max_side = b.robo_viewer_max_side;
        guns = std::move(b.guns);
        dock = b.dock;
        coll = b.coll;
    }
    
    TRoboProto &operator=(const TRoboProto &b)
    {
        viewer = b.viewer;
        matrix = b.matrix;
        field_30 = b.field_30;
        field_34 = b.field_34;
        robo_viewer_max_up = b.robo_viewer_max_up;
        robo_viewer_max_down = b.robo_viewer_max_down;
        robo_viewer_max_side = b.robo_viewer_max_side;
        guns = b.guns;
        dock = b.dock;
        coll = b.coll;
        return *this;
    }
};

}

#endif
