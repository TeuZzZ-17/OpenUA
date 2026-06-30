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

#include <algorithm>

namespace World
{
namespace Parsers
{

static int ParseSampleVariantId(const std::string &token)
{
    if ( token.size() < 6 || StriCmp(token.substr(0, 6), "sample") )
        return -1;

    if ( token.size() == 6 )
        return 0;

    int variant = 0;
    for (size_t i = 6; i < token.size(); i++)
    {
        if ( token[i] < '0' || token[i] > '9' )
            return -1;

        variant = variant * 10 + (token[i] - '0');
    }

    if ( variant < 2 || variant > 8 )
        return -1;

    return variant - 1;
}

static int ParseNumberedSlotId(const std::string &key, const char *base, int maxSlots)
{
    std::string baseStr(base);

    if ( key.size() < baseStr.size() || StriCmp(key.substr(0, baseStr.size()), baseStr) )
        return -1;

    std::string suffix = key.substr(baseStr.size());
    if ( suffix.empty() )
        return 0;

    int slot = 0;
    for (char ch : suffix)
    {
        if ( ch < '0' || ch > '9' )
            return -1;

        slot = slot * 10 + (ch - '0');
    }

    if ( slot < 2 || slot > maxSlots )
        return -1;

    return slot - 1;
}

static void EnsureDamagedFXSlot(std::vector<int16_t> &slots, int slot)
{
    if ( slot < 0 )
        return;

    if ( slots.size() <= (size_t)slot )
        slots.resize(slot + 1, 0);
}

static void EnsureDebuffFXSlot(std::vector<int16_t> &slots, int slot)
{
    if ( slot < 0 )
        return;

    if ( slots.size() <= (size_t)slot )
        slots.resize(slot + 1, 0);
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
        for( UserData::TInputConf &k: _o._GameShell->InputConfig )
        {
            k.PKeyCode = 0;
            k.NKeyCode = 0;
        }
        return true;
    }
    else if ( !StriCmp(word, "modify_input") )
        return true;

    return false;
}

int InputParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

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

    switch (sndTP)
    {
        case 0:
        {
            int sampleVariant = ParseSampleVariantId(val);

            if ( sampleVariant >= 0 )
                sndfx->SetMainSampleVariant(sampleVariant, p2);
            else if ( !StriCmp(val, "volume") )
                sndfx->volume = parser.stol(p2, NULL, 0);
            else if ( !StriCmp(val, "pitch") )
                sndfx->pitch = parser.stol(p2, NULL, 0);
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
                           TVisualTint &tint);

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

    if ( !StriCmp(p1, "decoration_fx_mode") )
    {
        if ( !StriCmp(p2, "persistent") )
            config.mode = World::DECORATION_FX_PERSISTENT;
        else
            config.mode = World::DECORATION_FX_PERIODIC;

        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_interval_min") )
    {
        config.interval_min = parser.stol(p2, NULL, 0);
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_interval_max") )
    {
        config.interval_max = parser.stol(p2, NULL, 0);
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_count_min") )
    {
        int count = parser.stol(p2, NULL, 0);
        config.count_min = std::max(0, std::min(count, 32));
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_count_max") )
    {
        int count = parser.stol(p2, NULL, 0);
        config.count_max = std::max(0, std::min(count, 32));
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_duration") )
    {
        int duration = parser.stol(p2, NULL, 0);
        config.duration = duration > 0 ? duration : 1000;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_random_pos") )
    {
        float radius = parser.stof(p2, 0);
        config.random_pos = radius > 0.0 ? radius : 0.0;
        return true;
    }

    if ( !StriCmp(p1, "decoration_fx_scale") )
    {
        float scale = parser.stof(p2, 0);
        config.scale = scale > 0.0 ? scale : 1.0;
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

    if ( ParseTintParam(parser, "decoration_fx_tint", p1, p2, config.tint) )
    {
        config.has_tint = true;
        return true;
    }

    return false;
}

static bool ParseVisualScaleParam(ScriptParser::Parser &parser,
                                  const std::string &p1,
                                  const std::string &p2,
                                  float &fixedScale,
                                  uint8_t &mode,
                                  float &randomMin,
                                  float &randomMax,
                                  vec3d &axisScale)
{
    if ( !StriCmp(p1, "visual_scale") )
    {
        float scale = parser.stof(p2, 0);
        fixedScale = scale > 0.0 ? scale : 1.0;
        mode = VISUAL_SCALE_FIXED;
        return true;
    }

    if ( !StriCmp(p1, "visual_scale_random_min") )
    {
        float scale = parser.stof(p2, 0);
        randomMin = scale > 0.0 ? scale : 1.0;
        mode = VISUAL_SCALE_RANDOM;
        return true;
    }

    if ( !StriCmp(p1, "visual_scale_random_max") )
    {
        float scale = parser.stof(p2, 0);
        randomMax = scale > 0.0 ? scale : 1.0;
        mode = VISUAL_SCALE_RANDOM;
        return true;
    }

    if ( !StriCmp(p1, "visual_scale_x") )
    {
        float scale = parser.stof(p2, 0);
        axisScale.x = scale > 0.0 ? scale : 1.0;
        mode = VISUAL_SCALE_AXIS;
        return true;
    }

    if ( !StriCmp(p1, "visual_scale_y") )
    {
        float scale = parser.stof(p2, 0);
        axisScale.y = scale > 0.0 ? scale : 1.0;
        mode = VISUAL_SCALE_AXIS;
        return true;
    }

    if ( !StriCmp(p1, "visual_scale_z") )
    {
        float scale = parser.stof(p2, 0);
        axisScale.z = scale > 0.0 ? scale : 1.0;
        mode = VISUAL_SCALE_AXIS;
        return true;
    }

    return false;
}

static bool ParseVisualOrientationParam(ScriptParser::Parser &parser,
                                        const std::string &p1,
                                        const std::string &p2,
                                        vec3d &visualRotation)
{
    if ( !StriCmp(p1, "visual_orientation") )
    {
        if ( !StriCmp(p2, "normal") )
            visualRotation = vec3d(0.0, 0.0, 0.0);
        else if ( !StriCmp(p2, "upside_down") )
            visualRotation = vec3d(180.0, 0.0, 0.0);
        else if ( !StriCmp(p2, "half_turn") )
            visualRotation = vec3d(0.0, 180.0, 0.0);
        else if ( !StriCmp(p2, "sideways_left") )
            visualRotation = vec3d(0.0, 0.0, 90.0);
        else if ( !StriCmp(p2, "sideways_right") )
            visualRotation = vec3d(0.0, 0.0, 270.0);
        else
        {
            ypa_log_out("Unknown visual_orientation '%s', using normal\n", p2.c_str());
            visualRotation = vec3d(0.0, 0.0, 0.0);
        }

        return true;
    }

    if ( !StriCmp(p1, "visual_rotation_x") )
    {
        visualRotation.x = parser.stof(p2, 0);
        return true;
    }

    if ( !StriCmp(p1, "visual_rotation_y") )
    {
        visualRotation.y = parser.stof(p2, 0);
        return true;
    }

    if ( !StriCmp(p1, "visual_rotation_z") )
    {
        visualRotation.z = parser.stof(p2, 0);
        return true;
    }

    return false;
}

// OpenUA custom: parse "*_tint = R_G_B_A" (each component 0..255).
// Alpha is optional and defaults to 255. Out-of-range values are clamped.
// Stored as normalized 0..1 float multipliers. Neutral default = no change.
static bool ParseTintParam(ScriptParser::Parser &parser,
                           const std::string &paramName,
                           const std::string &p1,
                           const std::string &p2,
                           TVisualTint &tint)
{
    if ( StriCmp(p1, paramName) )
        return false;

    std::vector<std::string> parts = Stok::Split(p2, "_");

    auto clamp255 = [](long v) -> float
    {
        if ( v < 0 )
            v = 0;
        if ( v > 255 )
            v = 255;
        return (float)v / 255.0f;
    };

    long comp[4] = {255, 255, 255, 255};
    for (size_t i = 0; i < parts.size() && i < 4; i++)
        comp[i] = parser.stol(parts[i], 0, 10);

    tint.r = clamp255(comp[0]);
    tint.g = clamp255(comp[1]);
    tint.b = clamp255(comp[2]);
    tint.a = clamp255(comp[3]);
    return true;
}

static bool ParseVisualTintParam(ScriptParser::Parser &parser,
                                 const std::string &p1,
                                 const std::string &p2,
                                 TVisualTint &tint)
{
    return ParseTintParam(parser, "visual_tint", p1, p2, tint);
}

static bool ParseWireframeTintParam(ScriptParser::Parser &parser,
                                    const std::string &p1,
                                    const std::string &p2,
                                    TVisualTint &tint)
{
    return ParseTintParam(parser, "wireframe_tint", p1, p2, tint);
}

static World::TChainFXConfig::Trigger ParseChainFXTrigger(const std::string &name, bool weaponPrototype)
{
    if ( weaponPrototype )
    {
        if ( !StriCmp(name, "detonate") )
            return World::TChainFXConfig::TRIGGER_DETONATE;

        if ( !StriCmp(name, "impact_world") )
            return World::TChainFXConfig::TRIGGER_IMPACT_WORLD;
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

    return World::TChainFXConfig::MODE_VISUAL;
}

static int ParseChainFXBlock(ScriptParser::Parser &parser,
                             std::vector<World::TChainFXConfig> *out,
                             bool weaponPrototype)
{
    World::TChainFXConfig::Mode mode = World::TChainFXConfig::MODE_VISUAL;
    float startSize = 1.0;
    float endSize = 0.0;
    bool hasEndSize = false;
    vec3d offset;
    int duration = 0;
    std::vector<int16_t> vpModels;
    int physicalVehicle = 0;
    bool inheritVelocity = false;
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
                            weaponPrototype ? "weapon" : "vehicle");
                return ScriptParser::RESULT_OK;
            }

            if ( !hasEndSize )
                endSize = 0.0;

            if ( mode == World::TChainFXConfig::MODE_VISUAL )
            {
                if ( duration > 0 && !vpModels.empty() )
                {
                    World::TChainFXConfig chain;
                    chain.mode = mode;
                    chain.trigger = trigger;
                    chain.offset = offset;
                    chain.start_size = startSize;
                    chain.end_size = endSize;
                    chain.duration = duration;
                    chain.vp_models = vpModels;
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
                    chain.inherit_velocity = inheritVelocity;
                    out->push_back(chain);
                }
                else
                {
                    ypa_log_out("WARNING: begin_chain_fx physical mode without physical_vehicle ignored\n");
                }
            }

            return ScriptParser::RESULT_OK;
        }

        if ( badMode )
            continue;

        if ( !StriCmp(p1, "mode") )
        {
            if ( !StriCmp(p2, "visual") || !StriCmp(p2, "physical") )
                mode = ParseChainFXMode(p2);
            else
            {
                ypa_log_out("WARNING: Unknown begin_chain_fx mode '%s' ignored\n", p2.c_str());
                badMode = true;
            }
        }
        else if ( !StriCmp(p1, "trigger") )
        {
            trigger = ParseChainFXTrigger(p2, weaponPrototype);
            hasTrigger = true;
            if ( trigger == World::TChainFXConfig::TRIGGER_NONE )
            {
                ypa_log_out("WARNING: Unknown or unsupported begin_chain_fx trigger '%s' ignored\n", p2.c_str());
                badTrigger = true;
            }
        }
        else if ( !StriCmp(p1, "start_size") )
            startSize = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "end_size") )
        {
            endSize = parser.stof(p2, 0);
            hasEndSize = true;
        }
        else if ( !StriCmp(p1, "duration") )
            duration = parser.stol(p2, NULL, 0);
        else if ( !StriCmp(p1, "offset_x") )
            offset.x = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "offset_y") )
            offset.y = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "offset_z") )
            offset.z = parser.stof(p2, 0);
        else if ( !StriCmp(p1, "vp_model") )
            vpModels.push_back(parser.stol(p2, NULL, 0));
        else if ( !StriCmp(p1, "physical_vehicle") )
            physicalVehicle = parser.stol(p2, NULL, 0);
        else if ( !StriCmp(p1, "inherit_velocity") )
            inheritVelocity = parser.stol(p2, NULL, 0) != 0;
        else
            return ScriptParser::RESULT_UNKNOWN;
    }

    return ScriptParser::RESULT_UNEXP_EOF;
}

static int ParseVehicleChainFXBlock(ScriptParser::Parser &parser, TVhclProto *vhcl)
{
    return ParseChainFXBlock(parser,
                             &vhcl->chain_fx,
                             false);
}

static int ParseWeaponChainFXBlock(ScriptParser::Parser &parser, TWeapProto *wpn)
{
    return ParseChainFXBlock(parser,
                             &wpn->chain_fx,
                             true);
}

int VhclProtoParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    TRoboProto *robo = _vhcl->RoboProto;
    int damagedFxSlot = -1;
    
    if (!robo)
        robo = &_roboTmp;    

    auto getUnitGun = [this]() -> TRoboGun *
    {
        if ( _unitGunID < 0 || (size_t)_unitGunID >= _vhcl->unit_guns.size() )
            return NULL;

        return &_vhcl->unit_guns.at(_unitGunID);
    };

    auto getUnitDummy = [this]() -> TUnitDummy *
    {
        if ( _unitDummyID < 0 || (size_t)_unitDummyID >= _vhcl->unit_dummies.size() )
            return NULL;

        return &_vhcl->unit_dummies.at(_unitDummyID);
    };

    auto getColl = [this]() -> TRoboColl *
    {
        if ( _collID < 0 || (size_t)_collID >= _vhcl->coll.roboColls.size() )
            return NULL;

        return &_vhcl->coll.roboColls.at(_collID);
    };

    if ( !StriCmp(p1, "end") )
    {
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

    if ( !StriCmp(p1, "model") )
    {
        if ( !StriCmp(p2, "heli") )
        {
            _vhcl->model_id = BACT_TYPES_BACT;
        }
        else if ( !StriCmp(p2, "tank") )
        {
            _vhcl->model_id = BACT_TYPES_TANK;
        }
        else if ( !StriCmp(p2, "robo") )
        {
            _vhcl->model_id = BACT_TYPES_ROBO;

            *robo = TRoboProto();
            robo->matrix = mat3x3::Ident();
        }
        else if ( !StriCmp(p2, "ufo") )
        {
            _vhcl->model_id = BACT_TYPES_UFO;
        }
        else if ( !StriCmp(p2, "car") )
        {
            _vhcl->model_id = BACT_TYPES_CAR;
        }
        else if ( !StriCmp(p2, "gun") )
        {
            _vhcl->model_id = BACT_TYPES_GUN;
        }
        else if ( !StriCmp(p2, "hover") )
        {
            _vhcl->model_id = BACT_TYPES_HOVER;
        }
        else if ( !StriCmp(p2, "plane") )
        {
            _vhcl->model_id = BACT_TYPES_FLYER;

            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)3);
        }
        else if ( !StriCmp(p2, "glider") )
        {
            _vhcl->model_id = BACT_TYPES_FLYER;
            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)2);
        }
        else if ( !StriCmp(p2, "zeppelin") )
        {
            _vhcl->model_id = BACT_TYPES_FLYER;
            _vhcl->initParams.Add(NC_STACK_ypaflyer::FLY_ATT_TYPE, (int32_t)0);
        }
        else if ( !StriCmp(p2, "dummy") )
        {
            // OpenUA custom: dummy modular attachment prototype.
            // Reuse the (immobile, attachable) gun runtime class as the visual
            // carrier; the is_dummy flag makes the runtime object fully inert.
            _vhcl->model_id = BACT_TYPES_GUN;
            _vhcl->is_dummy = 1;
        }
        else
        {
            return ScriptParser::RESULT_BAD_DATA;
        }
    }
    else if ( !StriCmp(p1, "enable") )
    {
        _vhcl->disable_enable_bitmask |= 1 << parser.stol(p2, NULL, 0);
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
    else if ( !StriCmp(p1, "radar") )
    {
        _vhcl->radar = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_energy") )
    {
        _vhcl->energy += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_shield") )
    {
        _vhcl->shield += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_radar") )
    {
        _vhcl->radar += parser.stol(p2, NULL, 0);
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
    else if ( (damagedFxSlot = ParseNumberedSlotId(p1, "damaged_fx_vp", World::DAMAGED_FX_SLOT_COUNT)) >= 0 )
    {
        int vp = parser.stol(p2, NULL, 0);
        EnsureDamagedFXSlot(_vhcl->damaged_fx.vps, damagedFxSlot);
        _vhcl->damaged_fx.vps[damagedFxSlot] = vp > 0 ? vp : 0;
    }
    else if ( !StriCmp(p1, "damaged_fx_threshold") )
    {
        float threshold = parser.stof(p2, 0);

        if ( threshold < 0.0 )
            threshold = 0.0;
        else if ( threshold > 1.0 )
            threshold = 1.0;

        _vhcl->damaged_fx.threshold = threshold;
    }
    else if ( !StriCmp(p1, "damaged_fx_interval_min") )
    {
        _vhcl->damaged_fx.interval_min = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "damaged_fx_interval_max") )
    {
        _vhcl->damaged_fx.interval_max = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "damaged_fx_random_pos") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->damaged_fx.random_pos = radius > 0.0 ? radius : 0.0;
    }
    else if ( ParseDecorationFXParam(parser, p1, p2, _vhcl->decoration_fx) )
    {
    }
    else if ( !StriCmp(p1, "damaged_icon") )
    {
        _vhcl->damaged_icon = p2;
    }
    else if ( !StriCmp(p1, "regen_icon") )
    {
        _vhcl->regen_icon = p2;
    }
    else if ( !StriCmp(p1, "drain_icon") )
    {
        _vhcl->drain_icon = p2;
    }
    else if ( !StriCmp(p1, "spawn_icon") )
    {
        _vhcl->spawn_icon = p2;
    }
    else if ( !StriCmp(p1, "radar_icon") )
    {
        _vhcl->radar_icon = p2;
    }
    else if ( !StriCmp(p1, "unit_gun_icon") )
    {
        if (TRoboGun *gun = getUnitGun())
            gun->icon = p2;
        else
            _vhcl->unit_gun_icon = p2;
    }
    else if ( !StriCmp(p1, "power_icon") )
    {
        _vhcl->power_icon = p2;
    }
    else if ( !StriCmp(p1, "seek_and_explode_icon") )
    {
        _vhcl->seek_and_explode_icon = p2;
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
    else if ( !StriCmp(p1, "shk_damaged_slot") )
    {
        _vhcl->damaged_fx.shake.slot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_damaged_mag0") )
    {
        _vhcl->damaged_fx.shake.mag0 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_damaged_mag1") )
    {
        _vhcl->damaged_fx.shake.mag1 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_damaged_time") )
    {
        _vhcl->damaged_fx.shake.time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_damaged_mute") )
    {
        _vhcl->damaged_fx.shake.mute = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "damaged_force_malus") )
    {
        float malus = parser.stof(p2, 0);
        if ( malus < 0.0 )
            malus = 0.0;
        else if ( malus > 1.0 )
            malus = 1.0;
        _vhcl->damaged_force_malus = malus;
    }
    else if ( !StriCmp(p1, "damaged_maxrot_malus") )
    {
        float malus = parser.stof(p2, 0);
        if ( malus < 0.0 )
            malus = 0.0;
        else if ( malus > 1.0 )
            malus = 1.0;
        _vhcl->damaged_maxrot_malus = malus;
    }
    else if ( !StriCmp(p1, "damaged_snd_pitch_mult") )
    {
        float mult = parser.stof(p2, 0);
        _vhcl->damaged_snd_pitch_mult = mult >= 0.0 ? mult : 1.0;
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
    else if ( !StriCmp(p1, "death_damage") )
    {
        int damage = parser.stol(p2, NULL, 0);
        _vhcl->death_damage = damage > 0 ? damage : 0;
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
    else if ( !StriCmp(p1, "proximity_defense_fire_x") )
    {
        _vhcl->proximity_defense_fire_pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_fire_y") )
    {
        _vhcl->proximity_defense_fire_pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_fire_z") )
    {
        _vhcl->proximity_defense_fire_pos.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_vp_launch") )
    {
        int vp = parser.stol(p2, NULL, 0);
        _vhcl->proximity_defense_vp_launch = vp > 0 ? vp : -1;
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
    else if ( !StriCmp(p1, "proximity_defense_at_death") )
    {
        _vhcl->proximity_defense_at_death = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "proximity_defense_random_yaw_min") )
    {
        _vhcl->proximity_defense_random_yaw_set = true;
        _vhcl->proximity_defense_random_yaw_min = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_random_yaw_max") )
    {
        _vhcl->proximity_defense_random_yaw_set = true;
        _vhcl->proximity_defense_random_yaw_max = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_random_pitch_min") )
    {
        _vhcl->proximity_defense_random_pitch_set = true;
        _vhcl->proximity_defense_random_pitch_min = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "proximity_defense_random_pitch_max") )
    {
        _vhcl->proximity_defense_random_pitch_set = true;
        _vhcl->proximity_defense_random_pitch_max = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "seek_and_explode") )
    {
        _vhcl->seek_and_explode = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "seek_and_explode_weapon") )
    {
        int weaponId = parser.stol(p2, NULL, 0);
        _vhcl->seek_and_explode_weapon = weaponId > 0 ? weaponId : 0;
    }
    else if ( !StriCmp(p1, "seek_and_explode_trigger_radius") )
    {
        float radius = parser.stof(p2, 0);
        _vhcl->seek_and_explode_trigger_radius = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "ai_max_active_at_once") )
    {
        int maxActive = parser.stol(p2, NULL, 0);
        _vhcl->ai_max_active_at_once = maxActive > 0 ? maxActive : 0;
    }
    else if ( ParseVisualScaleParam(parser, p1, p2,
                                    _vhcl->visual_scale,
                                    _vhcl->visual_scale_mode,
                                    _vhcl->visual_scale_random_min,
                                    _vhcl->visual_scale_random_max,
                                    _vhcl->visual_scale_axis) )
    {
    }
    else if ( ParseVisualTintParam(parser, p1, p2, _vhcl->visual_tint) )
    {
    }
    else if ( ParseWireframeTintParam(parser, p1, p2, _vhcl->wireframe_tint) )
    {
    }
    else if ( ParseVisualOrientationParam(parser, p1, p2, _vhcl->visual_rotation) )
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
    else if ( !StriCmp(p1, "weapon2") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->extra_weapons[0] = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "weapon3") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->extra_weapons[1] = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "weapon4") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->extra_weapons[2] = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "lowhp_weapon_enable") )
    {
        _vhcl->lowhp_weapon_enable = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "lowhp_threshold") )
    {
        float threshold = parser.stof(p2, 0);
        _vhcl->lowhp_threshold = threshold > 0.0 ? threshold : 0.30;
    }
    else if ( !StriCmp(p1, "lowhp_weapon") )
    {
        int weapon = parser.stol(p2, NULL, 0);
        _vhcl->lowhp_weapon = weapon > 0 ? weapon : 0;
    }
    else if ( !StriCmp(p1, "weapon_switch_mode") )
    {
        if ( !StriCmp(p2, "random") )
            _vhcl->weapon_switch_mode = 1;
        else
            _vhcl->weapon_switch_mode = 0;
    }
    else if ( !StriCmp(p1, "mgun") )
    {
        _vhcl->mgun = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "num_mguns") )
    {
        int numMguns = parser.stol(p2, NULL, 0);
        _vhcl->num_mguns = numMguns > 0 ? numMguns : 1;
    }
    else if ( !StriCmp(p1, "weapon_spread_x") )
    {
        _vhcl->weapon_spread_x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "weapon_spread_y") )
    {
        _vhcl->weapon_spread_y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "mgun_spread_x") )
    {
        _vhcl->mgun_spread_x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "mgun_spread_y") )
    {
        _vhcl->mgun_spread_y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "weapon_spread_x_user") )
    {
        _vhcl->weapon_spread_x_user = parser.stof(p2, 0);
        _vhcl->weapon_spread_x_user_set = true;
    }
    else if ( !StriCmp(p1, "weapon_spread_y_user") )
    {
        _vhcl->weapon_spread_y_user = parser.stof(p2, 0);
        _vhcl->weapon_spread_y_user_set = true;
    }
    else if ( !StriCmp(p1, "mgun_spread_x_user") )
    {
        _vhcl->mgun_spread_x_user = parser.stof(p2, 0);
        _vhcl->mgun_spread_x_user_set = true;
    }
    else if ( !StriCmp(p1, "mgun_spread_y_user") )
    {
        _vhcl->mgun_spread_y_user = parser.stof(p2, 0);
        _vhcl->mgun_spread_y_user_set = true;
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
    else if ( !StriCmp(p1, "mgun_fire_x") )
    {
        _vhcl->mgun_fire_x = parser.stof(p2, 0);
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
        _vhcl->num_weapons = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "kill_after_shot") )
    {
        _vhcl->kill_after_shot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "job_fighthelicopter") )
    {
        _vhcl->job_fighthelicopter = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "job_fightflyer") )
    {
        _vhcl->job_fightflyer = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "job_fighttank") )
    {
        _vhcl->job_fighttank = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "job_fightrobo") )
    {
        _vhcl->job_fightrobo = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "job_reconnoitre") )
    {
        _vhcl->job_reconnoitre = parser.stoi(p2);
    }
    else if ( !StriCmp(p1, "job_conquer") )
    {
        _vhcl->job_conquer = parser.stoi(p2);
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
        int gun_type = 0;
        if ( !StriCmp(p2, "flak") )
        {
            gun_type = 1;
        }
        else if ( !StriCmp(p2, "mg") )
        {
            gun_type = 2;
        }
        else
        {
            //StriCmp(p2, "dummy");
        }

        if ( gun_type )
            _vhcl->initParams.Add(NC_STACK_ypagun::GUN_ATT_FIRETYPE, (int32_t)gun_type);
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
    }
    else if ( !StriCmp(p1, "robo_act_gun") )
    {
        _gunID = parser.stol(p2, NULL, 0);
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

        _vhcl->unit_guns.resize(cnt);

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

        if ( (size_t)_unitGunID >= _vhcl->unit_guns.size() )
            _vhcl->unit_guns.resize(_unitGunID + 1);
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
    // ---- OpenUA custom: modular dummy attachments (parent side) ----
    else if ( !StriCmp(p1, "unit_num_dummies") )
    {
        int cnt = parser.stol(p2, NULL, 0);

        if ( cnt < 0 )
            cnt = 0;
        else if ( cnt > (int)UNIT_DUMMY_MAX_COUNT )
            cnt = UNIT_DUMMY_MAX_COUNT;

        _vhcl->unit_dummies.resize(cnt);

        if ( _unitDummyID >= cnt )
            _unitDummyID = cnt - 1;
    }
    else if ( !StriCmp(p1, "unit_act_dummy") )
    {
        _unitDummyID = parser.stol(p2, NULL, 0);

        if ( _unitDummyID < 0 )
            _unitDummyID = 0;

        if ( _unitDummyID >= (int)UNIT_DUMMY_MAX_COUNT )
            _unitDummyID = UNIT_DUMMY_MAX_COUNT - 1;

        if ( (size_t)_unitDummyID >= _vhcl->unit_dummies.size() )
            _vhcl->unit_dummies.resize(_unitDummyID + 1);
    }
    else if ( !StriCmp(p1, "unit_dummy_vehicle") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->vehicle_id = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_pos_x") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_pos_y") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_pos_z") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->pos.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_dir_x") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->dir.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_dir_y") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->dir.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_dir_z") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->dir.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "unit_dummy_protect") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->protect = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "unit_dummy_destroy_with_parent") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->destroy_with_parent = parser.stol(p2, NULL, 0) ? 1 : 0;
    }
    else if ( !StriCmp(p1, "unit_dummy_hide_when_destroyed") )
    {
        if (TUnitDummy *dmy = getUnitDummy())
            dmy->hide_when_destroyed = parser.stol(p2, NULL, 0) ? 1 : 0;
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
    // ---- OpenUA custom: universal compound collision spheres (any vehicle) ----
    // robo_coll_* below stays untouched for Robo/Host Station; coll_* writes into
    // the vehicle prototype's own compound-sphere set (bounds-checked).
    else if ( !StriCmp(p1, "coll_num") )
    {
        int cnt = parser.stol(p2, NULL, 0);

        if ( cnt < 0 )
            cnt = 0;
        else if ( cnt > (int)UNIT_COLL_MAX_COUNT )
            cnt = UNIT_COLL_MAX_COUNT;

        _vhcl->coll.roboColls.resize(cnt);

        if ( _collID >= cnt )
            _collID = cnt - 1;
    }
    else if ( !StriCmp(p1, "coll_act") )
    {
        _collID = parser.stol(p2, NULL, 0);

        if ( _collID < 0 )
            _collID = 0;

        if ( _collID >= (int)UNIT_COLL_MAX_COUNT )
            _collID = UNIT_COLL_MAX_COUNT - 1;

        if ( (size_t)_collID >= _vhcl->coll.roboColls.size() )
            _vhcl->coll.roboColls.resize(_collID + 1);
    }
    else if ( !StriCmp(p1, "coll_radius") )
    {
        if (TRoboColl *c = getColl())
            c->robo_coll_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "coll_x") )
    {
        if (TRoboColl *c = getColl())
            c->coll_pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "coll_y") )
    {
        if (TRoboColl *c = getColl())
            c->coll_pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "coll_z") )
    {
        if (TRoboColl *c = getColl())
            c->coll_pos.z = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_coll_num") )
    {
        robo->coll.roboColls.resize( parser.stol(p2, NULL, 0) );
    }
    else if ( !StriCmp(p1, "robo_coll_act") )
    {
        _collID = parser.stol(p2, NULL, 0);         
    }
    else if ( !StriCmp(p1, "robo_coll_radius") )
    {
        robo->coll.roboColls[ _collID ].robo_coll_radius = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_coll_x") )
    {
        robo->coll.roboColls[ _collID ].coll_pos.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_coll_y") )
    {
        robo->coll.roboColls[ _collID ].coll_pos.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "robo_coll_z") )
    {
        robo->coll.roboColls[ _collID ].coll_pos.z = parser.stof(p2, 0);
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
        if (IsModsAllow(true))
            _vhcl->hidden = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "invisible") )
    {
        // OpenUA custom: vehicle-only total-stealth-until-first-attack flag.
        // Deliberately separate from the legacy "hidden"/"unhide_radar" system.
        if (IsModsAllow(true))
            _vhcl->invisible = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "invisible_reveal_vp") )
    {
        if (IsModsAllow(true))
            _vhcl->invisible_reveal_vp = (int16_t)parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "unhide_radar") )
    {
        if (IsModsAllow(true))
        {
            _vhcl->unhideRadar = parser.stol(p2, NULL, 0);

            if (_vhcl->unhideRadar < 0)
                _vhcl->unhideRadar = 0;
            else if (_vhcl->unhideRadar > _vhcl->radar)
                _vhcl->unhideRadar = _vhcl->radar + 1;
        }
    }
    else if ( !StriCmp(p1, "add_unhide_radar") )
    {
        if (IsModsAllow(true))
        {
            _vhcl->unhideRadar += parser.stol(p2, NULL, 0);

            if (_vhcl->unhideRadar < 0)
                _vhcl->unhideRadar = 0;
            else if (_vhcl->unhideRadar > _vhcl->radar)
                _vhcl->unhideRadar = _vhcl->radar + 1;
        }
    }
    else
        return ParseSndFX(parser, p1, p2);

    return ScriptParser::RESULT_OK;
}


bool VhclProtoParser::IsScope(ScriptParser::Parser &parser, const std::string &word, const std::string &opt)
{
    if ( !StriCmp(word, "new_vehicle") )
    {
        _roboTmp = TRoboProto();
        _gunID = -1;
        _unitGunID = -1;
        _unitDummyID = -1;
        _collID = -1;
        _vhclID = parser.stol(opt, NULL, 0);
        _vhcl = &_o._vhclProtos.at(_vhclID);
        
        *_vhcl = TVhclProto();
        
        _vhcl->Index = _vhclID;

        _vhcl->model_id = BACT_TYPES_TANK;
        _vhcl->weapon = -1;
        _vhcl->extra_weapons = {0, 0, 0};
        _vhcl->weapon_switch_mode = 0;
        _vhcl->lowhp_weapon_enable = 0;
        _vhcl->lowhp_threshold = 0.30;
        _vhcl->lowhp_weapon = 0;
        _vhcl->mgun = -1;
        _vhcl->num_mguns = 1;
        _vhcl->weapon_spread_x = 0.0;
        _vhcl->weapon_spread_y = 0.0;
        _vhcl->mgun_spread_x = 0.0;
        _vhcl->mgun_spread_y = 0.0;
        _vhcl->weapon_spread_x_user = 0.0;
        _vhcl->weapon_spread_y_user = 0.0;
        _vhcl->mgun_spread_x_user = 0.0;
        _vhcl->mgun_spread_y_user = 0.0;
        _vhcl->weapon_spread_x_user_set = false;
        _vhcl->weapon_spread_y_user_set = false;
        _vhcl->mgun_spread_x_user_set = false;
        _vhcl->mgun_spread_y_user_set = false;
        _vhcl->type_icon = 65;
        _vhcl->vp_normal = 0;
        _vhcl->vp_fire = 1;
        _vhcl->vp_megadeth = 2;
        _vhcl->vp_wait = 3;
        _vhcl->vp_dead = 4;
        _vhcl->vp_genesis = 5;
        _vhcl->visual_scale = 1.0;
        _vhcl->visual_scale_mode = VISUAL_SCALE_FIXED;
        _vhcl->visual_scale_random_min = 1.0;
        _vhcl->visual_scale_random_max = 1.0;
        _vhcl->visual_scale_axis = vec3d(1.0, 1.0, 1.0);
        _vhcl->visual_tint = TVisualTint();
        _vhcl->wireframe_tint = TVisualTint();
        _vhcl->visual_rotation = vec3d(0.0, 0.0, 0.0);
        _vhcl->damaged_fx = TDamagedFXConfig();
        _vhcl->damaged_icon.clear();
        _vhcl->regen_icon.clear();
        _vhcl->drain_icon.clear();
        _vhcl->spawn_icon.clear();
        _vhcl->radar_icon.clear();
        _vhcl->unit_gun_icon.clear();
        _vhcl->power_icon.clear();
        _vhcl->seek_and_explode_icon.clear();
        _vhcl->power = 0;
        _vhcl->power_radius = 0.0;
        _vhcl->damaged_force_malus = 0.0;
        _vhcl->damaged_maxrot_malus = 0.0;
        _vhcl->damaged_snd_pitch_mult = 1.0;
        _vhcl->spawn_units = 0;
        _vhcl->spawn_vehicle = 0;
        _vhcl->spawn_interval = 5000;
        _vhcl->spawn_trigger_radius = 0.0;
        _vhcl->spawn_random_pos = 0.0;
        _vhcl->spawn_max_active = 0;
        _vhcl->spawn_count = 1;
        _vhcl->spawn_instant = 0;
        _vhcl->spawn_at_death_units = 0;
        _vhcl->spawn_at_death_vehicle = 0;
        _vhcl->spawn_at_death_count = 1;
        _vhcl->spawn_at_death_random_pos = 0.0;
        _vhcl->spawn_at_death_instant = 0;
        _vhcl->spawn_at_death_immunity_time = 0;
        _vhcl->death_damage = 0;
        _vhcl->proximity_defense_enable = 0;
        _vhcl->proximity_defense_weapon = 0;
        _vhcl->proximity_defense_trigger_radius = 0.0;
        _vhcl->proximity_defense_interval = 1000;
        _vhcl->proximity_defense_shots = 12;
        _vhcl->proximity_defense_fire_pos = vec3d(0.0, 0.0, 0.0);
        _vhcl->proximity_defense_vp_launch = -1;
        _vhcl->proximity_defense_fire_mode = 0;
        _vhcl->proximity_defense_sequence_delay = 100;
        _vhcl->proximity_defense_at_death = 0;
        _vhcl->proximity_defense_random_yaw_set = false;
        _vhcl->proximity_defense_random_yaw_min = 0.0;
        _vhcl->proximity_defense_random_yaw_max = 360.0;
        _vhcl->proximity_defense_random_pitch_set = false;
        _vhcl->proximity_defense_random_pitch_min = -10.0;
        _vhcl->proximity_defense_random_pitch_max = 45.0;
        _vhcl->seek_and_explode = 0;
        _vhcl->seek_and_explode_weapon = 0;
        _vhcl->seek_and_explode_trigger_radius = 0.0;
        _vhcl->ai_max_active_at_once = 0;
        _vhcl->shield = 50;
        _vhcl->energy = 10000;
        _vhcl->adist_sector = 800.0;
        _vhcl->adist_bact = 650.0;
        _vhcl->sdist_sector = 200.0;
        _vhcl->sdist_bact = 100.0;
        _vhcl->radar = 1;
        _vhcl->kill_after_shot = 0;
        _vhcl->mass = 400.0;
        _vhcl->force = 5000.0;
        _vhcl->airconst = 80.0;
        _vhcl->maxrot = 0.8;
        _vhcl->height = 150.0;
        _vhcl->radius = 25.0;
        _vhcl->overeof = 25.0;
        _vhcl->vwr_radius = 30.0;
        _vhcl->vwr_overeof = 30.0;
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

        _vhcl->initParams.clear();
        return true;
    }
    else if ( !StriCmp(word, "modify_vehicle") )
    {
        _gunID = -1;
        _unitGunID = -1;
        _unitDummyID = -1;
        _collID = -1;
        _vhclID = parser.stol(opt, NULL, 0);
        _vhcl = &_o._vhclProtos.at(_vhclID);
        
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
        int wpnId = parser.stol(opt, NULL, 0);
        _wpn = &_o._weaponProtos[wpnId];

        *_wpn = TWeapProto();
        
        _wpn->unitID = 4;
        _wpn->name.clear();
        _wpn->energy = 10000;
        _wpn->aoe_unit_energy = 0;
        _wpn->aoe_building_energy = 0;
        _wpn->aoe_sector_energy = 0;
        _wpn->aoe_falloff = 0;
        _wpn->aoe_unit_push = 0;
        _wpn->push = 0;
        _wpn->mass = 50.0;
        _wpn->force = 5000.0;
        _wpn->airconst = 50.0;
        _wpn->maxrot = 2.0;
        _wpn->radius = 20.0;
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
        _wpn->radius_heli = 0;
        _wpn->radius_tank = 0;
        _wpn->radius_flyer = 0;
        _wpn->radius_robo = 0;
        _wpn->start_speed = 70.0;
        _wpn->life_time = 20000;
        _wpn->life_time_nt = 0;
        _wpn->drive_time = 7000;
        _wpn->shot_time = 3000;
        _wpn->shot_time_user = 1000;
        _wpn->salve_delay = 0;
        _wpn->salve_shots = 0;
        _wpn->missile_multi_target = 0;
        _wpn->homing_bomb_multi_target = 0;
        // OpenUA custom: model = laser defaults (vanilla-safe / disabled by default)
        _wpn->laser_energy_tick_time = 250;
        _wpn->laser_energy_tick_time_user = 150;
        _wpn->laser_energy_increment_rate = 0.0;
        _wpn->laser_max_energy = 0.0;
        _wpn->laser_vp_spacing = 40.0;
        _wpn->laser_chain_allow = 0;
        _wpn->laser_chain_max_jumps = 0;
        _wpn->laser_chain_radius = 0.0;
        _wpn->laser_chain_damage_mult = 1.0;
        _wpn->laser_multi_target = 1;
        _wpn->vertical_laser_fire_radius = 300.0;
        _wpn->vp_normal = 0;
        _wpn->vp_fire = 1;
        _wpn->vp_megadeth = 2;
        _wpn->vp_wait = 3;
        _wpn->vp_dead = 4;
        _wpn->vp_genesis = 5;
        _wpn->vp_launch = 0;
        _wpn->visual_scale = 1.0;
        _wpn->visual_scale_mode = VISUAL_SCALE_FIXED;
        _wpn->visual_scale_random_min = 1.0;
        _wpn->visual_scale_random_max = 1.0;
        _wpn->visual_scale_axis = vec3d(1.0, 1.0, 1.0);
        _wpn->visual_tint = TVisualTint();
        _wpn->wireframe_tint = TVisualTint();
        _wpn->visual_rotation = vec3d(0.0, 0.0, 0.0);
        _wpn->projectile_spin_speed = vec3d(0.0, 0.0, 0.0);
        _wpn->type_icon = 65;
        _wpn->debuff = TWeaponDebuffConfig();
        _wpn->debuff.tick_snd.volume = 120;
        _wpn->debuff.tick_snd.sndPrm.mag0 = 1.0;
        _wpn->debuff.tick_snd.sndPrm.time = 1000;
        _wpn->debuff.tick_snd.sndPrm_shk.mag0 = 1.0;
        _wpn->debuff.tick_snd.sndPrm_shk.time = 1000;
        _wpn->debuff.tick_snd.sndPrm_shk.mute = 0.02;
        _wpn->debuff.tick_snd.sndPrm_shk.pos.x = 0.2;
        _wpn->debuff.tick_snd.sndPrm_shk.pos.y = 0.2;
        _wpn->debuff.tick_snd.sndPrm_shk.pos.z = 0.2;
        _wpn->cluster = TWeaponClusterConfig();
        _wpn->cluster.snd.volume = 120;
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

        _wpn->initParams.clear();
        return true;
    }
    else if (!StriCmp(word, "modify_weapon"))
    {
        int wpnId = parser.stol(opt, NULL, 0);
        _wpn = &_o._weaponProtos[wpnId];

        _o._upgradeWeaponId = wpnId;
        return true;
    }

    return false;
}

int WeaponProtoParser::Handle(ScriptParser::Parser &parser, const std::string &p1, const std::string &p2)
{
    if ( !StriCmp(p1, "end") )
        return ScriptParser::RESULT_SCOPE_END;

    int debuffFxSlot = -1;

    if ( !StriCmp(p1, "model") )
    {
        if ( !StriCmp(p2, "grenade") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_GRENADE;
        else if ( !StriCmp(p2, "rocket") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_ROCKET;
        else if ( !StriCmp(p2, "missile") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_MISSILE;
        else if ( !StriCmp(p2, "homing_bomb") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_HOMING_BOMB;
        else if ( !StriCmp(p2, "mortar") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_MORTAR;
        else if ( !StriCmp(p2, "laser") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_LASER;
        else if ( !StriCmp(p2, "vertical_laser") )
            _wpn->_weaponFlags = TWeapProto::WEAPON_FLAGS_VERTICAL_LASER;
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
        _wpn->aoe_unit_push = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "push") )
    {
        _wpn->push = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "debuff_allow") )
    {
        _wpn->debuff.allow = parser.stol(p2, NULL, 0) != 0;
    }
    else if ( !StriCmp(p1, "debuff_name") )
    {
        _wpn->debuff.name = p2;
    }
    else if ( !StriCmp(p1, "debuff_damage") )
    {
        int damage = parser.stol(p2, NULL, 0);
        _wpn->debuff.damage = damage > 0 ? damage : 0;
    }
    else if ( !StriCmp(p1, "debuff_damage_percent") )
    {
        float damagePercent = parser.stof(p2, 0);
        _wpn->debuff.damage_percent = damagePercent > 0.0 ? damagePercent : 0.0;
    }
    else if ( !StriCmp(p1, "debuff_mindcontrol") )
    {
        _wpn->debuff.mindcontrol = parser.stol(p2, NULL, 0) != 0;
    }
    else if ( !StriCmp(p1, "debuff_tick_time") )
    {
        int tickTime = parser.stol(p2, NULL, 0);
        _wpn->debuff.tick_time = tickTime > 0 ? tickTime : 1000;
    }
    else if ( !StriCmp(p1, "debuff_duration") )
    {
        int duration = parser.stol(p2, NULL, 0);
        _wpn->debuff.duration = duration > 0 ? duration : 0;
    }
    else if ( !StriCmp(p1, "debuff_force_malus") )
    {
        float malus = parser.stof(p2, 0);
        _wpn->debuff.force_malus = std::max(0.0f, std::min(malus, 1.0f));
    }
    else if ( !StriCmp(p1, "debuff_maxrot_malus") )
    {
        float malus = parser.stof(p2, 0);
        _wpn->debuff.maxrot_malus = std::max(0.0f, std::min(malus, 1.0f));
    }
    else if ( !StriCmp(p1, "debuff_shield_malus") )
    {
        float malus = parser.stof(p2, 0);
        _wpn->debuff.shield_malus = std::max(0.0f, std::min(malus, 1.0f));
    }
    else if ( !StriCmp(p1, "debuff_snd_pitch_mult") )
    {
        float mult = parser.stof(p2, 0);
        _wpn->debuff.snd_pitch_mult = mult >= 0.0 ? mult : 1.0;
    }
    else if ( (debuffFxSlot = ParseNumberedSlotId(p1, "debuff_fx_vp", World::DAMAGED_FX_SLOT_COUNT)) >= 0 )
    {
        int vp = parser.stol(p2, NULL, 0);
        EnsureDebuffFXSlot(_wpn->debuff.fx_vps, debuffFxSlot);
        _wpn->debuff.fx_vps[debuffFxSlot] = vp > 0 ? vp : 0;
    }
    else if ( !StriCmp(p1, "debuff_fx_random_pos") )
    {
        float radius = parser.stof(p2, 0);
        _wpn->debuff.fx_random_pos = radius > 0.0 ? radius : 0.0;
    }
    else if ( !StriCmp(p1, "debuff_icon") )
    {
        _wpn->debuff.icon = p2;
    }
    else if ( !StriCmp(p1, "snd_debuff_sample") )
    {
        _wpn->debuff.tick_snd.SetMainSampleVariant(0, p2);
    }
    else if ( !StriCmp(p1, "snd_debuff_pitch") )
    {
        _wpn->debuff.tick_snd.pitch = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "snd_debuff_volume") )
    {
        _wpn->debuff.tick_snd.volume = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pal_debuff_slot") )
    {
        _wpn->debuff.tick_snd.sndPrm.slot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "pal_debuff_mag0") )
    {
        _wpn->debuff.tick_snd.sndPrm.mag0 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "pal_debuff_mag1") )
    {
        _wpn->debuff.tick_snd.sndPrm.mag1 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "pal_debuff_time") )
    {
        _wpn->debuff.tick_snd.sndPrm.time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_debuff_slot") )
    {
        _wpn->debuff.tick_snd.sndPrm_shk.slot = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_debuff_mag0") )
    {
        _wpn->debuff.tick_snd.sndPrm_shk.mag0 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_debuff_mag1") )
    {
        _wpn->debuff.tick_snd.sndPrm_shk.mag1 = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "shk_debuff_time") )
    {
        _wpn->debuff.tick_snd.sndPrm_shk.time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "shk_debuff_mute") )
    {
        _wpn->debuff.tick_snd.sndPrm_shk.mute = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "energy_heli") )
    {
        _wpn->energy_heli = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "energy_tank") )
    {
        _wpn->energy_tank = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "energy_flyer") )
    {
        _wpn->energy_flyer = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "energy_robo") )
    {
        _wpn->energy_robo = parser.stof(p2, 0);
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
        // Legacy/deprecated: keep parsing for old SCR files, gameplay ignores it.
        _wpn->radius_heli = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_tank") )
    {
        // Legacy/deprecated: keep parsing for old SCR files, gameplay ignores it.
        _wpn->radius_tank = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_flyer") )
    {
        // Legacy/deprecated: keep parsing for old SCR files, gameplay ignores it.
        _wpn->radius_flyer = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "radius_robo") )
    {
        // Legacy/deprecated: keep parsing for old SCR files, gameplay ignores it.
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
        int count = parser.stol(p2, NULL, 0);
        _wpn->cluster.count = count > 0 ? count : 0;
    }
    else if ( !StriCmp(p1, "cluster_weapon_id") )
    {
        int weaponId = parser.stol(p2, NULL, 0);
        _wpn->cluster.weapon_id = weaponId > 0 ? weaponId : 0;
    }
    else if ( !StriCmp(p1, "cluster_trigger_time") )
    {
        int triggerTime = parser.stol(p2, NULL, 0);
        _wpn->cluster.trigger_time = triggerTime > 0 ? triggerTime : 0;
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
    else if ( !StriCmp(p1, "snd_cluster_sample") )
    {
        _wpn->cluster.snd.SetMainSampleVariant(0, p2);
    }
    else if ( !StriCmp(p1, "snd_cluster_pitch") )
    {
        _wpn->cluster.snd.pitch = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "snd_cluster_volume") )
    {
        _wpn->cluster.snd.volume = parser.stol(p2, NULL, 0);
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
        _wpn->life_time = parser.stol(p2, NULL, 0);
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
    else if ( !StriCmp(p1, "salve_shots") )
    {
        _wpn->salve_shots = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "salve_delay") )
    {
        _wpn->salve_delay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "missile_multi_target") )
    {
        int maxTargets = parser.stol(p2, NULL, 0);
        _wpn->missile_multi_target = maxTargets > 0 ? maxTargets : 0;
    }
    else if ( !StriCmp(p1, "homing_bomb_multi_target") )
    {
        int maxTargets = parser.stol(p2, NULL, 0);
        _wpn->homing_bomb_multi_target = maxTargets > 0 ? maxTargets : 0;
    }
    // ---- OpenUA custom: model = laser parameters ----
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
    else if ( !StriCmp(p1, "laser_vp_spacing") )
    {
        float spacing = parser.stof(p2, 0);
        if ( spacing <= 0.0 )
            spacing = 40.0;
        if ( spacing < 20.0 )
            spacing = 20.0;
        if ( spacing > 500.0 )
            spacing = 500.0;
        _wpn->laser_vp_spacing = spacing;
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
    else if ( !StriCmp(p1, "laser_multi_target") )
    {
        int maxTargets = parser.stol(p2, NULL, 0);
        _wpn->laser_multi_target = maxTargets > 1 ? maxTargets : 1;
    }
    else if ( !StriCmp(p1, "vertical_laser_fire_radius") )
    {
        float radius = parser.stof(p2, 0);
        _wpn->vertical_laser_fire_radius = radius > 0.0 ? radius : 300.0;
    }
    else if ( !StriCmp(p1, "snd_loop_sample") )
    {
        _wpn->snd_loop.SetMainSampleVariant(0, p2);
    }
    else if ( !StriCmp(p1, "snd_loop_volume") )
    {
        _wpn->snd_loop.volume = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "snd_loop_pitch") )
    {
        _wpn->snd_loop.pitch = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_energy") )
    {
        _wpn->energy += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_energy_heli") )
    {
        _wpn->energy_heli += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_energy_tank") )
    {
        _wpn->energy_tank += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_energy_flyer") )
    {
        _wpn->energy_flyer += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_energy_Robo") )
    {
        _wpn->energy_robo += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_shot_time") )
    {
        _wpn->shot_time += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "add_shot_time_user") )
    {
        _wpn->shot_time_user += parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_normal") )
    {
        _wpn->vp_normal = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "vp_fire") )
    {
        _wpn->vp_fire = parser.stol(p2, NULL, 0);
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
    else if ( !StriCmp(p1, "vp_launch") )
    {
        _wpn->vp_launch = parser.stol(p2, NULL, 0);
    }
    else if ( ParseVisualScaleParam(parser, p1, p2,
                                    _wpn->visual_scale,
                                    _wpn->visual_scale_mode,
                                    _wpn->visual_scale_random_min,
                                    _wpn->visual_scale_random_max,
                                    _wpn->visual_scale_axis) )
    {
    }
    else if ( ParseVisualTintParam(parser, p1, p2, _wpn->visual_tint) )
    {
    }
    else if ( ParseWireframeTintParam(parser, p1, p2, _wpn->wireframe_tint) )
    {
    }
    else if ( ParseVisualOrientationParam(parser, p1, p2, _wpn->visual_rotation) )
    {
    }
    else if ( !StriCmp(p1, "spin_x") )
    {
        _wpn->projectile_spin_speed.x = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "spin_y") )
    {
        _wpn->projectile_spin_speed.y = parser.stof(p2, 0);
    }
    else if ( !StriCmp(p1, "spin_z") )
    {
        _wpn->projectile_spin_speed.z = parser.stof(p2, 0);
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
    else if ( !StriCmp(p1, "mortar_min_range") )
    {
        float v = parser.stof(p2, 0);
        _wpn->mortar_min_range = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "mortar_max_range") )
    {
        float v = parser.stof(p2, 0);
        _wpn->mortar_max_range = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "mortar_requires_radar") )
    {
        _wpn->mortar_requires_radar = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "mortar_manual_mode_only") )
    {
        _wpn->mortar_manual_mode_only = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "mortar_prefer_host_station") )
    {
        _wpn->mortar_prefer_host_station = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "mortar_barrage_radius") )
    {
        float v = parser.stof(p2, 0);
        _wpn->mortar_barrage_radius = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "mortar_barrage_shots") )
    {
        int v = parser.stol(p2, NULL, 0);
        _wpn->mortar_barrage_shots = v > 0 ? v : 0;
    }
    else if ( !StriCmp(p1, "mortar_barrage_shot_delay") )
    {
        _wpn->mortar_barrage_shot_delay = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mortar_barrage_cooldown") )
    {
        _wpn->mortar_barrage_cooldown = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mortar_arc_height") )
    {
        float v = parser.stof(p2, 0);
        _wpn->mortar_arc_height = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "mortar_flight_time") )
    {
        _wpn->mortar_flight_time = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "mortar_spread_radius") )
    {
        float v = parser.stof(p2, 0);
        _wpn->mortar_spread_radius = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "mortar_inflight_drift") )
    {
        float v = parser.stof(p2, 0);
        _wpn->mortar_inflight_drift = v > 0.0 ? v : 0.0;
    }
    else if ( !StriCmp(p1, "mortar_airburst") )
    {
        _wpn->mortar_airburst = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
    }
    else if ( !StriCmp(p1, "mortar_minimap_marker") )
    {
        _wpn->mortar_minimap_marker = parser.stol(p2, NULL, 0) != 0 ? 1 : 0;
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
        int bldId = parser.stol(opt, NULL, 0);

        _o._buildProtos[bldId] = TBuildingProto();
        _bld = &_o._buildProtos[bldId];
        _bld->Index = bldId;
        _bld->Energy = 50000;
        _bld->TypeIcon = 65;
        _bld->SndFX.volume = 120;
        _bld->Guns.clear();
        return true;
    }
    else if (!StriCmp(word, "modify_building"))
    {
        int bldId = parser.stol(opt, NULL, 0);

        _bld = &_o._buildProtos[bldId];
        _bld->Index = bldId;
        _o._upgradeBuildId = bldId;
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
        _bld->EnableMask |= 1 << parser.stol(p2, NULL, 0);
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
    else if ( !StriCmp(p1, "spawn_icon") )
    {
        _bld->spawn_icon = p2;
    }
    else if ( p1.size() >= 11 && !StriCmp(p1.substr(0, 11), "snd_normal_") && ParseSampleVariantId(p1.substr(11)) >= 0 )
    {
        _bld->SndFX.SetMainSampleVariant(ParseSampleVariantId(p1.substr(11)), p2);
    }
    else if ( !StriCmp(p1, "snd_normal_volume") )
    {
        _bld->SndFX.volume = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "snd_normal_pitch") )
    {
        _bld->SndFX.pitch = parser.stol(p2, NULL, 0);
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
        if (IsModsAllow(true))
            _o._allowMultiBuildWorld = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "hidden_fractions") )
    {
        if (IsModsAllow(true))
            _o._worldHiddenFractions = parser.stol(p2, NULL, 0);
    }
    else if ( !StriCmp(p1, "fix_weapon_radius") )
    {
        // Legacy/deprecated: parsed for INI compatibility; gameplay no longer uses
        // class-specific weapon radii.
        if (IsModsAllow(true))
            _o._fixWeaponRadius = StrGetBool(p2);
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
        if (IsModsAllow(true))
            _o._luaScriptName = p2;
    }
    else if ( !StriCmp(p1, "multi_building") )
    {
        if (IsModsAllow(true))
            _o._allowMultiBuildLevel = StrGetBool(p2);
    }
    else if ( !StriCmp(p1, "hidden_fractions") )
    {
        if (IsModsAllow(true))
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
        {
            return ScriptParser::RESULT_BAD_DATA;
        }
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
            _g->ActionsList.push_back(tmp);
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
    else if ( !StriCmp(p1, "palette_theme") )
    {
        std::string theme = p2;
        if (!StriCmp(theme, "Original"))
            theme.clear();

        _o._GameShell->paletteTheme = theme;
        _o._GameShell->confPaletteTheme = theme;
        System::IniConf::GfxPaletteTheme.Value = theme.empty() ? std::string("Original") : theme;
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
