#ifndef WORLD_PROTOS_H_INCLUDED
#define WORLD_PROTOS_H_INCLUDED

#include "../system/sound.h"
#include "../nucleas.h"
#include "../sample.h"
#include "../skeleton.h"

#include <cmath>
#include <map>
#include <string>

class NC_STACK_ypabact;

namespace World
{
struct TRoboProto;

// OpenNeoUA custom: gameplay-facing Vehicle class used only by the extended
// energy/job systems. This deliberately does not add or alter BACT_TYPES, so
// physics, movement and legacy actor dispatch remain untouched.
enum VehicleCombatClass : uint8_t
{
    VEHICLE_COMBAT_CLASS_UNKNOWN = 0,
    VEHICLE_COMBAT_CLASS_HELI,
    VEHICLE_COMBAT_CLASS_TANK,
    VEHICLE_COMBAT_CLASS_PLANE,
    VEHICLE_COMBAT_CLASS_GLIDER,
    VEHICLE_COMBAT_CLASS_ZEPPELIN,
    VEHICLE_COMBAT_CLASS_UFO,
    VEHICLE_COMBAT_CLASS_CAR,
    VEHICLE_COMBAT_CLASS_ROBO,
    VEHICLE_COMBAT_CLASS_GUN,
    // Appended to preserve every existing combat-class numeric value.
    VEHICLE_COMBAT_CLASS_CRUISER,
    VEHICLE_COMBAT_CLASS_COUNT
};

enum DecorationFXMode
{
    DECORATION_FX_PERIODIC = 0,
    DECORATION_FX_PERSISTENT = 1
};

// OpenNeoUA custom: RGBA visual tint (see visual_tint / wireframe_tint and related params).
// RGB is a target hue: when it differs from white, renderers replace the source hue
// while preserving source intensity. Alpha remains multiplicative. Neutral default = no change.
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

    bool ColorizesRGB() const
    {
        return r != 1.0 || g != 1.0 || b != 1.0;
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

// OpenNeoUA custom: external visual paths for the legacy visual-state slots.
// Vehicle/Weapon prototypes keep one instance for 3DS and one for BASE paths;
// the numeric vp_* fields remain the vanilla-safe fallback.
struct TExternalVisualSet
{
    std::string normal;
    std::string fire;
    std::string dead;
    std::string wait;
    std::string megadeth;
    std::string genesis;
    std::string launch;
};

// Shared authored quantity for parameters that accept either an absolute value
// (for example 2000) or an explicit percentage (for example 5%). The parser
// owns syntax/range validation; runtime callers only consume the normalized
// representation.
struct TAbsoluteOrPercent
{
    float value = 0.0f;
    bool percent = false;
    bool defined = false;

    void Clear()
    {
        value = 0.0f;
        percent = false;
        defined = false;
    }
};

// OpenNeoUA custom: shared visual-only mesh tracer configuration.
// Physical Weapon tracers sample the authoritative projectile path; MGUN tracers
// reuse the same renderer while travelling along the already resolved hitscan ray.
struct TWeaponTracerConfig
{
    bool enabled = false;
    TVisualTint tint;
    float size_z = 300.0f;
    float size_x = 3.0f;
    // Runtime dimensions/effects are independent from geometry. Tracer geometry
    // is authored externally through mesh_path; there is no procedural fallback.
    bool has_size_y = false;
    float size_y = 0.0f;
    vec3d pos = vec3d(0.0, 0.0, 0.0);

    // Required external geometry for visible tracers. The authoring prefixes
    // expose this as mesh_tracer_path / mgun_mesh_tracer_path. Canonical paths
    // are Data-relative (for example 3DS/mgun_tracer_cross.3ds). Empty or
    // failed paths intentionally render no tracer.
    std::string mesh_path;

    bool has_tint_head = false;
    bool has_tint_tail = false;
    TVisualTint tint_head;
    TVisualTint tint_tail;

    // Endpoint size overrides are uniform across the transverse X/Y section.
    // Longitudinal extent remains owned exclusively by size_z. This preserves
    // continuous tracer geometry and avoids the gaps caused by scaling each
    // sampled segment's Z length independently.
    bool has_head_size = false;
    bool has_tail_size = false;
    float head_size = 0.0f;
    float tail_size = 0.0f;

    float glow_rate = 0.0f;
    float noise_rate = 0.0f;
    float pulse_rate = 0.0f;
    float pulse_speed = 0.0f;

    float ResolveSizeY() const
    {
        return has_size_y ? size_y : size_x;
    }

    float ResolveHeadSizeX() const
    {
        return has_head_size ? head_size : size_x;
    }

    float ResolveTailSizeX() const
    {
        return has_tail_size ? tail_size : size_x;
    }

    float ResolveHeadSizeY() const
    {
        return has_head_size ? head_size : ResolveSizeY();
    }

    float ResolveTailSizeY() const
    {
        return has_tail_size ? tail_size : ResolveSizeY();
    }

    TWeaponTracerConfig()
    {
        tint.r = 1.0f;
        tint.g = 210.0f / 255.0f;
        tint.b = 80.0f / 255.0f;
        tint.a = 180.0f / 255.0f;
    }
};

struct TDecorationFXConfig
{
    uint8_t mode = DECORATION_FX_PERIODIC;
    int16_t vp = 0;
    std::string mesh3ds;
    std::string basePath;
    int interval_min = 0;
    int interval_max = 0;
    int count_min = 0;
    int count_max = 0;
    int duration = 1000;
    float random_pos = 0.0;
    vec3d scale = vec3d(1.0, 1.0, 1.0);
    vec3d spin = vec3d(0.0, 0.0, 0.0);
    int fade_in = 0;
    int fade_out = 0;
    vec3d offset;
    TVisualTint tint;
    vec3d vp_trail_scale = vec3d(1.0, 1.0, 1.0);
    int vp_trail_fade_in = 0;
    int vp_trail_fade_out = 0;
    TVisualTint vp_trail_tint;
};

struct TChainFXVisual
{
    int16_t vp = 0;
    std::string mesh3ds;
    std::string basePath;
    bool has_tint = false;
    TVisualTint tint;
};

struct TAtmosphericFXProfile
{
    bool valid = false;
    std::string loop_sound;
    int loop_sound_volume = 100;
    std::string mesh3ds;
    int count = 0;
    vec3d spawn_radius = vec3d(0.0, 0.0, 0.0);
    vec3d spawn_offset = vec3d(0.0, 0.0, 0.0);
    int lifetime_min = 0;
    int lifetime_max = 0;
    vec3d velocity = vec3d(0.0, 0.0, 0.0);
    vec3d velocity_random = vec3d(0.0, 0.0, 0.0);
    vec3d scale = vec3d(1.0, 1.0, 1.0);
    vec3d spin = vec3d(0.0, 0.0, 0.0);
    int fade_in = 0;
    int fade_out = 0;
    TVisualTint tint;
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
        MODE_PHYSICAL,
        MODE_GROUND_DECAL
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
    float mid_size = 0.0;
    float end_size = 0.0;
    bool has_mid_size = false;
    int duration = 0;
    int fade_in = 0;
    int fade_out = 0;
    std::vector<TChainFXVisual> visuals;
    int physical_vehicle = 0;
    std::string ground_decal_texture;
    int ground_decal_points = 12;
    float ground_decal_jaggedness = 0.35f;
    float ground_decal_size = 0.0f;
    TVisualTint ground_decal_tint;
    bool ground_decal_random_rotation = false;
    float ground_decal_edge_fade = 0.0f;
};

struct TRoboColl
{
    float robo_coll_radius = 0.0;
    vec3d coll_pos;
    vec3d field_10;
    bool debug_visible = true;
};

struct rbcolls
{
    int field_0 = 0;
    std::vector<TRoboColl> roboColls;
    bool modelBoundsValid = false;
    vec3d modelMin;
    vec3d modelMax;

    rbcolls() {};

    rbcolls(const rbcolls &b)
    {
        operator =(b);
    }

    rbcolls(rbcolls &&b)
    {
        field_0 = b.field_0;
        roboColls = std::move(b.roboColls);
        modelBoundsValid = b.modelBoundsValid;
        modelMin = b.modelMin;
        modelMax = b.modelMax;
    }

    rbcolls &operator=(const rbcolls &b)
    {
        field_0 = b.field_0;
        roboColls = b.roboColls;
        modelBoundsValid = b.modelBoundsValid;
        modelMin = b.modelMin;
        modelMax = b.modelMax;
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

    std::vector<TSndSample> ExtSamples;
    int16_t volume = 0;
    int pitch_min = 0;
    int pitch_max = 0;
    float radius = 0.0f;
    TSndFXParam sndPrm;
    TSndFxPosParam sndPrm_shk;
    std::vector<TSampleParams> extS;

    void SetPitchRange(int minPitch, int maxPitch);
    void ConfigureSoundSourcePitch(TSoundSource &sound) const;
    void LoadSamples();
    void ClearSounds();
};

constexpr size_t ROBO_GUN_MAX_COUNT = 20;
constexpr size_t UNIT_COLL_MAX_COUNT = 32;  // OpenNeoUA: max compound collision spheres per vehicle

struct TDamagedFXConfig
{
    std::vector<int16_t> vps;
    std::vector<std::string> meshes3ds;
    float scale = 1.0;
    TAbsoluteOrPercent threshold;
    int count_min = 0;
    int count_max = 0;
    int interval_min = 0;
    int interval_max = 0;
    TAbsoluteOrPercent random_max_offset;
    bool trail_only = false;
};

struct TWeaponDebuffConfig
{
    bool allow = false;
    bool allow_on_host_station = false;
    bool inherit_to_children = false;
    std::string name;
    std::string icon;
    TAbsoluteOrPercent damage;
    bool mindcontrol = false;
    int tick_time = 1000;
    bool has_tick_time = false;
    int duration = 5000;
    bool stun = false;
    float stun_motion_level = 0.0f;
    bool stun_unit_fire = true;
    float force_malus = 0.0;
    float maxrot_malus = 0.0;
    float shield_malus = 0.0;
    float mgun_shot_time_malus = 0.0;
    float shot_time_malus = 0.0;
    float snd_pitch_multiplier = 1.0;
    TVisualTint target_tint;
    std::vector<int16_t> vps;
    std::string mesh3ds;
    float scale = 1.0;
    TVisualTint tint;
    TAbsoluteOrPercent random_max_offset;
    TVisualTint vp_trail_tint;
    bool has_vp_trail_tint = false;
    TVhclSound tick_snd;

    TWeaponDebuffConfig()
    {
        tick_snd.volume = 120;
        tick_snd.sndPrm.mag0 = 1.0;
        tick_snd.sndPrm.time = 1000;
        tick_snd.sndPrm_shk.mag0 = 1.0;
        tick_snd.sndPrm_shk.time = 1000;
        tick_snd.sndPrm_shk.mute = 0.02;
        tick_snd.sndPrm_shk.pos.x = 0.2;
        tick_snd.sndPrm_shk.pos.y = 0.2;
        tick_snd.sndPrm_shk.pos.z = 0.2;
    }
};

struct TSuperItemProfile
{
    std::string id;
    bool valid = false;
    bool duplicate = false;

    int wave_vp = 0;
    std::string wave_3ds;
    std::string wave_base;
    std::string fallout_fx_profile; // Data-relative profile activated locally behind the propagated wave
    vec3d wave_axis_scale = vec3d(1.0, 1.0, 1.0);
    TVisualTint wave_tint;
    float wave_start_speed = 0.0f;
    float wave_end_speed = 0.0f;
    float wave_speed_ramp_time = 0.0f;
    bool has_wave_start_speed = false;
    bool has_wave_end_speed = false;
    bool has_wave_speed_ramp_time = false;
    float push_force = 0.0f; // shared configured push intensity, 0..10
    float push_radius = 0.0f;
    int push_falloff = 0;
    float wave_push_force = 0.0f; // shared configured push intensity, 0..10
    int fade_in = 0;
    int fade_out = 0;

    int wave_unit_damage = 0;
    int wave_building_total_destruction = 0; // explicit authored percentage 0..100
    TWeaponDebuffConfig debuff;
    TVhclSound detonate_snd;
    TVhclSound wave_snd;
    std::vector<TChainFXConfig> detonate_chain_fx;

    TSuperItemProfile()
    {
        detonate_snd.volume = 120;
        detonate_snd.sndPrm.mag0 = 1.0;
        detonate_snd.sndPrm.time = 1000;
        detonate_snd.sndPrm_shk.mag0 = 1.0;
        detonate_snd.sndPrm_shk.time = 1000;
        detonate_snd.sndPrm_shk.mute = 0.02;
        detonate_snd.sndPrm_shk.pos.z = 1.0;

        wave_snd.volume = 120;
        wave_snd.sndPrm.mag0 = 1.0;
        wave_snd.sndPrm.time = 1000;
        wave_snd.sndPrm_shk.mag0 = 1.0;
        wave_snd.sndPrm_shk.time = 1000;
        wave_snd.sndPrm_shk.mute = 0.02;
        wave_snd.sndPrm_shk.pos.z = 1.0;
    }
};

struct TWeaponClusterConfig
{
    enum { MAX_COUNT = 64 };

    bool enable = false;
    int generations = 0;
    // count remains the scalar/lower endpoint for source compatibility.
    // count_max is equal to count for a fixed authored value.
    int count = 0;
    int count_max = 0;
    void GetCountRange(int &minCount, int &maxCount) const;
    int16_t weapon_id = 0;
    int trigger_time = 0;
    bool horizontal_angle_set = false;
    float horizontal_angle_min = 0.0f;
    float horizontal_angle_max = 0.0f;
    bool vertical_angle_set = false;
    float vertical_angle_min = 0.0f;
    float vertical_angle_max = 0.0f;
    float spread_x = 0.0;
    float spread_y = 0.0;
    int16_t vp = 0;
    std::string mesh3ds;
    std::string basePath;
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
    int protect = 0; // OpenNeoUA: optional damage-sponge attachment, unified attachment protection

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
        protect = b.protect;
    }

    TRoboGun& operator=(const TRoboGun &b)
    {
        pos = b.pos;
        dir = b.dir;
        gun_obj = b.gun_obj;
        robo_gun_name = b.robo_gun_name;
        icon = b.icon;
        robo_gun_type = b.robo_gun_type;
        protect = b.protect;
        return *this;
    }
};

struct TVhclProto
{
    enum WeaponPlayerSwitchMode
    {
        WEAPON_PLAYER_SWITCH_MODE_SEQUENCE = 0,
        WEAPON_PLAYER_SWITCH_MODE_RANDOM = 1,
        WEAPON_PLAYER_SWITCH_MODE_MANUAL = 2
    };

    enum WeaponAISwitchMode
    {
        WEAPON_AI_SWITCH_MODE_SEQUENCE = 0,
        WEAPON_AI_SWITCH_MODE_RANDOM = 1,
        WEAPON_AI_SWITCH_MODE_SMART = 2
    };

    enum FireXMode
    {
        FIRE_X_MODE_VANILLA = 0,
        FIRE_X_MODE_SEQUENCE = 1,
        FIRE_X_MODE_RANDOM = 2,
        FIRE_X_MODE_SALVE_SEQUENCE = 3,
        FIRE_X_MODE_SALVE_MIRROR = 4
    };

    enum { FIRE_X_MAX_SLOTS = 256 };

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
        SND_HANDBRAKE = 13,
        SND_PICKUP = 14,

        SND_MAX     = 15
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
    VehicleCombatClass combat_class = VEHICLE_COMBAT_CLASS_UNKNOWN;
    uint8_t disable_enable_bitmask = 0;
    int8_t weapon = 0;
    std::array<int16_t, 3> extra_weapons = {0, 0, 0};
    // OpenNeoUA custom: optional projectile-count overrides for weapon_2/_3/_4.
    // A zero value inherits the primary num_weapons count.
    std::array<int16_t, 3> extra_num_weapons = {0, 0, 0};
    // OpenNeoUA custom: inclusive min_max authoring for projectile counts.
    // The scalar fields above remain the legacy/lower-endpoint compatibility
    // values; zero on an extra slot still means "inherit primary".
    std::array<int16_t, 3> extra_num_weapons_min = {0, 0, 0};
    std::array<int16_t, 3> extra_num_weapons_max = {0, 0, 0};
    int weapon_player_switch_mode = WEAPON_PLAYER_SWITCH_MODE_SEQUENCE;
    int weapon_ai_switch_mode = WEAPON_AI_SWITCH_MODE_SEQUENCE;
    int field_4 = 0;
    int8_t mgun = 0;
    bool mgun_set = false;
    int16_t num_mguns = 1;
    int mgun_shot_time = 0;
    // OpenNeoUA custom: optional player-only override for vehicle-level MGUN
    // cadence. Zero/unset inherits mgun_shot_time, preserving old data exactly.
    int mgun_shot_time_user = 0;
    // OpenNeoUA: external/third-person MGUN visual recoil intensity 0..10.
    // Render-only: never changes Vehicle position, velocity or movement input.
    float mgun_recoil = 0.0f;
    // OpenNeoUA: independent cockpit-only MGUN SHK/camera-shake intensity 0..10.
    float mgun_recoil_cockpit = 0.0f;
    // OpenNeoUA: shared tracer config used by normal Vehicle MGUNs and
    // model = gun/module + gun_type = mg; authoring uses mgun_mesh_tracer_*.
    TWeaponTracerConfig mgun_tracer;
    bool mgun_decal_enable = false;
    TChainFXConfig mgun_decal;
    int16_t mgun_vp_dead = 0;
    int16_t mgun_vp_megadeth = 0;
    std::string mgun_3ds_dead;
    std::string mgun_3ds_megadeth;
    std::string mgun_base_dead;
    std::string mgun_base_megadeth;
    float mgun_power = 0.0;
    float mgun_angle = 0.0;
    std::string mgun_name;
    bool mgun_power_set = false;
    bool mgun_angle_set = false;
    float weapon_spread_x = 0.0;
    float weapon_spread_y = 0.0;
    float weapon_arc_x = 0.0;
    float weapon_arc_y = 0.0;
    float weapon_cone_xy = 0.0;
    float mgun_spread_x = 0.0;
    float mgun_spread_y = 0.0;
    uint8_t type_icon = 0;
    std::string name;
    int16_t vp_normal = 0;
    int16_t vp_fire = 0;
    int16_t vp_dead = 0;
    int16_t vp_wait = 0;
    int16_t vp_megadeth = 0;
    int16_t vp_genesis = 0;
    TExternalVisualSet visual_3ds;
    TExternalVisualSet visual_base;
    vec3d visual_scale = vec3d(1.0, 1.0, 1.0);
    vec3d visual_rotation = vec3d(0.0, 0.0, 0.0);
    vec3d visual_spin = vec3d(0.0, 0.0, 0.0);
    TVisualTint visual_tint; // OpenNeoUA custom: main model visual-only RGBA target hue/alpha
    TVisualTint wireframe_tint; // OpenNeoUA custom: UI wireframe-only RGBA target hue/alpha
    TDamagedFXConfig damaged_fx;
    TDecorationFXConfig decoration_fx;
    std::string unit_gun_icon;
    int power = 0;
    float power_radius = 0.0;
    int power_falloff = 1;
    std::string power_fx_profile; // Data-relative FX profile; XZ coverage is always power_radius
    // OpenNeoUA custom: fixed horizontal radius for automatic world-UI inspection
    // while directly controlling model = ufo. Independent from optical zoom.
    float spy_ui_radius = 0.0f;
    // OpenNeoUA custom: maximum number of 1.25x optical zoom-in steps for model = ufo.
    // -1 keeps the legacy OpenNeoUA zoom cap; 0 disables zoom for this vehicle.
    int zoom_steps = -1;
    float damaged_force_malus = 0.0;
    float damaged_maxrot_malus = 0.0;
    float damaged_mgun_shot_time_malus = 0.0;
    float damaged_shot_time_malus = 0.0;
    float damaged_snd_pitch_multiplier = 1.0;
    int spawn_units = 0;
    int16_t spawn_vehicle = 0;
    int spawn_interval = 5000;
    float spawn_trigger_radius = 0.0;
    float spawn_random_pos = 0.0;
    vec3d spawn_offset = vec3d(0.0, 0.0, 0.0);
    int spawn_max_active = 0;
    int spawn_count = 1;
    int spawn_instant = 0;
    int spawn_at_death_units = 0;
    int16_t spawn_at_death_vehicle = 0;
    int spawn_at_death_count = 1;
    float spawn_at_death_random_pos = 0.0;
    int spawn_at_death_instant = 0;
    int spawn_at_death_immunity_time = 0;
    int proximity_defense_enable = 0;
    int proximity_defense_weapon = 0;
    float proximity_defense_trigger_radius = 0.0;
    int proximity_defense_interval = 1000;
    int proximity_defense_shots = 12;
    int proximity_defense_vp_launch = -1;
    std::string proximity_defense_3ds_launch;
    std::string proximity_defense_base_launch;
    int proximity_defense_fire_mode = 0;
    int proximity_defense_sequence_delay = 100;
    int proximity_defense_mode = 0;
    bool proximity_defense_horizontal_angle_set = false;
    float proximity_defense_horizontal_angle_min = 0.0;
    float proximity_defense_horizontal_angle_max = 360.0;
    bool proximity_defense_vertical_angle_set = false;
    float proximity_defense_vertical_angle_min = -10.0;
    float proximity_defense_vertical_angle_max = 45.0;
    int max_active_at_once = 0;
    std::vector<DestFX> dest_fx;      // dest_fx
    std::vector<DestFX>    ExtDestroyFX; // ext_dest_fx
    std::array<TVhclSound, SND_MAX> sndFX;
    int vo_type = 0;
    // OpenNeoUA custom: sparse per-vehicle speech event path stems.
    // Missing or invalid entries fall back to the vanilla vo_type voice.
    std::map<std::string, std::string> speech_events;
    float max_pitch = 0.0;
    int16_t field_1D6D = 0;
    int16_t field_1D6F = 0;
    int shield = 0;
    int energy = 0;
    int mimic_energy_cost = 0; // OpenNeoUA custom: current mimic shell production cost; 0 keeps vanilla energy-as-cost
    int mimic_energy_cost_min = 0;
    int mimic_energy_cost_max = 0;
    int GetProductionCost() const { return mimic_energy_cost > 0 ? mimic_energy_cost : energy; }
    int RollMimicProductionCost();
    bool invulnerable = false;
    int field_1D79 = 0;
    float adist_sector = 0.0;
    float adist_bact = 0.0;
    float sdist_sector = 0.0;
    float sdist_bact = 0.0;
    // OpenNeoUA: optional atomic combat-distance authoring. The bit mask records
    // presence, so malformed/partial new sets can fall back to the untouched
    // adist_*/sdist_* path instead of mixing old and new semantics.
    float ai_attack_range = 0.0f;
    float ai_retreat_range = 0.0f;
    float ai_reengage_range = 0.0f;
    uint8_t ai_combat_distance_mask = 0;
    enum : uint8_t
    {
        AI_COMBAT_ATTACK_DEFINED = 1 << 0,
        AI_COMBAT_RETREAT_DEFINED = 1 << 1,
        AI_COMBAT_REENGAGE_DEFINED = 1 << 2,
        AI_COMBAT_ALL_DEFINED = AI_COMBAT_ATTACK_DEFINED |
                                AI_COMBAT_RETREAT_DEFINED |
                                AI_COMBAT_REENGAGE_DEFINED
    };
    bool HasValidUnifiedAICombatDistance() const
    {
        return ai_combat_distance_mask == AI_COMBAT_ALL_DEFINED &&
               std::isfinite(ai_attack_range) && ai_attack_range > 0.0f &&
               std::isfinite(ai_retreat_range) && ai_retreat_range >= 0.0f &&
               std::isfinite(ai_reengage_range) &&
               ai_reengage_range > ai_retreat_range &&
               ai_reengage_range <= ai_attack_range;
    }
    int8_t radar = 0;
    float push_resistance = 0.0; // OpenNeoUA custom: target-side resistance to push / aoe_unit_push
    bool has_push_resistance = false; // true only when push_resistance is explicitly authored
    float push_at_death_force = 0.0f; // OpenNeoUA custom: 0..10 radial push intensity emitted on actual vehicle death
    float push_at_death_radius = 0.0f;
    int push_at_death_falloff = 0;
    float mass = 0.0;
    float force = 0.0;
    float airconst = 0.0;
    float maxrot = 0.0;
    float height = 0.0;
    float radius = 0.0;
    // Tracks whether the script explicitly authored radius. The numeric value
    // still keeps the vanilla default, but manual coll_* spheres may suppress
    // that default collision unless radius was really present in the script.
    bool radius_defined = false;
    float overeof = 0.0;
    float vwr_radius = 0.0;
    float vwr_overeof = 0.0;
    // OpenNeoUA modern cockpit camera: per-vehicle offset only. Missing axes remain 0.
    vec3d cockpit_camera_offset = vec3d(0.0, 0.0, 0.0);
    // Player-only gun cockpit recoil multiplier. 0/absent keeps the current cockpit camera stable.
    float cockpit_gun_camera_recoil = 0.0f;
    float gun_angle = 0.0;
    float fire_x = 0.0;
    float fire_y = 0.0;
    float fire_z = 0.0;
    int8_t fire_x_mode = FIRE_X_MODE_VANILLA;
    float fire_x_start = 0.0;
    float fire_x_step = 0.0;
    int fire_x_slots = 0;
    bool fire_x_start_defined = false;
    bool fire_x_step_defined = false;
    bool fire_x_slots_defined = false;
    bool fire_x_advanced = false;
    int16_t num_weapons = 0;
    int16_t num_weapons_min = 0;
    int16_t num_weapons_max = 0;
    // OpenNeoUA custom: optional per-Vehicle percentage of maximum energy charged
    // once per successful normal Weapon firing event, regardless of num_weapons.
    // Presence is tracked separately so 0% can explicitly disable the legacy
    // per-projectile weapon drain while missing/invalid preserves vanilla behavior.
    float weapon_energy_cost = 0.0f;
    bool weapon_energy_cost_defined = false;
    // OpenNeoUA custom: optional per-Vehicle percentage of maximum energy charged
    // once per effective MGUN firing pulse. Presence is tracked separately so
    // 0% explicitly disables the legacy continuous MGUN drain while
    // missing/invalid preserves vanilla behavior.
    float mgun_fire_energy_cost = 0.0f;
    bool mgun_fire_energy_cost_defined = false;
    // 0/absent keeps the current unlimited per-projectile Weapon sound package.
    int16_t num_weapons_snd_events = 0;
    void GetWeaponProjectileCountRange(int sourceSlot, int &minCount, int &maxCount) const;
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
    int8_t job_fightplane = 0;
    int8_t job_fightcruiser = 0;
    int8_t job_fightglider = 0;
    int8_t job_fightzeppelin = 0;
    int8_t job_fightufo = 0;
    int8_t job_fightcar = 0;
    int8_t job_fightgun = 0;
    int8_t job_conquer = 0;
    int8_t job_reconnoitre = 0;
    bool job_fighttank_defined = false;
    bool job_fighthelicopter_defined = false;
    bool job_fightflyer_defined = false;
    bool job_fightrobo_defined = false;
    bool job_fightplane_defined = false;
    bool job_fightcruiser_defined = false;
    bool job_fightglider_defined = false;
    bool job_fightzeppelin_defined = false;
    bool job_fightufo_defined = false;
    bool job_fightcar_defined = false;
    bool job_fightgun_defined = false;
    bool job_conquer_defined = false;
    bool job_reconnoitre_defined = false;
    NC_STACK_skeleton *wireframe = NULL;
    NC_STACK_skeleton *hud_wireframe = NULL;
    NC_STACK_skeleton *mg_wireframe = NULL;
    // OpenNeoUA custom: optional Weapon-style info wireframe for gun_type = mg.
    NC_STACK_skeleton *mgun_wireframe = NULL;
    NC_STACK_skeleton *wpn_wireframe_1 = NULL;
    NC_STACK_skeleton *wpn_wireframe_2 = NULL;
    IDVList initParams;

    bool hidden = false;
    int8_t unhideRadar = 0;

    // OpenNeoUA custom: vehicle-only "invisible" stealth. When true every new instance
    // spawns fully cloaked (no render/radar/UI/sound/AI-target) until its first real
    // attack, after which it is permanently revealed. Independent from the legacy
    // owner-based `hidden`/`unhide_radar` system above. Default off.
    bool invisible = false;
    int16_t invisible_reveal_vp = 0;
    std::string invisible_reveal_3ds;
    std::string invisible_reveal_base;

    TRoboProto *RoboProto = NULL;
    std::vector<TRoboGun> unit_guns;

    int is_mimic = 0;                       // OpenNeoUA: model = mimic shell/disguise proto
    TVisualTint mimic_tint;                 // OpenNeoUA: model = mimic target hue/alpha composed with the copied visual tint
    TVhclSound snd_mimic;                   // OpenNeoUA: model = mimic persistent shell loop

    rbcolls coll;                           // OpenNeoUA: universal compound collision spheres (coll_*)

    ~TVhclProto();
};

struct TWeapProto
{
    // OpenNeoUA custom: optional external 3DS body for continuous laser beams.
    // The configuration is nested in the weapon prototype so normal and vertical
    // lasers share one data-driven visual path without changing gameplay state.
    // When enabled, external geometry is mandatory: empty/failed mesh paths render
    // no beam body and never fall back to procedural or vp_normal geometry.
    struct TLaserMeshConfig
    {
        bool enabled = false;
        std::string mesh_path;
        TVisualTint tint;
        float size_x = 5.0f;
        bool has_size_y = false;
        float size_y = 0.0f;
        float glow_rate = 1.0f;
        float pulse_rate = 0.0f;
        float pulse_speed = 0.0f;
        float noise_rate = 0.0f;
        // Visual-only fade distance before a real unit/world contact. Zero keeps
        // the current constant-alpha beam; the endpoint itself becomes nearly
        // transparent without changing the gameplay hit point.
        float impact_fade_length = 0.0f;

        float ResolveSizeY() const
        {
            return has_size_y && std::isfinite(size_y) && size_y > 0.0f
                       ? size_y
                       : size_x;
        }
    };

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
        WEAPON_FLAG_ARTILLERY_SHELL = 64, // OpenNeoUA custom: radar-guided ballistic barrage
        WEAPON_FLAG_LASER = 128, // OpenNeoUA custom: continuous targeted beam weapon
        WEAPON_FLAG_KAMIKAZE = 512, // OpenNeoUA custom: carrier-mounted detonation payload
        WEAPON_FLAG_ARC_GRENADE = 1024, // OpenNeoUA custom: dedicated ballistic arc-grenade identity

        WEAPON_FLAGS_BOMB = WEAPON_FLAG_PROJECTILE,
        WEAPON_FLAGS_ROCKET = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT,
        WEAPON_FLAGS_MISSILE = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT | WEAPON_FLAG_TARGETED,
        WEAPON_FLAGS_OBSAVOID = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT | WEAPON_FLAG_OBSAVOID,
        WEAPON_FLAGS_GRENADE = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_GRENADE,
        WEAPON_FLAGS_ARC_GRENADE = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_GRENADE | WEAPON_FLAG_ARC_GRENADE,
        WEAPON_FLAGS_HOMING_BOMB = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_HOMING_BOMB,
        WEAPON_FLAGS_ARTILLERY_SHELL = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_ARTILLERY_SHELL,
        // Laser keeps PROJECTILE|DIRECT|TARGETED so the AI/aim/lock logic treats it
        // like a normal targeted weapon, but the LASER bit reroutes firing to the
        // continuous-beam path (UpdateLaser) instead of spawning a projectile.
        WEAPON_FLAGS_LASER = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_DIRECT | WEAPON_FLAG_TARGETED | WEAPON_FLAG_LASER,
        WEAPON_FLAGS_KAMIKAZE = WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_KAMIKAZE
    };

    int8_t unitID = 0;
    uint8_t enable_mask = 0;
    int16_t _weaponFlags = 0;

    // OpenNeoUA custom: Arc Grenade has a dedicated Weapon identity while
    // retaining the shared grenade bit for compatible grenade-family behavior.
    bool IsArcGrenade() const
    {
        return (_weaponFlags & WEAPON_FLAG_ARC_GRENADE) != 0;
    }

    bool IsHomingBomb() const
    {
        return _weaponFlags == WEAPON_FLAGS_HOMING_BOMB;
    }

    // OpenNeoUA custom: true only for weapons declared as "model = artillery_shell".
    bool IsArtilleryShell() const
    {
        return (_weaponFlags & WEAPON_FLAG_ARTILLERY_SHELL) != 0;
    }

    // OpenNeoUA custom: true only for weapons declared as "model = laser".
    bool IsLaser() const
    {
        return (_weaponFlags & WEAPON_FLAG_LASER) != 0;
    }

    // OpenNeoUA custom: vertical fire is a mode of model = laser, never a separate model.
    bool IsVerticalLaser() const
    {
        return IsLaser() && vertical_laser_enable;
    }

    // OpenNeoUA custom: true only for carrier payloads declared as
    // "model = kamikaze". They are mounted but never fired as projectiles.
    bool IsKamikaze() const
    {
        return (_weaponFlags & WEAPON_FLAG_KAMIKAZE) != 0;
    }

    // OpenNeoUA custom: render-only projectile motion modifiers are available to
    // every physical projectile class. Continuous laser classes deliberately
    // remain excluded because they render beams instead of a travelling projectile VP.
    bool SupportsProjectileVisualMotion() const
    {
        return (_weaponFlags & WEAPON_FLAG_PROJECTILE) != 0 &&
               !IsLaser() && !IsKamikaze();
    }

    bool SupportsProjectileTracer() const
    {
        return (_weaponFlags & WEAPON_FLAG_PROJECTILE) != 0 &&
               !IsLaser() && !IsKamikaze();
    }

    bool IsBombLike() const
    {
        return _weaponFlags == WEAPON_FLAGS_BOMB || IsHomingBomb() || IsVerticalLaser();
    }

    int GetFireControlFlags() const
    {
        // Arc Grenade keeps the legacy grenade-family fire-control semantics;
        // its dedicated identity bit is runtime-only and must not alter AI aim.
        return IsBombLike() ? 0 :
            (_weaponFlags & ~(WEAPON_FLAG_PROJECTILE | WEAPON_FLAG_ARC_GRENADE));
    }

    uint8_t type_icon = 0;
    std::string name;
    int16_t vp_normal = 0;
    int16_t vp_fire = 0;
    // OpenNeoUA custom, Weapon-side: when this Weapon is fired, the carrier
    // temporarily uses its own Vehicle fire visual. Missing/0 keeps vanilla behavior.
    bool weapon_use_vehicle_fire_visual = false;
    int16_t vp_dead = 0;
    int16_t vp_wait = 0;
    int16_t vp_megadeth = 0;
    int16_t vp_genesis = 0;
    int16_t vp_launch = 0;
    TExternalVisualSet visual_3ds;
    TExternalVisualSet visual_base;
    vec3d launch_scale = vec3d(1.0, 1.0, 1.0);
    vec3d visual_scale = vec3d(1.0, 1.0, 1.0);
    vec3d visual_rotation = vec3d(0.0, 0.0, 0.0);
    vec3d visual_spin = vec3d(0.0, 0.0, 0.0);
    // OpenNeoUA custom: render-only spiral orbit for every physical projectile
    // class except model = laser (including vertical mode). Speed uses the shared
    // 0..10 revolutions-per-second scale; radius is the lateral orbit distance in
    // model/world units (0..1000). Physical movement and collision stay central.
    float spiral_speed = 0.0f;
    float spiral_radius = 0.0f;
    // OpenNeoUA custom: render-only erratic projectile motion. Factor controls
    // smooth random target changes per second (0..10), while radius bounds the
    // lateral deviation (0..1000). Chaos takes priority over Spiral when valid.
    float chaos_factor = 0.0f;
    float chaos_radius = 0.0f;
    TVisualTint visual_tint; // OpenNeoUA custom: main model visual-only RGBA target hue/alpha
    vec3d vp_trail_scale = vec3d(1.0, 1.0, 1.0);
    vec3d vp_trail_spin = vec3d(0.0, 0.0, 0.0);
    TVisualTint vp_trail_tint; // OpenNeoUA custom: weapon embedded particle/trail tint
    TVisualTint wireframe_tint; // OpenNeoUA custom: UI wireframe-only RGBA target hue/alpha
    TWeaponTracerConfig tracer; // OpenNeoUA custom: external-mesh projectile tracer
    std::vector<DestFX> dfx;
    std::vector<DestFX> ExtDestroyFX; // ext_dest_fx
    std::array<TVhclSound, SND_MAX> sndFXes;
    // OpenNeoUA custom: local-player-only launch shake. When configured it replaces
    // the generic shk_launch shake for the directly controlled player weapon and
    // is fired once per successful LaunchMissile() call, regardless of num_weapons.
    TSndFxPosParam shk_launch_player;
    TWeaponDebuffConfig debuff;
    TWeaponClusterConfig cluster;
    // OpenNeoUA UI helper: combines the carrier num_weapons range with
    // this weapon's valid cluster_count range into one total min/max.
    // Gameplay rolls stay independent; this is only a compact readout.
    void GetCombinedProjectileCountRange(int carrierMinCount,
                                         int carrierMaxCount,
                                         int &minCount,
                                         int &maxCount) const;
    TWeaponChainConfig chain;
    TDecorationFXConfig decoration_fx;
//    int field_870 = 0;
//    int field_874 = 0;
    int energy = 0;
    int aoe_unit_energy = 0;
    int aoe_building_energy = 0;
    int aoe_sector_energy = 0;
    int aoe_falloff = 0;
    float aoe_unit_push = 0.0f; // OpenNeoUA custom: radial push intensity, continuous 0..10
    // OpenNeoUA custom: 0..10 direct-hit single-target knockback. Same dispatcher as aoe_unit_push,
    // but only for the primary/direct-hit unit. If both push and aoe_unit_push are set,
    // the direct-hit unit receives only push; nearby units receive aoe_unit_push.
    float push = 0.0f;
    int armor_penetration_targets = 0; // OpenNeoUA custom: direct-hit unit penetrations before final impact
    float recoil = 0.0; // OpenNeoUA: physical Weapon recoil intensity 0..10
//    int field_87C = 0;
    int life_time = 0;
    // OpenNeoUA: life_time accepts either a fixed value or an inclusive
    // min_max range. life_time remains the stable scalar/lower endpoint for
    // legacy consumers; projectiles resolve the range when they are created.
    int life_time_min = 0;
    int life_time_max = 0;
    int life_time_nt = 0;
    int drive_time = 0;
    int delay_time = 0;
    float adistSector = 0;
    float adistBact = 0;
    int shot_time = 0;
    int shot_time_user = 0;
    // OpenNeoUA custom: optional ramp-up cadence for normal/main Weapons.
    // shot_time/shot_time_user remain the canonical starting cadence; while uninterrupted
    // normal Weapon fire is held, ramp_up_time interpolates toward ramp_up_max_shot_time.
    // Releasing FIRE or entering a structural firing pause/reset restores the base cadence.
    int ramp_up_time = 0;
    int ramp_up_max_shot_time = 0;
    int salve_shots = 0;
    int salve_delay = 0;
    // OpenNeoUA: generic multi-target count for compatible homing weapon models.
    // Consumed by missile and homing_bomb; 0/1 keeps single-target behaviour.
    int multi_target = 0;
    // OpenNeoUA custom: shared continuous beam parameters for model = laser.
    // vertical_laser_enable selects the downward-fire mode. "energy" is static base damage per tick; the class
    // multipliers below (energy_heli/tank/flyer/robo) are applied like normal weapons.
    int   laser_energy_tick_time = 250;        // ms between damage ticks for AI/non-player fire
    int   laser_energy_tick_time_user = 150;   // ms between damage ticks for player-controlled fire
    float laser_energy_increment_rate = 0.0;   // extra base damage added after each connected tick
    float laser_max_energy = 0.0;              // max base damage per tick (<=0 => no clamp)
    float laser_visual_spacing = 40.0;             // visual-only distance between vp_normal beam instances
    int   laser_chain_allow = 0;               // 1 = primary laser hit may chain to nearby enemy units
    int   laser_chain_max_jumps = 0;           // max unit-to-unit chain segments after the primary hit
    float laser_chain_radius = 0.0;            // search radius around the last chained unit
    float laser_chain_damage_mult = 1.0;       // cumulative damage multiplier per chain jump
    int   laser_beam_count = 1;                // total direct shooter-to-target laser beams (<=1 = off)
    bool  vertical_laser_enable = false;          // 1 = model=laser uses the downward vertical-fire mode
    float vertical_laser_ai_trigger_radius = 300.0; // X/Z distance required before AI fires downward
    TLaserMeshConfig laser_mesh;                    // visual-only mesh body for laser, including vertical mode
    float energy_heli = 0.0;
    float energy_tank = 0.0;
    float energy_flyer = 0.0;
    float energy_robo = 0.0;
    float energy_plane = 0.0;
    float energy_cruiser = 0.0;
    float energy_glider = 0.0;
    float energy_zeppelin = 0.0;
    float energy_ufo = 0.0;
    float energy_car = 0.0;
    float energy_gun = 0.0;
    bool energy_heli_defined = false;
    bool energy_tank_defined = false;
    bool energy_flyer_defined = false;
    bool energy_robo_defined = false;
    bool energy_plane_defined = false;
    bool energy_cruiser_defined = false;
    bool energy_glider_defined = false;
    bool energy_zeppelin_defined = false;
    bool energy_ufo_defined = false;
    bool energy_car_defined = false;
    bool energy_gun_defined = false;
    // Vanilla class-specific direct-hit radii. A zero value falls back to the
    // generic weapon radius, matching the original Urban Assault behaviour.
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
    // vp_scale never affects any gameplay radius.
    float radius = 0.0;
    // Keep the numeric Weapon default for compatibility/non-collision consumers,
    // while tracking whether the script actually authored radius. With coll_*,
    // this is what distinguishes compound-only from compound + legacy sphere.
    bool radius_defined = false;
    rbcolls coll;
    // OpenNeoUA custom, model = kamikaze only: true XYZ proximity fuse.
    // Zero means physical contact (effective carrier radius + target radius).
    float trigger_radius = 0.0;
    float fire_time_scale = 1.0f; // model=kamikaze/player: one FIRE press latches slowdown; 1.0 disables the sequence
    TAbsoluteOrPercent fire_time_scale_hp_drain; // absolute HP/sec or explicit max-HP percentage/sec; zero/absent disables it
    float aoe_unit_radius = 0.0;
    float aoe_building_radius = 0.0;
    float aoe_sector_radius = 0.0;
    float overeof = 0.0;
    float vwr_radius = 0.0;
    float vwr_overeof = 0.0;
    float start_speed = 0.0;
    // OpenNeoUA custom, model = arc_grenade only. The angle is applied once at
    // launch as an absolute elevation above the world horizontal plane. Gravity
    // is then applied every frame by the dedicated Arc Grenade runtime. A missing,
    // zero or invalid gravity uses the engine-standard 9.80665 fallback.
    float grenade_arc_angle = 0.0f;
    float grenade_arc_gravity = 0.0f;
    // OpenNeoUA custom: dedicated artillery shell barrage weapon ("model = artillery_shell").
    // All defaults are vanilla-safe: with artillery_shell_barrage_shots <= 0 / no max range,
    // an artillery shell weapon simply never fires.
    enum
    {
        ARTILLERY_SHELL_MODE_BALLISTIC = 0,
        ARTILLERY_SHELL_MODE_VERTICAL_BARRAGE = 1
    };
    int   artillery_shell_mode = ARTILLERY_SHELL_MODE_BALLISTIC; // ballistic arc, or vertical_barrage mortar-style ascent/fall
    int   artillery_shell_fall_delay = 0;           // vertical_barrage only: ms from launch before vertical descent may begin
    float artillery_shell_min_range = 0.0;          // min distance from artillery shell to target zone
    float artillery_shell_max_range = 0.0;          // max distance for manual fire and automatic target search (<=0 = disabled)
    int   artillery_shell_requires_radar = 1;       // 1 = target sector must be visible to the owner faction
    int   artillery_shell_manual_mode_only = 0;     // 1 = disable the auto AI; the artillery shell only fires via manual map-click
    float artillery_shell_barrage_radius = 0.0;     // bombardment zone radius (marker size)
    int   artillery_shell_barrage_shots = 0;        // shells per barrage (<=0 = no barrage)
    int   artillery_shell_barrage_shot_delay = 250; // ms between shells in the same barrage
    int   artillery_shell_barrage_cooldown = 10000; // ms cooldown after a barrage ends
    float artillery_shell_arc_height = 2500.0;      // extra ballistic arc height / vertical barrage ascent height (engine units)
    float artillery_shell_speed = 0.0;              // artillery trajectory speed; <=0 keeps the legacy-safe internal timing fallback
    float artillery_shell_vertical_spread_x = 0.0;  // vertical_barrage only: random ascent tilt toward +/- world X, in degrees
    float artillery_shell_vertical_spread_z = 0.0;  // vertical_barrage only: random ascent tilt toward +/- world Z, in degrees
    int   artillery_shell_airburst = 1;             // 1 = explode at the target height; 0 = land on the real terrain height at the shell's own impact point
    std::string artillery_shell_marker_path = "artillery_ring_classic.svg"; // SVG path relative to Data/Interface/Map/Markers; invalid/missing falls back to the classic marker
    NC_STACK_skeleton *wireframe = NULL;
    IDVList initParams;
    std::vector<TChainFXConfig> chain_fx;

    int RollLifeTime() const;
    ~TWeapProto();
};

// Resolve the authored gameplay class of a runtime unit. Mimics use their
// current disguise prototype; invalid/untyped actors fall back to the legacy
// runtime BACT type where that mapping is unambiguous.
VehicleCombatClass ResolveVehicleCombatClass(const NC_STACK_ypabact *unit);

// Return only a newly-authored fine-grained job value. Callers provide their
// own legacy fallback so every old AI path keeps its exact historical grouping
// when the new parameter is absent.
bool TryGetSpecificFightJob(const TVhclProto &proto,
                            VehicleCombatClass targetClass,
                            int *outValue);

// Return only a newly-authored fine-grained energy value. Legacy callers first
// resolve heli/tank/flyer/robo exactly as before, then use this as an override.
bool TryGetSpecificWeaponEnergy(const TWeapProto &proto,
                                VehicleCombatClass targetClass,
                                float *outValue);



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
    // Preserve the current OpenNeoUA Building-spawner placement when the
    // new data-driven positioning parameters are absent.
    float spawn_random_pos = 340.0f;
    vec3d spawn_offset = vec3d(37.0, 0.0, -41.0);
    float spawn_height_min = 650.0f;
    float spawn_height_max = 900.0f;
    int spawn_max_active = 0;
    int spawn_count = 1;
    int spawn_instant = 0;
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
