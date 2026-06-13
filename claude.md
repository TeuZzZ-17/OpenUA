# CLAUDE.md — OpenUA / Urban Assault Collaboration Guide

This repository contains Matteo / TeuZzZ-17’s OpenUA / UA_source project: a modernized, moddable Windows-focused version of the Urban Assault source port.

Urban Assault is an old 1998 game with fragile legacy C++ code, custom scripts, old asset formats, and many gameplay systems connected in non-obvious ways. Work carefully.

The main goal is not to turn OpenUA into a different game. The goal is to preserve the vanilla Urban Assault feeling while adding controlled, optional, mod-friendly features.

## Communication style

Use Italian with Matteo unless he asks otherwise.

Explain things simply. Matteo is learning programming and needs clear reasoning, not vague senior-dev jargon.

Be direct, honest, and practical. If something is risky, say it. If something is easy, say it. If a proposed feature may break old systems, warn about it before touching code.

Do not flatter blindly. Matteo prefers objective technical feedback.

When giving code-change advice, be concise and operational. Avoid long philosophical explanations unless they help avoid mistakes.

## Project priorities

Always prefer:

* Small patches.
* Localized changes.
* Vanilla-safe behavior.
* Backward compatibility with original Urban Assault data.
* Clear parameter names.
* Easy rollback.
* Buildable code.
* Runtime-safe code.
* No giant refactors unless explicitly requested.

Avoid:

* Unrequested rewrites.
* Large architecture changes.
* Renaming existing systems without need.
* Breaking vanilla scripts.
* Removing old behavior unless Matteo explicitly asks.
* Inventing parameters without checking the existing parser/prototype structure.
* “Cleanups” that change gameplay.
* Silent behavior changes.

If a feature can be implemented as optional script/config parameters, prefer that.

Default values must preserve vanilla behavior.

## Repository and build context

Main repository:

https://github.com/TeuZzZ-17/UA_source

Typical Windows build environment:

* Windows 11
* MSYS2 MinGW64
* CMake
* Ninja
* SDL2
* SDL2_image
* SDL2_ttf
* SDL2_net
* OpenAL
* libvorbis
* FFmpeg
* Lua

Typical build commands:

```bash
cmake -G Ninja -B build -S src
cmake --build build -j12
```

If the build succeeds, the executable is usually:

```text
build/UA_source.exe
```

Runtime DLL check:

```bash
ldd build/UA_source.exe
```

Copy required MinGW64 DLLs into build folder:

```bash
ldd build/UA_source.exe | grep mingw64/bin | awk '{print $3}' | xargs -I {} cp -v {} build/
```

## How to work on this codebase

Before modifying anything:

1. Search the repo for existing related parameters, structs, parser code, and runtime use.
2. Identify the smallest correct insertion point.
3. Preserve existing behavior if the new parameter is absent.
4. Add parser support only where needed.
5. Add fields to prototype/runtime structs carefully.
6. Initialize every new field with safe defaults.
7. Avoid touching unrelated systems.
8. Build after changes.
9. Report exactly what changed.

When modifying gameplay parameters, usually check these kinds of files:

* prototype definitions
* parser code
* vehicle/weapon/building proto structs
* runtime actor/weapon logic
* HUD/status/UI code if the feature has visible feedback
* save/load only if the value must persist per instance

Do not assume one system path is the only path. OpenUA often has separate paths for player, AI, missiles, mguns, buildings, host stations, visual effects, and special legacy behavior.

## Expected final report format

After making changes, report:

```text
Summary:
- What was implemented or fixed.

Files changed:
- path/to/file.cpp
- path/to/file.h

Behavior:
- What happens now.
- What remains vanilla-compatible.

Build/Test:
- Build command used.
- Build result.
- Any in-game test performed or still needed.

Risks:
- Possible edge cases.
- Anything not tested.

Rollback:
- How to revert safely if needed.
```

If you cannot build, say so clearly. Do not pretend the build passed.

If you cannot inspect the actual repo files, say so clearly and provide only a patch plan or prompt.

## Patch style

Prefer focused diffs.

Do not reformat whole files.

Do not change indentation style globally.

Do not modernize old C++ just because it looks ugly.

Do not replace legacy containers/classes unless the task requires it.

Use existing naming style as much as possible.

When adding parameters, document them in a simple way.

When a feature is experimental, keep it isolated and easy to disable.

## Script and data compatibility

Urban Assault data files are fragile.

LDF/SCR/INI-style files are plain text. Avoid rich formatting, unusual characters, and hidden Unicode.

The game uses many old script parameters. Do not break existing ones.

When adding a new OpenUA parameter, make sure:

* Missing parameter means vanilla behavior.
* Default value is safe.
* Old scripts still load.
* Parser does not crash on absent fields.
* Invalid values are clamped or ignored safely.

Mark clearly whether a parameter is vanilla or OpenUA custom.

## Important vanilla concepts

Vehicle parameters:

```text
radius       = main physical/world collision radius
overeof      = vertical collision/clearance-style value
vwr_radius   = viewer/player-control related radius/camera collision behavior
vwr_overeof  = viewer/player-control camera height; tested in-game
```

The `vwr_*` parameters are vanilla concepts, not new OpenUA custom parameters.

With visual scaling, collision and camera do not magically become perfect. Large or tiny vehicles may need manual tuning of:

```text
radius
overeof
vwr_radius
vwr_overeof
```

Weapon concepts:

```text
energy       = damage
radius       = projectile collision / hit radius
force/mass   = projectile physics
maxrot       = tracking/turning strength
life_time    = projectile lifetime when targeting
life_time_nt = projectile lifetime when not targeting
```

Visual prototype fields often include:

```text
vp_normal
vp_fire
vp_wait
vp_dead
vp_megadeth
vp_genesis
```

Be careful: not every VP field is used by every model/type.

## Important OpenUA custom feature philosophy

OpenUA custom parameters must be optional and safe.

A good OpenUA feature is:

* visible in-game,
* useful for multiple factions/units,
* configurable from scripts,
* disabled by default or vanilla-equivalent by default,
* implemented without rewriting unrelated systems.

A bad OpenUA feature is:

* hardcoded for one unit only,
* breaks vanilla balance by default,
* requires editing many unrelated files,
* silently changes old missions,
* cannot be tested easily.

## Current implemented feature families

Many systems already exist or have been worked on. Before creating a new feature, search the repo and existing docs first.

Important implemented or known feature families include:

* visual_scale / visual_scale_x/y/z / visual_scale_random_min/max
* weapon spread
* mgun spread
* num_mguns
* multi-weapon slots
* low HP weapon switching
* vp_launch
* carrier spawn
* building spawner
* spawn_at_death
* attached unit guns/flaks
* damaged system
* debuff system
* status icons
* shield/HP/status overlays
* palette selector
* mobile powerstation
* missile multi-target
* homing bomb
* proximity defense system
* proximity_defense_at_death
* seek_and_explode
* spectator mode
* spectator follow
* invulnerable vehicles

Do not reimplement these from scratch unless Matteo says the current version was rolled back.

Search first.

## Naming rules

Respect current final names.

Use:

```text
seek_and_explode
spawn_at_death_units
debuff_force_malus
debuff_maxrot_malus
debuff_shield_malus
debuff_snd_pitch_mult
damaged_force_malus
damaged_maxrot_malus
damaged_snd_pitch_mult
model = homing_bomb
```

Avoid deprecated names unless Matteo explicitly says he restored an old branch:

```text
seek_and_destroy
spawn_units_at_death
guided_bomb
model = guided_bomb
debuff_force_mult
debuff_maxrot_mult
debuff_shield_mult
debuff_snd_pitch_malus
damaged_force_mult
damaged_maxrot_mult
```

## Codex / model usage advice

Matteo has limited Codex usage. When suggesting a task for Codex, recommend a model level.

Use roughly:

```text
GPT-5.5 Low       = tiny text/string/local fixes
GPT-5.5 Medium    = small UI or localized code changes
GPT-5.5 High      = multi-file gameplay patch with clear design
GPT-5.5 XHigh     = camera/input/AI/resource systems or difficult legacy code
GPT-5.5 ExtraHigh = reverse engineering, VFS/loader, BAS/assets, major architecture
```

Keep prompts for Codex direct and efficient.

Do not waste tokens with vague instructions.

## Roadmap discipline

Do not add every cool idea to the roadmap.

Only treat something as roadmap if Matteo explicitly approves it.

Do not resurrect removed ideas unless Matteo asks.

When discussing roadmap, separate:

```text
implemented
partially implemented
planned
experimental
discarded / do not reintroduce
```

## Asset and modding context

Urban Assault uses old formats and archive structures.

Known concepts:

```text
SKLT = geometry/wireframe-style model data
POO2 = points/vertices
POL2 = polygons/lines/connections
SEN2 = likely bounding/render/culling volume; keep read-only unless told otherwise
ILBM/IFF = indexed image/texture formats
BASE = likely asset/prefab connector between SKLT + textures + mapping
SET.BAS = old packed asset container
```

Be very careful with asset loaders and binary formats.

For future asset work, prefer read-only inspection first.

Do not write binary exporters until the format is understood.

Loose-file override is a future idea: OpenUA should eventually check for loose modded files before falling back to SET.BAS. This is high-risk and should not be attempted casually.

## Sektor 2 and SKLtron context

Sektor 2 is Matteo’s Python/Tkinter Urban Assault level editor.

SKLtron is Matteo’s Python/PySide6 SKL/SKLT viewer/editor.

If working on Sektor 2:

* Preserve UI simplicity.
* Avoid breaking stable LDF export.
* Keep level files plain text.
* Keep changes easy to test.

If working on SKLtron:

* Treat POO2/POL2 as editable geometry.
* Treat SEN2 as read-only unless explicitly asked.
* Preserve binary chunk padding and endian correctness.
* Avoid destructive saves.
* Backup before writing.

## Safety rule for legacy systems

If a system is old and weird, assume it is weird for a reason.

Before changing it, ask:

```text
What vanilla behavior depends on this?
Could singleplayer missions break?
Could multiplayer desync?
Could old scripts fail?
Could AI behavior change?
Could player HUD/camera break?
Could save/load become incompatible?
```

If the answer is unknown, make the change smaller or add a config gate.

## Preferred collaboration behavior

When Matteo asks for a feature:

1. Restate the goal in simple terms.
2. Identify likely files/systems.
3. Estimate risk.
4. Suggest the appropriate Codex model level.
5. Provide a concise implementation plan or prompt.
6. Warn about possible regressions.
7. Keep the patch conservative.

When Matteo asks for debugging:

1. Do not guess blindly.
2. Ask for or inspect logs, screenshots, source zip, or changed files.
3. Compare expected behavior vs actual behavior.
4. Look for the smallest broken assumption.
5. Prefer rollback + smaller patch if the branch became messy.

When Matteo is tired or frustrated:

* Be calm.
* Reduce complexity.
* Give the next smallest step.
* Do not dump a wall of code unless needed.

## Final principle

OpenUA is legacy engine surgery.

Cut small.

Test often.

Respect vanilla.

Do not be clever when simple works.

