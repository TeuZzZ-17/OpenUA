#include "parsers.h"
#include "../fmtlib/core.h"
#include "../fmtlib/printf.h"
#include "../yw.h"
#include "../yparobo.h"
#include "../ypaflyer.h"
#include "../ypacar.h"
#include "../log.h"
#include "../utils.h"
#include "../system/inivals.h"
#include "spin.h"
#include "tools.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace World
{
namespace Parsers
{

static int GemFloatHundredths(float value)
{
    return (int)std::lround((double)value * 100.0);
}

static int ParsePositiveIntOrZero(const std::string &value)
{
    try
    {
        size_t parsed = 0;
        long long result = std::stoll(value, &parsed, 0);
        if ( parsed != value.size() || result <= 0 ||
             result > std::numeric_limits<int>::max() )
            return 0;

        return (int)result;
    }
    catch (...)
    {
        return 0;
    }
}

static void ParseSoundPitchRange(const std::string &value, TVhclSound &sound)
{
    int minPitch = 0;
    int maxPitch = 0;

    if ( !World::ParseIntRangeValue(value, minPitch, maxPitch) )
    {
        minPitch = 0;
        maxPitch = 0;
    }

    sound.SetPitchRange(minPitch, maxPitch);
}

static float ParseNonNegativeIniFloatOrZero(const std::string &value)
{
    if ( value.empty() )
        return 0.0f;

    errno = 0;
    char *end = NULL;
    const char *begin = value.c_str();
    float parsed = std::strtof(begin, &end);
    if ( end == begin || errno == ERANGE || *end != '\0' ||
         !std::isfinite(parsed) || parsed < 0.0f )
        return 0.0f;

    return parsed;
}

static bool ParseAbsoluteOrPercent(const std::string &value,
                                   TAbsoluteOrPercent &out,
                                   float maxPercent = 100.0f)
{
    TAuthoredScalar parsed;
    out.Clear();
    if ( !ParseAuthoredScalar(value, parsed) || parsed.value < 0.0f )
        return false;

    if ( parsed.percent )
        parsed.value = std::min(parsed.value, maxPercent);

    out.value = parsed.value;
    out.percent = parsed.percent;
    out.defined = true;
    return true;
}

static float ParseMalusPercent(const std::string &value)
{
    TAuthoredScalar parsed;
    if ( !ParseAuthoredScalar(value, parsed) || !parsed.percent ||
         parsed.value > 0.0f )
        return 0.0f;

    const float signedPercent = std::max(-100.0f, parsed.value);
    return -signedPercent / 100.0f;
}

static float ParseSignedPitchPercent(const std::string &value)
{
    TAuthoredScalar parsed;
    if ( !ParseAuthoredScalar(value, parsed) || !parsed.percent )
        return 1.0f;

    const float signedPercent = std::max(-100.0f, parsed.value);
    const float multiplier = 1.0f + signedPercent / 100.0f;
    return std::isfinite(multiplier) && multiplier >= 0.0f ? multiplier : 1.0f;
}

static float ParseNormalizedOrPercent(const std::string &value)
{
    TAuthoredScalar parsed;
    if ( !ParseAuthoredScalar(value, parsed) )
        return 0.0f;

    if ( parsed.percent )
        return std::max(0.0f, std::min(parsed.value, 100.0f)) / 100.0f;

    return std::max(0.0f, std::min(parsed.value, 1.0f));
}

static bool ParseSmartVPList(const std::string &value, std::vector<int16_t> &out)
{
    return ParsePositiveInt16ListValue(value, out);
}

static float Clamp01(float value)
{
    if ( value < 0.0f )
        return 0.0f;

    if ( value > 1.0f )
        return 1.0f;

    return value;
}

static float NonNegativeFiniteOrZero(float value)
{
    return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

static float ClampPushIntensity(float value)
{
    if ( !std::isfinite(value) || value <= 0.0f )
        return 0.0f;

    return std::min(value, 10.0f);
}

static int NonNegativeFiniteMilliseconds(ScriptParser::Parser &parser,
                                         const std::string &value)
{
    double milliseconds = parser.stod(value, 0);
    if ( !std::isfinite(milliseconds) || milliseconds <= 0.0 )
        return 0;

    const double maximum = (double)std::numeric_limits<int>::max();
    if ( milliseconds >= maximum )
        return std::numeric_limits<int>::max();

    return (int)std::lround(milliseconds);
}

static float ClampRecoilMultiplier(float value)
{
    if ( !(value >= 0.0f) )
        return 0.0f;

    if ( value > 10.0f )
        return 10.0f;

    return value;
}

static float ClampProjectileVisualMotionRadius(float value)
{
    if ( !std::isfinite(value) || value <= 0.0f )
        return 0.0f;

    // Lateral visual radius in model/world units. The upper bound still permits
    // intentionally extreme motion while preventing malformed scripts from throwing
    // the VP arbitrarily far from its physical collision path.
    return std::min(value, 1000.0f);
}

static void InitStatusSoundFXDefaults(World::TVhclSound &snd, int defaultVolume)
{
    snd.volume = defaultVolume;
    snd.sndPrm.mag0 = 1.0;
    snd.sndPrm.time = 1000;
    snd.sndPrm_shk.mag0 = 1.0;
    snd.sndPrm_shk.time = 1000;
    snd.sndPrm_shk.mute = 0.02;
    snd.sndPrm_shk.pos.x = 0.2;
    snd.sndPrm_shk.pos.y = 0.2;
    snd.sndPrm_shk.pos.z = 0.2;
}

static int ClampSectorPower(int power)
{
    if ( power < 0 )
        return 0;

    if ( power > 255 )
        return 255;

    return power;
}

static bool IsUsableScriptText(const std::string &text)
{
    for (unsigned char ch : text)
    {
        if ( ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' )
            return true;
    }

    return false;
}



bool UserParser::ReadUserNameFile(const std::string &filename)
{
    if ( !_o._GameShell->UserName.empty() )
        return false;

    std::string buf = fmt::sprintf("save:%s/%s", _o._GameShell->UserName, filename);

    // Optional legacy callsign storage. It is created on save when needed.
    if ( !uaFileExist(buf) )
        return false;

    FSMgr::FileHandle *signFile = uaOpenFileAlloc(buf, "r");

    if ( !signFile )
        return false;

    bool res = signFile->ReadLine(&_o._GameShell->netPlayerName);

    delete signFile;
    return res;
}


bool UserParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if (StriCmp(word, "new_user"))
        return false;

    if (!_o._GameShell->remoteMode)
    {
        if ( !ReadUserNameFile("callsign.def") )
            _o._GameShell->netPlayerName =  Locale::Text::Dialogs(Locale::DLG_P_UNNAMED);
    }
    return true;
}

int UserParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "username") )
    {
    }
    else if ( !StriCmp(p1, "netname") )
    {
    }
    else if ( !StriCmp(p1, "maxroboenergy") )
    {
        _o._maxRoboEnergy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "maxreloadconst") )
    {
        _o._maxReloadConst = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "numbuddies") )
    {
    }
    else if ( !StriCmp(p1, "beamenergy") )
    {
        _o._beamEnergyCapacity = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "playerstatus") )
    {
        Stok stok(p2, "_ \t");
        std::string val;
        if ( stok.GetNext(&val) )
        {
            int plid = parser.stol(val, NULL, 0);
            if ( stok.GetNext(&val) )
            {
                _o._playersStats[plid].DestroyedUnits = parser.stol(val, NULL, 0);
                if ( stok.GetNext(&val) )
                {
                    _o._playersStats[plid].DestroyedByUser = parser.stol(val, NULL, 0);
                    if ( stok.GetNext(&val) )
                    {
                        _o._playersStats[plid].ElapsedTime = parser.stol(val, NULL, 0);
                        if ( stok.GetNext(&val) )
                        {
                            _o._playersStats[plid].SectorsTaked = parser.stol(val, NULL, 0);
                            if ( stok.GetNext(&val) )
                            {
                                _o._playersStats[plid].Score = parser.stol(val, NULL, 0);
                                if ( stok.GetNext(&val) )
                                {
                                    _o._playersStats[plid].Power = parser.stol(val, NULL, 0);
                                    if ( stok.GetNext(&val) )
                                    {
                                        _o._playersStats[plid].Upgrades = parser.stol(val, NULL, 0);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else if ( !StriCmp(p1, "jodiefoster") )
    {
        Stok stok(p2, "_ \t");
        std::string val;
        if ( stok.GetNext(&val) )
        {
            _o._levelInfo.JodieFoster[0] = parser.stol(val, NULL, 0);
            if ( stok.GetNext(&val) )
            {
                _o._levelInfo.JodieFoster[1] = parser.stol(val, NULL, 0);
                if ( stok.GetNext(&val) )
                {
                    _o._levelInfo.JodieFoster[2] = parser.stol(val, NULL, 0);
                    if ( stok.GetNext(&val) )
                    {
                        _o._levelInfo.JodieFoster[3] = parser.stol(val, NULL, 0);
                        if ( stok.GetNext(&val) )
                        {
                            _o._levelInfo.JodieFoster[4] = parser.stol(val, NULL, 0);
                            if ( stok.GetNext(&val) )
                            {
                                _o._levelInfo.JodieFoster[5] = parser.stol(val, NULL, 0);
                                if ( stok.GetNext(&val) )
                                {
                                    _o._levelInfo.JodieFoster[6] = parser.stol(val, NULL, 0);
                                    if ( stok.GetNext(&val) )
                                        _o._levelInfo.JodieFoster[7] = parser.stol(val, NULL, 0);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}



bool InputParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( !StriCmp(word, "new_input") )
    {
        _isNewInputScope = true;
        _legacyCameraZoomInSeen = false;
        _legacyCameraZoomOutSeen = false;
        _ufoSpyUiToggleSeen = false;
        _mapFocusSeen = false;
        _legacyCameraZoomInKey = Input::KC_NONE;
        _legacyCameraZoomOutKey = Input::KC_NONE;

        for (size_t i = 0; i < _o._GameShell->InputConfig.size(); i++)
        {
            // Preserve defaults for bindings introduced after older user.txt files.
            // Explicit entries, including nop, still override these defaults below.
            if ( i == World::INPUT_BIND_SPRINT ||
                 i == World::INPUT_BIND_CYCLE_TARGET ||
                 i == World::INPUT_BIND_ALTERNATIVE_VIEW ||
                 i == World::INPUT_BIND_TOGGLE_UFO_SPY_UI ||
                 i == World::INPUT_BIND_MAP_FOCUS )
                continue;

            UserData::TInputConf &k = _o._GameShell->InputConfig[i];
            k.PKeyCode = 0;
            k.NKeyCode = 0;
        }
        return true;
    }
    else if ( !StriCmp(word, "modify_input") )
    {
        _isNewInputScope = false;
        return true;
    }

    return false;
}

int InputParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        if ( _isNewInputScope )
        {
            bool migrated = _legacyCameraZoomInSeen || _legacyCameraZoomOutSeen;

            UserData::TInputConf &zoomIn =
                _o._GameShell->InputConfig[World::INPUT_BIND_ZOOMIN];
            UserData::TInputConf &zoomOut =
                _o._GameShell->InputConfig[World::INPUT_BIND_ZOOMOUT];

            // Import the short-lived split Camera Zoom bindings only when the
            // surviving map binding still has that build's known Up/Down default
            // (or is empty). Custom map bindings remain authoritative.
            if ( zoomIn.PKeyCode == Input::KC_UP ||
                 (zoomIn.PKeyCode == Input::KC_NONE && _legacyCameraZoomInSeen) )
            {
                zoomIn.PKeyCode =
                    (_legacyCameraZoomInSeen && _legacyCameraZoomInKey != Input::KC_NONE)
                    ? _legacyCameraZoomInKey : Input::KC_NUMPLUS;
                _o.ReloadInput(World::INPUT_BIND_ZOOMIN);
                migrated = true;
            }

            if ( zoomOut.PKeyCode == Input::KC_DOWN ||
                 (zoomOut.PKeyCode == Input::KC_NONE && _legacyCameraZoomOutSeen) )
            {
                zoomOut.PKeyCode =
                    (_legacyCameraZoomOutSeen && _legacyCameraZoomOutKey != Input::KC_NONE)
                    ? _legacyCameraZoomOutKey : Input::KC_NUMMINUS;
                _o.ReloadInput(World::INPUT_BIND_ZOOMOUT);
                migrated = true;
            }

            // The legacy slots must never remain active: otherwise a key used
            // only by an old Camera Zoom entry could still produce HotKeyID 50/51.
            Input::Engine.SetHotKey(50, "nop");
            Input::Engine.SetHotKey(51, "nop");

            // OpenNeoUA input-default migration. Older builds used ALT for Switch
            // Weapon (or the earlier TAB/TAB pair) and CTRL for Launch Missile
            // Cam. Move only those known default combinations to the current
            // defaults: Switch Weapon = CTRL, Launch Missile Cam = Grave Accent.
            // Custom bindings remain authoritative and are never stolen.
            UserData::TInputConf &switchWeapon =
                _o._GameShell->InputConfig[World::INPUT_BIND_SWITCH_WEAPON];
            UserData::TInputConf &cycleTarget =
                _o._GameShell->InputConfig[World::INPUT_BIND_CYCLE_TARGET];
            UserData::TInputConf &missileCam =
                _o._GameShell->InputConfig[World::INPUT_BIND_CAMFIRE];

            const bool knownOldSwitchDefault =
                switchWeapon.PKeyCode == Input::KC_ALT ||
                (switchWeapon.PKeyCode == Input::KC_TAB &&
                 cycleTarget.PKeyCode == Input::KC_TAB);
            const bool knownOldMissileCamDefault =
                missileCam.PKeyCode == Input::KC_CTRL;

            if ( knownOldSwitchDefault && knownOldMissileCamDefault )
            {
                bool ctrlUsedElsewhere = false;
                bool graveUsedElsewhere = false;

                for ( size_t i = 1; i < _o._GameShell->InputConfig.size(); ++i )
                {
                    if ( i == World::INPUT_BIND_SWITCH_WEAPON ||
                         i == World::INPUT_BIND_CYCLE_TARGET ||
                         i == World::INPUT_BIND_CAMFIRE ||
                         UserData::IsInputBindingRetired((int)i) )
                        continue;

                    const UserData::TInputConf &cfg = _o._GameShell->InputConfig[i];
                    ctrlUsedElsewhere |=
                        cfg.PKeyCode == Input::KC_CTRL || cfg.NKeyCode == Input::KC_CTRL;
                    graveUsedElsewhere |=
                        cfg.PKeyCode == Input::KC_EXTRA7 || cfg.NKeyCode == Input::KC_EXTRA7;
                }

                if ( !ctrlUsedElsewhere && !graveUsedElsewhere )
                {
                    switchWeapon.PKeyCode = Input::KC_CTRL;
                    missileCam.PKeyCode = Input::KC_EXTRA7;
                    _o.ReloadInput(World::INPUT_BIND_SWITCH_WEAPON);
                    _o.ReloadInput(World::INPUT_BIND_CAMFIRE);
                    migrated = true;
                }
            }

            // UFO Spy UI Toggle is newer than older user.txt profiles. Preserve
            // the new U default only when that key is not already owned by a
            // custom binding in an older profile. Explicit slot 52 entries,
            // including nop, always remain authoritative.
            if ( !_ufoSpyUiToggleSeen )
            {
                UserData::TInputConf &ufoSpyToggle =
                    _o._GameShell->InputConfig[World::INPUT_BIND_TOGGLE_UFO_SPY_UI];
                bool uAlreadyUsed = false;

                for ( size_t i = 1; i < _o._GameShell->InputConfig.size(); ++i )
                {
                    if ( i == World::INPUT_BIND_TOGGLE_UFO_SPY_UI ||
                         UserData::IsInputBindingRetired((int)i) )
                        continue;

                    const UserData::TInputConf &cfg = _o._GameShell->InputConfig[i];
                    if ( cfg.PKeyCode == Input::KC_U || cfg.NKeyCode == Input::KC_U )
                    {
                        uAlreadyUsed = true;
                        break;
                    }
                }

                if ( uAlreadyUsed && ufoSpyToggle.PKeyCode == Input::KC_U )
                {
                    ufoSpyToggle.PKeyCode = Input::KC_NONE;
                    Input::Engine.SetHotKey(ufoSpyToggle.KeyID, "nop");
                    migrated = true;
                }
            }

            // Map Focus is newer than older user.txt profiles. Preserve the
            // new E default only when E is still free. An explicit slot 53
            // entry, including nop, is always authoritative.
            if ( !_mapFocusSeen )
            {
                UserData::TInputConf &mapFocus =
                    _o._GameShell->InputConfig[World::INPUT_BIND_MAP_FOCUS];
                bool eAlreadyUsed = false;

                for ( size_t i = 1; i < _o._GameShell->InputConfig.size(); ++i )
                {
                    if ( i == World::INPUT_BIND_MAP_FOCUS ||
                         UserData::IsInputBindingRetired((int)i) )
                        continue;

                    const UserData::TInputConf &cfg = _o._GameShell->InputConfig[i];
                    if ( cfg.PKeyCode == Input::KC_E || cfg.NKeyCode == Input::KC_E )
                    {
                        eAlreadyUsed = true;
                        break;
                    }
                }

                if ( eAlreadyUsed && mapFocus.PKeyCode == Input::KC_E )
                {
                    mapFocus.PKeyCode = Input::KC_NONE;
                    Input::Engine.SetHotKey(mapFocus.KeyID, "nop");
                    migrated = true;
                }
            }

            // Alternative View replaces the provisional Bomb Sight binding. New
            // profiles default to F. Profiles that still contain the old T default
            // are migrated to F only when F is free; custom bindings are preserved.
            // If an older profile has no entry for this newer action, its preserved
            // F default is cleared rather than stealing an already-used F key.
            UserData::TInputConf &alternativeView =
                _o._GameShell->InputConfig[World::INPUT_BIND_ALTERNATIVE_VIEW];

            bool fAlreadyUsed = false;
            bool currentAlternativeKeyAlreadyUsed = false;
            for ( size_t i = 1; i < _o._GameShell->InputConfig.size(); ++i )
            {
                if ( i == World::INPUT_BIND_ALTERNATIVE_VIEW ||
                     UserData::IsInputBindingRetired((int)i) )
                    continue;

                const UserData::TInputConf &cfg = _o._GameShell->InputConfig[i];
                if ( cfg.PKeyCode == Input::KC_F || cfg.NKeyCode == Input::KC_F )
                    fAlreadyUsed = true;

                if ( alternativeView.PKeyCode != Input::KC_NONE &&
                     (cfg.PKeyCode == alternativeView.PKeyCode ||
                      cfg.NKeyCode == alternativeView.PKeyCode) )
                    currentAlternativeKeyAlreadyUsed = true;
            }

            if ( alternativeView.PKeyCode == Input::KC_T && !fAlreadyUsed )
            {
                alternativeView.PKeyCode = Input::KC_F;
                _o.ReloadInput(World::INPUT_BIND_ALTERNATIVE_VIEW);
                migrated = true;
            }
            else if ( (alternativeView.PKeyCode == Input::KC_F && fAlreadyUsed) ||
                      (alternativeView.PKeyCode == Input::KC_T && currentAlternativeKeyAlreadyUsed) )
            {
                alternativeView.PKeyCode = Input::KC_NONE;
                // ReloadInput() rejects KC_NONE before updating the expression.
                Input::Engine.SetInputExpression(false, alternativeView.KeyID, "nop");
                migrated = true;
            }

            bool retiredBindingFound = false;
            for ( int binding = 1; binding < World::INPUT_BIND_MAX; ++binding )
            {
                if ( !UserData::IsInputBindingRetired(binding) )
                    continue;

                const UserData::TInputConf &cfg = _o._GameShell->InputConfig[binding];
                if ( cfg.PKeyCode != Input::KC_NONE || cfg.NKeyCode != Input::KC_NONE )
                {
                    retiredBindingFound = true;
                    break;
                }
            }

            if ( retiredBindingFound )
            {
                _o._GameShell->RetireInputBindings(false);
                migrated = true;
            }

            if ( migrated )
                _o._GameShell->inputDefaultsMigrated = true;
        }

        return ScriptParser::RESULT_SCOPE_END;
    }

    _o._GameShell->savedDataFlags |= World::SDF_INPUT;

    if ( !StriCmp(p1, "qualmode") )
    {
    }
    else if ( !StriCmp(p1, "joystick") )
    {
        if ( StrGetBool(p2) )
        {
             _o._GameShell->joystickEnabled = true;
             _o._preferences &= ~World::PREF_JOYDISABLE;
        }
        else
        {
            _o._GameShell->joystickEnabled = false;
            _o._preferences |= World::PREF_JOYDISABLE;
        }
    }
    else if ( !StriCmp(p1, "altjoystick") )
    {
        if ( StrGetBool(p2) )
        {
            _o._GameShell->altJoystickEnabled = true;
            _o._preferences |= World::PREF_ALTJOYSTICK;
        }
        else
        {
            _o._GameShell->altJoystickEnabled = false;
            _o._preferences &= ~World::PREF_ALTJOYSTICK;
        }
    }
    else if ( !StriCmp(p1, "forcefeedback") )
    {
        if ( StrGetBool(p2) )
            _o._preferences &= ~World::PREF_FFDISABLE;
        else
            _o._preferences |= World::PREF_FFDISABLE;
    }
    else
    {
        std::string buf;
        for (std::string::const_iterator it = p2.cbegin(); it != p2.cend(); it++)
        {
            if (*it == '_')
                buf += ' ';
            else if (*it == '$')
                buf += "winp:";
            else
                buf += *it;
        }

        if ( !StriCmp(p1.substr(0,13), "input.slider[") )
        {
            bool ok = false;
            int cfgIdex = parser.stoi( Stok::Fast(p1.substr(13), "] \t=\n") );

            if ( !Input::Engine.SetInputExpression(true, cfgIdex, buf) )
            {
                ypa_log_out("WARNING: cannot set slider %d with %s\n", cfgIdex, buf.c_str());
                return ScriptParser::RESULT_BAD_DATA;
            }


            int gsIndex = UserData::InputIndexFromConfig(World::INPUT_BIND_TYPE_SLIDER, cfgIdex);
            if ( gsIndex == -1 )
            {
                ypa_log_out("Unknown number in slider-declaration (%d)\n", cfgIdex);
                return ScriptParser::RESULT_BAD_DATA;
            }
            _o._GameShell->InputConfig[ gsIndex ].Type = World::INPUT_BIND_TYPE_SLIDER;
            _o._GameShell->InputConfig[ gsIndex ].KeyID = cfgIdex;

            Stok stok(buf, " :\t\n");
            std::string tmp;
            if ( stok.GetNext(&tmp) && stok.GetNext(&tmp) ) // skip drivername before ':'
            {
                _o._GameShell->InputConfig[ gsIndex ].NKeyCode = Input::Engine.GetKeyIDByName(tmp);

                if ( _o._GameShell->InputConfig[ gsIndex ].NKeyCode == -1 )
                {
                    ypa_log_out("Unknown keyword for slider %s\n", tmp.c_str());
                    return ScriptParser::RESULT_BAD_DATA;
                }

                if ( stok.GetNext(&tmp) && stok.GetNext(&tmp) ) // skip drivername before ':'
                {
                    _o._GameShell->InputConfig[ gsIndex ].PKeyCode = Input::Engine.GetKeyIDByName(tmp);

                    if ( _o._GameShell->InputConfig[ gsIndex ].PKeyCode == -1 )
                    {
                        ypa_log_out("Unknown keyword for slider %s\n", tmp.c_str());
                        return ScriptParser::RESULT_BAD_DATA;
                    }
                    ok = 1;
                }
            }

            if ( !ok )
            {
                ypa_log_out("Wrong input expression for slider %d\n", cfgIdex);
                return ScriptParser::RESULT_BAD_DATA;
            }
        }
        else if ( !StriCmp(p1.substr(0,13), "input.button[") )
        {
            bool ok = false;

            int cfgIdex = parser.stoi( Stok::Fast(p1.substr(13), "] \t=\n") );

            if ( !Input::Engine.SetInputExpression(false, cfgIdex, buf) )
            {
                ypa_log_out("WARNING: cannot set button %d with %s\n", cfgIdex, buf.c_str());
                return ScriptParser::RESULT_BAD_DATA;
            }

            int gsIndex = UserData::InputIndexFromConfig(World::INPUT_BIND_TYPE_BUTTON, cfgIdex);
            if ( gsIndex == -1 )
            {
                ypa_log_out("Unknown number in button-declaration (%d)\n", cfgIdex);
                return ScriptParser::RESULT_BAD_DATA;
            }
            _o._GameShell->InputConfig[ gsIndex ].Type = World::INPUT_BIND_TYPE_BUTTON;
            _o._GameShell->InputConfig[ gsIndex ].KeyID = cfgIdex;

            Stok stok(buf, " :\t\n");
            std::string tmp;
            if ( stok.GetNext(&tmp) && stok.GetNext(&tmp) ) // skip drivername before ':'
            {
                _o._GameShell->InputConfig[ gsIndex ].PKeyCode = Input::Engine.GetKeyIDByName(tmp);

                if ( _o._GameShell->InputConfig[ gsIndex ].PKeyCode == -1 )
                {
                    ypa_log_out("Unknown keyword for button %s\n", tmp.c_str());
                    return ScriptParser::RESULT_BAD_DATA;
                }
                ok = true;
            }

            if ( !ok )
            {
                ypa_log_out("Wrong input expression for button %d\n", cfgIdex);
                return ScriptParser::RESULT_BAD_DATA;
            }
        }
        else if ( !StriCmp(p1.substr(0,13), "input.hotkey[") )
        {
            bool ok = false;

            int cfgIdex = parser.stoi( Stok::Fast(p1.substr(13), "] \t=\n") );

            if ( !Input::Engine.SetHotKey(cfgIdex, buf) )
            {
                ypa_log_out("WARNING: cannot set hotkey %d with %s\n", cfgIdex, buf.c_str());
                return ScriptParser::RESULT_OK;
            }

            if ( cfgIdex == 50 || cfgIdex == 51 )
            {
                const std::string legacyKeyName = Stok::Fast(buf, " :\t\n");
                const int legacyKey = Input::Engine.GetKeyIDByName(legacyKeyName);

                if ( legacyKey == -1 )
                {
                    ypa_log_out("Unknown keyword for legacy camera zoom hotkey: %s\n",
                                legacyKeyName.c_str());
                    return ScriptParser::RESULT_OK;
                }

                if ( cfgIdex == 50 )
                {
                    _legacyCameraZoomInSeen = true;
                    _legacyCameraZoomInKey = legacyKey;
                }
                else
                {
                    _legacyCameraZoomOutSeen = true;
                    _legacyCameraZoomOutKey = legacyKey;
                }

                // Accept and immediately neutralize transient slots 50/51.
                Input::Engine.SetHotKey(cfgIdex, "nop");
                return ScriptParser::RESULT_OK;
            }

            if ( cfgIdex == 52 )
                _ufoSpyUiToggleSeen = true;
            else if ( cfgIdex == 53 )
                _mapFocusSeen = true;

            int gsIndex = UserData::InputIndexFromConfig(World::INPUT_BIND_TYPE_HOTKEY, cfgIdex);
            if ( gsIndex == -1 )
            {
                ypa_log_out("Unknown number in hotkey-declaration (%d)\n", cfgIdex);
                return ScriptParser::RESULT_OK;
            }

            _o._GameShell->InputConfig[ gsIndex ].Type = World::INPUT_BIND_TYPE_HOTKEY;
            _o._GameShell->InputConfig[ gsIndex ].KeyID = cfgIdex;

            std::string tmp = Stok::Fast(buf, " :\t\n");
            if ( !tmp.empty() )
            {
                _o._GameShell->InputConfig[ gsIndex ].PKeyCode = Input::Engine.GetKeyIDByName(tmp);
                if ( _o._GameShell->InputConfig[ gsIndex ].PKeyCode == -1 )
                {
                    ypa_log_out("Unknown keyword for hotkey: %s\n", tmp.c_str());
                    return ScriptParser::RESULT_OK;
                }
                ok = true;
            }

            if ( !ok )
            {
                ypa_log_out("Wrong input expression for hotkey %d\n", cfgIdex);
                return ScriptParser::RESULT_BAD_DATA;
            }
        }
        else
        {
            ypa_log_out("Unknown keyword %s in InputExpression\n", p1.c_str());
            return ScriptParser::RESULT_UNKNOWN;
        }
    }
    return ScriptParser::RESULT_OK;
}


TVhclSound *VhclProtoParser::GetSndFxByName(const std::string &sndname)
{
    struct SoundType
    {
        const std::string name;
        int id;
    };

    static const SoundType CmpVals[] = {
        {"normal",      0},
        {"fire",        1},
        {"wait",        2},
        {"genesis",     3},
        {"explode",     4},
        {"crashland",   5},
        {"crashvhcl",   6},
        {"goingdown",   7},
        {"cockpit",     8},
        {"beamin",      9},
        {"beamout",    10},
        {"build",      11},
        {"airexplode", 12},
    };

    for (const SoundType &t : CmpVals)
    {
        if ( !StriCmp(sndname, t.name) )
            return &_vhcl->sndFX[t.id];
    }

    return NULL;
}

bool FxParser::ParseExtSampleDef(ScriptParser::Parser &parser, TVhclSound *sndfx, const std::string &p2)
{
    Stok stok(p2, "_");
    std::string pp1, pp2, pp3, pp4, pp5, pname;

    if ( !stok.GetNext(&pp1) || !stok.GetNext(&pp2) || !stok.GetNext(&pp3) || !stok.GetNext(&pp4) || !stok.GetNext(&pp5) || !stok.GetNext(&pname) )
        return false;

    sndfx->extS.emplace_back();
    sndfx->ExtSamples.emplace_back();

    sndfx->ExtSamples.back().Name = pname;

    TSampleParams &sndEx = sndfx->extS.back();
    sndEx.Sample = NULL;
    sndEx.Loop = parser.stol(pp1, NULL, 0);
    sndEx.Vol = parser.stol(pp2, NULL, 0);
    sndEx.SampleRate = parser.stol(pp3, NULL, 0);
    sndEx.Offset = parser.stol(pp4, NULL, 0);
    sndEx.SampleCnt = parser.stol(pp5, NULL, 0);

    return true;
}

int FxParser::ParseSndFX(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    std::string val;
    Stok stok(p1, "_");
    stok.GetNext(&val);

    int sndTP;
    if ( !StriCmp(val, "snd") )
        sndTP = 0;
    else if ( !StriCmp(val, "pal") )
        sndTP = 1;
    else if ( !StriCmp(val, "shk") )
        sndTP = 2;
    else
        return ScriptParser::RESULT_UNKNOWN;

    stok.GetNext(&val);

    TVhclSound *sndfx = GetSndFxByName(val);
    if (!sndfx)
        return ScriptParser::RESULT_UNKNOWN;

    stok.GetNext(&val);

    std::string suffix;
    if ( stok.GetNext(&suffix) )
        val += "_" + suffix;

    switch (sndTP)
    {
        case 0:
        {
            if ( !StriCmp(val, "sample") )
                sndfx->MainSample.Name = p2;
            else if ( !StriCmp(val, "volume") )
                sndfx->volume = parser.stol(p2, NULL, 0);
            else if ( !StriCmp(val, "pitch") )
                ParseSoundPitchRange(p2, *sndfx);
            else if ( !StriCmp(val, "radius") )
                sndfx->radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
            else if ( !StriCmp(val, "ext") )
            {
                if ( !ParseExtSampleDef(parser, sndfx, p2) )
                    return ScriptParser::RESULT_BAD_DATA;
            }
            else
                return ScriptParser::RESULT_UNKNOWN;
        }
        break;

        case 1:
        {
            if ( !StriCmp(val, "slot") )
                sndfx->sndPrm.slot = parser.stol(p2, NULL, 0);
            else if ( !StriCmp(val, "mag0") )
                sndfx->sndPrm.mag0 = parser.stof(p2, 0);
            else if ( !StriCmp(val, "mag1") )
                sndfx->sndPrm.mag1 = parser.stof(p2, 0);
            else if ( !StriCmp(val, "time") )
                sndfx->sndPrm.time = parser.stol(p2, NULL, 0);
            else if ( !StriCmp(val, "radius") )
                sndfx->sndPrm.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
            else
                return ScriptParser::RESULT_UNKNOWN;
        }
        break;

        case 2:
        {
            if ( !StriCmp(val, "slot") )
                sndfx->sndPrm_shk.slot = parser.stol(p2, NULL, 0);
            else if ( !StriCmp(val, "mag0") )
                sndfx->sndPrm_shk.mag0 = parser.stof(p2, 0);
            else if ( !StriCmp(val, "mag1") )
                sndfx->sndPrm_shk.mag1 = parser.stof(p2, 0);
            else if ( !StriCmp(val, "time") )
                sndfx->sndPrm_shk.time = parser.stol(p2, NULL, 0);
            else if ( !StriCmp(val, "radius") )
                sndfx->sndPrm_shk.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
            else if ( !StriCmp(val, "mute") )
                sndfx->sndPrm_shk.mute = parser.stof(p2, 0);
            else if ( !StriCmp(val, "x") )
                sndfx->sndPrm_shk.pos.x = parser.stof(p2, 0);
            else if ( !StriCmp(val, "y") )
                sndfx->sndPrm_shk.pos.y = parser.stof(p2, 0);
            else if ( !StriCmp(val, "z") )
                sndfx->sndPrm_shk.pos.z = parser.stof(p2, 0);
            else
                return ScriptParser::RESULT_UNKNOWN;
        }
        break;

        default:
            return ScriptParser::RESULT_UNKNOWN;
    }

    return ScriptParser::RESULT_OK;
}

static void ResetVehicleScaleFX(TVhclProto *vhcl)
{
    vhcl->scale_fx_p0 = 0.0;
    vhcl->scale_fx_p1 = 0.0;
    vhcl->scale_fx_p2 = 0.0;
    vhcl->scale_fx_p3 = 0;
    vhcl->scale_fx_pXX.fill(0);
}

static bool ParseTintParam(ScriptParser::Parser &parser,
                           const std::string &paramName,
                           const std::string &p1,
                           const std::string &p2,
                           TVisualTint &tint,
                           bool invalidFallsBackToNeutral = false);

static float ParseVPScaleValue(ScriptParser::Parser &parser, const std::string &value)
{
    float scale = parser.stof(value, 0);
    return std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

static bool ParseVPScaleParam(ScriptParser::Parser &parser,
                              const std::string &prefix,
                              const std::string &p1,
                              const std::string &p2,
                              vec3d &scale)
{
    if ( !StriCmp(p1, prefix + "_scale_x") )
    {
        scale.x = ParseVPScaleValue(parser, p2);
        return true;
    }

    if ( !StriCmp(p1, prefix + "_scale_y") )
    {
        scale.y = ParseVPScaleValue(parser, p2);
        return true;
    }

    if ( !StriCmp(p1, prefix + "_scale_z") )
    {
        scale.z = ParseVPScaleValue(parser, p2);
        return true;
    }

    return false;
}

static bool ParseDebuffParam(ScriptParser::Parser &parser,
                             const std::string &p1,
                             const std::string &p2,
                             TWeaponDebuffConfig &debuff)
{
    if ( !StriCmp(p1, "debuff_allow") )
        debuff.allow = parser.stol(p2, NULL, 0) != 0;
    else if ( !StriCmp(p1, "debuff_allow_on_host_station") )
        debuff.allow_on_host_station = parser.stol(p2, NULL, 0) != 0;
    else if ( !StriCmp(p1, "debuff_inherit_to_children") )
        debuff.inherit_to_children = parser.stol(p2, NULL, 0) != 0;
    else if ( !StriCmp(p1, "debuff_name") )
        debuff.name = p2;
    else if ( !StriCmp(p1, "debuff_damage") )
        ParseAbsoluteOrPercent(p2, debuff.damage, 100.0f);
    else if ( !StriCmp(p1, "debuff_mindcontrol") )
        debuff.mindcontrol = parser.stol(p2, NULL, 0) != 0;
    else if ( !StriCmp(p1, "debuff_tick_time") )
    {
        int tickTime = parser.stol(p2, NULL, 0);
        debuff.tick_time = tickTime > 0 ? tickTime : 1000;
        debuff.has_tick_time = true;
    }
    else if ( !StriCmp(p1, "debuff_duration") )
    {
        int duration = parser.stol(p2, NULL, 0);
        debuff.duration = duration > 0 ? duration : 0;
    }
    else if ( !StriCmp(p1, "debuff_stun") )
        debuff.stun = parser.stol(p2, NULL, 0) != 0;
    else if ( !StriCmp(p1, "debuff_stun_motion_level") )
        debuff.stun_motion_level = ParseNormalizedOrPercent(p2);
    else if ( !StriCmp(p1, "debuff_stun_unit_fire") )
        debuff.stun_unit_fire = parser.stol(p2, NULL, 0) != 0;
    else if ( !StriCmp(p1, "debuff_force_malus") )
        debuff.force_malus = ParseMalusPercent(p2);
    else if ( !StriCmp(p1, "debuff_maxrot_malus") )
        debuff.maxrot_malus = ParseMalusPercent(p2);
    else if ( !StriCmp(p1, "debuff_shield_malus") )
        debuff.shield_malus = ParseMalusPercent(p2);
    else if ( !StriCmp(p1, "debuff_mgun_shot_time_malus") )
        debuff.mgun_shot_time_malus = ParseMalusPercent(p2);
    else if ( !StriCmp(p1, "debuff_shot_time_malus") )
        debuff.shot_time_malus = ParseMalusPercent(p2);
    else if ( !StriCmp(p1, "debuff_snd_pitch") )
        debuff.snd_pitch_multiplier = ParseSignedPitchPercent(p2);
    else if ( ParseTintParam(parser, "debuff_target_tint", p1, p2,
                             debuff.target_tint, true) )
        return true;
    else if ( !StriCmp(p1, "debuff_vp") )
    {
        if ( !ParseSmartVPList(p2, debuff.vps) )
            debuff.vps.clear();
    }
    else if ( !StriCmp(p1, "debuff_3ds") )
        debuff.mesh3ds = p2;
    else if ( !StriCmp(p1, "debuff_scale") )
        debuff.scale = ParseVPScaleValue(parser, p2);
    else if ( ParseTintParam(parser, "debuff_tint", p1, p2, debuff.tint) )
        return true;
    else if ( !StriCmp(p1, "debuff_random_max_offset") )
        ParseAbsoluteOrPercent(p2, debuff.random_max_offset, 100.0f);
    else if ( ParseTintParam(parser, "debuff_vp_trail_tint", p1, p2,
                             debuff.vp_trail_tint) )
    {
        debuff.has_vp_trail_tint = true;
        return true;
    }
    else if ( !StriCmp(p1, "debuff_icon") )
        debuff.icon = p2;
    else if ( !StriCmp(p1, "snd_debuff_sample") )
        debuff.tick_snd.MainSample.Name = p2;
    else if ( !StriCmp(p1, "snd_debuff_pitch") )
        ParseSoundPitchRange(p2, debuff.tick_snd);
    else if ( !StriCmp(p1, "snd_debuff_volume") )
        debuff.tick_snd.volume = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "snd_debuff_radius") )
        debuff.tick_snd.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "pal_debuff_slot") )
        debuff.tick_snd.sndPrm.slot = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "pal_debuff_mag0") )
        debuff.tick_snd.sndPrm.mag0 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "pal_debuff_mag1") )
        debuff.tick_snd.sndPrm.mag1 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "pal_debuff_time") )
        debuff.tick_snd.sndPrm.time = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "pal_debuff_radius") )
        debuff.tick_snd.sndPrm.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "shk_debuff_slot") )
        debuff.tick_snd.sndPrm_shk.slot = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "shk_debuff_mag0") )
        debuff.tick_snd.sndPrm_shk.mag0 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "shk_debuff_mag1") )
        debuff.tick_snd.sndPrm_shk.mag1 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "shk_debuff_time") )
        debuff.tick_snd.sndPrm_shk.time = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "shk_debuff_radius") )
        debuff.tick_snd.sndPrm_shk.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "shk_debuff_mute") )
        debuff.tick_snd.sndPrm_shk.mute = parser.stof(p2, 0);
    else
        return false;

    return true;
}

static bool ParseVPSpinParam(ScriptParser::Parser &parser,
                             const std::string &prefix,
                             const std::string &p1,
                             const std::string &p2,
                             vec3d &spin)
{
    if ( !StriCmp(p1, prefix + "_spin_x") )
    {
        spin.x = Spin::ClampStrength(parser.stof(p2, 0));
        return true;
    }

    if ( !StriCmp(p1, prefix + "_spin_y") )
    {
        spin.y = Spin::ClampStrength(parser.stof(p2, 0));
        return true;
    }

    if ( !StriCmp(p1, prefix + "_spin_z") )
    {
        spin.z = Spin::ClampStrength(parser.stof(p2, 0));
        return true;
    }

    return false;
}

static float ParseFiniteFloatOrFallback(ScriptParser::Parser &parser,
                                        const std::string &value,
                                        float fallback)
{
    size_t parsed = 0;
    float result = parser.stof(value, &parsed);
    if ( parsed != value.size() || !std::isfinite(result) )
        return fallback;

    return result;
}

static bool ParseVPRotationParam(ScriptParser::Parser &parser,
                                 const std::string &prefix,
                                 const std::string &p1,
                                 const std::string &p2,
                                 vec3d &rotation)
{
    if ( !StriCmp(p1, prefix + "_rotation_x") )
    {
        rotation.x = ParseFiniteFloatOrFallback(parser, p2, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, prefix + "_rotation_y") )
    {
        rotation.y = ParseFiniteFloatOrFallback(parser, p2, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, prefix + "_rotation_z") )
    {
        rotation.z = ParseFiniteFloatOrFallback(parser, p2, 0.0f);
        return true;
    }

    return false;
}

static bool ParseExternalVisualParam(const std::string &p1,
                                     const std::string &p2,
                                     const std::string &prefix,
                                     World::TExternalVisualSet &visuals,
                                     bool allowLaunch)
{
    if ( !StriCmp(p1, prefix + "_normal") )
        visuals.normal = p2;
    else if ( !StriCmp(p1, prefix + "_fire") )
        visuals.fire = p2;
    else if ( !StriCmp(p1, prefix + "_megadeth") )
        visuals.megadeth = p2;
    else if ( !StriCmp(p1, prefix + "_wait") )
        visuals.wait = p2;
    else if ( !StriCmp(p1, prefix + "_dead") )
        visuals.dead = p2;
    else if ( !StriCmp(p1, prefix + "_genesis") )
        visuals.genesis = p2;
    else if ( allowLaunch && !StriCmp(p1, prefix + "_launch") )
        visuals.launch = p2;
    else
        return false;

    return true;
}

static bool ParseScriptIntRange(const std::string &value, int &minValue, int &maxValue)
{
    return World::ParseIntRangeValue(value, minValue, maxValue);
}

static bool ParseScriptFloatRange(const std::string &value, float &minValue, float &maxValue)
{
    return World::ParseFloatRangeValue(value, minValue, maxValue);
}

static bool ParseNumWeaponsRange(const std::string &value,
                                 int &minCount,
                                 int &maxCount)
{
    if ( !ParseScriptIntRange(value, minCount, maxCount) )
        return false;

    // Preserve the vanilla-compatible fixed zero value. Mixed zero ranges are
    // rejected instead of giving zero two different meanings inside one roll.
    if ( minCount == 0 && maxCount == 0 )
        return true;

    return minCount >= 1 && maxCount <= 255;
}

static bool ParseMimicEnergyCostRange(const std::string &value,
                                      int &minCost,
                                      int &maxCost)
{
    if ( !ParseScriptIntRange(value, minCost, maxCost) )
        return false;

    if ( minCost == 0 && maxCost == 0 )
        return true;
    if ( minCost <= 0 || maxCost <= 0 )
        return false;

    return true;
}

static bool ParseDecorationFXParam(ScriptParser::Parser &parser,
                                   const std::string &p1,
                                   const std::string &p2,
                                   World::TDecorationFXConfig &config)
{
    if ( !StriCmp(p1, "decoration_fx_vp") )
    {
        int vp = parser.stol(p2, NULL, 0);
        config.vp = vp > 0 ? vp : 0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_3ds") )
    {
        config.mesh3ds = p2;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_base") )
    {
        config.basePath = p2;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_mode") )
    {
        if ( !StriCmp(p2, "persistent") )
            config.mode = World::DECORATION_FX_PERSISTENT;
        else
            config.mode = World::DECORATION_FX_PERIODIC;

        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_interval") )
    {
        int intervalMin = 0;
        int intervalMax = 0;
        if ( ParseScriptIntRange(p2, intervalMin, intervalMax) )
        {
            config.interval_min = intervalMin;
            config.interval_max = intervalMax;
        }
        else
        {
            config.interval_min = 0;
            config.interval_max = 0;
        }
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_count") )
    {
        int countMin = 0;
        int countMax = 0;
        if ( ParseScriptIntRange(p2, countMin, countMax) )
        {
            config.count_min = std::max(0, std::min(countMin, 32));
            config.count_max = std::max(0, std::min(countMax, 32));
        }
        else
        {
            config.count_min = 0;
            config.count_max = 0;
        }
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_duration") )
    {
        int duration = parser.stol(p2, NULL, 0);
        config.duration = duration > 0 ? duration : 1000;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_fade_in") )
    {
        int fade = parser.stol(p2, NULL, 0);
        config.fade_in = fade > 0 ? fade : 0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_fade_out") )
    {
        int fade = parser.stol(p2, NULL, 0);
        config.fade_out = fade > 0 ? fade : 0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_vp_trail_fade_in") )
    {
        int fade = parser.stol(p2, NULL, 0);
        config.vp_trail_fade_in = fade > 0 ? fade : 0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_vp_trail_fade_out") )
    {
        int fade = parser.stol(p2, NULL, 0);
        config.vp_trail_fade_out = fade > 0 ? fade : 0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_random_pos") )
    {
        float radius = parser.stof(p2, 0);
        config.random_pos = radius > 0.0 ? radius : 0.0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_offset_x") )
    {
        config.offset.x = parser.stof(p2, 0);
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_offset_y") )
    {
        config.offset.y = parser.stof(p2, 0);
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_offset_z") )
    {
        config.offset.z = parser.stof(p2, 0);
        return true;
    }

    if ( ParseVPScaleParam(parser, "decoration_fx", p1, p2, config.scale) )
    {
        return true;
    }

    if ( ParseVPScaleParam(parser, "decoration_fx_vp_trail", p1, p2, config.vp_trail_scale) )
    {
        return true;
    }

    if ( ParseVPSpinParam(parser, "decoration_fx", p1, p2, config.spin) )
    {
        return true;
    }

    if ( ParseTintParam(parser, "decoration_fx_tint", p1, p2, config.tint) )
    {
        return true;
    }

    if ( ParseTintParam(parser, "decoration_fx_vp_trail_tint", p1, p2, config.vp_trail_tint) )
    {
        return true;
    }

    return false;
}

// OpenNeoUA custom: parse "*_tint = R_G_B_A" (each component 0..255).
// Alpha is optional and defaults to 255. Out-of-range values are clamped.
// RGB is stored as a normalized target hue; alpha remains multiplicative.
// Neutral white/opaque default = no change.
static bool ParseTintParam(ScriptParser::Parser &parser,
                           const std::string &paramName,
                           const std::string &p1,
                           const std::string &p2,
                           TVisualTint &tint,
                           bool invalidFallsBackToNeutral)
{
    if ( StriCmp(p1, paramName) )
        return false;

    std::vector<std::string> parts = Stok::Split(p2, "_");

    long comp[4] = {255, 255, 255, 255};

    if ( invalidFallsBackToNeutral )
    {
        bool valid = parts.size() == 3 || parts.size() == 4;
        for (size_t i = 0; valid && i < parts.size(); i++)
        {
            const std::string &part = parts[i];
            size_t digit = 0;
            bool negative = false;
            if ( !part.empty() && (part[0] == '+' || part[0] == '-') )
            {
                negative = part[0] == '-';
                digit = 1;
            }

            if ( digit >= part.size() )
            {
                valid = false;
                break;
            }

            long value = 0;
            for (; digit < part.size(); digit++)
            {
                if ( part[digit] < '0' || part[digit] > '9' )
                {
                    valid = false;
                    break;
                }

                // Values outside 0..255 are clamped below. Saturating here avoids
                // integer overflow and parser error popups for absurdly long input.
                if ( value <= 255 )
                    value = value * 10 + (part[digit] - '0');
            }

            comp[i] = negative ? -value : value;
        }

        if ( !valid )
        {
            tint = TVisualTint();
            ypa_log_out("WARNING: invalid %s '%s', using neutral 255_255_255_255\n",
                        paramName.c_str(), p2.c_str());
            return true;
        }
    }
    else
    {
        for (size_t i = 0; i < parts.size() && i < 4; i++)
            comp[i] = parser.stol(parts[i], 0, 10);
    }

    auto clamp255 = [](long v) -> float
    {
        if ( v < 0 )
            v = 0;
        if ( v > 255 )
            v = 255;
        return (float)v / 255.0f;
    };

    tint.r = clamp255(comp[0]);
    tint.g = clamp255(comp[1]);
    tint.b = clamp255(comp[2]);
    tint.a = clamp255(comp[3]);
    return true;
}

static bool ParseBoundedIntegerParam(const std::string &paramName,
                                     const std::string &p1,
                                     const std::string &p2,
                                     int minValue,
                                     int maxValue,
                                     int fallback,
                                     int &value)
{
    if ( StriCmp(p1, paramName) )
        return false;

    size_t digit = 0;
    bool negative = false;
    if ( !p2.empty() && (p2[0] == '+' || p2[0] == '-') )
    {
        negative = p2[0] == '-';
        digit = 1;
    }

    bool valid = digit < p2.size();
    long parsed = 0;
    for (; valid && digit < p2.size(); digit++)
    {
        if ( p2[digit] < '0' || p2[digit] > '9' )
        {
            valid = false;
            break;
        }

        if ( parsed <= (long)maxValue + 1 )
            parsed = parsed * 10 + (p2[digit] - '0');
    }

    if ( !valid )
    {
        value = fallback;
        ypa_log_out("WARNING: invalid %s '%s', using %d\n",
                    paramName.c_str(), p2.c_str(), fallback);
        return true;
    }

    if ( negative )
        parsed = -parsed;
    value = (int)std::max((long)minValue, std::min(parsed, (long)maxValue));
    return true;
}

static float ParseBoundedPositiveFiniteOrZero(ScriptParser::Parser &parser,
                                               const std::string &value,
                                               float maximum)
{
    size_t parsed = 0;
    const float result = parser.stof(value, &parsed);
    if ( parsed != value.size() || !std::isfinite(result) || result <= 0.0f )
        return 0.0f;

    return result > maximum ? maximum : result;
}

static double ParseBoundedFiniteOrZero(ScriptParser::Parser &parser,
                                       const std::string &value,
                                       double maximum)
{
    size_t parsed = 0;
    const double result = parser.stod(value, &parsed);
    if ( parsed != value.size() || !std::isfinite(result) )
        return 0.0;

    return std::max(-maximum, std::min(maximum, result));
}

static float ParseBoundedNonNegativeFiniteOrFallback(
    ScriptParser::Parser &parser, const std::string &value,
    float maximum, float fallback)
{
    size_t parsed = 0;
    const float result = parser.stof(value, &parsed);
    if ( parsed != value.size() || !std::isfinite(result) || result < 0.0f )
        return fallback;

    return result > maximum ? maximum : result;
}

static bool ParseMeshTracerUniformSize(ScriptParser::Parser &parser,
                                       const std::string &value,
                                       float &result)
{
    // head_size / tail_size are single scalar endpoint overrides. They control
    // the transverse X/Y section uniformly; size_z remains the sole owner of
    // longitudinal tracer length so tapering cannot introduce gaps.
    const float size = ParseBoundedNonNegativeFiniteOrFallback(
        parser, value, 100.0f, -1.0f);
    if ( size < 0.0f )
        return false;

    result = size;
    return true;
}

static bool ParseMeshTracerParam(ScriptParser::Parser &parser,
                                   const std::string &p1,
                                   const std::string &p2,
                                   World::TWeaponTracerConfig &config,
                                   const std::string &prefix)
{
    const auto key = [&prefix](const char *suffix)
    {
        return prefix + suffix;
    };

    if ( !StriCmp(p1, key("enable")) )
    {
        config.enabled = p2 == "1";
        return true;
    }

    if ( ParseTintParam(parser, key("tint"), p1, p2, config.tint, true) )
        return true;

    if ( !StriCmp(p1, key("size_z")) )
    {
        config.size_z = ParseBoundedPositiveFiniteOrZero(parser, p2, 6000.0f);
        return true;
    }

    if ( !StriCmp(p1, key("size_x")) )
    {
        config.size_x = ParseBoundedPositiveFiniteOrZero(parser, p2, 100.0f);
        return true;
    }

    if ( !StriCmp(p1, key("size_y")) )
    {
        const float value = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 100.0f, -1.0f);
        config.has_size_y = value >= 0.0f;
        config.size_y = config.has_size_y ? value : 0.0f;
        return true;
    }

    if ( !StriCmp(p1, key("pos_x")) )
    {
        config.pos.x = ParseBoundedFiniteOrZero(parser, p2, 6000.0);
        return true;
    }

    if ( !StriCmp(p1, key("pos_y")) )
    {
        config.pos.y = ParseBoundedFiniteOrZero(parser, p2, 6000.0);
        return true;
    }

    if ( !StriCmp(p1, key("pos_z")) )
    {
        config.pos.z = ParseBoundedFiniteOrZero(parser, p2, 6000.0);
        return true;
    }

    if ( !StriCmp(p1, key("path")) )
    {
        config.mesh_path = p2;
        return true;
    }

    if ( ParseTintParam(parser, key("head_tint"), p1, p2, config.tint_head, true) )
    {
        config.has_tint_head = true;
        return true;
    }

    if ( ParseTintParam(parser, key("tail_tint"), p1, p2, config.tint_tail, true) )
    {
        config.has_tint_tail = true;
        return true;
    }

    if ( !StriCmp(p1, key("head_size")) )
    {
        float size = 0.0f;
        config.has_head_size = ParseMeshTracerUniformSize(parser, p2, size);
        config.head_size = config.has_head_size ? size : 0.0f;
        return true;
    }

    if ( !StriCmp(p1, key("tail_size")) )
    {
        float size = 0.0f;
        config.has_tail_size = ParseMeshTracerUniformSize(parser, p2, size);
        config.tail_size = config.has_tail_size ? size : 0.0f;
        return true;
    }

    if ( !StriCmp(p1, key("glow_rate")) )
    {
        config.glow_rate = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, key("noise_rate")) )
    {
        config.noise_rate = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, key("pulse_rate")) )
    {
        config.pulse_rate = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, key("pulse_speed")) )
    {
        config.pulse_speed = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    return false;
}

static bool ParseLaserMeshParam(
    ScriptParser::Parser &parser, const std::string &p1,
    const std::string &p2, World::TWeapProto::TLaserMeshConfig &config)
{
    if ( !StriCmp(p1, "laser_mesh_enable") )
    {
        // Only the exact value 1 enables the optional renderer.
        config.enabled = p2 == "1";
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_path") )
    {
        config.mesh_path = p2;
        return true;
    }

    if ( ParseTintParam(parser, "laser_mesh_tint", p1, p2, config.tint, true) )
        return true;

    if ( !StriCmp(p1, "laser_mesh_size_x") )
    {
        // A non-positive or malformed primary size leaves the external mesh
        // renderer disabled for this weapon; no hidden geometry is substituted.
        config.size_x = ParseBoundedPositiveFiniteOrZero(parser, p2, 100.0f);
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_size_y") )
    {
        // The secondary axis is optional. Zero, negative and malformed values
        // deliberately clear the override so it inherits size_x.
        const float value = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 100.0f, -1.0f);
        config.has_size_y = value > 0.0f;
        config.size_y = config.has_size_y ? value : 0.0f;
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_glow_rate") )
    {
        config.glow_rate = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_pulse_rate") )
    {
        config.pulse_rate = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_pulse_speed") )
    {
        config.pulse_speed = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_noise_rate") )
    {
        config.noise_rate = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 10.0f, 0.0f);
        return true;
    }

    if ( !StriCmp(p1, "laser_mesh_impact_fade_length") )
    {
        // World-space distance over which the laser body fades only when the
        // beam has a real unit/terrain/building contact. Zero/invalid disables.
        config.impact_fade_length = ParseBoundedNonNegativeFiniteOrFallback(
            parser, p2, 12000.0f, 0.0f);
        return true;
    }

    return false;
}


static bool ParseWireframeTintParam(ScriptParser::Parser &parser,
                                    const std::string &p1,
                                    const std::string &p2,
                                    TVisualTint &tint)
{
    return ParseTintParam(parser, "wireframe_tint", p1, p2, tint);
}

enum ChainFXParseContext
{
    CHAIN_FX_VEHICLE,
    CHAIN_FX_WEAPON,
    CHAIN_FX_SUPERITEM
};

static const char *ChainFXContextName(ChainFXParseContext context)
{
    if ( context == CHAIN_FX_WEAPON )
        return "weapon";
    if ( context == CHAIN_FX_SUPERITEM )
        return "SuperItem";
    return "vehicle";
}

static World::TChainFXConfig::Trigger ParseChainFXTrigger(const std::string &name,
                                                          ChainFXParseContext context)
{
    if ( context == CHAIN_FX_WEAPON )
    {
        if ( !StriCmp(name, "detonate") )
            return World::TChainFXConfig::TRIGGER_DETONATE;

        if ( !StriCmp(name, "impact_world") )
            return World::TChainFXConfig::TRIGGER_IMPACT_WORLD;
    }
    else if ( context == CHAIN_FX_SUPERITEM )
    {
        if ( !StriCmp(name, "detonate") )
            return World::TChainFXConfig::TRIGGER_DETONATE;
    }
    else
    {
        if ( !StriCmp(name, "destroyed") )
            return World::TChainFXConfig::TRIGGER_DESTROYED;

        if ( !StriCmp(name, "crash") )
            return World::TChainFXConfig::TRIGGER_CRASH;
    }

    return World::TChainFXConfig::TRIGGER_NONE;
}

static World::TChainFXConfig::Mode ParseChainFXMode(const std::string &name)
{
    if ( !StriCmp(name, "visual") )
        return World::TChainFXConfig::MODE_VISUAL;

    if ( !StriCmp(name, "physical") )
        return World::TChainFXConfig::MODE_PHYSICAL;

    if ( !StriCmp(name, "ground_decal") )
        return World::TChainFXConfig::MODE_GROUND_DECAL;

    return World::TChainFXConfig::MODE_VISUAL;
}

static int ParseChainFXBlock(ScriptParser::Parser &parser,
                             std::vector<World::TChainFXConfig> *out,
                             ChainFXParseContext context)
{
    World::TChainFXConfig::Mode mode = World::TChainFXConfig::MODE_VISUAL;
    float startSize = 1.0;
    float midSize = 0.0;
    float endSize = 0.0;
    bool hasMidSize = false;
    bool hasEndSize = false;
    vec3d offset;
    int duration = 0;
    bool groundDecalDurationValid = false;
    int fadeIn = 0;
    int fadeOut = 0;
    std::vector<World::TChainFXVisual> visuals;
    int physicalVehicle = 0;
    std::string groundDecalTexture;
    int groundDecalPoints = 12;
    int groundDecalJaggedness = 35;
    float groundDecalSize = 0.0f;
    TVisualTint groundDecalTint;
    bool groundDecalRandomRotation = false;
    float groundDecalEdgeFade = 0.0f;
    World::TChainFXConfig::Trigger trigger = World::TChainFXConfig::TRIGGER_NONE;
    bool hasTrigger = false;
    bool badTrigger = false;
    bool badMode = false;

    std::string p1;
    std::string p2;

    while ( parser.ReadLine(&p1) )
    {
        size_t line_start = p1.find(";#!");
        if (line_start != std::string::npos)
            p1 = p1.substr(line_start + 3);

        size_t line_end = p1.find_first_of(";\n\r");
        if (line_end != std::string::npos)
            p1.erase(line_end);

        Stok stok(p1, "= \t");
        if ( !stok.GetNext(&p1) )
            continue;

        p2.clear();
        stok.GetNext(&p2);

        if ( !StriCmp(p1, "end") )
        {
            if ( badTrigger || badMode )
                return ScriptParser::RESULT_OK;

            if ( !hasTrigger )
            {
                ypa_log_out("WARNING: begin_chain_fx without trigger ignored for %s prototype\n",
                            ChainFXContextName(context));
                return ScriptParser::RESULT_OK;
            }

            if ( !hasEndSize )
                endSize = 0.0;

            if ( mode == World::TChainFXConfig::MODE_VISUAL )
            {
                if ( duration > 0 && !visuals.empty() )
                {
                    World::TChainFXConfig chain;
                    chain.mode = mode;
                    chain.trigger = trigger;
                    chain.offset = offset;
                    chain.start_size = startSize;
                    chain.mid_size = midSize;
                    chain.end_size = endSize;
                    chain.has_mid_size = hasMidSize;
                    chain.duration = duration;
                    chain.fade_in = fadeIn;
                    chain.fade_out = fadeOut;
                    chain.visuals = visuals;
                    out->push_back(chain);
                }
            }
            else if ( mode == World::TChainFXConfig::MODE_PHYSICAL )
            {
                if ( physicalVehicle > 0 )
                {
                    World::TChainFXConfig chain;
                    chain.mode = mode;
                    chain.trigger = trigger;
                    chain.offset = offset;
                    chain.physical_vehicle = physicalVehicle;
                    out->push_back(chain);
                }
                else
                {
                    ypa_log_out("WARNING: begin_chain_fx physical mode without physical_vehicle ignored\n");
                }
            }
            else if ( mode == World::TChainFXConfig::MODE_GROUND_DECAL )
            {
                if ( context != CHAIN_FX_WEAPON ||
                     trigger != World::TChainFXConfig::TRIGGER_IMPACT_WORLD )
                {
                    ypa_log_out("WARNING: begin_chain_fx ground_decal requires weapon trigger impact_world; block ignored\n");
                }
                else if ( groundDecalPoints < 3 || groundDecalPoints > 32 ||
                          !std::isfinite(groundDecalSize) || groundDecalSize <= 0.0f ||
                          !groundDecalDurationValid || duration <= 0 ||
                          groundDecalTint.a <= 0.0f )
                {
                    ypa_log_out("WARNING: incomplete or disabled begin_chain_fx ground_decal block ignored\n");
                }
                else
                {
                    World::TChainFXConfig chain;
                    chain.mode = mode;
                    chain.trigger = trigger;
                    chain.duration = duration;
                    chain.fade_out = std::min(fadeOut, duration);
                    chain.fade_in = std::min(fadeIn, duration - chain.fade_out);
                    chain.ground_decal_texture = groundDecalTexture;
                    chain.ground_decal_points = groundDecalPoints;
                    chain.ground_decal_jaggedness = (float)groundDecalJaggedness / 100.0f;
                    chain.ground_decal_size = groundDecalSize;
                    chain.ground_decal_tint = groundDecalTint;
                    chain.ground_decal_random_rotation = groundDecalRandomRotation;
                    chain.ground_decal_edge_fade = groundDecalEdgeFade;
                    out->push_back(chain);
                }
            }

            return ScriptParser::RESULT_OK;
        }

        if ( badMode )
            continue;

        if ( !StriCmp(p1, "mode") )
        {
            if ( !StriCmp(p2, "visual") || !StriCmp(p2, "physical") ||
                 !StriCmp(p2, "ground_decal") )
            {
                mode = ParseChainFXMode(p2);
                if ( context == CHAIN_FX_SUPERITEM && mode != World::TChainFXConfig::MODE_VISUAL )
                {
                    ypa_log_out("WARNING: SuperItem begin_chain_fx supports only visual mode; block ignored\n");
                    badMode = true;
                }
                else if ( mode == World::TChainFXConfig::MODE_GROUND_DECAL &&
                          context != CHAIN_FX_WEAPON )
                {
                    ypa_log_out("WARNING: begin_chain_fx ground_decal is weapon-only; block ignored\n");
                    badMode = true;
                }
            }
            else
            {
                ypa_log_out("WARNING: Unknown begin_chain_fx mode '%s' ignored\n", p2.c_str());
                badMode = true;
            }
        }
        else if ( !StriCmp(p1, "trigger") )
        {
            trigger = ParseChainFXTrigger(p2, context);
            hasTrigger = true;
            if ( trigger == World::TChainFXConfig::TRIGGER_NONE )
            {
                ypa_log_out("WARNING: Unknown or unsupported begin_chain_fx trigger '%s' ignored\n", p2.c_str());
                badTrigger = true;
            }
        }
        else if ( !StriCmp(p1, "start_size") )
            startSize = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "mid_size") )
        {
            size_t parsed = 0;
            const float value = parser.stof(p2, &parsed);
            if ( parsed == p2.size() && std::isfinite(value) && value >= 0.0f )
            {
                midSize = value;
                hasMidSize = true;
            }
            else
            {
                ypa_log_out("WARNING: Invalid begin_chain_fx mid_size '%s'; using linear start_size -> end_size fallback\n",
                            p2.c_str());
                hasMidSize = false;
            }
        }
        else if ( !StriCmp(p1, "end_size") )
        {
            endSize = parser.stof(p2, 0);
            hasEndSize = true;
        }
        else if ( !StriCmp(p1, "duration") )
        {
            size_t parsed = 0;
            duration = parser.stol(p2, &parsed, 0);
            groundDecalDurationValid = parsed == p2.size() && duration > 0;
        }
        else if ( !StriCmp(p1, "fade_in") )
            fadeIn = NonNegativeFiniteMilliseconds(parser, p2);
        else if ( !StriCmp(p1, "fade_out") )
            fadeOut = NonNegativeFiniteMilliseconds(parser, p2);
        else if ( !StriCmp(p1, "offset_x") )
            offset.x = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "offset_y") )
            offset.y = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "offset_z") )
            offset.z = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "vp_model") )
        {
            World::TChainFXVisual visual;
            visual.vp = parser.stol(p2, NULL, 0);
            visuals.push_back(visual);
        }
        else if ( !StriCmp(p1, "3ds_model") )
        {
            if ( !visuals.empty() &&
                 (visuals.back().vp > 0 || !visuals.back().basePath.empty()) &&
                 visuals.back().mesh3ds.empty() )
                visuals.back().mesh3ds = p2;
            else
            {
                World::TChainFXVisual visual;
                visual.mesh3ds = p2;
                visuals.push_back(visual);
            }
        }
        else if ( !StriCmp(p1, "base_model") )
        {
            if ( !visuals.empty() && visuals.back().vp > 0 &&
                 visuals.back().mesh3ds.empty() && visuals.back().basePath.empty() )
                visuals.back().basePath = p2;
            else
            {
                World::TChainFXVisual visual;
                visual.basePath = p2;
                visuals.push_back(visual);
            }
        }
        else if ( !StriCmp(p1, "visual_tint") )
        {
            if ( visuals.empty() )
            {
                ypa_log_out("WARNING: begin_chain_fx visual_tint without preceding vp_model/base_model/3ds_model ignored\n");
                continue;
            }

            ParseTintParam(parser, "visual_tint", p1, p2, visuals.back().tint);
            visuals.back().has_tint = true;
        }
        else if ( !StriCmp(p1, "physical_vehicle") )
        {
            if ( context == CHAIN_FX_SUPERITEM )
            {
                ypa_log_out("WARNING: SuperItem begin_chain_fx does not support physical_vehicle; block ignored\n");
                badMode = true;
            }
            else
                physicalVehicle = parser.stol(p2, NULL, 0);
        }
        else if ( !StriCmp(p1, "texture") )
            groundDecalTexture = p2;
        else if ( ParseBoundedIntegerParam("points", p1, p2,
                                           3, 32, 12, groundDecalPoints) )
        {
        }
        else if ( ParseBoundedIntegerParam("jaggedness", p1, p2,
                                           0, 100, 35, groundDecalJaggedness) )
        {
        }
        else if ( !StriCmp(p1, "size") )
        {
            size_t parsed = 0;
            const float value = parser.stof(p2, &parsed);
            groundDecalSize = parsed == p2.size() && std::isfinite(value) && value > 0.0f
                            ? value : 0.0f;
        }
        else if ( ParseTintParam(parser, "tint", p1, p2,
                                 groundDecalTint, true) )
        {
        }
        else if ( !StriCmp(p1, "random_rotation") )
            groundDecalRandomRotation = p2 == "1";
        else if ( !StriCmp(p1, "edge_fade") )
        {
            size_t parsed = 0;
            const float value = parser.stof(p2, &parsed);
            groundDecalEdgeFade = parsed == p2.size() && std::isfinite(value)
                                ? std::max(0.0f, std::min(value, 10.0f)) : 0.0f;
        }
        else
        {
            if ( context == CHAIN_FX_SUPERITEM )
            {
                ypa_log_out("WARNING: Unknown SuperItem begin_chain_fx parameter '%s'; block ignored\n", p1.c_str());
                badMode = true;
                continue;
            }
            return ScriptParser::RESULT_UNKNOWN;
        }
    }

    return ScriptParser::RESULT_UNEXP_EOF;
}

static int ParseVehicleChainFXBlock(ScriptParser::Parser &parser, TVhclProto *vhcl)
{
    return ParseChainFXBlock(parser,
                             &vhcl->chain_fx,
                             CHAIN_FX_VEHICLE);
}

static int ParseWeaponChainFXBlock(ScriptParser::Parser &parser, TWeapProto *wpn)
{
    return ParseChainFXBlock(parser,
                             &wpn->chain_fx,
                             CHAIN_FX_WEAPON);
}

static int ParseSuperItemChainFXBlock(ScriptParser::Parser &parser, TSuperItemProfile *profile)
{
    return ParseChainFXBlock(parser,
                             &profile->detonate_chain_fx,
                             CHAIN_FX_SUPERITEM);
}

static bool IsMimicVehicleShellParam(const std::string &p1)
{
    return !StriCmp(p1, "model") ||
           !StriCmp(p1, "name") ||
           !StriCmp(p1, "type_icon") ||
           !StriCmp(p1, "mimic_energy_cost") ||
           !StriCmp(p1, "mimic_tint") ||
           !StriCmp(p1, "snd_mimic_sample") ||
           !StriCmp(p1, "snd_mimic_pitch") ||
           !StriCmp(p1, "snd_mimic_volume") ||
           !StriCmp(p1, "job_fightrobo") ||
           !StriCmp(p1, "job_fightflyer") ||
           !StriCmp(p1, "job_fighthelicopter") ||
           !StriCmp(p1, "job_fighttank") ||
           !StriCmp(p1, "job_fightplane") ||
           !StriCmp(p1, "job_fightcruiser") ||
           !StriCmp(p1, "job_fightglider") ||
           !StriCmp(p1, "job_fightzeppelin") ||
           !StriCmp(p1, "job_fightufo") ||
           !StriCmp(p1, "job_fightcar") ||
           !StriCmp(p1, "job_fightgun") ||
           !StriCmp(p1, "job_conquer") ||
           !StriCmp(p1, "job_reconnoitre") ||
           !StriCmp(p1, "spawn_at_death_units") ||
           !StriCmp(p1, "spawn_at_death_vehicle") ||
           !StriCmp(p1, "spawn_at_death_count") ||
           !StriCmp(p1, "spawn_at_death_random_pos") ||
           !StriCmp(p1, "spawn_at_death_instant") ||
           !StriCmp(p1, "spawn_at_death_immunity_time");
}


static float ParseFiniteCollisionFloat(ScriptParser::Parser &parser,
                                       const std::string &value)
{
    float parsed = parser.stof(value, 0);
    return std::isfinite(parsed) ? parsed : 0.0f;
}

static bool ParseCompoundCollisionParameter(
        ScriptParser::Parser &parser, const std::string &p1,
        const std::string &p2, rbcolls &coll, int32_t &collID)
{
    if ( !StriCmp(p1, "coll_num") )
    {
        int count = parser.stol(p2, NULL, 0);
        count = std::max(0, std::min(count, (int)UNIT_COLL_MAX_COUNT));
        coll.roboColls.resize(count);
        if ( collID >= count )
            collID = count - 1;
        return true;
    }

    if ( !StriCmp(p1, "coll_act") )
    {
        collID = parser.stol(p2, NULL, 0);
        collID = std::max(0, std::min(collID, (int)UNIT_COLL_MAX_COUNT - 1));
        if ( (size_t)collID >= coll.roboColls.size() )
            coll.roboColls.resize(collID + 1);
        return true;
    }

    if ( !StriCmp(p1, "coll_radius") ||
         !StriCmp(p1, "coll_x") ||
         !StriCmp(p1, "coll_y") ||
         !StriCmp(p1, "coll_z") )
    {
        if ( collID < 0 || (size_t)collID >= coll.roboColls.size() )
            return true;

        TRoboColl &sphere = coll.roboColls.at(collID);
        float value = ParseFiniteCollisionFloat(parser, p2);
        if ( !StriCmp(p1, "coll_radius") )
            sphere.robo_coll_radius = std::max(0.0f, value);
        else if ( !StriCmp(p1, "coll_x") )
            sphere.coll_pos.x = value;
        else if ( !StriCmp(p1, "coll_y") )
            sphere.coll_pos.y = value;
        else
            sphere.coll_pos.z = value;
        return true;
    }

    return false;
}

int VhclProtoParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    TRoboProto *robo = _vhcl->RoboProto;

    if (!robo)
        robo = &_roboTmp;

    auto getUnitGun = [this, robo]() -> TRoboGun *
    {
        // On Host Stations the unit_* family is a parser alias of the native
        // Robo gun list. This keeps one runtime attachment system (_roboGuns)
        // while allowing the richer Unit Gun fields (icon/protect) to be used.
        if ( _vhcl->model_id == BACT_TYPES_ROBO )
        {
            if ( _unitGunID < 0 || (size_t)_unitGunID >= robo->guns.size() )
                return NULL;

            return &robo->guns.at(_unitGunID);
        }

        if ( _unitGunID < 0 || (size_t)_unitGunID >= _vhcl->unit_guns.size() )
            return NULL;

        return &_vhcl->unit_guns.at(_unitGunID);
    };

    auto getRoboColl = [this, robo]() -> TRoboColl *
    {
        if ( _collID < 0 || (size_t)_collID >= robo->coll.roboColls.size() )
            return NULL;

        return &robo->coll.roboColls.at(_collID);
    };

    if ( !StriCmp(p1, "end") )
    {
        bool fireXAdvancedAuthored = _vhcl->fire_x_start_defined ||
                                     _vhcl->fire_x_step_defined ||
                                     _vhcl->fire_x_slots_defined;
        bool fireXSlotsValid = _vhcl->fire_x_slots_defined &&
                               _vhcl->fire_x_slots > 0 &&
                               _vhcl->fire_x_slots <= TVhclProto::FIRE_X_MAX_SLOTS;
        bool fireXAdvancedValid = _vhcl->fire_x_start_defined &&
                                  _vhcl->fire_x_step_defined &&
                                  fireXSlotsValid &&
                                  std::isfinite(_vhcl->fire_x_start) &&
                                  std::isfinite(_vhcl->fire_x_step);

        if ( fireXAdvancedValid )
        {
            double fireXLast = (double)_vhcl->fire_x_start +
                               (double)(_vhcl->fire_x_slots - 1) * (double)_vhcl->fire_x_step;
            fireXAdvancedValid = std::isfinite(fireXLast) &&
                                 std::abs(fireXLast) <= (double)std::numeric_limits<float>::max();
        }

        bool fireXAdvancedMode = _vhcl->fire_x_mode == TVhclProto::FIRE_X_MODE_SEQUENCE ||
                                 _vhcl->fire_x_mode == TVhclProto::FIRE_X_MODE_RANDOM;
        if ( fireXAdvancedMode )
        {
            _vhcl->fire_x_advanced = fireXAdvancedValid;

            if ( fireXAdvancedAuthored && !fireXAdvancedValid )
            {
                ypa_log_out("WARNING: vehicle %d fire_x advanced rack ignored; require finite fire_x_start/fire_x_step, a finite final slot, and fire_x_slots in range 1-%d. Falling back to simple fire_x mode.\n",
                            _vhclID, TVhclProto::FIRE_X_MAX_SLOTS);
            }
        }
        else
            _vhcl->fire_x_advanced = false;

        const bool weaponArcXActive = _vhcl->weapon_arc_x > 0.0f;
        const bool weaponArcYActive = _vhcl->weapon_arc_y > 0.0f;
        const bool weaponConeActive = _vhcl->weapon_cone_xy > 0.0f;
        if ( (weaponArcXActive || weaponArcYActive) && weaponConeActive )
        {
            ypa_log_out("WARNING: vehicle %d weapon Arc/Cone conflict (arc_x=%.3f, arc_y=%.3f, cone_xy=%.3f); Arc and Cone disabled, normal launch direction retained for every weapon slot.\n",
                        _vhclID, _vhcl->weapon_arc_x, _vhcl->weapon_arc_y,
                        _vhcl->weapon_cone_xy);
        }
        else if ( weaponArcXActive && weaponArcYActive )
        {
            const int weaponIds[4] = {_vhcl->weapon, _vhcl->extra_weapons[0],
                                      _vhcl->extra_weapons[1], _vhcl->extra_weapons[2]};

            for (int weaponSlot = 0; weaponSlot < 4; weaponSlot++)
            {
                if ( (weaponSlot == 0 && weaponIds[weaponSlot] < 0) ||
                     (weaponSlot > 0 && weaponIds[weaponSlot] <= 0) )
                    continue;

                int minWeaponCount = 1;
                int maxWeaponCount = 1;
                _vhcl->GetWeaponProjectileCountRange(
                    weaponSlot, minWeaponCount, maxWeaponCount);

                bool hasInvalidArcCount = false;
                for (int weaponCount = minWeaponCount;
                     weaponCount <= maxWeaponCount; ++weaponCount)
                {
                    if ( weaponCount > 1 &&
                         weaponCount % 4 != 0 && weaponCount % 4 != 1 )
                    {
                        hasInvalidArcCount = true;
                        break;
                    }
                }

                if ( hasInvalidArcCount )
                {
                    ypa_log_out("WARNING: vehicle %d weapon slot %d Arc cross requires every effective projectile count to be 4k or 4k+1 (configured %d_%d); Arc is disabled on incompatible random rolls.\n",
                                _vhclID, weaponSlot + 1,
                                minWeaponCount, maxWeaponCount);
                }
            }
        }

        if ( _vhcl->model_id == BACT_TYPES_ROBO )
        {
            if (!_vhcl->RoboProto)
                _vhcl->RoboProto = new TRoboProto(_roboTmp);

            _vhcl->initParams.Add(NC_STACK_yparobo::ROBO_ATT_PROTO, _vhcl->RoboProto);
        }

        if ( _vhcl->model_id == BACT_TYPES_BACT )
            _vhcl->field_1D6F = (_vhcl->force * 0.6) / _vhcl->airconst;
        else
            _vhcl->field_1D6F = (_vhcl->force) / _vhcl->airconst;

        _vhcl->field_1D6D = (_vhcl->field_1D6F / 10) * 1200.0;

        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( ParseCompoundCollisionParameter(parser, p1, p2, _vhcl->coll, _collID) )
        return ScriptParser::RESULT_OK;

    if ( !StriCmp(p1, "model") )
    {
        _vhcl->is_mimic = 0;

        if ( !StriCmp(p2, "heli") )
        {
            _vhcl->model_id = BACT_TYPES_BACT;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_HELI;
        }
        else if ( !StriCmp(p2, "tank") )
        {
            _vhcl->model_id = BACT_TYPES_TANK;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_TANK;
        }
        else if ( !StriCmp(p2, "robo") )
        {
            _vhcl->model_id = BACT_TYPES_ROBO;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_ROBO;

            *robo = TRoboProto();
            robo->matrix = mat3x3::Ident();

            // If unit_* gun fields appeared before model = robo, fold them into
            // the native Robo list now. Script order must not create a second
            // attachment runtime on Host Stations.
            if ( !_vhcl->unit_guns.empty() )
            {
                robo->guns = _vhcl->unit_guns;
                _vhcl->unit_guns.clear();
                _gunID = _unitGunID;
            }
        }
        else if ( !StriCmp(p2, "ufo") )
        {
            _vhcl->model_id = BACT_TYPES_UFO;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_UFO;
        }
        else if ( !StriCmp(p2, "car") )
        {
            _vhcl->model_id = BACT_TYPES_CAR;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_CAR;
        }
        else if ( !StriCmp(p2, "gun") || !StriCmp(p2, "module") )
        {
            // OpenNeoUA: model = module is a semantic alias of the existing gun
            // runtime. Behaviour is selected by gun_type; no parallel actor
            // class or attachment system is introduced.
            _vhcl->model_id = BACT_TYPES_GUN;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_GUN;
        }
        else if ( !StriCmp(p2, "plane") )
        {
            _vhcl->model_id = BACT_TYPES_FLYER;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_PLANE;

            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)3);
        }
        else if ( !StriCmp(p2, "cruiser") )
        {
            // OpenNeoUA: expose the unused Flyer bit-combination already handled
            // by ypaflyer: pitch follows vertical motion, lateral banking stays rigid.
            _vhcl->model_id = BACT_TYPES_FLYER;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_CRUISER;
            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)1);
        }
        else if ( !StriCmp(p2, "glider") )
        {
            _vhcl->model_id = BACT_TYPES_FLYER;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_GLIDER;
            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)2);
        }
        else if ( !StriCmp(p2, "zeppelin") )
        {
            _vhcl->model_id = BACT_TYPES_FLYER;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_ZEPPELIN;
            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)0);
        }
        else if ( !StriCmp(p2, "mimic") )
        {
            // OpenNeoUA custom: runtime shell that copies one vehicle enabled by the
            // current level, then keeps this proto's spawn_at_death_* reveal data.
            _vhcl->model_id = BACT_TYPES_TANK;
            _vhcl->combat_class = VEHICLE_COMBAT_CLASS_UNKNOWN;
            _vhcl->is_mimic = 1;
            _vhcl->job_fightrobo = 6;
            _vhcl->job_fightflyer = 6;
            _vhcl->job_fighthelicopter = 6;
            _vhcl->job_fighttank = 6;
            _vhcl->job_conquer = 6;
            _vhcl->job_reconnoitre = 6;
        }
        else
        {
            return ScriptParser::RESULT_BAD_DATA;
        }
    }
    else if ( _vhcl->is_mimic && !IsMimicVehicleShellParam(p1) )
    {
        return ScriptParser::RESULT_OK;
    }
    else if ( !StriCmp(p1, "enable") )
    {
        int fraction = parser.stol(p2, NULL, 0);
        bool wasEnabled = (_vhcl->disable_enable_bitmask & (1 << fraction)) != 0;
        _vhcl->disable_enable_bitmask |= 1 << fraction;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                           TGemNotificationEntry::CHANGE_ENABLE,
                                           wasEnabled ? 1 : 0, 1, !wasEnabled);
    }
    else if ( !StriCmp(p1, "disable") )
    {
        _vhcl->disable_enable_bitmask &= ~(1 << parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "name") )
    {
        _vhcl->name = p2;
        std::replace(_vhcl->name.begin(), _vhcl->name.end(), '_', ' ');
    }
    else if ( !StriCmp(p1, "energy") )
    {
        _vhcl->energy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mimic_energy_cost") )
    {
        int minCost = 0;
        int maxCost = 0;
        if ( ParseMimicEnergyCostRange(p2, minCost, maxCost) )
        {
            _vhcl->mimic_energy_cost_min = minCost;
            _vhcl->mimic_energy_cost_max = maxCost;
            _vhcl->RollMimicProductionCost();
        }
        else
        {
            _vhcl->mimic_energy_cost = 0;
            _vhcl->mimic_energy_cost_min = 0;
            _vhcl->mimic_energy_cost_max = 0;
        }
    }
    else if ( !StriCmp(p1, "shield") )
    {
        _vhcl->shield = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mass") )
    {
        _vhcl->mass = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "force") )
    {
        _vhcl->force = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "maxrot") )
    {
        _vhcl->maxrot = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "airconst") )
    {
        _vhcl->airconst = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "height") )
    {
        _vhcl->height = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius") )
    {
        _vhcl->radius = parser.stof(p2, 0);
        _vhcl->radius_defined = true;
    }
    else if ( !StriCmp(p1, "overeof") )
    {
        _vhcl->overeof = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "vwr_radius") )
    {
        _vhcl->vwr_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "vwr_overeof") )
    {
        _vhcl->vwr_overeof = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "adist_sector") )
    {
        _vhcl->adist_sector = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "adist_bact") )
    {
        _vhcl->adist_bact = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "sdist_sector") )
    {
        _vhcl->sdist_sector = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "sdist_bact") )
    {
        _vhcl->sdist_bact = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "ai_attack_range") )
    {
        _vhcl->ai_attack_range = parser.stof(p2, 0);
        _vhcl->ai_combat_distance_mask |= TVhclProto::AI_COMBAT_ATTACK_DEFINED;
    }
    else if ( !StriCmp(p1, "ai_retreat_range") )
    {
        _vhcl->ai_retreat_range = parser.stof(p2, 0);
        _vhcl->ai_combat_distance_mask |= TVhclProto::AI_COMBAT_RETREAT_DEFINED;
    }
    else if ( !StriCmp(p1, "ai_reengage_range") )
    {
        _vhcl->ai_reengage_range = parser.stof(p2, 0);
        _vhcl->ai_combat_distance_mask |= TVhclProto::AI_COMBAT_REENGAGE_DEFINED;
    }
    else if ( !StriCmp(p1, "radar") )
    {
        _vhcl->radar = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "push_resistance") )
    {
        _vhcl->push_resistance = Clamp01(parser.stof(p2, 0));
        _vhcl->has_push_resistance = true;
    }
    else if ( !StriCmp(p1, "push_at_death_force") )
    {
        _vhcl->push_at_death_force = ClampPushIntensity(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "push_at_death_radius") )
    {
        _vhcl->push_at_death_radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "push_at_death_falloff") )
    {
        _vhcl->push_at_death_falloff = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "add_energy") )
    {
        int previousValue = _vhcl->energy;
        _vhcl->energy += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                           TGemNotificationEntry::CHANGE_ENERGY,
                                           previousValue, _vhcl->energy);
    }
    else if ( !StriCmp(p1, "add_shield") )
    {
        int previousValue = _vhcl->shield;
        _vhcl->shield += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                           TGemNotificationEntry::CHANGE_SHIELD,
                                           previousValue, _vhcl->shield);
    }
    else if ( !StriCmp(p1, "add_radar") )
    {
        int previousValue = _vhcl->radar;
        _vhcl->radar += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                           TGemNotificationEntry::CHANGE_RADAR,
                                           previousValue, _vhcl->radar);
    }
    else if ( !StriCmp(p1, "add_max_active_at_once") )
    {
        const int increment = ParsePositiveIntOrZero(p2);
        if ( increment > 0 && _vhcl->max_active_at_once > 0 )
        {
            const int previousValue = _vhcl->max_active_at_once;
            if ( previousValue > std::numeric_limits<int>::max() - increment )
                _vhcl->max_active_at_once = std::numeric_limits<int>::max();
            else
                _vhcl->max_active_at_once += increment;

            if ( _vhcl->max_active_at_once != previousValue &&
                 _isModify && _o.IsGemNotificationCaptureActive() )
                _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                               TGemNotificationEntry::CHANGE_MAX_ACTIVE_AT_ONCE,
                                               previousValue, _vhcl->max_active_at_once);
        }
    }
    else if ( !StriCmp(p1, "vp_normal") )
    {
        _vhcl->vp_normal = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_fire") )
    {
        _vhcl->vp_fire = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_megadeth") )
    {
        _vhcl->vp_megadeth = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_wait") )
    {
        _vhcl->vp_wait = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_dead") )
    {
        _vhcl->vp_dead = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_genesis") )
    {
        _vhcl->vp_genesis = parser.stol(p2, NULL, 0);
    }
    else if ( ParseExternalVisualParam(p1, p2, "3ds", _vhcl->visual_3ds, false) )
    {
    }
    else if ( ParseExternalVisualParam(p1, p2, "base", _vhcl->visual_base, false) )
    {
    }
    else if ( !StriCmp(p1, "damaged_fx_vp") )
    {
        if ( !ParseSmartVPList(p2, _vhcl->damaged_fx.vps) )
            _vhcl->damaged_fx.vps.clear();
    }
    else if ( !StriCmp(p1, "damaged_fx_3ds") )
    {
        if ( !p2.empty() )
            _vhcl->damaged_fx.meshes3ds.push_back(p2);
    }
    else if ( !StriCmp(p1, "damaged_fx_scale") )
    {
        _vhcl->damaged_fx.scale = ParseVPScaleValue(parser, p2);
    }
    else if ( !StriCmp(p1, "damaged_fx_threshold") )
    {
        ParseAbsoluteOrPercent(p2, _vhcl->damaged_fx.threshold, 100.0f);
    }
    else if ( !StriCmp(p1, "damaged_fx_count") )
    {
        int countMin = 0;
        int countMax = 0;
        if ( ParseScriptIntRange(p2, countMin, countMax) )
        {
            _vhcl->damaged_fx.count_min = std::max(0, std::min(countMin, 32));
            _vhcl->damaged_fx.count_max = std::max(0, std::min(countMax, 32));
        }
        else
        {
            _vhcl->damaged_fx.count_min = 0;
            _vhcl->damaged_fx.count_max = 0;
        }
    }
    else if ( !StriCmp(p1, "damaged_fx_interval") )
    {
        int intervalMin = 0;
        int intervalMax = 0;
        if ( ParseScriptIntRange(p2, intervalMin, intervalMax) )
        {
            _vhcl->damaged_fx.interval_min = intervalMin;
            _vhcl->damaged_fx.interval_max = intervalMax;
        }
        else
        {
            _vhcl->damaged_fx.interval_min = 0;
            _vhcl->damaged_fx.interval_max = 0;
        }
    }
    else if ( !StriCmp(p1, "damaged_fx_random_max_offset") )
    {
        ParseAbsoluteOrPercent(p2, _vhcl->damaged_fx.random_max_offset, 100.0f);
    }
    else if ( !StriCmp(p1, "damaged_fx_trail_only") )
    {
        _vhcl->damaged_fx.trail_only = parser.stol(p2, NULL, 0) != 0;
    }
    else if ( ParseDecorationFXParam(parser, p1, p2, _vhcl->decoration_fx) )
    {
    }
    else if ( !StriCmp(p1, "regen_icon") ||
              !StriCmp(p1, "drain_icon") ||
              !StriCmp(p1, "damaged_icon") ||
              !StriCmp(p1, "spawn_icon") ||
              !StriCmp(p1, "radar_icon") ||
              !StriCmp(p1, "power_icon") )
    {
        // Legacy no-op: vehicle capability icons are assigned automatically.
        // Keep accepting old script keys so vanilla/OpenNeoUA scripts still load.
    }
    else if ( !StriCmp(p1, "unit_gun_icon") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->icon = p2;
        else
            _vhcl->unit_gun_icon = p2;
    }
    else if ( !StriCmp(p1, "power") )
    {
        _vhcl->power = ClampSectorPower(parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "power_radius") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->power_radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "power_falloff") )
    {
        _vhcl->power_falloff = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "power_fx_profile") )
    {
        _vhcl->power_fx_profile = p2;
    }
    else if ( !StriCmp(p1, "spy_ui_radius") )
    {
        // Vehicle-side and intentionally runtime-active only for model = ufo.
        // Parse independently of declaration order so scripts may place model
        // before or after this key without changing the result.
        float radius = parser.stof(p2, 0);
        _vhcl->spy_ui_radius =
            std::isfinite(radius) && radius > 0.0f ? radius : 0.0f;
    }
    else if ( !StriCmp(p1, "zoom_steps") )
    {
        // -1 means the parameter is absent/invalid and preserves the current
        // OpenNeoUA UFO zoom range. Zero intentionally disables optical zoom.
        size_t parsed = 0;
        const long steps = parser.stol(p2, &parsed, 0);
        _vhcl->zoom_steps = parsed == p2.size() && steps >= 0
            ? (int)std::min<long>(steps, 10)
            : -1;
    }
    else if ( !StriCmp(p1, "damaged_force_malus") )
    {
        _vhcl->damaged_force_malus = ParseMalusPercent(p2);
    }
    else if ( !StriCmp(p1, "damaged_maxrot_malus") )
    {
        _vhcl->damaged_maxrot_malus = ParseMalusPercent(p2);
    }
    else if ( !StriCmp(p1, "damaged_mgun_shot_time_malus") )
    {
        _vhcl->damaged_mgun_shot_time_malus = ParseMalusPercent(p2);
    }
    else if ( !StriCmp(p1, "damaged_shot_time_malus") )
    {
        _vhcl->damaged_shot_time_malus = ParseMalusPercent(p2);
    }
    else if ( !StriCmp(p1, "damaged_snd_pitch") )
    {
        _vhcl->damaged_snd_pitch_multiplier = ParseSignedPitchPercent(p2);
    }
    else if ( !StriCmp(p1, "spawn_units") )
    {
        _vhcl->spawn_units = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "spawn_vehicle") )
    {
        int vehicleId = parser.stol(p2, NULL, 0);
        _vhcl->spawn_vehicle = vehicleId > 0 ? vehicleId : 0;
    }
    else if ( !StriCmp(p1, "spawn_interval") )
    {
        int interval = parser.stol(p2, NULL, 0);

        if ( interval <= 0 )
            interval = 5000;
        else if ( interval < 1000 )
            interval = 1000;

        _vhcl->spawn_interval = interval;
    }
    else if ( !StriCmp(p1, "spawn_trigger_radius") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->spawn_trigger_radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "spawn_random_pos") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->spawn_random_pos = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "spawn_offset_x") )
    {
        _vhcl->spawn_offset.x = ParseFiniteFloatOrFallback(parser, p2, 0.0f);
    }
    else if ( !StriCmp(p1, "spawn_offset_y") )
    {
        _vhcl->spawn_offset.y = ParseFiniteFloatOrFallback(parser, p2, 0.0f);
    }
    else if ( !StriCmp(p1, "spawn_offset_z") )
    {
        _vhcl->spawn_offset.z = ParseFiniteFloatOrFallback(parser, p2, 0.0f);
    }
    else if ( !StriCmp(p1, "spawn_max_active") )
    {
        int maxActive = parser.stol(p2, NULL, 0);
        _vhcl->spawn_max_active = maxActive > 0 ? maxActive : 0;
    }
    else if ( !StriCmp(p1, "spawn_count") )
    {
        int count = parser.stol(p2, NULL, 0);

        if ( count <= 0 )
            count = 1;
        else if ( count > 8 )
            count = 8;

        _vhcl->spawn_count = count;
    }
    else if ( !StriCmp(p1, "spawn_instant") )
    {
        _vhcl->spawn_instant = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "spawn_at_death_units") )
    {
        _vhcl->spawn_at_death_units = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "spawn_at_death_vehicle") )
    {
        int vehicleId = parser.stol(p2, NULL, 0);
        _vhcl->spawn_at_death_vehicle = vehicleId > 0 ? vehicleId : 0;
    }
    else if ( !StriCmp(p1, "spawn_at_death_count") )
    {
        int count = parser.stol(p2, NULL, 0);

        if ( count <= 0 )
            count = 1;
        else if ( count > 8 )
            count = 8;

        _vhcl->spawn_at_death_count = count;
    }
    else if ( !StriCmp(p1, "spawn_at_death_random_pos") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->spawn_at_death_random_pos = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "spawn_at_death_instant") )
    {
        _vhcl->spawn_at_death_instant = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "spawn_at_death_immunity_time") )
    {
        int time = parser.stol(p2, NULL, 0);
        _vhcl->spawn_at_death_immunity_time = time > 0 ? time : 0;
    }
    else if ( !StriCmp(p1, "snd_mimic_sample") )
    {
        _vhcl->snd_mimic.MainSample.Name = p2;
    }
    else if ( !StriCmp(p1, "snd_mimic_pitch") )
    {
        ParseSoundPitchRange(p2, _vhcl->snd_mimic);
    }
    else if ( !StriCmp(p1, "snd_mimic_volume") )
    {
        _vhcl->snd_mimic.volume = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_enable") )
    {
        _vhcl->proximity_defense_enable = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "proximity_defense_weapon") )
    {
        int weaponId = parser.stol(p2, NULL, 0);
        _vhcl->proximity_defense_weapon = weaponId > 0 ? weaponId : 0;
    }
    else if ( !StriCmp(p1, "proximity_defense_trigger_radius") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->proximity_defense_trigger_radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "proximity_defense_interval") )
    {
        int interval = parser.stol(p2, NULL, 0);
        _vhcl->proximity_defense_interval = interval > 0 ? interval : 1000;
    }
    else if ( !StriCmp(p1, "proximity_defense_shots") )
    {
        int shots = parser.stol(p2, NULL, 0);
        _vhcl->proximity_defense_shots = shots > 0 ? shots : 1;
    }
    else if ( !StriCmp(p1, "proximity_defense_vp_launch") )
    {
        int vp = parser.stol(p2, NULL, 0);
        _vhcl->proximity_defense_vp_launch = vp > 0 ? vp : -1;
    }
    else if ( !StriCmp(p1, "proximity_defense_3ds_launch") )
    {
        _vhcl->proximity_defense_3ds_launch = p2;
    }
    else if ( !StriCmp(p1, "proximity_defense_base_launch") )
    {
        _vhcl->proximity_defense_base_launch = p2;
    }
    else if ( !StriCmp(p1, "proximity_defense_fire_mode") )
    {
        if ( !StriCmp(p2, "sequential") )
            _vhcl->proximity_defense_fire_mode = 1;
        else
            _vhcl->proximity_defense_fire_mode = 0;
    }
    else if ( !StriCmp(p1, "proximity_defense_sequence_delay") )
    {
        int delay = parser.stol(p2, NULL, 0);
        _vhcl->proximity_defense_sequence_delay = delay > 0 ? delay : 100;
    }
    else if ( !StriCmp(p1, "proximity_defense_mode") )
    {
        _vhcl->proximity_defense_mode = !StriCmp(p2, "at_death") ? 1 : 0;
    }
    else if ( !StriCmp(p1, "proximity_defense_horizontal_angle") )
    {
        float angleMin = 0.0f;
        float angleMax = 360.0f;
        _vhcl->proximity_defense_horizontal_angle_set =
            ParseScriptFloatRange(p2, angleMin, angleMax);
        _vhcl->proximity_defense_horizontal_angle_min = angleMin;
        _vhcl->proximity_defense_horizontal_angle_max = angleMax;
    }
    else if ( !StriCmp(p1, "proximity_defense_vertical_angle") )
    {
        float angleMin = -10.0f;
        float angleMax = 45.0f;
        _vhcl->proximity_defense_vertical_angle_set =
            ParseScriptFloatRange(p2, angleMin, angleMax);
        _vhcl->proximity_defense_vertical_angle_min = angleMin;
        _vhcl->proximity_defense_vertical_angle_max = angleMax;
    }
    else if ( !StriCmp(p1, "max_active_at_once") )
    {
        _vhcl->max_active_at_once = ParsePositiveIntOrZero(p2);
    }
    else if ( ParseVPScaleParam(parser, "visual", p1, p2, _vhcl->visual_scale) )
    {
    }
    else if ( ParseTintParam(parser, "visual_tint", p1, p2, _vhcl->visual_tint) )
    {
    }
    else if ( ParseTintParam(parser, "mimic_tint", p1, p2, _vhcl->mimic_tint) )
    {
    }
    else if ( ParseWireframeTintParam(parser, p1, p2, _vhcl->wireframe_tint) )
    {
    }
    else if ( ParseVPRotationParam(parser, "visual", p1, p2, _vhcl->visual_rotation) )
    {
    }
    else if ( ParseVPSpinParam(parser, "visual", p1, p2, _vhcl->visual_spin) )
    {
    }
    else if ( !StriCmp(p1, "invulnerable") )
    {
        _vhcl->invulnerable = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "type_icon") )
    {
        _vhcl->type_icon = p2[0];
    }
    else if ( !StriCmp(p1, "dest_fx") )
    {
        Stok stok(p2, " _");
        std::string fx_type, pp1, pp2, pp3, pp4;

        if ( stok.GetNext(&fx_type) && stok.GetNext(&pp1) && stok.GetNext(&pp2) && stok.GetNext(&pp3) && stok.GetNext(&pp4) )
        {
            _vhcl->dest_fx.emplace_back();
            DestFX &dfx = _vhcl->dest_fx.back();
            dfx.Type = DestFX::ParseTypeName(fx_type);

            if (dfx.Type == DestFX::FX_NONE)
                return ScriptParser::RESULT_BAD_DATA;

            dfx.ModelID = parser.stol(pp1, NULL, 0);
            dfx.Pos.x = parser.stof(pp2, 0);
            dfx.Pos.y = parser.stof(pp3, 0);
            dfx.Pos.z = parser.stof(pp4, 0);

            std::string pp5;
            if ( stok.GetNext(&pp5) )
            {
                if (parser.stol(pp5, NULL, 0) != 0 )
                    dfx.Accel = true;
                else
                    dfx.Accel = false;
            }
        }
        else
        {
            return ScriptParser::RESULT_BAD_DATA;
        }
    }
    else if ( !StriCmp(p1, "ext_dest_fx") || !StriCmp(p1, "extended_dest_fx") )
    {
        Stok stok(p2, " _");
        std::string fx_type, pp1, pp2, pp3, pp4;

        if ( stok.GetNext(&fx_type) && stok.GetNext(&pp1) && stok.GetNext(&pp2) && stok.GetNext(&pp3) && stok.GetNext(&pp4) )
        {
            _vhcl->ExtDestroyFX.emplace_back();

            DestFX &dfx = _vhcl->ExtDestroyFX.back();

            dfx.Type = DestFX::ParseTypeName(fx_type);

            if (dfx.Type == DestFX::FX_NONE)
                return ScriptParser::RESULT_BAD_DATA;

            dfx.ModelID = parser.stol(pp1, NULL, 0);
            dfx.Pos.x = parser.stof(pp2, 0);
            dfx.Pos.y = parser.stof(pp3, 0);
            dfx.Pos.z = parser.stof(pp4, 0);

            std::string pp5;
            if ( stok.GetNext(&pp5) )
            {
                if (parser.stol(pp5, NULL, 0) != 0 )
                    dfx.Accel = true;
                else
                    dfx.Accel = false;
            }
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "weapon") )
    {
        _vhcl->weapon = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "weapon_2") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->extra_weapons[0] = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "weapon_3") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->extra_weapons[1] = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "weapon_4") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->extra_weapons[2] = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "weapon_player_switch_mode") )
    {
        if ( !StriCmp(p2, "random") )
            _vhcl->weapon_player_switch_mode = TVhclProto::WEAPON_PLAYER_SWITCH_MODE_RANDOM;
        else if ( !StriCmp(p2, "manual") )
            _vhcl->weapon_player_switch_mode = TVhclProto::WEAPON_PLAYER_SWITCH_MODE_MANUAL;
        else if ( !StriCmp(p2, "sequence") )
            _vhcl->weapon_player_switch_mode = TVhclProto::WEAPON_PLAYER_SWITCH_MODE_SEQUENCE;
        else
        {
            _vhcl->weapon_player_switch_mode = TVhclProto::WEAPON_PLAYER_SWITCH_MODE_SEQUENCE;
            ypa_log_out("WARNING: vehicle %d unknown weapon_player_switch_mode '%s'; sequence used.\n",
                        _vhclID, p2.c_str());
        }
    }
    else if ( !StriCmp(p1, "weapon_ai_switch_mode") )
    {
        if ( !StriCmp(p2, "random") )
            _vhcl->weapon_ai_switch_mode = TVhclProto::WEAPON_AI_SWITCH_MODE_RANDOM;
        else if ( !StriCmp(p2, "smart") )
            _vhcl->weapon_ai_switch_mode = TVhclProto::WEAPON_AI_SWITCH_MODE_SMART;
        else if ( !StriCmp(p2, "sequence") )
            _vhcl->weapon_ai_switch_mode = TVhclProto::WEAPON_AI_SWITCH_MODE_SEQUENCE;
        else
        {
            _vhcl->weapon_ai_switch_mode = TVhclProto::WEAPON_AI_SWITCH_MODE_SEQUENCE;
            ypa_log_out("WARNING: vehicle %d unknown weapon_ai_switch_mode '%s'; sequence used.\n",
                        _vhclID, p2.c_str());
        }
    }
    else if ( !StriCmp(p1, "mgun") )
    {
        _vhcl->mgun = parser.stol(p2, NULL, 0);
        _vhcl->mgun_set = true;
    }
    else if ( !StriCmp(p1, "num_mguns") )
    {
        int numMguns = parser.stol(p2, NULL, 0);
        _vhcl->num_mguns = numMguns > 0 ? numMguns : 1;
    }
    else if ( !StriCmp(p1, "mgun_shot_time") )
    {
        _vhcl->mgun_shot_time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mgun_shot_time_user") )
    {
        _vhcl->mgun_shot_time_user = parser.stol(p2, NULL, 0);
        if ( _vhcl->mgun_shot_time_user < 0 )
            _vhcl->mgun_shot_time_user = 0;
    }
    else if ( !StriCmp(p1, "mgun_name") )
    {
        _vhcl->mgun_name = p2;
    }
    else if ( !StriCmp(p1, "mgun_wireframe") )
    {
        if ( _vhcl->mgun_wireframe )
            _vhcl->mgun_wireframe->Delete();

        _vhcl->mgun_wireframe = Nucleus::CInit<NC_STACK_sklt>(
            {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "mgun_recoil") )
    {
        float intensity = parser.stof(p2, 0);
        if ( !std::isfinite(intensity) || intensity < 0.0f )
            intensity = 0.0f;
        else if ( intensity > 10.0f )
            intensity = 10.0f;
        _vhcl->mgun_recoil = intensity;
    }
    else if ( !StriCmp(p1, "mgun_recoil_cockpit") )
    {
        float intensity = parser.stof(p2, 0);
        if ( !std::isfinite(intensity) || intensity < 0.0f )
            intensity = 0.0f;
        else if ( intensity > 10.0f )
            intensity = 10.0f;
        _vhcl->mgun_recoil_cockpit = intensity;
    }
    else if ( ParseMeshTracerParam(parser, p1, p2, _vhcl->mgun_tracer, "mgun_mesh_tracer_") )
    {
        // Every MGUN path, including model = gun/module + gun_type = mg,
        // uses the dedicated mgun_mesh_tracer_* authoring namespace while
        // reusing the same shared tracer config and renderer as physical Weapons.
    }
    else if ( !StriCmp(p1, "mgun_decal_enable") )
    {
        size_t parsed = 0;
        const long enabled = parser.stol(p2, &parsed, 0);
        _vhcl->mgun_decal_enable = parsed == p2.size() && enabled == 1;
        _vhcl->mgun_decal.mode = World::TChainFXConfig::MODE_GROUND_DECAL;
        _vhcl->mgun_decal.trigger = World::TChainFXConfig::TRIGGER_IMPACT_WORLD;
    }
    else if ( !StriCmp(p1, "mgun_decal_texture") )
        _vhcl->mgun_decal.ground_decal_texture = p2;
    else if ( ParseBoundedIntegerParam("mgun_decal_points", p1, p2,
                                       3, 32, 12, _vhcl->mgun_decal.ground_decal_points) )
    {
    }
    else if ( !StriCmp(p1, "mgun_decal_jaggedness") )
    {
        int jaggedness = 35;
        ParseBoundedIntegerParam("mgun_decal_jaggedness", p1, p2,
                                 0, 100, 35, jaggedness);
        _vhcl->mgun_decal.ground_decal_jaggedness = (float)jaggedness / 100.0f;
    }
    else if ( !StriCmp(p1, "mgun_decal_size") )
    {
        size_t parsed = 0;
        const float size = parser.stof(p2, &parsed);
        _vhcl->mgun_decal.ground_decal_size =
            parsed == p2.size() && std::isfinite(size) && size > 0.0f ? size : 0.0f;
    }
    else if ( ParseTintParam(parser, "mgun_decal_tint", p1, p2,
                             _vhcl->mgun_decal.ground_decal_tint, true) )
    {
    }
    else if ( !StriCmp(p1, "mgun_decal_random_rotation") )
        _vhcl->mgun_decal.ground_decal_random_rotation = p2 == "1";
    else if ( !StriCmp(p1, "mgun_decal_edge_fade") )
    {
        size_t parsed = 0;
        const float value = parser.stof(p2, &parsed);
        _vhcl->mgun_decal.ground_decal_edge_fade =
            parsed == p2.size() && std::isfinite(value)
            ? std::max(0.0f, std::min(value, 10.0f)) : 0.0f;
    }
    else if ( !StriCmp(p1, "mgun_decal_duration") )
        _vhcl->mgun_decal.duration = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "mgun_decal_fade_in") )
        _vhcl->mgun_decal.fade_in = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "mgun_decal_fade_out") )
        _vhcl->mgun_decal.fade_out = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "mgun_vp_dead") )
    {
        _vhcl->mgun_vp_dead = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mgun_vp_megadeth") )
    {
        _vhcl->mgun_vp_megadeth = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mgun_3ds_dead") )
    {
        _vhcl->mgun_3ds_dead = p2;
    }
    else if ( !StriCmp(p1, "mgun_3ds_megadeth") )
    {
        _vhcl->mgun_3ds_megadeth = p2;
    }
    else if ( !StriCmp(p1, "mgun_base_dead") )
    {
        _vhcl->mgun_base_dead = p2;
    }
    else if ( !StriCmp(p1, "mgun_base_megadeth") )
    {
        _vhcl->mgun_base_megadeth = p2;
    }
    else if ( !StriCmp(p1, "mgun_power") )
    {
        _vhcl->mgun_power = parser.stof(p2, 0);
        _vhcl->mgun_power_set = true;
    }
    else if ( !StriCmp(p1, "mgun_angle") )
    {
        _vhcl->mgun_angle = parser.stof(p2, 0);
        _vhcl->mgun_angle_set = true;
    }
    else if ( !StriCmp(p1, "weapon_spread_x") )
    {
        _vhcl->weapon_spread_x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "weapon_spread_y") )
    {
        _vhcl->weapon_spread_y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "weapon_arc_x") )
    {
        float value = parser.stof(p2, 0);
        _vhcl->weapon_arc_x = std::isfinite(value) && value > 0.0f ? value : 0.0f;
    }
    else if ( !StriCmp(p1, "weapon_arc_y") )
    {
        float value = parser.stof(p2, 0);
        _vhcl->weapon_arc_y = std::isfinite(value) && value > 0.0f ? value : 0.0f;
    }
    else if ( !StriCmp(p1, "weapon_cone_xy") )
    {
        float value = parser.stof(p2, 0);
        _vhcl->weapon_cone_xy = std::isfinite(value) && value > 0.0f ? value : 0.0f;
    }
    else if ( !StriCmp(p1, "mgun_spread_x") )
    {
        _vhcl->mgun_spread_x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "mgun_spread_y") )
    {
        _vhcl->mgun_spread_y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "fire_x") )
    {
        _vhcl->fire_x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "fire_y") )
    {
        _vhcl->fire_y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "fire_z") )
    {
        _vhcl->fire_z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "fire_x_mode") )
    {
        if ( !StriCmp(p2, "sequence") )
            _vhcl->fire_x_mode = TVhclProto::FIRE_X_MODE_SEQUENCE;
        else if ( !StriCmp(p2, "random") )
            _vhcl->fire_x_mode = TVhclProto::FIRE_X_MODE_RANDOM;
        else if ( !StriCmp(p2, "salve_sequence") )
            _vhcl->fire_x_mode = TVhclProto::FIRE_X_MODE_SALVE_SEQUENCE;
        else if ( !StriCmp(p2, "salve_mirror") )
            _vhcl->fire_x_mode = TVhclProto::FIRE_X_MODE_SALVE_MIRROR;
        else
        {
            _vhcl->fire_x_mode = TVhclProto::FIRE_X_MODE_VANILLA;
            ypa_log_out("WARNING: vehicle %d unknown fire_x_mode '%s'; vanilla fire_x selection used.\n",
                        _vhclID, p2.c_str());
        }
    }
    else if ( !StriCmp(p1, "fire_x_start") )
    {
        size_t parsed = 0;
        float value = parser.stof(p2, &parsed);
        _vhcl->fire_x_start = (parsed == p2.size() && std::isfinite(value))
                            ? value
                            : std::numeric_limits<float>::quiet_NaN();
        _vhcl->fire_x_start_defined = true;
    }
    else if ( !StriCmp(p1, "fire_x_step") )
    {
        size_t parsed = 0;
        float value = parser.stof(p2, &parsed);
        _vhcl->fire_x_step = (parsed == p2.size() && std::isfinite(value))
                           ? value
                           : std::numeric_limits<float>::quiet_NaN();
        _vhcl->fire_x_step_defined = true;
    }
    else if ( !StriCmp(p1, "fire_x_slots") )
    {
        size_t parsed = 0;
        long value = parser.stol(p2, &parsed, 0);
        _vhcl->fire_x_slots = (parsed == p2.size() &&
                               value >= std::numeric_limits<int>::min() &&
                               value <= std::numeric_limits<int>::max())
                              ? (int)value
                              : 0;
        _vhcl->fire_x_slots_defined = true;
    }
    else if ( !StriCmp(p1, "cockpit_camera_offset_x") )
    {
        _vhcl->cockpit_camera_offset.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "cockpit_camera_offset_y") )
    {
        _vhcl->cockpit_camera_offset.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "cockpit_camera_offset_z") )
    {
        _vhcl->cockpit_camera_offset.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "cockpit_gun_camera_recoil") )
    {
        _vhcl->cockpit_gun_camera_recoil = ParseBoundedPositiveFiniteOrZero(
            parser, p2, 5.0f);
    }
    else if ( !StriCmp(p1, "gun_radius") )
    {
        _vhcl->gun_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "gun_power") )
    {
        _vhcl->gun_power = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "gun_angle") )
    {
        _vhcl->gun_angle = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "num_weapons") )
    {
        int previousValue = _vhcl->num_weapons;
        int minCount = 0;
        int maxCount = 0;

        if ( !ParseNumWeaponsRange(p2, minCount, maxCount) )
        {
            ypa_log_out("WARNING: vehicle %d num_weapons='%s' is invalid; expected 0 or an inclusive value/min_max in range 1-255. Keeping the previous value.\n",
                        _vhclID, p2.c_str());
            return ScriptParser::RESULT_OK;
        }

        _vhcl->num_weapons = minCount;
        _vhcl->num_weapons_min = minCount;
        _vhcl->num_weapons_max = maxCount;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                           TGemNotificationEntry::CHANGE_NUM_WEAPONS,
                                           previousValue, _vhcl->num_weapons);
    }
    else if ( !StriCmp(p1, "weapon_energy_cost") )
    {
        TAuthoredScalar value;
        _vhcl->weapon_energy_cost = 0.0f;
        _vhcl->weapon_energy_cost_defined = false;

        if ( ParseAuthoredScalar(p2, value) && value.percent &&
             std::isfinite(value.value) && value.value >= 0.0f )
        {
            _vhcl->weapon_energy_cost = std::min(value.value, 100.0f);
            _vhcl->weapon_energy_cost_defined = true;
        }
        else
        {
            ypa_log_out("WARNING: vehicle %d weapon_energy_cost='%s' is invalid; expected an explicit percentage in range 0%%-100%%. Falling back to legacy weapon energy drain.\n",
                        _vhclID, p2.c_str());
        }
    }
    else if ( !StriCmp(p1, "mgun_fire_energy_cost") )
    {
        TAuthoredScalar value;
        _vhcl->mgun_fire_energy_cost = 0.0f;
        _vhcl->mgun_fire_energy_cost_defined = false;

        if ( ParseAuthoredScalar(p2, value) && value.percent &&
             std::isfinite(value.value) && value.value >= 0.0f )
        {
            _vhcl->mgun_fire_energy_cost = std::min(value.value, 100.0f);
            _vhcl->mgun_fire_energy_cost_defined = true;
        }
        else
        {
            ypa_log_out("WARNING: vehicle %d mgun_fire_energy_cost='%s' is invalid; expected an explicit percentage in range 0%%-100%%. Falling back to legacy MGUN energy drain.\n",
                        _vhclID, p2.c_str());
        }
    }
    else if ( !StriCmp(p1, "num_weapons_snd_events") )
    {
        int value = parser.stol(p2, NULL, 0);
        _vhcl->num_weapons_snd_events =
            value > 0 ? std::min(value, 255) : 0;
    }
    else if ( !StriCmp(p1, "num_weapons_2") ||
              !StriCmp(p1, "num_weapons_3") ||
              !StriCmp(p1, "num_weapons_4") )
    {
        int sourceSlot = p1.back() - '2';
        int minCount = 0;
        int maxCount = 0;

        if ( !ParseNumWeaponsRange(p2, minCount, maxCount) )
        {
            ypa_log_out("WARNING: vehicle %d %s='%s' is invalid; expected 0/inherit or an inclusive value/min_max in range 1-255. Keeping the previous value.\n",
                        _vhclID, p1.c_str(), p2.c_str());
        }
        else
        {
            _vhcl->extra_num_weapons[sourceSlot] = minCount;
            _vhcl->extra_num_weapons_min[sourceSlot] = minCount;
            _vhcl->extra_num_weapons_max[sourceSlot] = maxCount;
        }
    }
    else if ( !StriCmp(p1, "kill_after_shot") )
    {
        _vhcl->kill_after_shot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "job_fighthelicopter") )
    {
        _vhcl->job_fighthelicopter = parser.stoi(p2);
        _vhcl->job_fighthelicopter_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightflyer") )
    {
        _vhcl->job_fightflyer = parser.stoi(p2);
        _vhcl->job_fightflyer_defined = true;
    }
    else if ( !StriCmp(p1, "job_fighttank") )
    {
        _vhcl->job_fighttank = parser.stoi(p2);
        _vhcl->job_fighttank_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightrobo") )
    {
        _vhcl->job_fightrobo = parser.stoi(p2);
        _vhcl->job_fightrobo_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightplane") )
    {
        _vhcl->job_fightplane = parser.stoi(p2);
        _vhcl->job_fightplane_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightcruiser") )
    {
        _vhcl->job_fightcruiser = parser.stoi(p2);
        _vhcl->job_fightcruiser_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightglider") )
    {
        _vhcl->job_fightglider = parser.stoi(p2);
        _vhcl->job_fightglider_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightzeppelin") )
    {
        _vhcl->job_fightzeppelin = parser.stoi(p2);
        _vhcl->job_fightzeppelin_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightufo") )
    {
        _vhcl->job_fightufo = parser.stoi(p2);
        _vhcl->job_fightufo_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightcar") )
    {
        _vhcl->job_fightcar = parser.stoi(p2);
        _vhcl->job_fightcar_defined = true;
    }
    else if ( !StriCmp(p1, "job_fightgun") )
    {
        _vhcl->job_fightgun = parser.stoi(p2);
        _vhcl->job_fightgun_defined = true;
    }
    else if ( !StriCmp(p1, "job_reconnoitre") )
    {
        _vhcl->job_reconnoitre = parser.stoi(p2);
        _vhcl->job_reconnoitre_defined = true;
    }
    else if ( !StriCmp(p1, "job_conquer") )
    {
        _vhcl->job_conquer = parser.stoi(p2);
        _vhcl->job_conquer_defined = true;
    }
    else if ( !StriCmp(p1, "gun_side_angle") )
    {
        _vhcl->initParams.Add(NC_STACK_ypagun::GUN_ATT_SIDEANGLE, (int32_t)parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "gun_up_angle") )
    {
        _vhcl->initParams.Add(NC_STACK_ypagun::GUN_ATT_UPANGLE, (int32_t)parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "gun_down_angle") )
    {
        _vhcl->initParams.Add(NC_STACK_ypagun::GUN_ATT_DOWNANGLE, (int32_t)parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "gun_type") )
    {
        int gun_type = NC_STACK_ypagun::GUN_TYPE_REAL;
        bool recognized = true;
        if ( !StriCmp(p2, "flak") )
        {
            gun_type = NC_STACK_ypagun::GUN_TYPE_REAL;
        }
        else if ( !StriCmp(p2, "mg") )
        {
            gun_type = NC_STACK_ypagun::GUN_TYPE_PROTO;
        }
        else if ( !StriCmp(p2, "dummy") )
        {
            gun_type = NC_STACK_ypagun::GUN_TYPE_DUMMY;
        }
        else if ( !StriCmp(p2, "radar") )
        {
            gun_type = NC_STACK_ypagun::GUN_TYPE_RADAR;
        }
        else if ( !StriCmp(p2, "power") )
        {
            gun_type = NC_STACK_ypagun::GUN_TYPE_POWER;
        }
        else
        {
            recognized = false;
        }

        if ( recognized )
            _vhcl->initParams.Add(NC_STACK_ypagun::GUN_ATT_FIRETYPE, (int32_t)gun_type);
    }
    else if ( !StriCmp(p1, "gun_does_not_fall") )
    {
        if ( _vhcl->model_id == BACT_TYPES_GUN )
            _vhcl->initParams.Add(NC_STACK_ypagun::GUN_ATT_NO_FALL, (int32_t)1);
    }
    else if ( !StriCmp(p1, "kamikaze") )
    {
        _vhcl->initParams.Add(NC_STACK_ypacar::CAR_ATT_KAMIKAZE, (int32_t)1);

        _vhcl->initParams.Add(NC_STACK_ypacar::CAR_ATT_BLAST, (int32_t)parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "wireframe") )
    {
        if ( _vhcl->wireframe )
            _vhcl->wireframe->Delete();

        _vhcl->wireframe = Nucleus::CInit<NC_STACK_sklt>( {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "hud_wireframe") )
    {
        if ( _vhcl->hud_wireframe )
            _vhcl->hud_wireframe->Delete();

        _vhcl->hud_wireframe = Nucleus::CInit<NC_STACK_sklt>( {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "mg_wireframe") )
    {
        if ( _vhcl->mg_wireframe )
            _vhcl->mg_wireframe->Delete();

        _vhcl->mg_wireframe = Nucleus::CInit<NC_STACK_sklt>( {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "wpn_wireframe_1") )
    {
        if ( _vhcl->wpn_wireframe_1 )
            _vhcl->wpn_wireframe_1->Delete();

        _vhcl->wpn_wireframe_1 = Nucleus::CInit<NC_STACK_sklt>( {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "wpn_wireframe_2") )
    {
        if ( _vhcl->wpn_wireframe_2 )
            _vhcl->wpn_wireframe_2->Delete();

        _vhcl->wpn_wireframe_2 = Nucleus::CInit<NC_STACK_sklt>( {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "vo_type") )
    {
        _vhcl->vo_type = parser.stol(p2, NULL, 16);
    }
    else if ( p1.size() > 13 && !StriCmp(p1.substr(0, 13), "speech_event_") )
    {
        std::string eventKey = p1.substr(13);
        if ( eventKey.empty() || p2.empty() )
            return ScriptParser::RESULT_BAD_DATA;

        std::transform(eventKey.begin(), eventKey.end(), eventKey.begin(),
                       [](unsigned char ch) { return (char)std::tolower(ch); });
        _vhcl->speech_events[eventKey] = p2;
    }
    else if ( !StriCmp(p1, "max_pitch") )
    {
        _vhcl->max_pitch = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "scale_fx") )
    {
        Stok stok(p2, "_");
        std::string pp0, pp1, pp2, pp3;

        if ( stok.GetNext(&pp0) && stok.GetNext(&pp1) && stok.GetNext(&pp2) && stok.GetNext(&pp3) )
        {
            ResetVehicleScaleFX(_vhcl);

            _vhcl->scale_fx_p0 = parser.stof(pp0, 0);
            _vhcl->scale_fx_p1 = parser.stof(pp1, 0);
            _vhcl->scale_fx_p2 = parser.stof(pp2, 0);
            _vhcl->scale_fx_p3 = parser.stol(pp3, NULL, 0);

            int tmp = 0;
            while ( stok.GetNext(&pp0) )
            {
                _vhcl->scale_fx_pXX[tmp] = parser.stol(pp0, NULL, 0);
                tmp++;
            }
        }
    }
    else if ( !StriCmp(p1, "begin_chain_fx") )
    {
        return ParseVehicleChainFXBlock(parser, _vhcl);
    }
    else if ( !StriCmp(p1, "robo_data_slot") )
    {
    }
    else if ( !StriCmp(p1, "robo_num_guns") )
    {
        int cnt = parser.stol(p2, NULL, 0);

        if ( cnt < 0 )
            cnt = 0;
        else if ( cnt > (int)ROBO_GUN_MAX_COUNT )
            cnt = ROBO_GUN_MAX_COUNT;

        robo->guns.resize(cnt);

        if ( _vhcl->model_id == BACT_TYPES_ROBO && _unitGunID >= cnt )
            _unitGunID = cnt - 1;
    }
    else if ( !StriCmp(p1, "robo_act_gun") )
    {
        _gunID = parser.stol(p2, NULL, 0);

        // robo_* and unit_* select the same native Robo gun slot when parsing
        // a Host Station, so mixed syntax remains deterministic: later fields
        // overwrite the same TRoboGun entry instead of creating duplicates.
        if ( _vhcl->model_id == BACT_TYPES_ROBO )
            _unitGunID = _gunID;
    }
    else if ( !StriCmp(p1, "robo_gun_pos_x") )
    {
        robo->guns[ _gunID ].pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_pos_y") )
    {
        robo->guns[ _gunID ].pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_pos_z") )
    {
        robo->guns[ _gunID ].pos.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_dir_x") )
    {
        robo->guns[ _gunID ].dir.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_dir_y") )
    {
        robo->guns[ _gunID ].dir.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_dir_z") )
    {
        robo->guns[ _gunID ].dir.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_type") )
    {
        robo->guns[ _gunID ].robo_gun_type = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "robo_gun_name") )
    {
        robo->guns[ _gunID ].robo_gun_name = p2;
    }
    else if ( !StriCmp(p1, "unit_num_guns") )
    {
        int cnt = parser.stol(p2, NULL, 0);

        if ( cnt < 0 )
            cnt = 0;
        else if ( cnt > (int)ROBO_GUN_MAX_COUNT )
            cnt = ROBO_GUN_MAX_COUNT;

        if ( _vhcl->model_id == BACT_TYPES_ROBO )
        {
            robo->guns.resize(cnt);

            if ( _gunID >= cnt )
                _gunID = cnt - 1;
        }
        else
        {
            _vhcl->unit_guns.resize(cnt);
        }

        if ( _unitGunID >= cnt )
            _unitGunID = cnt - 1;
    }
    else if ( !StriCmp(p1, "unit_act_gun") )
    {
        _unitGunID = parser.stol(p2, NULL, 0);

        if ( _unitGunID < 0 )
            _unitGunID = 0;

        if ( _unitGunID >= (int)ROBO_GUN_MAX_COUNT )
            _unitGunID = ROBO_GUN_MAX_COUNT - 1;

        if ( _vhcl->model_id == BACT_TYPES_ROBO )
        {
            _gunID = _unitGunID;

            if ( (size_t)_unitGunID >= robo->guns.size() )
                robo->guns.resize(_unitGunID + 1);
        }
        else if ( (size_t)_unitGunID >= _vhcl->unit_guns.size() )
        {
            _vhcl->unit_guns.resize(_unitGunID + 1);
        }
    }
    else if ( !StriCmp(p1, "unit_gun_pos_x") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_pos_y") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_pos_z") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->pos.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_dir_x") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->dir.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_dir_y") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->dir.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_dir_z") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->dir.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_type") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->robo_gun_type = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unit_gun_name") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->robo_gun_name = p2;
    }
    else if ( !StriCmp(p1, "unit_gun_protect") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->protect = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "robo_dock_x") )
    {
        robo->dock.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_dock_y") )
    {
        robo->dock.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_dock_z") )
    {
        robo->dock.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_coll_num") )
    {
        int cnt = parser.stol(p2, NULL, 0);

        if ( cnt < 0 )
            cnt = 0;
        else if ( cnt > (int)UNIT_COLL_MAX_COUNT )
            cnt = UNIT_COLL_MAX_COUNT;

        robo->coll.roboColls.resize(cnt);

        if ( _collID >= cnt )
            _collID = cnt - 1;
    }
    else if ( !StriCmp(p1, "robo_coll_act") )
    {
        _collID = parser.stol(p2, NULL, 0);

        if ( _collID < 0 )
            _collID = 0;

        if ( _collID >= (int)UNIT_COLL_MAX_COUNT )
            _collID = UNIT_COLL_MAX_COUNT - 1;

        if ( (size_t)_collID >= robo->coll.roboColls.size() )
            robo->coll.roboColls.resize(_collID + 1);
    }
    else if ( !StriCmp(p1, "robo_coll_radius") )
    {
        if (TRoboColl *c = getRoboColl())
            c->robo_coll_radius = std::max(0.0f, ParseFiniteCollisionFloat(parser, p2));
    }
    else if ( !StriCmp(p1, "robo_coll_x") )
    {
        if (TRoboColl *c = getRoboColl())
            c->coll_pos.x = ParseFiniteCollisionFloat(parser, p2);
    }
    else if ( !StriCmp(p1, "robo_coll_y") )
    {
        if (TRoboColl *c = getRoboColl())
            c->coll_pos.y = ParseFiniteCollisionFloat(parser, p2);
    }
    else if ( !StriCmp(p1, "robo_coll_z") )
    {
        if (TRoboColl *c = getRoboColl())
            c->coll_pos.z = ParseFiniteCollisionFloat(parser, p2);
    }
    else if ( !StriCmp(p1, "robo_viewer_x") )
    {
        robo->viewer.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_viewer_y") )
    {
        robo->viewer.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_viewer_z") )
    {
        robo->viewer.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_viewer_max_up") )
    {
        robo->robo_viewer_max_up = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_viewer_max_down") )
    {
        robo->robo_viewer_max_down = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_viewer_max_side") )
    {
        robo->robo_viewer_max_side = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_does_twist") )
    {
        _vhcl->initParams.Add(NC_STACK_yparobo::ROBO_ATT_WAIT_ROTATE, (int32_t)1);
    }
    else if ( !StriCmp(p1, "robo_does_flux") )
    {
        _vhcl->initParams.Add(NC_STACK_yparobo::ROBO_ATT_WAIT_SWAY, (int32_t)1);
    }
    else if ( !StriCmp(p1, "hidden") )
    {
        _vhcl->hidden = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "invisible") )
    {
        // OpenNeoUA custom: vehicle-only total-stealth-until-first-attack flag.
        // Deliberately separate from the legacy "hidden"/"unhide_radar" system.
        _vhcl->invisible = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "invisible_reveal_vp") )
    {
        _vhcl->invisible_reveal_vp = (int16_t)parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "invisible_reveal_3ds") )
    {
        _vhcl->invisible_reveal_3ds = p2;
    }
    else if ( !StriCmp(p1, "invisible_reveal_base") )
    {
        _vhcl->invisible_reveal_base = p2;
    }
    else if ( !StriCmp(p1, "unhide_radar") )
    {
        _vhcl->unhideRadar = parser.stol(p2, NULL, 0);

        if (_vhcl->unhideRadar < 0)
            _vhcl->unhideRadar = 0;
        else if (_vhcl->unhideRadar > _vhcl->radar)
            _vhcl->unhideRadar = _vhcl->radar + 1;
    }
    else if ( !StriCmp(p1, "add_unhide_radar") )
    {
        int previousValue = _vhcl->unhideRadar;
        _vhcl->unhideRadar += parser.stol(p2, NULL, 0);

        if (_vhcl->unhideRadar < 0)
            _vhcl->unhideRadar = 0;
        else if (_vhcl->unhideRadar > _vhcl->radar)
            _vhcl->unhideRadar = _vhcl->radar + 1;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_VEHICLE, _vhclID,
                                           TGemNotificationEntry::CHANGE_UNHIDE_RADAR,
                                           previousValue, _vhcl->unhideRadar);
    }
    else
        return ParseSndFX(parser, p1, p2);

    return ScriptParser::RESULT_OK;
}


bool VhclProtoParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( !StriCmp(word, "new_vehicle") )
    {
        _isModify = false;
        _roboTmp = TRoboProto();
        _gunID = -1;
        _unitGunID = -1;
        _collID = -1;
        _vhclID = _forcedVhclID >= 0 ? _forcedVhclID : parser.stol(opt, NULL, 0);
        _vhcl = &_o._vhclProtos.at(_vhclID);

        *_vhcl = TVhclProto();

        _vhcl->Index = _vhclID;

        _vhcl->model_id = BACT_TYPES_TANK;
        _vhcl->weapon = -1;
        _vhcl->extra_weapons = {0, 0, 0};
        _vhcl->extra_num_weapons = {0, 0, 0};
        _vhcl->extra_num_weapons_min = {0, 0, 0};
        _vhcl->extra_num_weapons_max = {0, 0, 0};
        _vhcl->weapon_player_switch_mode = TVhclProto::WEAPON_PLAYER_SWITCH_MODE_SEQUENCE;
        _vhcl->weapon_ai_switch_mode = TVhclProto::WEAPON_AI_SWITCH_MODE_SEQUENCE;
        _vhcl->mgun = -1;
        _vhcl->mgun_set = false;
        _vhcl->num_mguns = 1;
        _vhcl->mgun_shot_time = 0;
        _vhcl->mgun_shot_time_user = 0;
        _vhcl->mgun_recoil = 0.0f;
        _vhcl->mgun_recoil_cockpit = 0.0f;
        _vhcl->mgun_tracer = TWeaponTracerConfig();
        _vhcl->mgun_decal_enable = false;
        _vhcl->mgun_decal = World::TChainFXConfig();
        _vhcl->mgun_decal.mode = World::TChainFXConfig::MODE_GROUND_DECAL;
        _vhcl->mgun_decal.trigger = World::TChainFXConfig::TRIGGER_IMPACT_WORLD;
        _vhcl->mgun_vp_dead = 0;
        _vhcl->mgun_vp_megadeth = 0;
        _vhcl->mgun_power = 0.0;
        _vhcl->mgun_angle = 0.0;
        _vhcl->mgun_power_set = false;
        _vhcl->mgun_angle_set = false;
        _vhcl->mgun_3ds_dead.clear();
        _vhcl->mgun_3ds_megadeth.clear();
        _vhcl->mgun_base_dead.clear();
        _vhcl->mgun_base_megadeth.clear();
        _vhcl->weapon_spread_x = 0.0;
        _vhcl->weapon_spread_y = 0.0;
        _vhcl->weapon_arc_x = 0.0;
        _vhcl->weapon_arc_y = 0.0;
        _vhcl->weapon_cone_xy = 0.0;
        _vhcl->mgun_spread_x = 0.0;
        _vhcl->mgun_spread_y = 0.0;
        _vhcl->type_icon = 65;
        _vhcl->vp_normal = 0;
        _vhcl->vp_fire = 1;
        _vhcl->vp_megadeth = 2;
        _vhcl->vp_wait = 3;
        _vhcl->vp_dead = 4;
        _vhcl->vp_genesis = 5;
        _vhcl->visual_3ds = TExternalVisualSet();
        _vhcl->visual_base = TExternalVisualSet();
        _vhcl->visual_scale = vec3d(1.0, 1.0, 1.0);
        _vhcl->visual_rotation = vec3d(0.0, 0.0, 0.0);
        _vhcl->visual_spin = vec3d(0.0, 0.0, 0.0);
        _vhcl->visual_tint = TVisualTint();
        _vhcl->wireframe_tint = TVisualTint();
        _vhcl->damaged_fx = TDamagedFXConfig();
        _vhcl->unit_gun_icon.clear();
        _vhcl->power = 0;
        _vhcl->power_radius = 0.0;
        _vhcl->power_falloff = 1;
        _vhcl->power_fx_profile.clear();
        _vhcl->spy_ui_radius = 0.0f;
        _vhcl->zoom_steps = -1;
        _vhcl->damaged_force_malus = 0.0;
        _vhcl->damaged_maxrot_malus = 0.0;
        _vhcl->damaged_mgun_shot_time_malus = 0.0;
        _vhcl->damaged_shot_time_malus = 0.0;
        _vhcl->damaged_snd_pitch_multiplier = 1.0;
        _vhcl->spawn_units = 0;
        _vhcl->spawn_vehicle = 0;
        _vhcl->spawn_interval = 5000;
        _vhcl->spawn_trigger_radius = 0.0;
        _vhcl->spawn_random_pos = 0.0;
        _vhcl->spawn_offset = vec3d(0.0, 0.0, 0.0);
        _vhcl->spawn_max_active = 0;
        _vhcl->spawn_count = 1;
        _vhcl->spawn_instant = 0;
        _vhcl->spawn_at_death_units = 0;
        _vhcl->spawn_at_death_vehicle = 0;
        _vhcl->spawn_at_death_count = 1;
        _vhcl->spawn_at_death_random_pos = 0.0;
        _vhcl->spawn_at_death_instant = 0;
        _vhcl->spawn_at_death_immunity_time = 0;
        _vhcl->proximity_defense_enable = 0;
        _vhcl->proximity_defense_weapon = 0;
        _vhcl->proximity_defense_trigger_radius = 0.0;
        _vhcl->proximity_defense_interval = 1000;
        _vhcl->proximity_defense_shots = 12;
        _vhcl->proximity_defense_vp_launch = -1;
        _vhcl->proximity_defense_3ds_launch.clear();
        _vhcl->proximity_defense_base_launch.clear();
        _vhcl->proximity_defense_fire_mode = 0;
        _vhcl->proximity_defense_sequence_delay = 100;
        _vhcl->proximity_defense_mode = 0;
        _vhcl->proximity_defense_horizontal_angle_set = false;
        _vhcl->proximity_defense_horizontal_angle_min = 0.0;
        _vhcl->proximity_defense_horizontal_angle_max = 360.0;
        _vhcl->proximity_defense_vertical_angle_set = false;
        _vhcl->proximity_defense_vertical_angle_min = -10.0;
        _vhcl->proximity_defense_vertical_angle_max = 45.0;
        _vhcl->max_active_at_once = 0;
        _vhcl->shield = 50;
        _vhcl->energy = 10000;
        _vhcl->mimic_energy_cost = 0;
        _vhcl->mimic_energy_cost_min = 0;
        _vhcl->mimic_energy_cost_max = 0;
        _vhcl->adist_sector = 800.0;
        _vhcl->adist_bact = 650.0;
        _vhcl->sdist_sector = 200.0;
        _vhcl->sdist_bact = 100.0;
        _vhcl->radar = 1;
        _vhcl->kill_after_shot = 0;
        _vhcl->push_resistance = 0.0;
        _vhcl->has_push_resistance = false;
        _vhcl->push_at_death_force = 0.0f;
        _vhcl->push_at_death_radius = 0.0f;
        _vhcl->push_at_death_falloff = 0;
        _vhcl->mass = 400.0;
        _vhcl->force = 5000.0;
        _vhcl->airconst = 80.0;
        _vhcl->maxrot = 0.8;
        _vhcl->height = 150.0;
        _vhcl->radius = 25.0;
        _vhcl->radius_defined = false;
        _vhcl->overeof = 25.0;
        _vhcl->vwr_radius = 30.0;
        _vhcl->vwr_overeof = 30.0;
        _vhcl->cockpit_camera_offset = vec3d(0.0, 0.0, 0.0);
        _vhcl->cockpit_gun_camera_recoil = 0.0f;
        _vhcl->gun_power = 4000.0;
        _vhcl->gun_radius = 5.0;
        _vhcl->max_pitch = -1.0;
        _vhcl->job_fightflyer = 0;
        _vhcl->job_fighthelicopter = 0;
        _vhcl->job_fightrobo = 0;
        _vhcl->job_fighttank = 0;
        _vhcl->job_reconnoitre = 0;
        _vhcl->job_conquer = 0;

        for (auto &x : _vhcl->sndFX)
        {
            x.sndPrm.mag0 = 1.0;
            x.sndPrm_shk.mag0 = 1.0;
            x.sndPrm_shk.mute = 0.02;
            x.sndPrm_shk.pos.x = 0.2;
            x.sndPrm_shk.pos.y = 0.2;
            x.sndPrm_shk.pos.z = 0.2;
            x.volume = 120;
            x.sndPrm.time = 1000;
            x.sndPrm_shk.time = 1000;
        }

        // Pickup audio is global in Nucleus.ini rather than authored per Vehicle.
        // The event remains attached to the collecting unit so playback stays positional.
        TVhclSound &pickupSnd = _vhcl->sndFX[TVhclProto::SND_PICKUP];
        pickupSnd.MainSample.Name =
            System::IniConf::GamePlasmaSndPickupSample.Get<std::string>();
        const int pickupVolume =
            System::IniConf::GamePlasmaSndPickupVolume.Get<int32_t>();
        pickupSnd.volume = pickupVolume >= 0 ? pickupVolume : 90;
        ParseSoundPitchRange(
            System::IniConf::GamePlasmaSndPickupPitch.Get<std::string>(), pickupSnd);
        // Radius remains 0 so SFXEngine uses the classic legacy distance attenuation.

        _vhcl->sndFX[TVhclProto::SND_HANDBRAKE].MainSample.Name =
            System::IniConf::GameHandBrakeSound.Get<std::string>();

        _vhcl->initParams.clear();
        _vhcl->is_mimic = 0;
        _vhcl->mimic_tint = TVisualTint();
        _vhcl->snd_mimic = TVhclSound();
        _vhcl->coll = rbcolls();
        return true;
    }
    else if ( !StriCmp(word, "modify_vehicle") )
    {
        _gunID = -1;
        _unitGunID = -1;
        _collID = -1;
        _vhclID = parser.stol(opt, NULL, 0);

        if ( _vhclID < 0 || (size_t)_vhclID >= _o._vhclProtos.size() )
        {
            ypa_log_out("WARNING: modify_vehicle ignored invalid prototype ID %d.\n", _vhclID);
            _vhcl = NULL;
            _isModify = false;
            return false;
        }

        _vhcl = &_o._vhclProtos.at(_vhclID);
        _isModify = true;

        _vhcl->Index = _vhclID;

        _o._upgradeVehicleId = _vhclID;
        return true;
    }

    return false;
}

TVhclSound *WeaponProtoParser::GetSndFxByName(const std::string &sndname)
{
    if ( !StriCmp(sndname, "normal") )
        return &_wpn->sndFXes[TWeapProto::SND_NORMAL];
    else if ( !StriCmp(sndname, "launch") )
        return &_wpn->sndFXes[TWeapProto::SND_LAUNCH];
    else if ( !StriCmp(sndname, "hit") )
        return &_wpn->sndFXes[TWeapProto::SND_HIT];

    return NULL;
}

bool WeaponProtoParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if (!StriCmp(word, "new_weapon"))
    {
        _isModify = false;
        _collID = -1;
        _wpnID = parser.stol(opt, NULL, 0);
        _wpn = &_o._weaponProtos[_wpnID];

        *_wpn = TWeapProto();

        _wpn->unitID = 4;
        _wpn->name.clear();
        _wpn->energy = 10000;
        _wpn->aoe_unit_energy = 0;
        _wpn->aoe_building_energy = 0;
        _wpn->aoe_sector_energy = 0;
        _wpn->aoe_falloff = 0;
        _wpn->aoe_unit_push = 0.0f;
        _wpn->push = 0.0f;
        _wpn->armor_penetration_targets = 0;
        _wpn->recoil = 0.0;
        _wpn->mass = 50.0;
        _wpn->force = 5000.0;
        _wpn->airconst = 50.0;
        _wpn->maxrot = 2.0;
        _wpn->radius = 20.0;
        _wpn->trigger_radius = 0.0;
        _wpn->aoe_unit_radius = 0.0;
        _wpn->aoe_building_radius = 0.0;
        _wpn->aoe_sector_radius = 0.0;
        _wpn->overeof = 10.0;
        _wpn->vwr_radius = 20.0;
        _wpn->vwr_overeof = 20.0;
        _wpn->energy_heli = 1.0;
        _wpn->energy_tank = 1.0;
        _wpn->energy_flyer = 1.0;
        _wpn->energy_robo = 1.0;
        _wpn->energy_plane = 1.0;
        _wpn->energy_cruiser = 1.0;
        _wpn->energy_glider = 1.0;
        _wpn->energy_zeppelin = 1.0;
        _wpn->energy_ufo = 1.0;
        _wpn->energy_car = 1.0;
        _wpn->energy_gun = 1.0;
        _wpn->radius_heli = 0;
        _wpn->radius_tank = 0;
        _wpn->radius_flyer = 0;
        _wpn->radius_robo = 0;
        _wpn->start_speed = 70.0;
        _wpn->grenade_arc_angle = 0.0f;
        _wpn->grenade_arc_gravity = 0.0f;
        _wpn->life_time = 20000;
        _wpn->life_time_min = 20000;
        _wpn->life_time_max = 20000;
        _wpn->life_time_nt = 0;
        _wpn->drive_time = 7000;
        _wpn->shot_time = 3000;
        _wpn->shot_time_user = 1000;
        _wpn->ramp_up_time = 0;
        _wpn->ramp_up_max_shot_time = 0;
        _wpn->salve_delay = 0;
        _wpn->salve_shots = 0;
        _wpn->multi_target = 0;
        // OpenNeoUA custom: model = laser defaults (vanilla-safe / disabled by default)
        _wpn->laser_energy_tick_time = 250;
        _wpn->laser_energy_tick_time_user = 150;
        _wpn->laser_energy_increment_rate = 0.0;
        _wpn->laser_max_energy = 0.0;
        _wpn->laser_visual_spacing = 40.0;
        _wpn->laser_chain_allow = 0;
        _wpn->laser_chain_max_jumps = 0;
        _wpn->laser_chain_radius = 0.0;
        _wpn->laser_chain_damage_mult = 1.0;
        _wpn->laser_beam_count = 1;
        _wpn->vertical_laser_enable = false;
        _wpn->vertical_laser_ai_trigger_radius = 300.0f;
        _wpn->fire_time_scale = 1.0f;
        _wpn->fire_time_scale_hp_drain.Clear();
        _wpn->vp_normal = 0;
        _wpn->vp_fire = 1;
        _wpn->vp_megadeth = 2;
        _wpn->vp_wait = 3;
        _wpn->vp_dead = 4;
        _wpn->vp_genesis = 5;
        _wpn->vp_launch = 0;
        _wpn->visual_3ds = TExternalVisualSet();
        _wpn->visual_base = TExternalVisualSet();
        _wpn->launch_scale = vec3d(1.0, 1.0, 1.0);
        _wpn->visual_scale = vec3d(1.0, 1.0, 1.0);
        _wpn->visual_rotation = vec3d(0.0, 0.0, 0.0);
        _wpn->visual_spin = vec3d(0.0, 0.0, 0.0);
        _wpn->spiral_speed = 0.0f;
        _wpn->spiral_radius = 0.0f;
        _wpn->chaos_factor = 0.0f;
        _wpn->chaos_radius = 0.0f;
        _wpn->visual_tint = TVisualTint();
        _wpn->vp_trail_scale = vec3d(1.0, 1.0, 1.0);
        _wpn->vp_trail_spin = vec3d(0.0, 0.0, 0.0);
        _wpn->vp_trail_tint = TVisualTint();
        _wpn->wireframe_tint = TVisualTint();
        _wpn->tracer = TWeaponTracerConfig();
        _wpn->laser_mesh = TWeapProto::TLaserMeshConfig();
        _wpn->type_icon = 65;
        _wpn->debuff = TWeaponDebuffConfig();
        _wpn->cluster = TWeaponClusterConfig();
        _wpn->cluster.snd.volume = 120;
        _wpn->cluster.snd.sndPrm.mag0 = 1.0;
        _wpn->cluster.snd.sndPrm.time = 1000;
        _wpn->cluster.snd.sndPrm_shk.mag0 = 1.0;
        _wpn->cluster.snd.sndPrm_shk.time = 1000;
        _wpn->cluster.snd.sndPrm_shk.mute = 0.02;
        _wpn->cluster.snd.sndPrm_shk.pos.x = 0.2;
        _wpn->cluster.snd.sndPrm_shk.pos.y = 0.2;
        _wpn->cluster.snd.sndPrm_shk.pos.z = 0.2;
        _wpn->chain = TWeaponChainConfig();

        for (TVhclSound &fx : _wpn->sndFXes)
        {
            fx.sndPrm.mag0 = 1.0;
            fx.sndPrm_shk.mag0 = 1.0;
            fx.sndPrm_shk.mute = 0.02;
            fx.sndPrm_shk.pos.x = 0.2;
            fx.sndPrm_shk.pos.y = 0.2;
            fx.sndPrm_shk.pos.z = 0.2;
            fx.volume = 120;
            fx.sndPrm.time = 1000;
            fx.sndPrm_shk.time = 1000;
        }

        // OpenNeoUA custom: player-only launch shake defaults. Slot 0 keeps the
        // feature disabled; mute 0 makes the local feedback independent from
        // third-person camera distance.
        _wpn->shk_launch_player.mag0 = 1.0;
        _wpn->shk_launch_player.time = 1000;
        _wpn->shk_launch_player.mute = 0.0;
        _wpn->shk_launch_player.pos.x = 0.2;
        _wpn->shk_launch_player.pos.y = 0.2;
        _wpn->shk_launch_player.pos.z = 0.2;

        _wpn->initParams.clear();
        return true;
    }
    else if (!StriCmp(word, "modify_weapon"))
    {
        _collID = -1;
        _wpnID = parser.stol(opt, NULL, 0);

        if ( _wpnID < 0 || (size_t)_wpnID >= _o._weaponProtos.size() )
        {
            ypa_log_out("WARNING: modify_weapon ignored invalid prototype ID %d.\n", _wpnID);
            _wpn = NULL;
            _isModify = false;
            return false;
        }

        _wpn = &_o._weaponProtos[_wpnID];
        _isModify = true;

        _o._upgradeWeaponId = _wpnID;
        return true;
    }

    return false;
}

int WeaponProtoParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( ParseCompoundCollisionParameter(parser, p1, p2, _wpn->coll, _collID) )
        return ScriptParser::RESULT_OK;

    if ( !StriCmp(p1, "model") )
    {
        if ( !StriCmp(p2, "grenade") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_GRENADE;
        else if ( !StriCmp(p2, "arc_grenade") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_ARC_GRENADE;
        else if ( !StriCmp(p2, "rocket") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_ROCKET;
        else if ( !StriCmp(p2, "missile") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_MISSILE;
        else if ( !StriCmp(p2, "homing_bomb") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_HOMING_BOMB;
        else if ( !StriCmp(p2, "artillery_shell") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_ARTILLERY_SHELL;
        else if ( !StriCmp(p2, "laser") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_LASER;
        else if ( !StriCmp(p2, "kamikaze") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_KAMIKAZE;
        else if ( !StriCmp(p2, "bomb") || !StriCmp(p2, "special") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_BOMB;
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "enable") )
    {
        _wpn->enable_mask |= 1 << parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "disable") )
    {
        _wpn->enable_mask &= ~(1 << parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "name") )
    {
        _wpn->name = p2;
        std::replace(_wpn->name.begin(), _wpn->name.end(), '_', ' ');
    }
    else if ( !StriCmp(p1, "energy") )
    {
        _wpn->energy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "aoe_unit_energy") )
    {
        _wpn->aoe_unit_energy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "aoe_building_energy") )
    {
        _wpn->aoe_building_energy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "aoe_sector_energy") )
    {
        _wpn->aoe_sector_energy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "aoe_falloff") )
    {
        _wpn->aoe_falloff = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "aoe_unit_push") )
    {
        _wpn->aoe_unit_push = ClampPushIntensity(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "push") )
    {
        _wpn->push = ClampPushIntensity(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "armor_penetration_targets") )
    {
        _wpn->armor_penetration_targets = std::max(parser.stol(p2, NULL, 0), 0L);
    }
    else if ( !StriCmp(p1, "recoil") )
    {
        _wpn->recoil = ClampRecoilMultiplier(parser.stof(p2, 0));
    }
    else if ( ParseDebuffParam(parser, p1, p2, _wpn->debuff) )
    {}
    else if ( !StriCmp(p1, "energy_heli") )
    {
        _wpn->energy_heli = parser.stof(p2, 0);
        _wpn->energy_heli_defined = true;
    }
    else if ( !StriCmp(p1, "energy_tank") )
    {
        _wpn->energy_tank = parser.stof(p2, 0);
        _wpn->energy_tank_defined = true;
    }
    else if ( !StriCmp(p1, "energy_flyer") )
    {
        _wpn->energy_flyer = parser.stof(p2, 0);
        _wpn->energy_flyer_defined = true;
    }
    else if ( !StriCmp(p1, "energy_robo") )
    {
        _wpn->energy_robo = parser.stof(p2, 0);
        _wpn->energy_robo_defined = true;
    }
    else if ( !StriCmp(p1, "energy_plane") )
    {
        _wpn->energy_plane = parser.stof(p2, 0);
        _wpn->energy_plane_defined = true;
    }
    else if ( !StriCmp(p1, "energy_cruiser") )
    {
        _wpn->energy_cruiser = parser.stof(p2, 0);
        _wpn->energy_cruiser_defined = true;
    }
    else if ( !StriCmp(p1, "energy_glider") )
    {
        _wpn->energy_glider = parser.stof(p2, 0);
        _wpn->energy_glider_defined = true;
    }
    else if ( !StriCmp(p1, "energy_zeppelin") )
    {
        _wpn->energy_zeppelin = parser.stof(p2, 0);
        _wpn->energy_zeppelin_defined = true;
    }
    else if ( !StriCmp(p1, "energy_ufo") )
    {
        _wpn->energy_ufo = parser.stof(p2, 0);
        _wpn->energy_ufo_defined = true;
    }
    else if ( !StriCmp(p1, "energy_car") )
    {
        _wpn->energy_car = parser.stof(p2, 0);
        _wpn->energy_car_defined = true;
    }
    else if ( !StriCmp(p1, "energy_gun") )
    {
        _wpn->energy_gun = parser.stof(p2, 0);
        _wpn->energy_gun_defined = true;
    }
    else if ( !StriCmp(p1, "mass") )
    {
        _wpn->mass = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "force") )
    {
        _wpn->force = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "maxrot") )
    {
        _wpn->maxrot = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "airconst") )
    {
        _wpn->airconst = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius") )
    {
        _wpn->radius = parser.stof(p2, 0);
        _wpn->radius_defined = true;
    }
    else if ( !StriCmp(p1, "trigger_radius") )
    {
        // Canonical model=kamikaze fuse value. Zero, absent, negative,
        // malformed and non-finite values all mean physical contact.
        _wpn->trigger_radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "fire_time_scale") )
    {
        float scale = parser.stof(p2, 0);
        if ( !std::isfinite(scale) || scale <= 0.0f || scale >= 1.0f )
            _wpn->fire_time_scale = 1.0f;
        else
            _wpn->fire_time_scale = std::max(0.05f, scale);
    }
    else if ( !StriCmp(p1, "fire_time_scale_hp_drain") )
    {
        ParseAbsoluteOrPercent(p2, _wpn->fire_time_scale_hp_drain, 100.0f);
    }
    else if ( !StriCmp(p1, "aoe_unit_radius") )
    {
        _wpn->aoe_unit_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "aoe_building_radius") )
    {
        _wpn->aoe_building_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "aoe_sector_radius") )
    {
        _wpn->aoe_sector_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_heli") )
    {
        _wpn->radius_heli = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_tank") )
    {
        _wpn->radius_tank = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_flyer") )
    {
        _wpn->radius_flyer = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_robo") )
    {
        _wpn->radius_robo = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "overeof") )
    {
        _wpn->overeof = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "vwr_radius") )
    {
        _wpn->vwr_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "vwr_overeof") )
    {
        _wpn->vwr_overeof = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "start_speed") )
    {
        _wpn->start_speed = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "grenade_arc_angle") )
    {
        // model = arc_grenade only: one-time launch elevation in degrees.
        _wpn->grenade_arc_angle = std::min(ParseNonNegativeIniFloatOrZero(p2), 89.0f);
    }
    else if ( !StriCmp(p1, "grenade_arc_gravity") )
    {
        // model = arc_grenade only: downward acceleration. Zero/absent/invalid
        // selects the engine-standard gravity in the runtime.
        _wpn->grenade_arc_gravity = std::min(ParseNonNegativeIniFloatOrZero(p2), 1000.0f);
    }
    else if ( !StriCmp(p1, "cluster_enable") )
    {
        _wpn->cluster.enable = parser.stol(p2, NULL, 0) != 0;
    }
    else if ( !StriCmp(p1, "cluster_generations") )
    {
        int generations = parser.stol(p2, NULL, 0);
        _wpn->cluster.generations = generations > 0 ? generations : 0;
    }
    else if ( !StriCmp(p1, "cluster_count") )
    {
        int countMin = 0;
        int countMax = 0;
        if ( World::ParsePositiveIntRangeValue(
                 p2, (int)TWeaponClusterConfig::MAX_COUNT,
                 countMin, countMax) )
        {
            _wpn->cluster.count = countMin;
            _wpn->cluster.count_max = countMax;
        }
        else
        {
            _wpn->cluster.count = 0;
            _wpn->cluster.count_max = 0;
        }
    }
    else if ( !StriCmp(p1, "cluster_weapon_id") )
    {
        int weaponId = parser.stol(p2, NULL, 0);
        _wpn->cluster.weapon_id = weaponId > 0 ? weaponId : 0;
    }
    else if ( !StriCmp(p1, "cluster_trigger_time") )
    {
        int triggerTime = 0;
        int triggerTimeMax = 0;
        _wpn->cluster.trigger_time =
            ParseScriptIntRange(p2, triggerTime, triggerTimeMax) &&
            triggerTime == triggerTimeMax && triggerTime > 0
                ? triggerTime
                : 0;
    }
    else if ( !StriCmp(p1, "cluster_horizontal_angle") )
    {
        float angleMin = 0.0f;
        float angleMax = 0.0f;
        _wpn->cluster.horizontal_angle_set =
            ParseScriptFloatRange(p2, angleMin, angleMax);
        _wpn->cluster.horizontal_angle_min =
            _wpn->cluster.horizontal_angle_set ? angleMin : 0.0f;
        _wpn->cluster.horizontal_angle_max =
            _wpn->cluster.horizontal_angle_set ? angleMax : 0.0f;
    }
    else if ( !StriCmp(p1, "cluster_vertical_angle") )
    {
        float angleMin = 0.0f;
        float angleMax = 0.0f;
        _wpn->cluster.vertical_angle_set =
            ParseScriptFloatRange(p2, angleMin, angleMax);
        _wpn->cluster.vertical_angle_min =
            _wpn->cluster.vertical_angle_set ? angleMin : 0.0f;
        _wpn->cluster.vertical_angle_max =
            _wpn->cluster.vertical_angle_set ? angleMax : 0.0f;
    }
    else if ( !StriCmp(p1, "cluster_spread_x") )
    {
        float spread = parser.stof(p2, 0);
        _wpn->cluster.spread_x = spread > 0.0 ? spread : 0.0;
    }
    else if ( !StriCmp(p1, "cluster_spread_y") )
    {
        float spread = parser.stof(p2, 0);
        _wpn->cluster.spread_y = spread > 0.0 ? spread : 0.0;
    }
    else if ( !StriCmp(p1, "cluster_vp") )
    {
        int vp = parser.stol(p2, NULL, 0);
        _wpn->cluster.vp = vp > 0 ? vp : 0;
    }
    else if ( !StriCmp(p1, "cluster_3ds") )
    {
        _wpn->cluster.mesh3ds = p2;
    }
    else if ( !StriCmp(p1, "cluster_base") )
    {
        _wpn->cluster.basePath = p2;
    }
    else if ( !StriCmp(p1, "snd_cluster_sample") )
    {
        _wpn->cluster.snd.MainSample.Name = p2;
    }
    else if ( !StriCmp(p1, "snd_cluster_pitch") )
    {
        ParseSoundPitchRange(p2, _wpn->cluster.snd);
    }
    else if ( !StriCmp(p1, "snd_cluster_volume") )
    {
        _wpn->cluster.snd.volume = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "snd_cluster_radius") )
    {
        _wpn->cluster.snd.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "pal_cluster_slot") )
    {
        _wpn->cluster.snd.sndPrm.slot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pal_cluster_mag0") )
    {
        _wpn->cluster.snd.sndPrm.mag0 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "pal_cluster_mag1") )
    {
        _wpn->cluster.snd.sndPrm.mag1 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "pal_cluster_time") )
    {
        _wpn->cluster.snd.sndPrm.time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pal_cluster_radius") )
    {
        _wpn->cluster.snd.sndPrm.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "shk_cluster_slot") )
    {
        _wpn->cluster.snd.sndPrm_shk.slot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_mag0") )
    {
        _wpn->cluster.snd.sndPrm_shk.mag0 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_mag1") )
    {
        _wpn->cluster.snd.sndPrm_shk.mag1 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_time") )
    {
        _wpn->cluster.snd.sndPrm_shk.time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_radius") )
    {
        _wpn->cluster.snd.sndPrm_shk.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "shk_cluster_mute") )
    {
        _wpn->cluster.snd.sndPrm_shk.mute = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_x") )
    {
        _wpn->cluster.snd.sndPrm_shk.pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_y") )
    {
        _wpn->cluster.snd.sndPrm_shk.pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_cluster_z") )
    {
        _wpn->cluster.snd.sndPrm_shk.pos.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "chain_allow") )
    {
        _wpn->chain.allow = parser.stol(p2, NULL, 0) != 0;
    }
    else if ( !StriCmp(p1, "chain_max_jumps") )
    {
        int maxJumps = parser.stol(p2, NULL, 0);
        _wpn->chain.max_jumps = maxJumps > 0 ? maxJumps : 0;
    }
    else if ( !StriCmp(p1, "chain_radius") )
    {
        float radius = parser.stof(p2, 0);
        _wpn->chain.radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "chain_damage_mult") )
    {
        float mult = parser.stof(p2, 0);
        _wpn->chain.damage_mult = mult > 0.0 ? mult : 0.0;
    }
    else if ( !StriCmp(p1, "chain_jump_delay") )
    {
        int delay = parser.stol(p2, NULL, 0);
        _wpn->chain.jump_delay = delay > 0 ? delay : 0;
    }
    else if ( !StriCmp(p1, "life_time") )
    {
        int minLifeTime = 0;
        int maxLifeTime = 0;
        if ( ParseScriptIntRange(p2, minLifeTime, maxLifeTime) )
        {
            _wpn->life_time = minLifeTime;
            _wpn->life_time_min = minLifeTime;
            _wpn->life_time_max = maxLifeTime;
        }
        else if ( p2.find('_') == std::string::npos )
        {
            _wpn->life_time = parser.stol(p2, NULL, 0);
            _wpn->life_time_min = _wpn->life_time;
            _wpn->life_time_max = _wpn->life_time;
        }
        else
        {
            // Do not let stol() silently accept only the first endpoint of a
            // malformed range such as 2500_3000_3500.
            _wpn->life_time = 0;
            _wpn->life_time_min = 0;
            _wpn->life_time_max = 0;
        }
    }
    else if ( !StriCmp(p1, "life_time_nt") )
    {
        _wpn->life_time_nt = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "drive_time") )
    {
        _wpn->drive_time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "delay_time") )
    {
        _wpn->delay_time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shot_time") )
    {
        _wpn->shot_time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shot_time_user") )
    {
        _wpn->shot_time_user = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "ramp_up_time") )
    {
        _wpn->ramp_up_time = NonNegativeFiniteMilliseconds(parser, p2);
    }
    else if ( !StriCmp(p1, "ramp_up_max_shot_time") )
    {
        _wpn->ramp_up_max_shot_time = NonNegativeFiniteMilliseconds(parser, p2);
    }
    else if ( !StriCmp(p1, "shk_launch_player_slot") )
    {
        const int slot = parser.stol(p2, NULL, 0);
        _wpn->shk_launch_player.slot = slot > 0 ? slot : 0;
    }
    else if ( !StriCmp(p1, "shk_launch_player_time") )
    {
        const int time = parser.stol(p2, NULL, 0);
        _wpn->shk_launch_player.time = time > 0 ? time : 0;
    }
    else if ( !StriCmp(p1, "shk_launch_player_mag0") )
    {
        const float mag = parser.stof(p2, 0);
        _wpn->shk_launch_player.mag0 = std::isfinite(mag) && mag >= 0.0f ? mag : 0.0f;
    }
    else if ( !StriCmp(p1, "shk_launch_player_mag1") )
    {
        const float mag = parser.stof(p2, 0);
        _wpn->shk_launch_player.mag1 = std::isfinite(mag) && mag >= 0.0f ? mag : 0.0f;
    }
    else if ( !StriCmp(p1, "salve_shots") )
    {
        _wpn->salve_shots = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "salve_delay") )
    {
        _wpn->salve_delay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "multi_target") )
    {
        int maxTargets = parser.stol(p2, NULL, 0);
        _wpn->multi_target = maxTargets > 0 ? maxTargets : 0;
    }
    // ---- OpenNeoUA custom: model = laser parameters ----
    else if ( !StriCmp(p1, "laser_energy_tick_time") )
    {
        int tickTime = parser.stol(p2, NULL, 0);
        _wpn->laser_energy_tick_time = tickTime > 0 ? tickTime : 250;
    }
    else if ( !StriCmp(p1, "laser_energy_tick_time_user") )
    {
        int tickTime = parser.stol(p2, NULL, 0);
        _wpn->laser_energy_tick_time_user = tickTime > 0 ? tickTime : 150;
    }
    else if ( !StriCmp(p1, "laser_energy_increment_rate") )
    {
        float rate = parser.stof(p2, 0);
        _wpn->laser_energy_increment_rate = rate > 0.0 ? rate : 0.0;
    }
    else if ( !StriCmp(p1, "laser_max_energy") )
    {
        float maxEnergy = parser.stof(p2, 0);
        _wpn->laser_max_energy = maxEnergy > 0.0 ? maxEnergy : 0.0;
    }
    else if ( !StriCmp(p1, "laser_visual_spacing") )
    {
        float spacing = parser.stof(p2, 0);
        if ( spacing <= 0.0 )
            spacing = 40.0;
        if ( spacing < 20.0 )
            spacing = 20.0;
        if ( spacing > 500.0 )
            spacing = 500.0;
        _wpn->laser_visual_spacing = spacing;
    }
    else if ( !StriCmp(p1, "laser_chain_allow") )
    {
        _wpn->laser_chain_allow = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "laser_chain_max_jumps") )
    {
        int maxJumps = parser.stol(p2, NULL, 0);
        _wpn->laser_chain_max_jumps = maxJumps > 0 ? maxJumps : 0;
    }
    else if ( !StriCmp(p1, "laser_chain_radius") )
    {
        float radius = parser.stof(p2, 0);
        _wpn->laser_chain_radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "laser_chain_damage_mult") )
    {
        float mult = parser.stof(p2, 0);
        _wpn->laser_chain_damage_mult = mult > 0.0 ? mult : 1.0;
    }
    else if ( !StriCmp(p1, "laser_beam_count") )
    {
        int maxTargets = parser.stol(p2, NULL, 0);
        _wpn->laser_beam_count = maxTargets > 1 ? maxTargets : 1;
    }
    else if ( !StriCmp(p1, "vertical_laser_enable") )
    {
        _wpn->vertical_laser_enable = parser.stol(p2, NULL, 0) == 1;
    }
    else if ( !StriCmp(p1, "vertical_laser_ai_trigger_radius") )
    {
        float radius = parser.stof(p2, 0);
        _wpn->vertical_laser_ai_trigger_radius = std::isfinite(radius) && radius > 0.0f ? radius : 300.0f;
    }
    else if ( !StriCmp(p1, "add_energy") )
    {
        int previousValue = _wpn->energy;
        _wpn->energy += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY,
                                           previousValue, _wpn->energy);
    }
    else if ( !StriCmp(p1, "add_energy_heli") )
    {
        int previousValue = GemFloatHundredths(_wpn->energy_heli);
        _wpn->energy_heli += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_HELI,
                                           previousValue, GemFloatHundredths(_wpn->energy_heli));
    }
    else if ( !StriCmp(p1, "add_energy_tank") )
    {
        int previousValue = GemFloatHundredths(_wpn->energy_tank);
        _wpn->energy_tank += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_TANK,
                                           previousValue, GemFloatHundredths(_wpn->energy_tank));
    }
    else if ( !StriCmp(p1, "add_energy_flyer") )
    {
        int previousValue = GemFloatHundredths(_wpn->energy_flyer);
        _wpn->energy_flyer += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_FLYER,
                                           previousValue, GemFloatHundredths(_wpn->energy_flyer));
    }
    else if ( !StriCmp(p1, "add_energy_Robo") )
    {
        int previousValue = GemFloatHundredths(_wpn->energy_robo);
        _wpn->energy_robo += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_ROBO,
                                           previousValue, GemFloatHundredths(_wpn->energy_robo));
    }
    else if ( !StriCmp(p1, "add_energy_plane") )
    {
        if ( !_wpn->energy_plane_defined )
            _wpn->energy_plane = _wpn->energy_flyer;
        int previousValue = GemFloatHundredths(_wpn->energy_plane);
        _wpn->energy_plane += parser.stol(p2, NULL, 0);
        _wpn->energy_plane_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_PLANE,
                                           previousValue, GemFloatHundredths(_wpn->energy_plane));
    }
    else if ( !StriCmp(p1, "add_energy_cruiser") )
    {
        if ( !_wpn->energy_cruiser_defined )
            _wpn->energy_cruiser = _wpn->energy_flyer;
        int previousValue = GemFloatHundredths(_wpn->energy_cruiser);
        _wpn->energy_cruiser += parser.stol(p2, NULL, 0);
        _wpn->energy_cruiser_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_CRUISER,
                                           previousValue, GemFloatHundredths(_wpn->energy_cruiser));
    }
    else if ( !StriCmp(p1, "add_energy_glider") )
    {
        if ( !_wpn->energy_glider_defined )
            _wpn->energy_glider = _wpn->energy_flyer;
        int previousValue = GemFloatHundredths(_wpn->energy_glider);
        _wpn->energy_glider += parser.stol(p2, NULL, 0);
        _wpn->energy_glider_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_GLIDER,
                                           previousValue, GemFloatHundredths(_wpn->energy_glider));
    }
    else if ( !StriCmp(p1, "add_energy_zeppelin") )
    {
        if ( !_wpn->energy_zeppelin_defined )
            _wpn->energy_zeppelin = _wpn->energy_flyer;
        int previousValue = GemFloatHundredths(_wpn->energy_zeppelin);
        _wpn->energy_zeppelin += parser.stol(p2, NULL, 0);
        _wpn->energy_zeppelin_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_ZEPPELIN,
                                           previousValue, GemFloatHundredths(_wpn->energy_zeppelin));
    }
    else if ( !StriCmp(p1, "add_energy_ufo") )
    {
        if ( !_wpn->energy_ufo_defined )
            _wpn->energy_ufo = _wpn->energy_flyer;
        int previousValue = GemFloatHundredths(_wpn->energy_ufo);
        _wpn->energy_ufo += parser.stol(p2, NULL, 0);
        _wpn->energy_ufo_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_UFO,
                                           previousValue, GemFloatHundredths(_wpn->energy_ufo));
    }
    else if ( !StriCmp(p1, "add_energy_car") )
    {
        if ( !_wpn->energy_car_defined )
            _wpn->energy_car = _wpn->energy_tank;
        int previousValue = GemFloatHundredths(_wpn->energy_car);
        _wpn->energy_car += parser.stol(p2, NULL, 0);
        _wpn->energy_car_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_CAR,
                                           previousValue, GemFloatHundredths(_wpn->energy_car));
    }
    else if ( !StriCmp(p1, "add_energy_gun") )
    {
        if ( !_wpn->energy_gun_defined )
            _wpn->energy_gun = 1.0f;
        int previousValue = GemFloatHundredths(_wpn->energy_gun);
        _wpn->energy_gun += parser.stol(p2, NULL, 0);
        _wpn->energy_gun_defined = true;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_ENERGY_GUN,
                                           previousValue, GemFloatHundredths(_wpn->energy_gun));
    }
    else if ( !StriCmp(p1, "add_shot_time") )
    {
        int previousValue = _wpn->shot_time;
        _wpn->shot_time += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_SHOT_TIME,
                                           previousValue, _wpn->shot_time);
    }
    else if ( !StriCmp(p1, "add_shot_time_user") )
    {
        int previousValue = _wpn->shot_time_user;
        _wpn->shot_time_user += parser.stol(p2, NULL, 0);

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_WEAPON, _wpnID,
                                           TGemNotificationEntry::CHANGE_SHOT_TIME_USER,
                                           previousValue, _wpn->shot_time_user);
    }
    else if ( !StriCmp(p1, "vp_normal") )
    {
        _wpn->vp_normal = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_fire") )
    {
        _wpn->vp_fire = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "weapon_use_vehicle_fire_visual") )
    {
        _wpn->weapon_use_vehicle_fire_visual = parser.stol(p2, NULL, 0) == 1;
    }
    else if ( !StriCmp(p1, "vp_megadeth") )
    {
        _wpn->vp_megadeth = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_wait") )
    {
        _wpn->vp_wait = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_dead") )
    {
        _wpn->vp_dead = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_genesis") )
    {
        _wpn->vp_genesis = parser.stol(p2, NULL, 0);
    }
    else if ( ParseExternalVisualParam(p1, p2, "3ds", _wpn->visual_3ds, true) )
    {
    }
    else if ( ParseExternalVisualParam(p1, p2, "base", _wpn->visual_base, true) )
    {
    }
    else if ( !StriCmp(p1, "vp_launch") )
    {
        _wpn->vp_launch = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "launch_scale") )
    {
        float scale = ParseVPScaleValue(parser, p2);
        _wpn->launch_scale = vec3d(scale, scale, scale);
    }
    else if ( ParseVPScaleParam(parser, "visual", p1, p2, _wpn->visual_scale) )
    {
    }
    else if ( ParseTintParam(parser, "visual_tint", p1, p2, _wpn->visual_tint) )
    {
    }
    else if ( ParseWireframeTintParam(parser, p1, p2, _wpn->wireframe_tint) )
    {
    }
    else if ( ParseVPRotationParam(parser, "visual", p1, p2, _wpn->visual_rotation) )
    {
    }
    else if ( ParseVPSpinParam(parser, "visual", p1, p2, _wpn->visual_spin) )
    {
    }
    else if ( !StriCmp(p1, "spiral_speed") )
    {
        _wpn->spiral_speed = (float)World::Spin::ClampStrength(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "spiral_radius") )
    {
        _wpn->spiral_radius = ClampProjectileVisualMotionRadius(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "chaos_factor") )
    {
        _wpn->chaos_factor = (float)World::Spin::ClampStrength(parser.stof(p2, 0));
    }
    else if ( !StriCmp(p1, "chaos_radius") )
    {
        _wpn->chaos_radius = ClampProjectileVisualMotionRadius(parser.stof(p2, 0));
    }
    else if ( ParseVPScaleParam(parser, "vp_trail", p1, p2, _wpn->vp_trail_scale) )
    {
    }
    else if ( ParseVPSpinParam(parser, "vp_trail", p1, p2, _wpn->vp_trail_spin) )
    {
    }
    else if ( ParseTintParam(parser, "vp_trail_tint", p1, p2, _wpn->vp_trail_tint) )
    {
    }
    else if ( ParseMeshTracerParam(parser, p1, p2, _wpn->tracer, "mesh_tracer_") )
    {
    }
    else if ( ParseLaserMeshParam(parser, p1, p2, _wpn->laser_mesh) )
    {
    }
    else if ( ParseDecorationFXParam(parser, p1, p2, _wpn->decoration_fx) )
    {
    }
    else if ( !StriCmp(p1, "type_icon") )
    {
        _wpn->type_icon = p2[0];
    }
    else if ( !StriCmp(p1, "wireframe") )
    {
        if ( _wpn->wireframe )
            _wpn->wireframe->Delete();

        _wpn->wireframe = Nucleus::CInit<NC_STACK_sklt>( {{NC_STACK_rsrc::RSRC_ATT_NAME, std::string(p2)}} );
    }
    else if ( !StriCmp(p1, "dest_fx") )
    {
        Stok stok(p2, " _");
        std::string fx_type, pp1, pp2, pp3, pp4;

        if ( stok.GetNext(&fx_type) && stok.GetNext(&pp1) && stok.GetNext(&pp2) && stok.GetNext(&pp3) && stok.GetNext(&pp4) )
        {
            _wpn->dfx.emplace_back();
            DestFX &dfx = _wpn->dfx.back();
            dfx.Type = DestFX::ParseTypeName(fx_type);

            if (dfx.Type == DestFX::FX_NONE)
                return ScriptParser::RESULT_BAD_DATA;

            dfx.ModelID = parser.stol(pp1, NULL, 0);
            dfx.Pos.x = parser.stof(pp2, 0);
            dfx.Pos.y = parser.stof(pp3, 0);
            dfx.Pos.z = parser.stof(pp4, 0);

            std::string pp5;
            if ( stok.GetNext(&pp5) )
            {
                if (parser.stol(pp5, NULL, 0) != 0 )
                    dfx.Accel = true;
                else
                    dfx.Accel = false;
            }
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "ext_dest_fx") || !StriCmp(p1, "extended_dest_fx") )
    {
        Stok stok(p2, " _");
        std::string fx_type, pp1, pp2, pp3, pp4;

        if ( stok.GetNext(&fx_type) && stok.GetNext(&pp1) && stok.GetNext(&pp2) && stok.GetNext(&pp3) && stok.GetNext(&pp4) )
        {
            _wpn->ExtDestroyFX.emplace_back();

            DestFX &dfx = _wpn->ExtDestroyFX.back();

            dfx.Type = DestFX::ParseTypeName(fx_type);

            if (dfx.Type == DestFX::FX_NONE)
                return ScriptParser::RESULT_BAD_DATA;

            dfx.ModelID = parser.stol(pp1, NULL, 0);
            dfx.Pos.x = parser.stof(pp2, 0);
            dfx.Pos.y = parser.stof(pp3, 0);
            dfx.Pos.z = parser.stof(pp4, 0);

            std::string pp5;
            if ( stok.GetNext(&pp5) )
            {
                if (parser.stol(pp5, NULL, 0) != 0 )
                    dfx.Accel = true;
                else
                    dfx.Accel = false;
            }
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "artillery_shell_mode") )
    {
        if ( !StriCmp(p2, "ballistic") )
            _wpn->artillery_shell_mode = TWeapProto::ARTILLERY_SHELL_MODE_BALLISTIC;
        else if ( !StriCmp(p2, "vertical_barrage") )
            _wpn->artillery_shell_mode = TWeapProto::ARTILLERY_SHELL_MODE_VERTICAL_BARRAGE;
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "artillery_shell_fall_delay") )
    {
        int v = parser.stol(p2, NULL, 0);
        _wpn->artillery_shell_fall_delay = v > 0 ? v : 0;
    }
    else if ( !StriCmp(p1, "artillery_shell_min_range") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_min_range = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "artillery_shell_max_range") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_max_range = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "artillery_shell_requires_radar") )
    {
        _wpn->artillery_shell_requires_radar = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "artillery_shell_manual_mode_only") )
    {
        _wpn->artillery_shell_manual_mode_only = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "artillery_shell_barrage_radius") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_barrage_radius = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "artillery_shell_barrage_shots") )
    {
        int v = parser.stol(p2, NULL, 0);
        _wpn->artillery_shell_barrage_shots = v > 0 ? v : 0;
    }
    else if ( !StriCmp(p1, "artillery_shell_barrage_shot_delay") )
    {
        _wpn->artillery_shell_barrage_shot_delay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "artillery_shell_barrage_cooldown") )
    {
        _wpn->artillery_shell_barrage_cooldown = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "artillery_shell_arc_height") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_arc_height = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "artillery_shell_speed") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_speed = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "artillery_shell_vertical_spread_x") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_vertical_spread_x = (std::isfinite(v) && v > 0.0f) ? std::min(v, 45.0f) : 0.0f;
    }
    else if ( !StriCmp(p1, "artillery_shell_vertical_spread_z") )
    {
        float v = parser.stof(p2, 0);
        _wpn->artillery_shell_vertical_spread_z = (std::isfinite(v) && v > 0.0f) ? std::min(v, 45.0f) : 0.0f;
    }
    else if ( !StriCmp(p1, "artillery_shell_airburst") )
    {
        _wpn->artillery_shell_airburst = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "artillery_shell_marker_path") )
    {
        // Author relative to Data/Interface/Map/Markers. Empty/invalid/missing
        // assets are handled by the UI loader with the canonical classic SVG fallback.
        _wpn->artillery_shell_marker_path = p2;
    }
    else if ( !StriCmp(p1, "begin_chain_fx") )
    {
        return ParseWeaponChainFXBlock(parser, _wpn);
    }
    else
        return ParseSndFX(parser, p1, p2);

    return ScriptParser::RESULT_OK;
}

bool BuildProtoParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if (!StriCmp(word, "new_building"))
    {
        _isModify = false;
        _bldID = parser.stol(opt, NULL, 0);

        _o._buildProtos[_bldID] = TBuildingProto();
        _bld = &_o._buildProtos[_bldID];
        _bld->Index = _bldID;
        _bld->Energy = 50000;
        _bld->TypeIcon = 65;
        _bld->SndFX.volume = 120;
        _bld->Guns.clear();
        return true;
    }
    else if (!StriCmp(word, "modify_building"))
    {
        _bldID = parser.stol(opt, NULL, 0);

        if ( _bldID < 0 || (size_t)_bldID >= _o._buildProtos.size() )
        {
            ypa_log_out("WARNING: modify_building ignored invalid prototype ID %d.\n", _bldID);
            _bld = NULL;
            _isModify = false;
            return false;
        }

        _bld = &_o._buildProtos[_bldID];
        _bld->Index = _bldID;
        _isModify = true;
        _o._upgradeBuildId = _bldID;
        return true;
    }

    return false;
}


int BuildProtoParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "model") )
    {
        if ( !StriCmp(p2, "building") )
        {
            _bld->ModelID = 0;
        }
        else if ( !StriCmp(p2, "kraftwerk") )
        {
            _bld->ModelID = 1;
        }
        else if ( !StriCmp(p2, "radar") )
        {
            _bld->ModelID = 2;
        }
        else if ( !StriCmp(p2, "defcenter") )
        {
            _bld->ModelID = 3;
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "enable") )
    {
        int fraction = parser.stol(p2, NULL, 0);
        bool wasEnabled = (_bld->EnableMask & (1 << fraction)) != 0;
        _bld->EnableMask |= 1 << fraction;

        if ( _isModify && _o.IsGemNotificationCaptureActive() )
            _o.RecordGemNotificationChange(TGemNotificationEntry::TARGET_BUILDING, _bldID,
                                           TGemNotificationEntry::CHANGE_ENABLE,
                                           wasEnabled ? 1 : 0, 1, !wasEnabled);
    }
    else if ( !StriCmp(p1, "disable") )
    {
        _bld->EnableMask &= ~(1 << parser.stol(p2, NULL, 0));
    }
    else if ( !StriCmp(p1, "name") )
    {
        _bld->Name = p2;
        std::replace(_bld->Name.begin(), _bld->Name.end(), '_', ' ');
    }
    else if ( !StriCmp(p1, "power") )
    {
        _bld->Power = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "energy") )
    {
        _bld->Energy = parser.stol(p2, NULL, 0);
    }
    else if ( ParseDecorationFXParam(parser, p1, p2, _bld->DecorationFX) )
    {
    }
    else if ( !StriCmp(p1, "sec_type") )
    {
        _bld->SecType = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "type_icon") )
    {
        _bld->TypeIcon = p2[0];
    }
    else if ( !StriCmp(p1, "spawn_units") )
    {
        _bld->spawn_units = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "spawn_vehicle") )
    {
        _bld->spawn_vehicle = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "spawn_interval") )
    {
        int interval = parser.stol(p2, NULL, 0);
        _bld->spawn_interval = interval > 0 ? interval : 0;
    }
    else if ( !StriCmp(p1, "spawn_trigger_radius") )
    {
        float radius = parser.stof(p2, 0);
        _bld->spawn_trigger_radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "spawn_random_pos") )
    {
        float radius = ParseFiniteFloatOrFallback(parser, p2, 340.0f);
        _bld->spawn_random_pos = radius >= 0.0 ? radius : 340.0f;
    }
    else if ( !StriCmp(p1, "spawn_offset_x") )
    {
        _bld->spawn_offset.x = ParseFiniteFloatOrFallback(parser, p2, 37.0f);
    }
    else if ( !StriCmp(p1, "spawn_offset_z") )
    {
        _bld->spawn_offset.z = ParseFiniteFloatOrFallback(parser, p2, -41.0f);
    }
    else if ( !StriCmp(p1, "spawn_height") )
    {
        float minHeight = 650.0f;
        float maxHeight = 900.0f;
        if ( !ParseScriptFloatRange(p2, minHeight, maxHeight) ||
             minHeight < 0.0f || maxHeight < 0.0f )
        {
            minHeight = 650.0f;
            maxHeight = 900.0f;
        }
        _bld->spawn_height_min = minHeight;
        _bld->spawn_height_max = maxHeight;
    }
    else if ( !StriCmp(p1, "spawn_max_active") )
    {
        int maxActive = parser.stol(p2, NULL, 0);
        _bld->spawn_max_active = maxActive > 0 ? maxActive : 0;
    }
    else if ( !StriCmp(p1, "spawn_count") )
    {
        int count = parser.stol(p2, NULL, 0);
        _bld->spawn_count = count > 0 ? count : 1;
    }
    else if ( !StriCmp(p1, "spawn_instant") )
    {
        _bld->spawn_instant = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "snd_normal_sample") )
    {
        _bld->SndFX.MainSample.Name = p2;
    }
    else if ( !StriCmp(p1, "snd_normal_volume") )
    {
        _bld->SndFX.volume = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "snd_normal_pitch") )
    {
        ParseSoundPitchRange(p2, _bld->SndFX);
    }
    else if ( !StriCmp(p1, "sbact_act") )
    {
        _gunID = parser.stol(p2, NULL, 0);
        if (_gunID >= _bld->Guns.size())
            _bld->Guns.resize(_gunID + 1);
    }
    else
    {
        TBuildingProto::TGun &pGun = _bld->Guns.at(_gunID);
        if ( !StriCmp(p1, "sbact_vehicle") )
        {
            pGun.VhclID = parser.stol(p2, NULL, 0);
        }
        else if ( !StriCmp(p1, "sbact_pos_x") )
        {
            pGun.Pos.x = parser.stof(p2, 0);
        }
        else if ( !StriCmp(p1, "sbact_pos_y") )
        {
            pGun.Pos.y = parser.stof(p2, 0);
        }
        else if ( !StriCmp(p1, "sbact_pos_z") )
        {
            pGun.Pos.z = parser.stof(p2, 0);
        }
        else if ( !StriCmp(p1, "sbact_dir_x") )
        {
            pGun.Dir.x = parser.stof(p2, 0);
        }
        else if ( !StriCmp(p1, "sbact_dir_y") )
        {
            pGun.Dir.y = parser.stof(p2, 0);
        }
        else if ( !StriCmp(p1, "sbact_dir_z") )
        {
            pGun.Dir.z = parser.stof(p2, 0);
        }
        else
            return ScriptParser::RESULT_UNKNOWN;
    }

    return ScriptParser::RESULT_OK;
}

bool MovieParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_movies") )
        return false;

    for (std::string &movie : _o._movies)
        movie.clear();
    return true;
}

int MovieParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
	if ( !StriCmp(p1, "end") )
		return ScriptParser::RESULT_SCOPE_END;
	else if ( !StriCmp(p1, "game_intro") )
		_o._movies[World::MOVIE_INTRO] = p2;
	else if ( !StriCmp(p1, "win_extro") )
		_o._movies[World::MOVIE_WIN] = p2;
	else if ( !StriCmp(p1, "lose_extro") )
		_o._movies[World::MOVIE_LOSE] = p2;
	else if ( !StriCmp(p1, "user_intro") )
		_o._movies[World::MOVIE_USER] = p2;
	else if ( !StriCmp(p1, "kyt_intro") )
		_o._movies[World::MOVIE_KYT] = p2;
	else if ( !StriCmp(p1, "taer_intro") )
		_o._movies[World::MOVIE_TAER] = p2;
	else if ( !StriCmp(p1, "myk_intro") )
		_o._movies[World::MOVIE_MYK] = p2;
	else if ( !StriCmp(p1, "sulg_intro") )
		_o._movies[World::MOVIE_SULG] = p2;
	else if ( !StriCmp(p1, "black_intro") )
		_o._movies[World::MOVIE_BLACK] = p2;
	else
		return ScriptParser::RESULT_UNKNOWN;
	return ScriptParser::RESULT_OK;
}


BkgParser::BkgParser(NC_STACK_ypaworld *o)
: _o(o->_globalMapRegions)
{}

int BkgParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        _o.NumSets++;
        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "background_map") )
    {
        _o.background_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "rollover_map") )
    {
        _o.rollover_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "finished_map") )
    {
        _o.finished_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "enabled_map") )
    {
        _o.enabled_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "mask_map") )
    {
        _o.mask_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "tut_background_map") )
    {
        _o.tut_background_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "tut_rollover_map") )
    {
        _o.tut_rollover_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "tut_mask_map") )
    {
        _o.tut_mask_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "menu_map") )
    {
        _o.menu_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "input_map") )
    {
        _o.input_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "settings_map") )
    {
        _o.settings_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "network_map") )
    {
        _o.network_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "locale_map") )
    {
        _o.locale_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "save_map") )
    {
        _o.save_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "about_map") )
    {
        _o.about_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "help_map") )
    {
        _o.help_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "brief_map") )
    {
        _o.brief_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "debrief_map") )
    {
        _o.debrief_map[_o.NumSets].PicName = p2;
    }
    else if ( !StriCmp(p1, "size_x") )
    {
        _o.background_map[_o.NumSets].Size.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "size_y") )
    {
        _o.background_map[_o.NumSets].Size.y = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}


int ColorParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "owner_0") )
    {
        _o.ParseColorString(World::COLOR_OWNER_0, p2);
    }
    else if ( !StriCmp(p1, "owner_1") )
    {
        _o.ParseColorString(World::COLOR_OWNER_1, p2);
    }
    else if ( !StriCmp(p1, "owner_2") )
    {
        _o.ParseColorString(World::COLOR_OWNER_2, p2);
    }
    else if ( !StriCmp(p1, "owner_3") )
    {
        _o.ParseColorString(World::COLOR_OWNER_3, p2);
    }
    else if ( !StriCmp(p1, "owner_4") )
    {
        _o.ParseColorString(World::COLOR_OWNER_4, p2);
    }
    else if ( !StriCmp(p1, "owner_5") )
    {
        _o.ParseColorString(World::COLOR_OWNER_5, p2);
    }
    else if ( !StriCmp(p1, "owner_6") )
    {
        _o.ParseColorString(World::COLOR_OWNER_6, p2);
    }
    else if ( !StriCmp(p1, "owner_7") )
    {
        _o.ParseColorString(World::COLOR_OWNER_7, p2);
    }
    else if ( !StriCmp(p1, "map_direction") )
    {
        _o.ParseColorString(World::COLOR_MAP_DIRECTION, p2);
    }
    else if ( !StriCmp(p1, "map_primtarget") )
    {
        _o.ParseColorString(World::COLOR_MAP_PRIMTARGET, p2);
    }
    else if ( !StriCmp(p1, "map_sectarget") )
    {
        _o.ParseColorString(World::COLOR_MAP_SECTARGET, p2);
    }
    else if ( !StriCmp(p1, "map_commander") )
    {
        _o.ParseColorString(World::COLOR_MAP_COMMANDER, p2);
    }
    else if ( !StriCmp(p1, "map_dragbox") )
    {
        _o.ParseColorString(World::COLOR_MAP_DRAGBOX, p2);
    }
    else if ( !StriCmp(p1, "map_viewer") )
    {
        _o.ParseColorString(World::COLOR_MAP_VIEWER, p2);
    }
    else if ( !StriCmp(p1, "hud_weapon") )
    {
        _o.ParseColorString(World::COLOR_HUD_WEAPON_0, p2);
    }
    else if ( !StriCmp(p1, "hud_weapon_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_WEAPON_1, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_commandvec") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_CMDVEC_0, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_commandvec_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_CMDVEC_1, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_primtarget") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_PRIMTGT_0, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_primtarget_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_PRIMTGT_1, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_locktarget") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_LOCKTGT_0, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_locktarget_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_LOCKTGT_1, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_compass") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_0, p2);
    }
    else if ( !StriCmp(p1, "hud_compass_compass_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_COMPASS_1, p2);
    }
    else if ( !StriCmp(p1, "hud_vehicle") )
    {
        _o.ParseColorString(World::COLOR_HUD_VEHICLE_0, p2);
    }
    else if ( !StriCmp(p1, "hud_vehicle_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_VEHICLE_1, p2);
    }
    else if ( !StriCmp(p1, "hud_visor_mg") )
    {
        _o.ParseColorString(World::COLOR_HUD_VISOR_MG_0, p2);
    }
    else if ( !StriCmp(p1, "hud_visor_mg_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_VISOR_MG_1, p2);
    }
    else if ( !StriCmp(p1, "hud_visor_locked") )
    {
        _o.ParseColorString(World::COLOR_HUD_VISOR_LOCKED_0, p2);
    }
    else if ( !StriCmp(p1, "hud_visor_locked_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_VISOR_LOCKED_1, p2);
    }
    else if ( !StriCmp(p1, "hud_visor_autonom") )
    {
        _o.ParseColorString(World::COLOR_HUD_VISOR_AUTONOM_0, p2);
    }
    else if ( !StriCmp(p1, "hud_visor_autonom_1") )
    {
        _o.ParseColorString(World::COLOR_HUD_VISOR_AUTONOM_1, p2);
    }
    else if ( !StriCmp(p1, "brief_lines") )
    {
        _o.ParseColorString(World::COLOR_BRIEF_LINES, p2);
    }
    else if ( !StriCmp(p1, "text_default") )
    {
        _o.ParseColorString(World::COLOR_TEXT_DEFAULT, p2);
    }
    else if ( !StriCmp(p1, "text_list") )
    {
        _o.ParseColorString(World::COLOR_TEXT_LIST, p2);
    }
    else if ( !StriCmp(p1, "text_list_sel") )
    {
        _o.ParseColorString(World::COLOR_TEXT_LIST_SEL, p2);
    }
    else if ( !StriCmp(p1, "text_tooltip") )
    {
        _o.ParseColorString(World::COLOR_TEXT_TOOLTIP, p2);
    }
    else if ( !StriCmp(p1, "text_message") )
    {
        _o.ParseColorString(World::COLOR_TEXT_MESSAGE, p2);
    }
    else if ( !StriCmp(p1, "text_hud") )
    {
        _o.ParseColorString(World::COLOR_TEXT_HUD, p2);
    }
    else if ( !StriCmp(p1, "text_briefing") )
    {
        _o.ParseColorString(World::COLOR_TEXT_BRIEFING, p2);
    }
    else if ( !StriCmp(p1, "text_debriefing") )
    {
        _o.ParseColorString(World::COLOR_TEXT_DEBRIEFING, p2);
    }
    else if ( !StriCmp(p1, "text_button") )
    {
        _o.ParseColorString(World::COLOR_TEXT_BUTTON, p2);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

bool MiscParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_misc") )
        return false;

    _o._beamEnergyStart = 500;
    _o._beamEnergyAdd = 100;
    _o._defaultUnitLimit = 512;
    _o._defaultUnitLimitType = 0;
    _o._defaultUnitLimitArg = 0;
    _o._easyCheatKeys = false;

    return true;
}


bool AtmosphericFXProfileParser::IsScope(ScriptParser::Parser &parser,
                                         const std::string &word,
                                         const std::string &opt)
{
    if ( StriCmp(word, "begin_fx_profile") )
        return false;

    if ( _seenScope )
    {
        _duplicateScope = true;
        return true;
    }

    _profile = TAtmosphericFXProfile();
    _seenScope = true;
    return true;
}

static float ParseAtmosphericFXPositiveScale(ScriptParser::Parser &parser,
                                             const std::string &param,
                                             const std::string &value)
{
    float scale = parser.stof(value, 0);
    if ( std::isfinite(scale) && scale > 0.0f )
        return scale;

    ypa_log_out("WARNING: Atmospheric FX parameter '%s' must be finite and greater than zero; using 1.0\n",
                param.c_str());
    return 1.0f;
}

int AtmosphericFXProfileParser::Handle(ScriptParser::Parser &parser,
                                       const std::string &p1,
                                       const std::string &p2)
{
    if ( _duplicateScope )
        return ScriptParser::RESULT_BAD_DATA;

    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "loop_sound") )
        _profile.loop_sound = p2;
    else if ( !StriCmp(p1, "loop_sound_volume") )
        _profile.loop_sound_volume = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "mesh_3ds") )
        _profile.mesh3ds = p2;
    else if ( !StriCmp(p1, "count") )
        _profile.count = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "spawn_radius_x") )
        _profile.spawn_radius.x = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "spawn_radius_y") )
        _profile.spawn_radius.y = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "spawn_radius_z") )
        _profile.spawn_radius.z = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "spawn_offset_x") )
        _profile.spawn_offset.x = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "spawn_offset_y") )
        _profile.spawn_offset.y = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "spawn_offset_z") )
        _profile.spawn_offset.z = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "lifetime_min") )
        _profile.lifetime_min = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "lifetime_max") )
        _profile.lifetime_max = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "velocity_x") )
        _profile.velocity.x = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "velocity_y") )
        _profile.velocity.y = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "velocity_z") )
        _profile.velocity.z = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "velocity_random_x") )
        _profile.velocity_random.x = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "velocity_random_y") )
        _profile.velocity_random.y = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "velocity_random_z") )
        _profile.velocity_random.z = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "scale_x") )
        _profile.scale.x = ParseAtmosphericFXPositiveScale(parser, p1, p2);
    else if ( !StriCmp(p1, "scale_y") )
        _profile.scale.y = ParseAtmosphericFXPositiveScale(parser, p1, p2);
    else if ( !StriCmp(p1, "scale_z") )
        _profile.scale.z = ParseAtmosphericFXPositiveScale(parser, p1, p2);
    else if ( !StriCmp(p1, "spin_x") )
        _profile.spin.x = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "spin_y") )
        _profile.spin.y = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "spin_z") )
        _profile.spin.z = parser.stof(p2, 0);
    else if ( !StriCmp(p1, "fade_in") )
        _profile.fade_in = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "fade_out") )
        _profile.fade_out = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "tint") )
        ParseTintParam(parser, "tint", p1, p2, _profile.tint, true);
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

int MiscParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }
    else if ( !StriCmp(p1, "one_game_res") )
    {
        _o._oneGameRes = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "shell_default_res") )
    {
		Stok stok(p2, "_ \t");
		std::string pp1, pp2;
        if ( stok.GetNext(&pp1) && stok.GetNext(&pp2) )
        {
            _o._shellDefaultRes = Common::Point(parser.stol(pp1, NULL, 0), parser.stol(pp2, NULL, 0));
            _o._shellGfxMode = Common::Point(parser.stol(pp1, NULL, 0), parser.stol(pp2, NULL, 0));
        }
    }
    else if ( !StriCmp(p1, "game_default_res") )
    {
		Stok stok(p2, "_ \t");
        std::string pp1, pp2;
        if ( stok.GetNext(&pp1) && stok.GetNext(&pp2) )
        {
            _o._gameDefaultRes = Common::Point(parser.stol(pp1, NULL, 0), parser.stol(pp2, NULL, 0));
            _o._gfxMode = Common::Point(parser.stol(pp1, NULL, 0), parser.stol(pp2, NULL, 0));
        }
    }
    else if ( !StriCmp(p1, "max_impulse") )
    {
        _o._maxImpulse = parser.stof(p2);
    }
    else if ( !StriCmp(p1, "unit_limit") )
    {
        _o._defaultUnitLimit = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unit_limit_type") )
    {
        _o._defaultUnitLimitType = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unit_limit_arg") )
    {
        _o._defaultUnitLimitArg = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "beam_energy_start") )
    {
        _o._beamEnergyStart = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "beam_energy_add") )
    {
        _o._beamEnergyAdd = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "easy_cheat_keys") )
    {
        _o._easyCheatKeys = parser.stol(p2, NULL, 0) != 0;
    }
    else if ( !StriCmp(p1, "multi_building") )
    {
        _o._allowMultiBuildWorld = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "hidden_fractions") )
    {
        _o._worldHiddenFractions = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}


bool SuperItemParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_superitem") )
        return false;

    _o._stoudsonWaveVehicleId = 0;
    _o._stoudsonCenterVehicleId = 0;
    return true;
}

int SuperItemParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }
	else if ( !StriCmp(p1, "superbomb_center_vproto") )
    {
        _o._stoudsonCenterVehicleId = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "superbomb_wall_vproto") )
    {
        _o._stoudsonWaveVehicleId = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;
    return ScriptParser::RESULT_OK;
}

bool SuperItemProfileParser::IsScope(ScriptParser::Parser &parser,
                                     const std::string &word,
                                     const std::string &opt)
{
    if ( StriCmp(word, "begin_superitem_profile") )
        return false;

    _profiles.emplace_back();
    _profile = &_profiles.back();
    return true;
}

static float ParseSuperItemScale(ScriptParser::Parser &parser,
                                 const std::string &param,
                                 const std::string &value)
{
    float scale = parser.stof(value, 0);
    if ( std::isfinite(scale) && scale > 0.0f )
        return scale;

    ypa_log_out("WARNING: SuperItem profile %s must be finite and greater than zero; using 1.0\n",
                param.c_str());
    return 1.0f;
}

static bool ParseSuperItemSoundEventParam(ScriptParser::Parser &parser,
                                          const std::string &eventName,
                                          const std::string &p1,
                                          const std::string &p2,
                                          TVhclSound &sound)
{
    const std::string sndPrefix = "snd_" + eventName + "_";
    const std::string palPrefix = "pal_" + eventName + "_";
    const std::string shkPrefix = "shk_" + eventName + "_";

    if ( !StriCmp(p1, sndPrefix + "sample") )
        sound.MainSample.Name = p2;
    else if ( !StriCmp(p1, sndPrefix + "volume") )
        sound.volume = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, sndPrefix + "pitch") )
        ParseSoundPitchRange(p2, sound);
    else if ( !StriCmp(p1, sndPrefix + "radius") )
        sound.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, palPrefix + "slot") )
        sound.sndPrm.slot = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, palPrefix + "time") )
        sound.sndPrm.time = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, palPrefix + "radius") )
        sound.sndPrm.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, palPrefix + "mag0") )
        sound.sndPrm.mag0 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, palPrefix + "mag1") )
        sound.sndPrm.mag1 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, shkPrefix + "slot") )
        sound.sndPrm_shk.slot = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, shkPrefix + "time") )
        sound.sndPrm_shk.time = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, shkPrefix + "radius") )
        sound.sndPrm_shk.radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, shkPrefix + "mag0") )
        sound.sndPrm_shk.mag0 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, shkPrefix + "mag1") )
        sound.sndPrm_shk.mag1 = parser.stof(p2, 0);
    else if ( !StriCmp(p1, shkPrefix + "mute") )
        sound.sndPrm_shk.mute = parser.stof(p2, 0);
    else if ( !StriCmp(p1, shkPrefix + "x") )
        sound.sndPrm_shk.pos.x = parser.stof(p2, 0);
    else if ( !StriCmp(p1, shkPrefix + "y") )
        sound.sndPrm_shk.pos.y = parser.stof(p2, 0);
    else if ( !StriCmp(p1, shkPrefix + "z") )
        sound.sndPrm_shk.pos.z = parser.stof(p2, 0);
    else
        return false;

    return true;
}

int SuperItemProfileParser::Handle(ScriptParser::Parser &parser,
                                   const std::string &p1,
                                   const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "begin_chain_fx") )
        return ParseSuperItemChainFXBlock(parser, _profile);

    if ( !StriCmp(p1, "id") )
        _profile->id = p2;
    else if ( !StriCmp(p1, "wave_vp") )
        _profile->wave_vp = parser.stol(p2, NULL, 0);
    else if ( !StriCmp(p1, "wave_3ds") )
        _profile->wave_3ds = p2;
    else if ( !StriCmp(p1, "wave_base") )
        _profile->wave_base = p2;
    else if ( !StriCmp(p1, "fallout_fx_profile") )
        _profile->fallout_fx_profile = p2;
    else if ( !StriCmp(p1, "wave_scale_x") )
        _profile->wave_axis_scale.x = ParseSuperItemScale(parser, p1, p2);
    else if ( !StriCmp(p1, "wave_scale_y") )
        _profile->wave_axis_scale.y = ParseSuperItemScale(parser, p1, p2);
    else if ( !StriCmp(p1, "wave_scale_z") )
        _profile->wave_axis_scale.z = ParseSuperItemScale(parser, p1, p2);
    else if ( !StriCmp(p1, "wave_tint") )
        ParseTintParam(parser, "wave_tint", p1, p2, _profile->wave_tint);
    else if ( !StriCmp(p1, "wave_start_speed") )
    {
        _profile->wave_start_speed = parser.stof(p2, 0);
        _profile->has_wave_start_speed = true;
    }
    else if ( !StriCmp(p1, "wave_speed_ramp_time") )
    {
        _profile->wave_speed_ramp_time = parser.stof(p2, 0);
        _profile->has_wave_speed_ramp_time = true;
    }
    else if ( !StriCmp(p1, "wave_end_speed") )
    {
        _profile->wave_end_speed = parser.stof(p2, 0);
        _profile->has_wave_end_speed = true;
    }
    else if ( !StriCmp(p1, "push_force") )
        _profile->push_force = ClampPushIntensity(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "push_radius") )
        _profile->push_radius = NonNegativeFiniteOrZero(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "push_falloff") )
    {
        const int falloff = parser.stol(p2, NULL, 0);
        _profile->push_falloff = std::max(0, std::min(falloff, 1));
    }
    else if ( !StriCmp(p1, "wave_push_force") )
        _profile->wave_push_force = ClampPushIntensity(parser.stof(p2, 0));
    else if ( !StriCmp(p1, "wave_push_radius") ||
              !StriCmp(p1, "wave_push_falloff") )
    {
        // Compatibility only: older profiles remain loadable, but the wave
        // contact push now uses wave_push_force alone.
        ypa_log_out("WARNING: SuperItem parameter '%s' is deprecated and ignored; wave_push_force now acts on wave contact.\n",
                    p1.c_str());
    }
    else if ( !StriCmp(p1, "fade_in") )
        _profile->fade_in = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "fade_out") )
        _profile->fade_out = NonNegativeFiniteMilliseconds(parser, p2);
    else if ( !StriCmp(p1, "wave_unit_damage") )
    {
        int damage = parser.stol(p2, NULL, 0);
        _profile->wave_unit_damage = damage > 0 ? damage : 0;
    }
    else if ( !StriCmp(p1, "wave_building_total_destruction") )
    {
        TAuthoredScalar value;
        _profile->wave_building_total_destruction = 0;
        if ( ParseAuthoredScalar(p2, value) && value.percent && value.value >= 0.0f )
            _profile->wave_building_total_destruction =
                (int)std::lround(std::min(value.value, 100.0f));
    }
    else if ( ParseDebuffParam(parser, p1, p2, _profile->debuff) )
    {}
    else if ( ParseSuperItemSoundEventParam(parser, "detonate", p1, p2,
                                             _profile->detonate_snd) )
    {}
    else if ( ParseSuperItemSoundEventParam(parser, "wave", p1, p2,
                                             _profile->wave_snd) )
    {}
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}


Common::Point MapSizesParser::ParseSizes(ScriptParser::Parser &parser)
{
    std::string tmp;
    parser.ReadLine(&tmp);

    Stok stok(tmp, " \r\n");

    std::string sX, sY;
    stok.GetNext(&sX);
    stok.GetNext(&sY);

    int32_t x = parser.stol(sX, NULL, 0);
    int32_t y = parser.stol(sY, NULL, 0);

    for(int i = 0; i < y; i++)
        parser.ReadLine(&tmp);

    return Common::Point(x, y);
}


int MapSizesParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "typ_map") )
    {
        _m.MapSize = ParseSizes(parser);
    }
    else if ( !StriCmp(p1, "own_map") || !StriCmp(p1, "hgt_map") || !StriCmp(p1, "blg_map") )
    {
        ParseSizes(parser);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

bool LevelDataParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_level") )
        return false;

    _o._levelInfo.MapName = "<NO NAME>";
    _gotLocalizedTitle = false;
    _o._levelInfo.MovieStr.clear();
    _o._levelInfo.MovieWinStr.clear();
    _o._levelInfo.MovieLoseStr.clear();
    _o._vehicleSectorRatio = 0;
    _o._levelUnitLimit = _o._defaultUnitLimit;
    _o._allowMultiBuildLevel = _o._allowMultiBuildWorld;
    _o._levelUnitLimitType = _o._defaultUnitLimitType;
    _o._levelUnitLimitArg = _o._defaultUnitLimitArg;
    _o._luaScriptName = "";
    _m.AtmosphericFXProfilePath.clear();
    return true;
}

int LevelDataParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( p1.find("title_") != std::string::npos )
    {
        std::string title_lang = std::string("title_") + Locale::Text::GetLocaleName();

        if ( !StriCmp(p1, title_lang) && IsUsableScriptText(p2) )
        {
                _o._levelInfo.MapName = p2;
                _gotLocalizedTitle = true;
        }
        else if ( !StriCmp(p1, "title_default") && !_gotLocalizedTitle && IsUsableScriptText(p2) )
        {
                _o._levelInfo.MapName = p2;
        }
    }
    else if ( !StriCmp(p1, "set") )
    {
        _m.SetID = parser.stol(p2, NULL, 0);
        _m.ReadedPartsBits |= TLevelDescription::BIT_SET;
    }
    else if ( !StriCmp(p1, "sky") )
    {
        _m.SkyStr = p2;
        _m.ReadedPartsBits |= TLevelDescription::BIT_SKY;
    }
    else if ( !StriCmp(p1, "typ") )
    {
        _m.TypStr = p2;
        _m.ReadedPartsBits |= TLevelDescription::BIT_TYP;
    }
    else if ( !StriCmp(p1, "own") )
    {
        _m.OwnStr = p2;
        _m.ReadedPartsBits |= TLevelDescription::BIT_OWN;
    }
    else if ( !StriCmp(p1, "hgt") )
    {
        _m.HgtStr = p2;
        _m.ReadedPartsBits |= TLevelDescription::BIT_HGT;
    }
    else if ( !StriCmp(p1, "blg") )
    {
        _m.BlgStr = p2;
        _m.ReadedPartsBits |= TLevelDescription::BIT_BLG;
    }
    else if ( !StriCmp(p1, "palette") )
    {
        _m.Palettes[0] = p2;
    }
    else if ( !StriCmp(p1, "slot0") )
    {
        _m.Palettes[0] = p2;
    }
    else if ( !StriCmp(p1, "slot1") )
    {
        _m.Palettes[1] = p2;
    }
    else if ( !StriCmp(p1, "slot2") )
    {
        _m.Palettes[2] = p2;
    }
    else if ( !StriCmp(p1, "slot3") )
    {
        _m.Palettes[3] = p2;
    }
    else if ( !StriCmp(p1, "slot4") )
    {
        _m.Palettes[4] = p2;
    }
    else if ( !StriCmp(p1, "slot5") )
    {
        _m.Palettes[5] = p2;
    }
    else if ( !StriCmp(p1, "slot6") )
    {
        _m.Palettes[6] = p2;
    }
    else if ( !StriCmp(p1, "slot7") )
    {
        _m.Palettes[7] = p2;
    }
    else if ( !StriCmp(p1, "script") )
    {
        if ( !_o.LoadProtosScript(p2) )
            return ScriptParser::RESULT_BAD_DATA;
        return ScriptParser::RESULT_OK;
    }
    else if ( !StriCmp(p1, "ambiencetrack") )
    {
        _o._levelInfo.MusicTrackMinDelay = 0;
        _o._levelInfo.MusicTrackMaxDelay = 0;

        Stok stok(p2, " \t_\n");
        std::string tmp;
        stok.GetNext(&tmp);
        _o._levelInfo.MusicTrack = parser.stol(tmp, NULL, 0);

        if ( stok.GetNext(&tmp) )
        {
            _o._levelInfo.MusicTrackMinDelay = parser.stol(tmp, NULL, 0);

            if ( stok.GetNext(&tmp) )
                _o._levelInfo.MusicTrackMaxDelay = parser.stol(tmp, NULL, 0);
        }
    }
    else if ( !StriCmp(p1, "ambient_sound") )
    {
        _m.AmbientSoundStr = p2;
    }
    else if ( !StriCmp(p1, "atmospheric_fx_profile") )
    {
        _m.AtmosphericFXProfilePath = p2;
    }
    else if ( !StriCmp(p1, "movie") )
    {
        _o._levelInfo.MovieStr = p2;
    }
    else if ( !StriCmp(p1, "win_movie") )
    {
        _o._levelInfo.MovieWinStr = p2;
    }
    else if ( !StriCmp(p1, "lose_movie") )
    {
        _o._levelInfo.MovieLoseStr = p2;
    }
    else if ( !StriCmp(p1, "event_loop") )
    {
        _m.EventLoopID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "slow_connection") )
    {
        if ( StrGetBool(p2) )
        {
            _m.SlowConnection = true;
        }
        else
        {
            _m.SlowConnection = false;
        }
    }
    else if ( !StriCmp(p1, "vehicle_sector_ratio") )
    {
        _o._vehicleSectorRatio = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_limit") )
    {
        _o._levelUnitLimit = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unit_limit_type") )
    {
        _o._levelUnitLimitType = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unit_limit_arg") )
    {
        _o._levelUnitLimitArg = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "lua_script") )
    {
        _o._luaScriptName = p2;
    }
    else if ( !StriCmp(p1, "multi_building") )
    {
        _o._allowMultiBuildLevel = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "hidden_fractions") )
    {
        _o._hiddenFractions = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

bool MapRobosParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if (StriCmp(word, "begin_robo"))
        return false;

    _m.Robos.emplace_back(); // Construct new element
    _r = &_m.Robos.back();
    _r->MbStatus = 0;
    return true;
}


int MapRobosParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        if (_m.Robos.size() == 1) //If it's first host station - save owner for brief
            _m.PlayerOwner = _r->Owner;

        _m.ReadedPartsBits |= TLevelDescription::BIT_END;
        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "owner") )
    {
        _r->Owner = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vehicle") )
    {
        _r->VhclID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "sec_x") )
    {
        int tmp = parser.stol(p2, NULL, 0);
        _r->Pos.y = -300;
        _r->Pos.x = tmp * CVSectorLength + CVSectorHalfLength;
    }
    else if ( !StriCmp(p1, "sec_y") )
    {
        int tmp = parser.stol(p2, NULL, 0);
        _r->Pos.y = -300;
        _r->Pos.z = -(tmp * CVSectorLength + CVSectorHalfLength);
    }
    else if ( !StriCmp(p1, "pos_x") )
    {
        _r->Pos.x = parser.stof(p2, 0) + 0.3;
    }
    else if ( !StriCmp(p1, "pos_y") )
    {
        _r->Pos.y = parser.stof(p2, 0) + 0.3;
    }
    else if ( !StriCmp(p1, "pos_z") )
    {
        _r->Pos.z = parser.stof(p2, 0) + 0.3;
    }
    else if ( !StriCmp(p1, "energy") )
    {
        _r->Energy = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "con_budget") )
    {
        _r->ConBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "rad_budget") )
    {
        _r->RadBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pow_budget") )
    {
        _r->PowBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "def_budget") )
    {
        _r->DefBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "saf_budget") )
    {
        _r->SafBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "rec_budget") )
    {
        _r->RecBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "cpl_budget") )
    {
        _r->CplBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "rob_budget") )
    {
        _r->RobBudget = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "viewangle") )
    {
        _r->ViewAngle = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "saf_delay") )
    {
        _r->SafDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pow_delay") )
    {
        _r->PowDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "rad_delay") )
    {
        _r->RadDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "cpl_delay") )
    {
        _r->CplDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "def_delay") )
    {
        _r->DefDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "con_delay") )
    {
        _r->ConDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "rec_delay") )
    {
        _r->RecDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "rob_delay") )
    {
        _r->RobDelay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mb_status") )
    {
        if ( !StriCmp(p2, "known") )
        {
            _r->MbStatus = 0;
        }
        else if ( !StriCmp(p2, "unknown") )
        {
            _r->MbStatus = 1;
        }
        else if ( !StriCmp(p2, "hidden") )
        {
            _r->MbStatus = 2;
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "reload_const") )
    {
        _r->ReloadConst = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}




int ShellSoundParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    struct ShellSoundNames
    {
        const std::string name;
        const int id;
    };

    static const ShellSoundNames block1[] =
    {
        {"volume", World::SOUND_ID_VOLUME},
        {"right", World::SOUND_ID_RIGHT},
        {"left", World::SOUND_ID_LEFT},
        {"button", World::SOUND_ID_BUTTON},
        {"quit", World::SOUND_ID_QUIT},
        {"slider", World::SOUND_ID_SLIDER},
        {"welcome", World::SOUND_ID_WELCOME},
        {"menuopen", World::SOUND_ID_MENUOPEN},
        {"overlevel", World::SOUND_ID_OVERLEVEL},
        {"levelselect", World::SOUND_ID_LEVELSEL},
        {"textappear", World::SOUND_ID_TEXTAPPEAR},
        {"objectappear", World::SOUND_ID_OBJAPPEAR},
        {"sectorconquered", World::SOUND_ID_SECTCONQ},
        {"vhcldestroyed", World::SOUND_ID_VHCLDESTR},
        {"bldgconquered", World::SOUND_ID_BLDGCONQ},
        {"timercount", World::SOUND_ID_TIMERCOUNT},
        {"select", World::SOUND_ID_SELECT},
        {"error", World::SOUND_ID_ERROR},
        {"attention", World::SOUND_ID_ATTEN},
        {"secret", World::SOUND_ID_SECRET},
        {"plasma", World::SOUND_ID_PLASMA}
    };

    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }
    else
    {
        std::string sm, tp;
        Stok stok(p1, "_");

        if (stok.GetNext(&sm) && stok.GetNext(&tp))
        {
            for (auto &t: block1)
            {
                if ( !StriCmp(t.name, sm) )
                {
                    if (!StriCmp("sample", tp))
                        return ( _o.LoadSample(t.id, p2) ? ScriptParser::RESULT_OK : ScriptParser::RESULT_BAD_DATA );
                    else if (!StriCmp("volume", tp))
                        _o.samples1_info.Sounds[t.id].Volume = parser.stoi(p2);
                    else if (!StriCmp("pitch", tp))
                        _o.samples1_info.Sounds[t.id].Pitch = parser.stoi(p2);
                    else
                        return ScriptParser::RESULT_UNKNOWN;

                    return ScriptParser::RESULT_OK;
                }
            }
        }

        return ScriptParser::RESULT_UNKNOWN;
    }

    return ScriptParser::RESULT_OK;
}

int ShellTracksParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    Stok stok(p2, " \t_\n");

    if ( !StriCmp(p1, "shelltrack") )
    {
        _o.shelltrack__adv.min_delay = 0;
        _o.shelltrack__adv.max_delay = 0;

        std::string val;
        stok.GetNext(&val);

        _o.shelltrack = parser.stol(val, NULL, 0);
        if ( stok.GetNext(&val) )
        {
            _o.shelltrack__adv.min_delay = parser.stol(val, NULL, 0);

            if ( stok.GetNext(&val) )
                _o.shelltrack__adv.max_delay = parser.stol(val, NULL, 0);
        }
    }
    else if ( !StriCmp(p1, "missiontrack") )
    {
        _o.missiontrack__adv.min_delay = 0;
        _o.missiontrack__adv.max_delay = 0;

        std::string val;
        stok.GetNext(&val);

        _o.missiontrack = parser.stol(val, NULL, 0);
        if ( stok.GetNext(&val) )
        {
            _o.missiontrack__adv.min_delay = parser.stol(val, NULL, 0);

            if ( stok.GetNext(&val) )
                _o.missiontrack__adv.max_delay = parser.stol(val, NULL, 0);
        }
    }
    else if ( !StriCmp(p1, "debriefingtrack") )
    {
        _o.debriefingtrack__adv.min_delay = 0;
        _o.debriefingtrack__adv.max_delay = 0;

        std::string val;
        stok.GetNext(&val);

        _o.debriefingtrack = parser.stol(val, NULL, 0);
        if ( stok.GetNext(&val) )
        {
            _o.debriefingtrack__adv.min_delay = parser.stol(val, NULL, 0);

            if ( stok.GetNext(&val) )
                _o.debriefingtrack__adv.max_delay = parser.stol(val, NULL, 0);
        }
    }
    else if ( !StriCmp(p1, "loadingtrack") )
    {
        _o.loadingtrack__adv.min_delay = 0;
        _o.loadingtrack__adv.max_delay = 0;

        std::string val;
        stok.GetNext(&val);

        _o.loadingtrack = parser.stol(val, NULL, 0);
        if ( stok.GetNext(&val) )
        {
            _o.loadingtrack__adv.min_delay = parser.stol(val, NULL, 0);

            if ( stok.GetNext(&val) )
                _o.loadingtrack__adv.max_delay = parser.stol(val, NULL, 0);
        }
    }
    else
        return ScriptParser::RESULT_UNKNOWN;
    return ScriptParser::RESULT_OK;
}



bool LevelSquadParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if (StriCmp(word, "begin_squad"))
        return false;

    _m.Squads.emplace_back();
    _s = &_m.Squads.back();
    return true;
}

int LevelSquadParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        if ( !_s->VhclID )
        {
            ypa_log_out("Squad init: squad[%d]аno vehicle defined!\n", _m.Squads.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( !_s->Count )
        {
            ypa_log_out("Squad init: squad[%d] num of vehicles is 0!\n", _m.Squads.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( _s->X == 0.0 || _s->Z == 0.0 )
        {
            ypa_log_out("Squad init: squad[%d] no pos given!\n", _m.Squads.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "owner") )
    {
        _s->Owner = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "useable") )
    {
        _s->Useable = true;
    }
    else if ( !StriCmp(p1, "vehicle") )
    {
        _s->VhclID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "num") )
    {
        _s->Count = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pos_x") )
    {
        _s->X = parser.stod(p2, 0) + 0.3;
    }
    else if ( !StriCmp(p1, "pos_z") )
    {
        _s->Z = parser.stod(p2, 0) + 0.3;
    }
    else if ( !StriCmp(p1, "mb_status") )
    {
        if ( !StriCmp(p2, "known") )
        {
            _s->MbStatus = 0;
        }
        else if ( !StriCmp(p2, "unknown") )
        {
            _s->MbStatus = 1;
        }
        else if ( !StriCmp(p2, "hidden") )
        {
            _s->MbStatus = 2;
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}


bool LevelGatesParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_gate") )
        return false;

    _o._levelInfo.Gates.emplace_back();
    _g = &_o._levelInfo.Gates.back();
    _g->MbStatus = 0;
    return true;
}

int LevelGatesParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        if ( !_g->ClosedBldID )
        {
            ypa_log_out("Gate init: gate[%d] no closed building defined!\n", _o._levelInfo.Gates.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( !_g->OpenBldID )
        {
            ypa_log_out("Gate init: gate[%d] no opened building defined!\n", _o._levelInfo.Gates.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( _g->CellId.x == 0 || _g->CellId.y == 0)
        {
            ypa_log_out("Gate init: gate[%d] no sector coords!\n", _o._levelInfo.Gates.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( !_g->PassToLevels.size() )
        {
            ypa_log_out("Gate init: gate[%d] no target levels defined!\n", _o._levelInfo.Gates.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "sec_x") )
    {
        _g->CellId.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "sec_y") )
    {
        _g->CellId.y = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "closed_bp") )
    {
        _g->ClosedBldID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "opened_bp") )
    {
        _g->OpenBldID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "target_level") )
    {
        _g->PassToLevels.push_back( parser.stol(p2, NULL, 0) );
    }
    else if ( !StriCmp(p1, "keysec_x") )
    {
        _g->KeySectors.emplace_back();
        _g->KeySectors.back().CellId.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "keysec_y") )
    {
        _g->KeySectors.back().CellId.y = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mb_status") )
    {
        if ( !StriCmp(p2, "known") )
        {
            _g->MbStatus = 0;
        }
        else if ( !StriCmp(p2, "unknown") )
        {
            _g->MbStatus = 1;
        }
        else if ( !StriCmp(p2, "hidden") )
        {
            _g->MbStatus = 2;
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}





bool LevelMbMapParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_mbmap") )
        return false;

    _m.Mbmaps.emplace_back();
    _d = &_m.Mbmaps.back();
    return true;
}

int LevelMbMapParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "name") )
    {
        _d->PicName = p2;
    }
    else if ( !StriCmp(p1, "size_x") )
    {
        _d->Size.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "size_y") )
    {
        _d->Size.y = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}




bool LevelGemParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_gem") )
        return false;

    _o._techUpgrades.emplace_back();
    _g = &_o._techUpgrades.back();
    _g->MbStatus = 0;
    return true;
}

static bool SetGemMbStatus(TMapGem *gem, const std::string &value)
{
    if ( !StriCmp(value, "known") )
        gem->MbStatus = 0;
    else if ( !StriCmp(value, "unknown") )
        gem->MbStatus = 1;
    else if ( !StriCmp(value, "hidden") )
        gem->MbStatus = 2;
    else
        return false;

    return true;
}

int LevelGemParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        if ( !_g->BuildingID )
        {
            ypa_log_out("WStein init: gem[%d] no building defined!\n", _o._techUpgrades.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( _g->CellId.x == 0 || _g->CellId.y == 0 )
        {
            ypa_log_out("WStein init: gem[%d] sector pos wonky tonk!\n", _o._techUpgrades.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( p1.find("msg_") != std::string::npos )
    {
        std::string tmp = fmt::sprintf("msg_%s", Locale::Text::GetLocaleName());

        if ( !StriCmp(p1, "msg_default") || !StriCmp(p1, tmp) )
            _g->MsgDefault = p2;
    }
    else if ( !StriCmp(p1, "sec_x") )
    {
        _g->CellId.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "sec_y") )
    {
        _g->CellId.y = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "building") )
    {
        _g->BuildingID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "type") )
    {
        _g->Type = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "script") )
    {
        _g->ScriptFile = p2;

        FSMgr::FileHandle *tmp = uaOpenFileAlloc(_g->ScriptFile, "r");

        if ( !tmp )
            return ScriptParser::RESULT_BAD_DATA;

        delete tmp;
    }
    else if ( !StriCmp(p1, "mb_status") )
    {
        if ( !SetGemMbStatus(_g, p2) )
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "nw_vproto_num") )
    {
        Stok stok(p2, "_ \t");
        std::string tmp;
        if ( stok.GetNext(&tmp) )
        {
            _g->NwVprotoNum1 = parser.stol(tmp, NULL, 0);
            if ( stok.GetNext(&tmp) )
            {
                _g->NwVprotoNum2 = parser.stol(tmp, NULL, 0);
                if ( stok.GetNext(&tmp) )
                {
                    _g->NwVprotoNum3 = parser.stol(tmp, NULL, 0);
                    if ( stok.GetNext(&tmp) )
                        _g->NwVprotoNum4 = parser.stol(tmp, NULL, 0);
                }
            }
        }
    }
    else if ( !StriCmp(p1, "nw_bproto_num") )
    {
        Stok stok(p2, "_ \t");
        std::string tmp;
        if ( stok.GetNext(&tmp) )
        {
            _g->NwBprotoNum1 = parser.stol(tmp, NULL, 0);
            if ( stok.GetNext(&tmp) )
            {
                _g->NwBprotoNum2 = parser.stol(tmp, NULL, 0);
                if ( stok.GetNext(&tmp) )
                {
                    _g->NwBprotoNum3 = parser.stol(tmp, NULL, 0);
                    if ( stok.GetNext(&tmp) )
                        _g->NwBprotoNum4 = parser.stol(tmp, NULL, 0);
                }
            }
        }
    }
    else if ( !StriCmp(p1, "begin_action") )
    {
        std::string tmp;

        while( parser.ReadLine(&tmp) && (tmp.find("end_action") == std::string::npos) )
        {
            std::string actionLine = tmp;
            size_t commentPos = actionLine.find_first_of(";\n\r");
            if ( commentPos != std::string::npos )
                actionLine.erase(commentPos);

            Stok stok(actionLine, "= \t");
            std::string actionKey;
            std::string actionValue;
            if ( stok.GetNext(&actionKey) && !StriCmp(actionKey, "mb_status") )
            {
                if ( !stok.GetNext(&actionValue) || !SetGemMbStatus(_g, actionValue) )
                    return ScriptParser::RESULT_BAD_DATA;

                continue;
            }

            _g->ActionsList.push_back(tmp);
        }
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}




bool LevelEnableParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_enable") )
        return false;

    _fraction = parser.stol(opt, NULL, 0);

    for (TVhclProto &vhcl : _o._vhclProtos)
        vhcl.disable_enable_bitmask &= ~(1 << _fraction);

    for (TBuildingProto &bld : _o._buildProtos)
        bld.EnableMask &= ~(1 << _fraction);

    return true;
}

int LevelEnableParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "vehicle") )
    {
        size_t id = parser.stol(p2, NULL, 0);
        if ( id >= 0 && id < _o._vhclProtos.size() ) //_o.ypaworld.VhclProtos.size() )
            _o._vhclProtos[id].disable_enable_bitmask |= (1 << _fraction);
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "building") )
    {
        size_t id = parser.stol(p2, NULL, 0);
        if ( id >= 0 && id < _o._buildProtos.size() )
            _o._buildProtos[id].EnableMask |= (1 << _fraction);
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}



bool LevelDebMapParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_dbmap") )
        return false;

    _m.Dbmaps.emplace_back();
    _d = &_m.Dbmaps.back();
    return true;
}

int LevelDebMapParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "name") )
    {
        _d->PicName = p2;
    }
    else if ( !StriCmp(p1, "size_x") )
    {
        _d->Size.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "size_y") )
    {
        _d->Size.y = parser.stol(p2, NULL, 0);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}


bool LevelSuperItemsParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_item") )
        return false;

    _o._levelInfo.SuperItems.emplace_back();

    _s = &_o._levelInfo.SuperItems.back();
    _s->Type = 0;
    _s->TimerValue = 60000; //1hour
    _s->State = TMapSuperItem::STATE_INACTIVE;
    _s->MbStatus = 0;
    return true;
}

int LevelSuperItemsParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        if ( _s->CellId.x == 0 || _s->CellId.y == 0)
        {
            ypa_log_out("Super item #%d: invalid sector coordinates!\n", _o._levelInfo.SuperItems.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( !_s->InactiveBldID )
        {
            ypa_log_out("Super item #%d: no <inactive_bp> defined!\n", _o._levelInfo.SuperItems.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( !_s->ActiveBldID )
        {
            ypa_log_out("Super item #%d: no <active_bp> defined!\n", _o._levelInfo.SuperItems.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( !_s->TriggerBldID )
        {
            ypa_log_out("Super item #%d: no <trigger_bp> defined!\n", _o._levelInfo.SuperItems.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        if ( _s->Type != TMapSuperItem::TYPE_BOMB && _s->Type != TMapSuperItem::TYPE_WAVE )
        {
            ypa_log_out("Super item #%d: no valid <type> defined!\n", _o._levelInfo.SuperItems.size() - 1);
            return ScriptParser::RESULT_BAD_DATA;
        }

        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "sec_x") )
    {
        _s->CellId.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "sec_y") )
    {
        _s->CellId.y = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "inactive_bp") )
    {
        _s->InactiveBldID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "active_bp") )
    {
        _s->ActiveBldID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "trigger_bp") )
    {
        _s->TriggerBldID = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "keysec_x") )
    {
        _s->KeySectors.emplace_back();
        _s->KeySectors.back().CellId.x = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "keysec_y") )
    {
        _s->KeySectors.back().CellId.y = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mb_status") )
    {
        if ( !StriCmp(p2, "known") )
        {
            _s->MbStatus = 0;
        }
        else if ( !StriCmp(p2, "unknown") )
        {
            _s->MbStatus = 1;
        }
        else if ( !StriCmp(p2, "hidden") )
        {
            _s->MbStatus = 2;
        }
        else
            return ScriptParser::RESULT_BAD_DATA;
    }
    else if ( !StriCmp(p1, "type") )
    {
        _s->Type = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "countdown") )
    {
        _s->TimerValue = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "profile") )
    {
        _s->ProfileId = p2;
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

Common::PlaneBytes MapAsPlaneBytes::ReadMapAsPlaneBytes(ScriptParser::Parser &parser)
{
    std::string buf;
    parser.ReadLine(&buf);

    std::string tmp;
    Stok stok(buf, " \t\r\n");
    stok.GetNext(&tmp);
    int w = parser.stol(tmp, NULL, 0);
    stok.GetNext(&tmp);
    int h = parser.stol(tmp, NULL, 0);

    if (w <= 0 || h <= 0)
        return Common::PlaneBytes();

    Common::PlaneBytes bmp(w, h);

    for (int j = 0; j < h; j++)
    {
        parser.ReadLine(&buf);
        stok = buf;

        uint8_t *ln = bmp.Line(j);

        for (int i = 0; i < w; i++)
        {
            stok.GetNext(&tmp);
            ln[i] = parser.stol(tmp, NULL, 16);
        }
    }

    return bmp;
}

bool LevelMapsParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_maps") )
        return false;

    _o._lvlTypeMap.Clear();
    _o._lvlOwnMap.Clear();
    _o._lvlHeightMap.Clear();
    _o._lvlBuildingsMap.Clear();

    return true;
}

int LevelMapsParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( !StriCmp(p1, "typ_map") )
    {
        _o._lvlTypeMap = ReadMapAsPlaneBytes(parser);

        if ( !_o._lvlTypeMap.IsOk() )
            return ScriptParser::RESULT_BAD_DATA;

        _m.MapSize = _o._lvlTypeMap.Size();

        _m.ReadedPartsBits |= TLevelDescription::BIT_TYP;
    }
    else if ( !StriCmp(p1, "own_map") )
    {
        _o._lvlOwnMap = ReadMapAsPlaneBytes(parser);
        if ( !_o._lvlOwnMap.IsOk() )
            return ScriptParser::RESULT_BAD_DATA;

        _m.ReadedPartsBits |= TLevelDescription::BIT_OWN;
    }
    else if ( !StriCmp(p1, "hgt_map") )
    {
        _o._lvlHeightMap = ReadMapAsPlaneBytes(parser);
        if ( !_o._lvlHeightMap.IsOk() )
            return ScriptParser::RESULT_BAD_DATA;

        _m.ReadedPartsBits |= TLevelDescription::BIT_HGT;
    }
    else if ( !StriCmp(p1, "blg_map") )
    {
        _o._lvlBuildingsMap = ReadMapAsPlaneBytes(parser);
        if ( !_o._lvlBuildingsMap.IsOk() )
            return ScriptParser::RESULT_BAD_DATA;

        _m.ReadedPartsBits |= TLevelDescription::BIT_BLG;
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}



int VideoParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if (!_initialized)
    {
        // Existing profiles predate interface_style. Reset each parsed video
        // block to the Nucleus.ini default so loading another profile cannot
        // inherit the previous profile's choice. A profile-side value, when
        // present, is parsed below and remains authoritative for that profile.
        const GFX::VirtualUIStyle defaultStyle =
            System::IniConf::UiRetroInterface.Get<bool>()
                ? GFX::VirtualUIStyle::RETRO
                : GFX::VirtualUIStyle::SMOOTH;
        _o._GameShell->interfaceStyle = defaultStyle;
        _o._GameShell->confInterfaceStyle = defaultStyle;
        GFX::Engine.SetVirtualUIStyle(defaultStyle);
        _initialized = true;
    }

    if ( !StriCmp(p1, "end") )
    {
        if ( GFX::Engine.getWDD_drawPrim() )
            _o._GameShell->GFXFlags |= World::GFX_FLAG_DRAWPRIMITIVES;
        else
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_DRAWPRIMITIVES;

        if ( GFX::Engine.getWDD_16bitTex() )
            _o._GameShell->GFXFlags |= World::GFX_FLAG_16BITTEXTURE;
        else
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_16BITTEXTURE;

        return ScriptParser::RESULT_SCOPE_END;
    }

    _o._GameShell->savedDataFlags |= World::SDF_VIDEO;

    if ( !StriCmp(p1, "videomode") )
    {
        int modeid = parser.stoi(p2);
        _o._gameDefaultRes = Common::Point((modeid >> 12 & 0xFFF), (modeid & 0xFFF));
        _o._GameShell->game_default_res = modeid;
    }
    else if ( !StriCmp(p1, "farview") )
    {
        if ( StrGetBool(p2) )
        {
            _o._GameShell->GFXFlags |= World::GFX_FLAG_FARVIEW;
            _o.SetFarView(true);
        }
        else
        {
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_FARVIEW;
            _o.SetFarView(false);
        }
    }
    else if ( !StriCmp(p1, "filtering") )
    {
    }
    else if ( !StriCmp(p1, "drawprimitive") )
    {
        if ( StrGetBool(p2) )
            _o._GameShell->GFXFlags |= World::GFX_FLAG_DRAWPRIMITIVES;
        else
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_DRAWPRIMITIVES;
    }
    else if ( !StriCmp(p1, "16bittexture") )
    {
        if ( StrGetBool(p2) )
            _o._GameShell->GFXFlags |= World::GFX_FLAG_16BITTEXTURE;
        else
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_16BITTEXTURE;
    }
    else if ( !StriCmp(p1, "softmouse") )
    {
        if ( StrGetBool(p2) )
        {
            _o._GameShell->GFXFlags |= World::GFX_FLAG_SOFTMOUSE;
            _o._preferences |= World::PREF_SOFTMOUSE;

            GFX::Engine.setWDD_cursor(1);
        }
        else
        {
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_SOFTMOUSE;
            _o._preferences &= ~World::PREF_SOFTMOUSE;

            GFX::Engine.setWDD_cursor(0);
        }
    }
    else if ( !StriCmp(p1, "palettefx") )
    {
    }
    else if ( !StriCmp(p1, "heaven") )
    {
        if ( StrGetBool(p2) )
        {
            _o._GameShell->GFXFlags |= World::GFX_FLAG_SKYRENDER;
            _o.setYW_skyRender(true);
        }
        else
        {
            _o._GameShell->GFXFlags &= ~World::GFX_FLAG_SKYRENDER;
            _o.setYW_skyRender(false);
        }
    }
    else if ( !StriCmp(p1, "fxnumber") )
    {
        _o._fxLimit = parser.stoi(p2);
        _o._GameShell->fxnumber = _o._fxLimit;
    }
    else if ( !StriCmp(p1, "default_view") )
    {
        // Retired profile key: keep old user.txt files loadable without restoring
        // POV as a saved default. OpenNeoUA always starts first-person play in cockpit.
        _o._GameShell->cockpitCameraRuntimeMode = true;
    }
    else if ( !StriCmp(p1, "interface_style") )
    {
        const GFX::VirtualUIStyle style = !StriCmp(p2, "smooth")
                                              ? GFX::VirtualUIStyle::SMOOTH
                                              : GFX::VirtualUIStyle::RETRO;
        _o._GameShell->interfaceStyle = style;
        _o._GameShell->confInterfaceStyle = style;
        GFX::Engine.SetVirtualUIStyle(style);
    }
    else if ( !StriCmp(p1, "palette_theme") )
    {
        std::string theme = p2;
        if (!StriCmp(theme, "Original"))
            theme.clear();

        _o._GameShell->paletteTheme = theme;
        _o._GameShell->confPaletteTheme = theme;
    }
    else if ( !StriCmp(p1, "enemyindicator") )
    {
        if ( StrGetBool(p2) )
        {
            _o._preferences |= World::PREF_ENEMYINDICATOR;
            _o._GameShell->enemyIndicator = true;
        }
        else
        {
            _o._preferences &= ~World::PREF_ENEMYINDICATOR;
            _o._GameShell->enemyIndicator = false;
        }
    }
    else if ( !StriCmp(p1, "gfxmode") )
    {
        Stok stok(p2, " _");
        std::string resW, resH, resWin;

        if ( stok.GetNext(&resW) && stok.GetNext(&resH) && stok.GetNext(&resWin))
        {
            int w = parser.stoi(resW);
            int h = parser.stoi(resH);
            int win = parser.stoi(resWin);

            const std::vector<GFX::GfxMode> &pModes = GFX::GFXEngine::Instance.GetAvailableModes();
            for (size_t i = 0; i < pModes.size(); ++i)
            {
                const GFX::GfxMode &mode = pModes.at(i);
                if (mode.w == w && mode.h == h)
                {
                    _o._GameShell->_gfxModeIndex = i;
                    _o._gfxMode = mode;

                    if (win)
                        _o._GameShell->GFXFlags |= World::GFX_FLAG_WINDOWED;
                    else
                        _o._GameShell->GFXFlags &= ~World::GFX_FLAG_WINDOWED;

                    _o._GameShell->_gfxMode = mode;
                    break;
                }
            }
        }
    }
    else
        return ScriptParser::RESULT_UNKNOWN;
    return ScriptParser::RESULT_OK;
}

int SoundParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    _o._GameShell->savedDataFlags |= World::SDF_SOUND;

    if ( !StriCmp(p1, "channels") )
    {
    }
    else if ( !StriCmp(p1, "volume") )
    {
        _o._GameShell->soundVolume = parser.stoi(p2);
        SFXEngine::SFXe.setMasterVolume(_o._GameShell->soundVolume);
    }
    else if ( !StriCmp(p1, "cdvolume") )
    {
        _o._GameShell->musicVolume = parser.stoi(p2);
        SFXEngine::SFXe.SetMusicVolume(_o._GameShell->musicVolume);
    }
    else if ( !StriCmp(p1, "invertlr") )
    {
        if ( !StriCmp(p2, "yes") )
        {
            _o._GameShell->soundFlags |= World::SF_INVERTLR;
            SFXEngine::SFXe.setReverseStereo(true);
        }
        else
        {
            _o._GameShell->soundFlags &= ~World::SF_INVERTLR;
            SFXEngine::SFXe.setReverseStereo(false);
        }
    }
    else if ( !StriCmp(p1, "sound") )
    {
    }
    else if ( !StriCmp(p1, "cdsound") )
    {
        if ( !StriCmp(p2, "yes") )
        {
            _o._GameShell->soundFlags |= World::SF_CDSOUND;
            _o._preferences |= World::PREF_CDMUSICDISABLE;

            SFXEngine::SFXe.SetMusicIgnoreCommandsFlag(true);
        }
        else
        {
            _o._GameShell->soundFlags &= ~World::SF_CDSOUND;
            _o._preferences &= ~World::PREF_CDMUSICDISABLE;

            SFXEngine::SFXe.SetMusicIgnoreCommandsFlag(false);
        }
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}


bool LevelStatusParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( StriCmp(word, "begin_levelstatus") )
        return false;

    _levelId = parser.stoi(opt);
    return true;
}

int LevelStatusParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    if ( _setFlag )
        _o._GameShell->savedDataFlags |= World::SDF_SCORE;

    if ( !StriCmp(p1, "status") )
    {
        if ( _o._globalMapRegions.MapRegions[_levelId].Status != TMapRegionInfo::STATUS_NONE )
            _o._globalMapRegions.MapRegions[_levelId].Status = parser.stoi(p2);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

bool BuddyParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if (!StriCmp(word, "begin_buddy"))
    {
        _o._levelInfo.Buddies.emplace_back();
        return true;
    }
    return false;
}

int BuddyParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        return ScriptParser::RESULT_SCOPE_END;
    }

    if ( !StriCmp(p1, "commandid") )
    {
        _o._levelInfo.Buddies.back().CommandID = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "type") )
    {
        _o._levelInfo.Buddies.back().Type = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "energy") )
    {
        _o._levelInfo.Buddies.back().Energy = parser.stoi(p2);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}

void ShellParser::ParseStatus(ScriptParser::Parser &parser, TMFWinStatus *status, const std::string &p2)
{
    Stok stok(p2, " _");
    std::string val;

    if ( stok.GetNext(&val) )
        status->Valid = parser.stoi(val) != 0;

    if ( stok.GetNext(&val) )
        status->IsOpen = parser.stoi(val) != 0;

    if ( stok.GetNext(&val) )
        status->Rect.x = parser.stoi(val);

    if ( stok.GetNext(&val) )
        status->Rect.y = parser.stoi(val);

    if ( stok.GetNext(&val) )
        status->Rect.w = parser.stoi(val);

    if ( stok.GetNext(&val) )
        status->Rect.h = parser.stoi(val);

    for (auto &x : status->Data)
    {
        if ( !stok.GetNext(&val) )
            break;

        x = parser.stoi(val);
    }
}


int ShellParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
    {
        _o._shellConfIsParsed = true;
        return ScriptParser::RESULT_SCOPE_END;
    }

    _o._GameShell->savedDataFlags |= World::SDF_SHELL;

    if ( !StriCmp(p1, "LANGUAGE") )
    {
        std::string * deflt = NULL;
        std::string * slct = NULL;

        for(std::string &s : _o._GameShell->lang_dlls)
        {
            if ( !StriCmp(s, p2) )
                slct = &s;
            if ( !StriCmp(s, "language") )
                deflt = &s;
        }

        if ( slct )
            _o._GameShell->default_lang_dll = slct;
        else
            _o._GameShell->default_lang_dll = deflt;

        _o._GameShell->prev_lang = _o._GameShell->default_lang_dll;

        if ( !_o.ReloadLanguage() )
        {
            ypa_log_out("Unable to set new language\n");
        }
    }
    else if ( !StriCmp(p1, "SOUND") || !StriCmp(p1, "VIDEO") ||
              !StriCmp(p1, "INPUT") || !StriCmp(p1, "DISK") ||
              !StriCmp(p1, "LOCALE") || !StriCmp(p1, "NET") ||
              !StriCmp(p1, "FINDER") || !StriCmp(p1, "LOG") ||
              !StriCmp(p1, "ENERGY") || !StriCmp(p1, "MESSAGE") ||
              !StriCmp(p1, "MAP") )
    {

    }
    else if ( !StriCmp(p1, "robo_map_status") )
    {
        ParseStatus(parser, &_o._roboMapStatus, p2);
    }
    else if ( !StriCmp(p1, "robo_finder_status") )
    {
        ParseStatus(parser, &_o._roboFinderStatus, p2);
    }
    else if ( !StriCmp(p1, "vhcl_map_status") )
    {
        ParseStatus(parser, &_o._vhclMapStatus, p2);
    }
    else if ( !StriCmp(p1, "vhcl_finder_status") )
    {
        ParseStatus(parser, &_o._vhclFinderStatus, p2);
    }
    else
        return ScriptParser::RESULT_UNKNOWN;

    return ScriptParser::RESULT_OK;
}




}
}
