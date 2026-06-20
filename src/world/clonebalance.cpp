#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "clonebalance.h"
#include "../ypabact.h"          // NC_STACK_ypabact, BACT_TYPES_*
#include "../system/inivals.h"
#include "../log.h"

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

static bool ParseTintComponent(const char *&pos, int *out)
{
    errno = 0;
    char *end = NULL;
    long value = std::strtol(pos, &end, 10);

    if ( end == pos || errno == ERANGE )
        return false;

    *out = (int)value;
    pos = end;
    return true;
}

static bool ParseTintComponents(const std::string &str, int comp[4])
{
    const char *pos = str.c_str();

    for (int i = 0; i < 4; i++)
    {
        if ( !ParseTintComponent(pos, &comp[i]) )
            return false;

        if ( i < 3 )
        {
            if ( *pos != '_' )
                return false;

            pos++;
        }
    }

    return *pos == '\0';
}

// Parse "R_G_B_A" (each component 0..255) into a normalized 0..1 TVisualTint.
// Malformed values fall back to the documented grey clone tint and emit a warning.
static TVisualTint ParseTint(const std::string &str)
{
    int comp[4] = { 140, 140, 140, 255 };

    if ( !ParseTintComponents(str, comp) )
    {
        ypa_log_out("Warning: invalid game.black_sect_clone_tint '%s', using 140_140_140_255\n",
                    str.c_str());
    }

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

}
}
