#include <inttypes.h>
#include "inivals.h"
#include "utils.h"

namespace System
{
Common::Ini::PKeyList IniConf::_varList;

// Win3d Class
Common::Ini::Key IniConf::GfxDither("gfx.dither", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxFilter("gfx.filter", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxAntialias("gfx.antialias", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxAlpha("gfx.alpha", Common::Ini::KT_DIGIT, (int32_t)192);
Common::Ini::Key IniConf::GfxZbufWhenTracy("gfx.zbuf_when_tracy", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxColorkey("gfx.colorkey", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxForceEmul("gfx.force_emul", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxForceSoftCursor("gfx.force_soft_cursor", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxAllModes("gfx.all_modes", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxMoviePlayer("gfx.movie_player", Common::Ini::KT_BOOL, true);
Common::Ini::Key IniConf::GfxForceAlphaTex("gfx.force_alpha_textures", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxUseDrawPrimitive("gfx.use_draw_primitive", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxDisableLowres("gfx.disable_lowres", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxExportWindowMode("gfx.export_window_mode", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GfxBlending("gfx.blending", Common::Ini::KT_DIGIT, (int32_t) 0);
Common::Ini::Key IniConf::GfxSolidFont("gfx.solidfont", Common::Ini::KT_BOOL, false);
Common::Ini::Key IniConf::GfxVsync("gfx.vsync", Common::Ini::KT_DIGIT, (int32_t)1);
Common::Ini::Key IniConf::GfxMaxFps("gfx.maxfps", Common::Ini::KT_DIGIT, (int32_t)60);
Common::Ini::Key IniConf::GfxNewSky("gfx.newsky", Common::Ini::KT_BOOL, false);
Common::Ini::Key IniConf::GfxSkyDistance("gfx.skydistance", Common::Ini::KT_DIGIT, (int32_t)3000);
Common::Ini::Key IniConf::GfxSkyLength("gfx.skylength", Common::Ini::KT_DIGIT, (int32_t)500);
Common::Ini::Key IniConf::GfxHorizonFogEnable("gfx.horizon_fog_enable", Common::Ini::KT_BOOL, true);
Common::Ini::Key IniConf::GfxHorizonFogStart("gfx.horizon_fog_start", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxHorizonFogLength("gfx.horizon_fog_length", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxHorizonFogStrength("gfx.horizon_fog_strength", Common::Ini::KT_WORD, std::string("1.0"));
Common::Ini::Key IniConf::GfxHorizonDarkEnable("gfx.horizon_dark_enable", Common::Ini::KT_BOOL, true);
Common::Ini::Key IniConf::GfxHorizonDarkStart("gfx.horizon_dark_start", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxHorizonDarkLength("gfx.horizon_dark_length", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxHorizonDarkStrength("gfx.horizon_dark_strength", Common::Ini::KT_WORD, std::string("1.0"));
Common::Ini::Key IniConf::GfxHorizonDarkColor("gfx.horizon_dark_color", Common::Ini::KT_WORD, std::string("0_0_0"));
Common::Ini::Key IniConf::GfxRenderSectors("gfx.render_sectors", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxNormalVisualLimit("gfx.normal_visual_limit", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxNormalFadeLength("gfx.normal_fade_length", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxSkyVisualLimit("gfx.sky_visual_limit", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxSkyFadeLength("gfx.sky_fade_length", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxSkyHeight("gfx.sky_height", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxSkyRender("gfx.sky_render", Common::Ini::KT_WORD, std::string());
Common::Ini::Key IniConf::GfxAdditionalModes("gfx.custommodes", Common::Ini::KT_STRING);

// Gfx Engine
Common::Ini::Key IniConf::GfxMode("gfx.mode", Common::Ini::KT_DIGIT);
Common::Ini::Key IniConf::GfxXRes("gfx.xres", Common::Ini::KT_DIGIT);
Common::Ini::Key IniConf::GfxYRes("gfx.yres", Common::Ini::KT_DIGIT);
Common::Ini::Key IniConf::GfxPalette("gfx.palette", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::GfxPaletteTheme("gfx.palette_theme", Common::Ini::KT_WORD);
// OpenUA custom: modern fullscreen visual filter (replaces the legacy palette-theme remap).
Common::Ini::Key IniConf::GfxVisualFilter("gfx.visual_filter", Common::Ini::KT_WORD, std::string("Standard"));
Common::Ini::Key IniConf::GfxVisualFilterStrength("gfx.visual_filter_strength", Common::Ini::KT_WORD, std::string("0.30"));
Common::Ini::Key IniConf::GfxVhsFilter("gfx.vhs_filter", Common::Ini::KT_BOOL, false);
Common::Ini::Key IniConf::GfxVhsFilterName("gfx.vhs_filter_name", Common::Ini::KT_WORD, std::string("VHS"));
Common::Ini::Key IniConf::GfxVhsFilterShader("gfx.vhs_filter_shader", Common::Ini::KT_STRING, std::string("res/vhs.ps"));
Common::Ini::Key IniConf::GfxVhsFilterShaderVbo("gfx.vhs_filter_shader_vbo", Common::Ini::KT_STRING, std::string("res/vhs_vbo.ps"));
Common::Ini::Key IniConf::GfxVhsFilterStrength("gfx.vhs_filter_strength", Common::Ini::KT_WORD, std::string("0.20"));
Common::Ini::Key IniConf::GfxDisplay("gfx.display", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::GfxDisplay2("gfx.display2", Common::Ini::KT_WORD);

Common::Ini::Key IniConf::GfxColorEffects("gfx.color_effects", Common::Ini::KT_DIGIT, (int32_t)1);
Common::Ini::Key IniConf::GfxColorEffPower1("gfx.color_eff_pwr[1]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower2("gfx.color_eff_pwr[2]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower3("gfx.color_eff_pwr[3]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower4("gfx.color_eff_pwr[4]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower5("gfx.color_eff_pwr[5]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower6("gfx.color_eff_pwr[6]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower7("gfx.color_eff_pwr[7]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower8("gfx.color_eff_pwr[8]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower9("gfx.color_eff_pwr[9]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower10("gfx.color_eff_pwr[10]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower11("gfx.color_eff_pwr[11]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower12("gfx.color_eff_pwr[12]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower13("gfx.color_eff_pwr[13]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower14("gfx.color_eff_pwr[14]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower15("gfx.color_eff_pwr[15]", Common::Ini::KT_DIGIT, (int32_t)100);
Common::Ini::Key IniConf::GfxColorEffPower16("gfx.color_eff_pwr[16]", Common::Ini::KT_DIGIT, (int32_t)100);

Common::Ini::Key IniConf::GfxVBO("gfx.vbo", Common::Ini::KT_BOOL, true);

// OpenUA custom: hide the legacy passive menu hover/help description texts by default.
Common::Ini::Key IniConf::UiHideMenuHints("ui.hide_menu_hints", Common::Ini::KT_BOOL, true);
// Stored as a single token when saved by Options, e.g. Liberation_Mono_Regular.
// KT_STRING is intentionally kept so older test builds with spaces still parse.
Common::Ini::Key IniConf::UiMenuFont("ui.menu_font", Common::Ini::KT_STRING, std::string("Default"));


// Input Engine
Common::Ini::Key IniConf::InputDebug("input.debug", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::InputTimer("input.timer", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputWimp("input.wimp", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputKeyboard("input.keyboard", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputButton0("input.button[0]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton1("input.button[1]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton2("input.button[2]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton3("input.button[3]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton4("input.button[4]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton5("input.button[5]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton6("input.button[6]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton7("input.button[7]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton8("input.button[8]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton9("input.button[9]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton10("input.button[10]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton11("input.button[11]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton12("input.button[12]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton13("input.button[13]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton14("input.button[14]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton15("input.button[15]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton16("input.button[16]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton17("input.button[17]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton18("input.button[18]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton19("input.button[19]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton20("input.button[20]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton21("input.button[21]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton22("input.button[22]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton23("input.button[23]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton24("input.button[24]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton25("input.button[25]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton26("input.button[26]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton27("input.button[27]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton28("input.button[28]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton29("input.button[29]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton30("input.button[30]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputButton31("input.button[31]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider0("input.slider[0]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider1("input.slider[1]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider2("input.slider[2]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider3("input.slider[3]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider4("input.slider[4]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider5("input.slider[5]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider6("input.slider[6]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider7("input.slider[7]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider8("input.slider[8]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider9("input.slider[9]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider10("input.slider[10]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider11("input.slider[11]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider12("input.slider[12]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider13("input.slider[13]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider14("input.slider[14]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider15("input.slider[15]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider16("input.slider[16]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider17("input.slider[17]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider18("input.slider[18]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider19("input.slider[19]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider20("input.slider[20]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider21("input.slider[21]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider22("input.slider[22]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider23("input.slider[23]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider24("input.slider[24]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider25("input.slider[25]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider26("input.slider[26]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider27("input.slider[27]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider28("input.slider[28]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider29("input.slider[29]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider30("input.slider[30]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputSlider31("input.slider[31]", Common::Ini::KT_STRING);
Common::Ini::Key IniConf::InputHotkey0("input.hotkey[0]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey1("input.hotkey[1]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey2("input.hotkey[2]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey3("input.hotkey[3]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey4("input.hotkey[4]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey5("input.hotkey[5]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey6("input.hotkey[6]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey7("input.hotkey[7]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey8("input.hotkey[8]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey9("input.hotkey[9]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey10("input.hotkey[10]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey11("input.hotkey[11]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey12("input.hotkey[12]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey13("input.hotkey[13]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey14("input.hotkey[14]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey15("input.hotkey[15]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey16("input.hotkey[16]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey17("input.hotkey[17]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey18("input.hotkey[18]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey19("input.hotkey[19]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey20("input.hotkey[20]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey21("input.hotkey[21]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey22("input.hotkey[22]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey23("input.hotkey[23]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey24("input.hotkey[24]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey25("input.hotkey[25]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey26("input.hotkey[26]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey27("input.hotkey[27]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey28("input.hotkey[28]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey29("input.hotkey[29]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey30("input.hotkey[30]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey31("input.hotkey[31]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey32("input.hotkey[32]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey33("input.hotkey[33]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey34("input.hotkey[34]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey35("input.hotkey[35]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey36("input.hotkey[36]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey37("input.hotkey[37]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey38("input.hotkey[38]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey39("input.hotkey[39]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey40("input.hotkey[40]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey41("input.hotkey[41]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey42("input.hotkey[42]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey43("input.hotkey[43]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey44("input.hotkey[44]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey45("input.hotkey[45]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey46("input.hotkey[46]", Common::Ini::KT_WORD);
Common::Ini::Key IniConf::InputHotkey47("input.hotkey[47]", Common::Ini::KT_WORD);

// Audio Engine
Common::Ini::Key IniConf::AudioChannels("audio.channels", Common::Ini::KT_DIGIT, (int32_t)64);
Common::Ini::Key IniConf::AudioVolume("audio.volume",   Common::Ini::KT_DIGIT, (int32_t)127);
Common::Ini::Key IniConf::AudioNumPalfx("audio.num_palfx", Common::Ini::KT_DIGIT, (int32_t)4);
Common::Ini::Key IniConf::AudioRevStereo("audio.rev_stereo", Common::Ini::KT_BOOL);

// Tform Engine
Common::Ini::Key IniConf::TformBackplane("tform.backplane",  Common::Ini::KT_DIGIT, (int32_t)4096);
Common::Ini::Key IniConf::TformFrontplane("tform.frontplane", Common::Ini::KT_DIGIT, (int32_t)16);
Common::Ini::Key IniConf::TformZoomx("tform.zoomx", Common::Ini::KT_DIGIT, (int32_t)320);
Common::Ini::Key IniConf::TformZoomy("tform.zoomy", Common::Ini::KT_DIGIT, (int32_t)200);

// Windp keys
Common::Ini::Key IniConf::NetGmode("net.gmode",  Common::Ini::KT_DIGIT);
Common::Ini::Key IniConf::NetVersionCheck("net.versioncheck", Common::Ini::KT_BOOL, true);

Common::Ini::Key IniConf::GameDebug("game.debug", Common::Ini::KT_BOOL);
Common::Ini::Key IniConf::GameNewDebug("game.new.debug", Common::Ini::KT_WORD, std::string("no"));

// Yparobo keys
Common::Ini::Key IniConf::GameNewAI("game.newai",    Common::Ini::KT_BOOL, true);
// OpenUA: frame-rate independent gameplay timing (historical key name).
// game.fixed_tick = no keeps vanilla biased timing. Set yes to use the true
// measured frame delta (no legacy +1 bias) for frame-rate independent gameplay.
Common::Ini::Key IniConf::GameFixedTick("game.fixed_tick", Common::Ini::KT_BOOL, false);
// OpenUA custom: tank ground-pose response used only when game.fixed_tick = yes.
// 2.0 is near vanilla alignment; 5.5 is the balanced default; 10.0 is very reactive.
Common::Ini::Key IniConf::GameFixedTickTankGroundPoseMult("game.fixed_tick_tank_ground_pose_mult", Common::Ini::KT_WORD, std::string("5.5"));
Common::Ini::Key IniConf::GameTimeLine("game.timeline", Common::Ini::KT_DIGIT, (int32_t)600000);
Common::Ini::Key IniConf::GameRoboPlayerAIBehavior("game.robo_player_ai_behavior", Common::Ini::KT_BOOL, false);
Common::Ini::Key IniConf::GameSpectatorMode("game.spectator_mode", Common::Ini::KT_BOOL, false);
Common::Ini::Key IniConf::GameSpectatorVehicleID("game.spectator_vehicle_id", Common::Ini::KT_DIGIT, (int32_t)0);
Common::Ini::Key IniConf::GameWeaponWeaponCollision("game.weapon_weapon_collision", Common::Ini::KT_BOOL, false);
// OpenUA custom: radius-only scale for automatic weapon VP collision spheres.
Common::Ini::Key IniConf::GameWeaponAutoCollisionScale("game.weapon_auto_collision_scale", Common::Ini::KT_WORD, std::string("1.0"));
Common::Ini::Key IniConf::GameRoboBuildingCollisionDamagePercent("game.robo_building_collision_damage_percent", Common::Ini::KT_DIGIT, (int32_t)0);
// OpenUA custom: multiplier for the vanilla power-station sector energy effect.
// game.powerstation_energy_multiplier = 1.0 keeps vanilla; 3.0 triples recharge/drain.
Common::Ini::Key IniConf::GamePowerStationEnergyMultiplier("game.powerstation_energy_multiplier", Common::Ini::KT_WORD, std::string("1.0"));
// OpenUA custom: fall damage and lethal weapon-push tuning.
Common::Ini::Key IniConf::GameFallDamageMultiplier("game.fall_damage_mult", Common::Ini::KT_WORD, std::string("1.0"));
Common::Ini::Key IniConf::GamePushAtDeathMultiplier("game.push_at_death_mult", Common::Ini::KT_WORD, std::string("1.0"));

// OpenUA custom: Black Sect "imperfect grey clone" runtime balance (owner/faction 5).
// When enabled, live Black Sect actors get a small malus (default 5%) to effective
// defense, attack speed, outgoing damage, force, maxrot and sound pitch, plus an
// automatic grey identity tint. These are RUNTIME-only effective-value adjustments:
// they never modify the shared vehicle/weapon prototypes and never touch energy/maxHP.
// game.black_sect_clone_balance = no    ; master switch (default off, vanilla behavior)
Common::Ini::Key IniConf::GameBlackSectCloneBalance("game.black_sect_clone_balance", Common::Ini::KT_BOOL, false);
// game.black_sect_clone_malus_percent = 5 ; malus magnitude in percent (defense/damage/force/maxrot/pitch -p%, shot_time +p%)
Common::Ini::Key IniConf::GameBlackSectCloneMalusPercent("game.black_sect_clone_malus_percent", Common::Ini::KT_DIGIT, (int32_t)5);
// game.black_sect_clone_tint = 140_140_140_255 ; grey clone identity tint, R_G_B_A each 0..255
Common::Ini::Key IniConf::GameBlackSectCloneTint("game.black_sect_clone_tint", Common::Ini::KT_WORD, std::string("140_140_140_255"));

// Ypaworld keys
Common::Ini::Key IniConf::NetGameExclusiveGem("netgame.exclusivegem", Common::Ini::KT_BOOL, true);
Common::Ini::Key IniConf::NetWaitStart("net.waitstart", Common::Ini::KT_DIGIT, (int32_t)150000);
Common::Ini::Key IniConf::NetKickoff("net.kickoff", Common::Ini::KT_DIGIT, (int32_t)20000);

// Particles
Common::Ini::Key IniConf::ParticlesLimit("particles.limit", Common::Ini::KT_DIGIT, (int32_t)5000);

Common::Ini::Key IniConf::MenuWindowed("menu.windowed", Common::Ini::KT_BOOL, false);

void IniConf::Init()
{
    _varList = {
          &GfxDither
        , &GfxFilter
        , &GfxAntialias
        , &GfxAlpha
        , &GfxZbufWhenTracy
        , &GfxColorkey
        , &GfxForceEmul
        , &GfxForceSoftCursor
        , &GfxAllModes
        , &GfxMoviePlayer
        , &GfxForceAlphaTex
        , &GfxUseDrawPrimitive
        , &GfxDisableLowres
        , &GfxExportWindowMode
        , &GfxBlending
        , &GfxSolidFont
        , &GfxVsync
        , &GfxMaxFps
        , &GfxNewSky
        , &GfxSkyDistance
        , &GfxSkyLength
        , &GfxHorizonFogEnable
        , &GfxHorizonFogStart
        , &GfxHorizonFogLength
        , &GfxHorizonFogStrength
        , &GfxHorizonDarkEnable
        , &GfxHorizonDarkStart
        , &GfxHorizonDarkLength
        , &GfxHorizonDarkStrength
        , &GfxHorizonDarkColor
        , &GfxRenderSectors
        , &GfxNormalVisualLimit
        , &GfxNormalFadeLength
        , &GfxSkyVisualLimit
        , &GfxSkyFadeLength
        , &GfxSkyHeight
        , &GfxSkyRender
        , &GfxMode
        , &GfxXRes
        , &GfxYRes
        , &GfxPalette
        , &GfxPaletteTheme
        , &GfxVisualFilter
        , &GfxVisualFilterStrength
        , &GfxDisplay
        , &GfxDisplay2

        , &InputDebug
        , &InputTimer
        , &InputWimp
        , &InputKeyboard
        , &InputButton0
        , &InputButton1
        , &InputButton2
        , &InputButton3
        , &InputButton4
        , &InputButton5
        , &InputButton6
        , &InputButton7
        , &InputButton8
        , &InputButton9
        , &InputButton10
        , &InputButton11
        , &InputButton12
        , &InputButton13
        , &InputButton14
        , &InputButton15
        , &InputButton16
        , &InputButton17
        , &InputButton18
        , &InputButton19
        , &InputButton20
        , &InputButton21
        , &InputButton22
        , &InputButton23
        , &InputButton24
        , &InputButton25
        , &InputButton26
        , &InputButton27
        , &InputButton28
        , &InputButton29
        , &InputButton30
        , &InputButton31
        , &InputSlider0
        , &InputSlider1
        , &InputSlider2
        , &InputSlider3
        , &InputSlider4
        , &InputSlider5
        , &InputSlider6
        , &InputSlider7
        , &InputSlider8
        , &InputSlider9
        , &InputSlider10
        , &InputSlider11
        , &InputSlider12
        , &InputSlider13
        , &InputSlider14
        , &InputSlider15
        , &InputSlider16
        , &InputSlider17
        , &InputSlider18
        , &InputSlider19
        , &InputSlider20
        , &InputSlider21
        , &InputSlider22
        , &InputSlider23
        , &InputSlider24
        , &InputSlider25
        , &InputSlider26
        , &InputSlider27
        , &InputSlider28
        , &InputSlider29
        , &InputSlider30
        , &InputSlider31
        , &InputHotkey0
        , &InputHotkey1
        , &InputHotkey2
        , &InputHotkey3
        , &InputHotkey4
        , &InputHotkey5
        , &InputHotkey6
        , &InputHotkey7
        , &InputHotkey8
        , &InputHotkey9
        , &InputHotkey10
        , &InputHotkey11
        , &InputHotkey12
        , &InputHotkey13
        , &InputHotkey14
        , &InputHotkey15
        , &InputHotkey16
        , &InputHotkey17
        , &InputHotkey18
        , &InputHotkey19
        , &InputHotkey20
        , &InputHotkey21
        , &InputHotkey22
        , &InputHotkey23
        , &InputHotkey24
        , &InputHotkey25
        , &InputHotkey26
        , &InputHotkey27
        , &InputHotkey28
        , &InputHotkey29
        , &InputHotkey30
        , &InputHotkey31
        , &InputHotkey32
        , &InputHotkey33
        , &InputHotkey34
        , &InputHotkey35
        , &InputHotkey36
        , &InputHotkey37
        , &InputHotkey38
        , &InputHotkey39
        , &InputHotkey40
        , &InputHotkey41
        , &InputHotkey42
        , &InputHotkey43
        , &InputHotkey44
        , &InputHotkey45
        , &InputHotkey46
        , &InputHotkey47

        , &AudioChannels
        , &AudioVolume
        , &AudioNumPalfx
        , &AudioRevStereo

        , &TformBackplane
        , &TformFrontplane
        , &TformZoomx
        , &TformZoomy

        , &NetGmode
        , &NetVersionCheck

        , &GameDebug
        , &GameNewDebug

        , &GameNewAI
        , &GameFixedTick
        , &GameFixedTickTankGroundPoseMult
        , &GameTimeLine
        , &GameRoboPlayerAIBehavior
        , &GameSpectatorMode
        , &GameSpectatorVehicleID
        , &GameWeaponWeaponCollision
        , &GameWeaponAutoCollisionScale
        , &GameRoboBuildingCollisionDamagePercent
        , &GamePowerStationEnergyMultiplier
        , &GameFallDamageMultiplier
        , &GamePushAtDeathMultiplier
        , &GameBlackSectCloneBalance
        , &GameBlackSectCloneMalusPercent
        , &GameBlackSectCloneTint

        , &NetGameExclusiveGem
        , &NetWaitStart
        , &NetKickoff

        , &GfxColorEffects
        , &GfxColorEffPower1
        , &GfxColorEffPower2
        , &GfxColorEffPower3
        , &GfxColorEffPower4
        , &GfxColorEffPower5
        , &GfxColorEffPower6
        , &GfxColorEffPower7
        , &GfxColorEffPower8
        , &GfxColorEffPower9
        , &GfxColorEffPower10
        , &GfxColorEffPower11
        , &GfxColorEffPower12
        , &GfxColorEffPower13
        , &GfxColorEffPower14
        , &GfxColorEffPower15
        , &GfxColorEffPower16

        , &ParticlesLimit

        , &MenuWindowed

        , &GfxAdditionalModes
        , &GfxVBO
        , &GfxVhsFilter
        , &GfxVhsFilterName
        , &GfxVhsFilterShader
        , &GfxVhsFilterShaderVbo
        , &GfxVhsFilterStrength

        , &UiHideMenuHints
        , &UiMenuFont
    };
}

bool IniConf::ReadFromNucleusIni()
{
    return Common::Ini::ParseIniFile(uaDataFirstNucleusIniPath(), &_varList);
}

bool IniConf::ReadFromIni(const std::string &fname)
{
    return Common::Ini::ParseIniFile(fname, &_varList);
}

bool IniConf::IsGameNewDebugEnabled()
{
    return !StriCmp(GameNewDebug.Get<std::string>(), "yes");
}


}
