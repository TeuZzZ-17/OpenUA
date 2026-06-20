#include <cstdio>
#include <cstdint>
#include <string>

#include "clonebalance.h"
#include "../ypabact.h"          // NC_STACK_ypabact, BACT_TYPES_ROBO
#include "../system/inivals.h"

namespace World
{
namespace CloneBalance
{

// Cached config (filled by Init() from the INI). Defaults match the documented
// out-of-the-box behavior: feature off, 5% malus, neutral-until-parsed grey tint.
static bool        s_enabled          = false;
static float       s_downFactor       = 0.95f; // 1 - 5/100
static float       s_attackTimeFactor = 1.05f; // 1 + 5/100
static TVisualTint s_tint;                      // overwritten by Init()

// Parse "R_G_B_A" (each component 0..255, alpha optional -> 255) into a normalized
// 0..1 TVisualTint. Mirrors the visual_tint script parser so the INI value uses the
// same human-readable format as the per-prototype visual_tint param.
static TVisualTint ParseTint(const std::string &str)
{
    int comp[4] = { 255, 255, 255, 255 };
    std::sscanf(str.c_str(), "%d_%d_%d_%d", &comp[0], &comp[1], &comp[2], &comp[3]);

    auto clamp255 = [](int v) -> float
    {
        if ( v < 0 )   v = 0;
        if ( v > 255 ) v = 255;
        return (float)v / 255.0f;
    };

    TVisualTint tint;
    tint.r = clamp255(comp[0]);
    tint.g = clamp255(comp[1]);
    tint.b = clamp255(comp[2]);
    tint.a = clamp255(comp[3]);
    return tint;
}

void Init()
{
    s_enabled = System::IniConf::GameBlackSectCloneBalance.Get<bool>();

    int percent = System::IniConf::GameBlackSectCloneMalusPercent.Get<int32_t>();
    // Keep the derived multipliers sane and positive even with odd INI values.
    if ( percent < 0 )  percent = 0;
    if ( percent > 90 ) percent = 90;

    s_downFactor       = 1.0f - (float)percent / 100.0f;
    s_attackTimeFactor = 1.0f + (float)percent / 100.0f;

    s_tint = ParseTint(System::IniConf::GameBlackSectCloneTint.Get<std::string>());
}

bool  Enabled()         { return s_enabled; }
float DownFactor()      { return s_downFactor; }
float AttackTimeFactor(){ return s_attackTimeFactor; }
const TVisualTint &Tint() { return s_tint; }

bool IsCloneActor(const NC_STACK_ypabact *bact)
{
    // Fast out when the feature is off (the common case).
    if ( !s_enabled || !bact )
        return false;

    if ( bact->_owner != OWNER_BLACK_SECT )
        return false;

    // The Host Station (BACT_TYPES_ROBO mobile base) is the faction command center,
    // not a disposable grey clone: it keeps full prototype stats.
    if ( bact->_bact_type == BACT_TYPES_ROBO )
        return false;

    // Projectiles (BACT_TYPES_MISSLE) are not clone units: they must keep vanilla
    // flight (force/maxrot), pitch and visuals. The only weapon-side malus is the
    // firing/reload rate, which lives on the firing UNIT (shot_time), and the
    // outgoing-damage malus is charged to the emitter unit in ModifyEnergy — so
    // excluding the projectile here changes none of those.
    if ( bact->_bact_type == BACT_TYPES_MISSLE )
        return false;

    return true;
}

}
}
