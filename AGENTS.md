# AGENTS.md — OpenUA / UA_source

Persistent instructions for AI coding agents working on this repository.

This repository belongs to Matteo / TeuZzZ-17 and contains the OpenUA / UA_source project: a modernized, moddable source-port workflow for Microsoft Urban Assault 1998.

This is not a generic C++ modernization project.

This is a legacy game preservation, reverse engineering, modding and gameplay-extension project.

The main goal is:

* preserve the original Urban Assault feeling
* preserve compatibility with vanilla game data whenever possible
* add modern modding features carefully
* keep every change small, testable and reversible

Do not treat this repository as a greenfield engine.

Do not rewrite systems just because they look old.

Do not perform broad refactors unless explicitly requested.

---

## 0. Primary project context source

When working on this repository, use `OpenUA_Project_Memory_Bible.md` as the primary compact source of truth for current project state, roadmap, parameter naming, and completed features.

Other project documents such as parameter guides, LDF/SCR guides, SKLtron notes, lore bible, README, and build guides are useful specialist references.

Do not rely on old temporary test files, old NUCLEUS examples, old patch zips, screenshots, or intermediate Codex reports as current truth unless the task explicitly points to them.

Do not save or propagate obsolete experimental names if the Memory Bible says they were removed or renamed.

---

## 1. Core mission

When working on this repo, always optimize for:

* vanilla compatibility
* small patches
* clear SCR/INI parameters
* easy in-game testing
* easy rollback
* readable code
* no unrelated changes
* no hidden behavior changes
* no overengineering

OpenUA should remain compatible with original Urban Assault game data unless a task explicitly says otherwise.

New custom OpenUA behavior should normally be disabled by default.

Missing new parameters must preserve vanilla behavior.

---

## 2. Project owner expectations

The project owner is not an expert C++ programmer, but has strong practical knowledge of:

* Urban Assault gameplay
* OpenUA behavior
* SCR/LDF modding
* Sektor 2
* SKLtron
* in-game testing
* old Urban Assault data quirks

Use simple and direct language in final reports.

Explain practical effects, not just code theory.

Always report:

1. What changed
2. Files modified
3. Build/test result
4. Suggested in-game test
5. Risks or limitations
6. Anything intentionally not touched

Do not say “works” unless it was built/tested or clearly qualified.

Do not invent successful tests.

If something is uncertain, say it clearly.

Gameplay observations from in-game testing are very important. If the owner reports behavior seen in-game, treat it as strong evidence.

---

## 3. Patch philosophy

Every patch should be:

* surgical
* local
* reversible
* reviewable
* narrowly scoped
* compatible with existing code style

Avoid:

* changing unrelated formatting
* renaming unrelated variables
* moving unrelated code
* broad cleanup
* broad modernization
* global architecture rewrites
* “while I was here” changes
* silent gameplay changes
* changing data formats without request

If the task is complex, implement the smallest useful version first.

For big features, prefer staged work:

1. data fields / parser
2. default-safe behavior
3. core gameplay logic
4. UI/status/icon integration
5. polish only after validation

Small working features beat giant unstable rewrites.

---

## 4. Vanilla compatibility rules

Default behavior must remain vanilla-safe.

New SCR parameters should default to disabled or vanilla-equivalent values.

New INI parameters should default to vanilla behavior.

Do not change vanilla semantics unless explicitly requested.

Do not remove vanilla parameters.

Do not rename vanilla parameters.

Do not reintroduce old experimental parameter names unless explicitly requested.

Original Urban Assault compatibility matters more than compatibility with failed experimental OpenUA parameter names.

Experimental OpenUA custom parameters do not need eternal backward compatibility if they were wrong, badly named, or rejected.

---

## 5. Build environment

Primary target environment:

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

Typical build commands from repo root:

```bash
cmake -G Ninja -B build -S src
cmake --build build -j12
```

If the build directory is already configured:

```bash
cmake --build build -j12
```

Check runtime DLL dependencies:

```bash
ldd build/UA_source.exe
```

Copy required MinGW64 DLLs into `build/`:

```bash
ldd build/UA_source.exe | grep mingw64/bin | awk '{print $3}' | xargs -I {} cp -v {} build/
```

CMake/FFmpeg warnings about optional components such as `avresample` or `postproc` may be environment noise. Do not treat them as patch failures unless the build actually fails.

---

## 6. Git and patch rules

Prefer normal diffs and commits.

Useful commands:

```bash
git status
git diff
git apply --check patch_name.patch
git apply patch_name.patch
```

Prefer `.diff` / `.patch` workflow over replacing whole source folders.

Avoid zip source replacement unless explicitly requested.

Do not modify build artifacts.

Do not commit generated files unless explicitly requested.

Do not hide unrelated file changes.

---

## 7. Final report format

After every task, use this structure:

```text
Summary:
- ...

Files modified:
- ...

Build/test:
- ...

Suggested in-game test:
- ...

Risks/limitations:
- ...

Not touched:
- ...
```

Bad final report:

```text
Done, improved the system.
```

Never give vague reports like that.

---

## 8. Coding style

Use the existing style of the surrounding code.

Do not introduce unnecessary modern C++ abstractions.

Avoid large class redesigns.

Avoid excessive templates.

Avoid generic frameworks unless explicitly requested.

Prefer local helper functions when they make code clearer.

Do not silence warnings globally.

Do not add permanent noisy logs.

Temporary debug logs must be behind a clear debug guard or removed before final.

---

## 9. Parser / parameter rules

When adding SCR parameters:

* add storage fields in the correct prototype structure
* initialize safe defaults
* parse the exact requested names
* preserve vanilla behavior when missing
* avoid magic sentinel values if absence can mean disabled
* keep parameter names readable
* report accepted values
* do not add aliases unless requested

When adding INI parameters:

* default must preserve vanilla behavior
* name must be explicit
* report exact parameter name
* report default value

---

## 10. Data file rules

Urban Assault data files are fragile legacy text/data files.

Important text formats:

* `.SCR`
* `.LDF`
* `.INI`
* `.LNG`
* `.FON`
* list/config files

Rules:

* plain text only
* no Word formatting
* no rich text
* no smart quotes
* no unnecessary Unicode decorations
* preserve legacy formatting where practical
* avoid mass reformatting
* backup/diff before changing important data

If something fails in-game, check logs such as `YPA_Log` / ENV logs when applicable.

---

## 11. Common project areas

Common source/data areas may include:

* `src/` — C++ engine source
* `src/world/` — prototypes, parsers, world/game logic
* `src/system/` — INI/system config
* `locale/language.lng` — UI/localized text
* `Data/Scripts/` — SCR prototype data
* `Data/Fonts/` — HUD/map/radar/icon atlas assets
* `levels/` — LDF level data
* `tool/` — tools such as SKLtron or related utilities

Do not assume exact paths without searching the repo.

Before modifying behavior, inspect similar existing code.

---

## 12. Urban Assault level basics

Level files use blocks such as:

```text
begin_level
end
```

Common level sections:

* `begin_level`
* `begin_robo`
* `begin_gate`
* `begin_squad`
* `begin_squadron`
* `begin_gem`
* `begin_maps`

Common maps:

* `typ_map` — visual sector type
* `own_map` — sector owner
* `hgt_map` — height map
* `blg_map` — functional buildings

Each sector is 1200 x 1200.

Avoid unit/host coordinates ending in `0` or `5`, because this can cause spawn/slurp problems.

Prefer endings like:

```text
1, 2, 3, 4, 6, 7, 8, 9
```

Owner values:

```text
0 = neutral
1 = Resistance
2 = Sulgogars
3 = Mykonians
4 = Taerkasten
5 = Black Sect
6 = Ghorkov
7 = special neutral for power/radar/beamgate contexts
```

`begin_gem type` values:

```text
0 = universal / multiplayer
1 = weapon upgrade
2 = shield upgrade
3 = new vehicle
4 = new building
5 = radar expansion scout
6 = combined vehicle/building upgrade
```

For high Host Station energy values, `reload_const` matters. Without it, Host Stations can become nearly immortal or spam units.

---

## 13. Vehicle parameters

Common vehicle parameters:

```text
new_vehicle
enable
model
name
wireframe
hud_wireframe
wpn_wireframe_1
wpn_wireframe_2
mg_wireframe
type_icon
vo_type
weapon
num_weapons
weapon2
weapon3
weapon4
weapon_switch_mode
mgun
num_mguns
mgun_fire_x
energy
shield
mass
force
maxrot
airconst
height
radar
radius
overeof
vwr_radius
vwr_overeof
adist_sector
adist_bact
sdist_sector
sdist_bact
job_fightrobo
job_fightflyer
job_fighthelicopter
job_fighttank
job_conquer
job_reconnoitre
vp_normal
vp_fire
vp_wait
vp_dead
vp_megadeth
vp_genesis
dest_fx
scale_fx
visual_scale
```

Important notes:

* `radius` is the main vehicle collision/world radius.
* `overeof` is vertical collision/height/terrain clearance behavior.
* `vwr_radius` is a vanilla viewer/player-control related radius, not an OpenUA custom feature.
* `vwr_overeof` is vanilla and affects camera/view height when controlling the vehicle.
* With `visual_scale`, collision and camera still need manual tuning through `radius`, `overeof`, `vwr_radius`, `vwr_overeof`.
* `scale_fx` is a legacy temporary/animated FX scaling system. Do not treat it as permanent model scaling.
* `wireframe`, `hud_wireframe`, `wpn_wireframe_1`, `wpn_wireframe_2`, and `mg_wireframe` are interface/HUD model references, often SKL/SKLT-like assets.

---

## 14. Weapon parameters

Common weapon parameters:

```text
new_weapon
model
name
wireframe
energy
mass
force
maxrot
airconst
start_speed
delay_time
drive_time
life_time
life_time_nt
shot_time
shot_time_user
salve_shots
salve_delay
radius
type_icon
vp_normal
vp_wait
vp_dead
vp_megadeth
vp_fire
vp_genesis
dest_fx
energy_tank
energy_heli
energy_flyer
energy_robo
radius_tank
radius_heli
radius_flyer
radius_robo
vp_launch
missile_multi_target
```

Important notes:

* `energy` is damage.
* `radius` is generic projectile collision radius against units.
* `energy_tank`, `energy_heli`, `energy_flyer`, `energy_robo` are useful class damage multipliers.
* `radius_tank`, `radius_heli`, `radius_flyer`, `radius_robo` are legacy/per-class hit radius behavior and should generally not be expanded in modern OpenUA features unless explicitly requested.
* Prefer simple `radius` + `energy_*` balancing.
* `force`, `mass`, `maxrot`, `airconst`, `start_speed`, `drive_time`, and `life_time` affect projectile physics/tracking/lifetime.
* `dest_fx = death/megadeth_ID_X_Y_Z`

  * `death` is used on unit hit or projectile end-of-life behavior.
  * `megadeth` is used on terrain/building/world surface impact.

---

## 15. Visual prototype behavior

Vehicle visual prototypes:

```text
vp_normal   = normal visual state
vp_fire     = firing visual state
vp_wait     = waiting/idle visual state
vp_dead     = initial destroyed/death state, especially visible for airborne units
vp_megadeth = crash/final death/world impact state
vp_genesis  = spawn/beam/genesis visual state
```

Weapon visual prototypes:

```text
vp_normal   = projectile in flight
vp_wait     = idle/wait if applicable
vp_dead     = detonation/death on unit hit or projectile end-of-life
vp_megadeth = impact on world/surface/building/terrain
vp_fire     = may be unused or limited
vp_genesis  = may be unused or limited
```

Do not assume vehicle and weapon `vp_dead` / `vp_megadeth` have identical semantics.

---

## 16. Implemented OpenUA features

The following are already implemented or considered complete in the current project state.

Do not reimplement them from scratch.

Do not propose them as new roadmap items unless the task is explicitly about fixing, auditing, or extending them.

Implemented / complete:

* `visual_scale`
* HUD weapon names
* GEM weapon names
* MGUN multi-barrel: `num_mguns` + `mgun_fire_x`
* multi-weapon slots: `weapon2`, `weapon3`, `weapon4`, `weapon_switch_mode`
* `vp_launch`
* carrier spawn system
* building spawner system
* attached unit guns/flaks
* damaged system
* status icons base system
* palette selector
* player Host Station AI behavior / physical relocation
* AoE damage
* cluster weapons
* modern audio loader
* modern audio formats, with runtime/DLL caveat
* audio channel expansion
* particle crash fix for invalid particle point IDs
* Sektor 2
* SKLtron in evolution
* debuff system
* Sulg infection/conversion via debuff mind control
* `decoration_fx` follow unit
* chain FX system expansion
* `delay_time` on unit impact
* missile multi-target
* missile multi-lock HUD
* power generating vehicles / mobile powerstation behavior
* Proximity Defense System V1/V1.1

---

## 17. Roadmap / future candidate features

Approved or candidate future work may include:

* Spectator Mode
* Unit Tactical Database
* Mini descriptions in build menu
* Limited ammo
* Faction voice packs
* OpenUA diff/changelog generator
* Modular huge units
* Advanced laser/beam weapons
* Water/naval mega-feature
* non-uniform per-axis visual scaling:

  * `visual_scale_x`
  * `visual_scale_y`
  * `visual_scale_z`
* Seek and Destroy / kamikaze units:

  * `seek_and_destroy`
  * `seek_and_destroy_weapon`

Do not implement roadmap items unless explicitly requested.

Do not expand the roadmap with duplicates.

Current Spectator Mode parameter names:

```ini
game.spectator_mode = yes/no
game.spectator_vehicle_id = <vehicle_id>
game.spectator_owner1_ai = balanced
```

Old removed Spectator Mode names must not be used or re-added unless explicitly requested:

```ini
game.spectator_owner_profile
game.spectator_owner_ai_mode
copy_random/off mode
```

Spectator Follow Unit V1 exists but requires stabilization. Do not treat it as fully polished unless later validated.

---

## 18. Things not to propose as new roadmap

Do not list these as new roadmap items:

* `vwr_radius` as OpenUA feature
* `vwr_overeof` as OpenUA feature
* render distance multiplier objects/buildings
* AoE impulse
* healing beam
* radial burst weapon
* render distance UI
* status icons animate
* terrain render distance V2
* separate unit encyclopedia duplicate of Unit Tactical Database
* carrier Zeppelin Taerkasten as engine feature
* decoration_fx multiple slots
* RGB tracers / advanced trails
* separate damage behavior expansion
* AI vs AI mode
* generic support aura units
* mobile radar vehicles as a new engine concept
* Host Station physically collecting GEMs
* separate mental conversion feature
* biological debuff multiples/stacking
* parasite/sticky units as separate roadmap
* units generating other units as new concept
* boss colossals separate from modular huge units
* Stoudson Bomb extensible/scriptable
* lore PNG database separate from Unit Tactical Database
* loose-file override as standalone roadmap unless explicitly reopened

---

## 19. Player Host Station AI behavior / physical relocation

Implemented feature:

```ini
game.player_robo_ai_behavior = yes/no
```

Default must remain off/no/false.

When off, vanilla player Host Station telebeam/teleport behavior remains unchanged.

Old parameter name:

```ini
game.player_robo_mobile
```

was intentionally removed. Do not re-add alias/fallback unless explicitly requested.

When enabled:

* player can select/click own Host Station on map and give a destination
* player Host Station moves physically instead of teleporting instantly
* movement is manual, not autonomous enemy AI
* do not use full enemy Host Station AI for the player
* Host Station collides with buildings
* movement drains beam/move energy gradually, equivalent to vanilla teleport cost
* during physical relocation, vanilla `EnergyInteract()` must not refill/rebalance `_roboEnergyMove`
* movement should continue while player controls another unit/flak
* twist/flux can be used while stationary if enabled
* during movement, relocation behavior wins over twist/flux

Common files:

```text
src/yparobo.cpp
src/yparobo.h
src/yw_game_ui.cpp
src/yw.cpp
src/system/inivals.cpp
src/system/inivals.h
locale/language.lng
```

---

## 20. Mobile power / power generating vehicles

Implemented/support system.

Goal:

* allow certain vehicles to generate/provide energy like mobile power stations
* power effect follows the moving unit
* nearby allied units can receive regeneration/effect
* status icon should appear where applicable
* useful for large support units such as future Taerkasten Zeppelin concepts

Important bug context:

* mobile power must not incorrectly depend on static powerstation reload ratio after static power changes/destruction
* if icon remains visible but regeneration stops, inspect energy calculation source and owner/faction power logic

---

## 21. Missile multi-target

Implemented.

Behavior:

* weapon-level feature
* `missile_multi_target = 0` disables it
* values greater than 0 define maximum distinct targets
* limited by `num_weapons`
* distribution should be dynamic and balanced
* works through common `LaunchMissile` path
* applies to player, enemy AI, and allied/non-player units using eligible missiles
* HUD multi-lock reuses existing red missile lock wireframes
* HUD remains vanilla/single-lock when disabled or when only one valid target exists

Do not reimplement from scratch.

---

## 22. Proximity Defense System

Implemented V1/V1.1 and validated in-game.

Concept:

Vehicle-level auxiliary proximity-triggered radial/scattered weapon system.

It is not enemy-targeted multi-target.

It does not assign targets.

It does not use homing or HUD locks in V1.

V1 parameters:

```ini
proximity_defense_enable
proximity_defense_weapon
proximity_defense_trigger_radius
proximity_defense_interval
proximity_defense_shots
proximity_defense_fire_x
proximity_defense_fire_y
proximity_defense_fire_z
proximity_defense_vp_launch
proximity_defense_fire_mode = all_at_once/sequential
proximity_defense_sequence_delay
```

V1.1 random direction parameters:

```ini
proximity_defense_random_yaw_min
proximity_defense_random_yaw_max
proximity_defense_random_pitch_min
proximity_defense_random_pitch_max
```

Default behavior remains deterministic radial 360-degree ring if random ranges are absent.

`all_at_once` and `sequential` remain unchanged.

Sequential mode fires first shot immediately, then one shot per sequence delay, and only after sequence completion does interval cooldown begin.

Netgame may disable this to avoid unsynchronized projectiles.

Do not rename it back to Porcupine Weapon.

---

## 23. Seek and Destroy / kamikaze units — future candidate

Candidate feature, not implemented unless later state says otherwise.

Goal:

Allow a vehicle/unit to use its body as a delivery system for an explosive payload.

Official V1 parameters:

```ini
seek_and_destroy = 1
seek_and_destroy_weapon = <weapon_id>
```

Old provisional names are obsolete and must not be used:

```ini
seek_and_destroy
seek_and_destroy_weapon
```

No `scan_radius` in V1.

No `trigger_radius` in V1.

Target should be the normal current AI/player target already chosen by the game.

Behavior intent:

* force attack behavior to close physical distance to target
* detonation occurs when vehicle radii touch:

  * conceptually `distance <= self.radius + target.radius`
* use `seek_and_destroy_weapon` as instant payload at suicide unit position
* do not spawn a fake missile trajectory
* after detonation, suicide unit dies/is removed
* unit may optionally also have a normal weapon while chasing
* must not stop at normal firing distance if `seek_and_destroy` is enabled
* should continue closing until physical contact

Likely complexity: High, because it touches AI movement, target handling, collision/radius, death/removal, and weapon payload logic.

---
## 24. Building spawner system

Already handled/completed.

If touching it for fixes:

* building ownership changes must stop invalid old-owner spawn behavior
* building-spawned units should spawn in sensible positions above/near the building as designed
* vehicle spawner behavior must not be accidentally changed while fixing building spawner behavior
* GEM/UI unlock popup should show spawner icon when unlocking a building with spawn capability

Do not propose building spawner as a new feature.

---

## 25. Status icon rules

Status/building-style icons should follow the project visual language.

Color guidance:

* debuff icons: generally yellow
* vanilla/neutral utility icons such as radar/flak/energy/power: gray or neutral
* mind control icons: purple
* severe/high-damage debuffs: orange
* regeneration/life regeneration/mobile power support: green
* drain icons: red

Keep icons close to vanilla Urban Assault style when possible.

---

## 26. Unit Tactical Database

Approved roadmap.

Goal:

Create an in-game technical/tactical unit database, not a fantasy RPG bestiary.

Preferred names:

* Unit Tactical Database
* Tactical Database
* Unit Database
* Combat Archive

Avoid:

* Bestiary

Planned features may include:

* mini descriptions in build/creation menu
* replacing old obsolete Help button with a new tech-style button
* screen listing units with image/screenshot, description, lore, tactical role, strengths/weaknesses
* data-driven design where possible
* vanilla-safe behavior

Do not create separate duplicate “unit encyclopedia” roadmap item.

---

## 27. Sektor 2

Sektor 2 is the visual LDF editor for Urban Assault/OpenUA.

Technology:

* Python
* Tkinter

Repo:

```text
TeuZzZ-17/Sektor_map_editor
```

License:

```text
GPLv3
```

Common files:

```text
Sektor.py
sektor_canvas.py
sektor_ui.py
sektor_map_io.py
sektor_constants.py
sektor_assets.py
sektor_dialogs.py
JSON definitions
```

Correct PyInstaller build:

```bash
python -m PyInstaller --onefile --windowed --clean --noconfirm --icon ".\icons\Sektor2.ico" --name "Sektor 2" ".\Sektor.py"
```

Compile `Sektor.py`, not `sektor_ui.py`.

Sektor 2 is considered implemented/complete except future refinements.

Do not redesign Sektor unless explicitly requested.

---

## 28. SKLtron

Correct name:

```text
SKLtron
```

Incorrect name:

```text
SKLTron
```

SKLtron is the modern Python/PySide6 editor/viewer for Urban Assault SKL/SKLT/wireframe/skeleton assets.

Current/future tool area:

```text
tool/sklt_wireframe_viewer
```

Build command:

```bash
python -m PyInstaller --onefile --windowed --clean --noconfirm --icon=icons\SKLtron.ico --name="SKLtron" --add-data "icons;icons" main.py
```

Known SKL/SKLT format theory:

* FORM(SKLT), IFF-like chunk structure
* big-endian data
* even-byte padding
* `POO2` = 3D points/vertices, 3 big-endian floats, 12 bytes per point
* `POL2` = polygons/lines/connections
* `SEN2` = likely bounding/render/culling cuboid or control volume, often 8 points

Treat `SEN2` as read-only unless explicitly asked.

SKLtron already proved a real pipeline:

* edit POO2/POL2
* save asset
* replace asset
* OpenUA shows deformation in-game

This is a major project milestone.

---

## 29. SKLtron future editing roadmap

Approved SKLtron roadmap under the SKLtron umbrella:

* multi-selection of vertices and lines/polygons
* Ctrl+Click selection
* move selected elements together
* Cut / Copy / Paste / Delete / Duplicate
* right-click Edit context menu
* possible future Box Selection

Do not split this into unrelated separate tools unless requested.

---

## 30. Asset reverse engineering: SKLT / BASE / ILBM / ANM / SET.BAS

Current theory:

```text
SKLT = real model geometry
ILBM = indexed/palette texture
BASE = asset/prefab glue connecting SKLT + textures + face/material/UV mapping
ANM = likely material/texture animation or related asset animation
```

Example BP_FLAK1 analysis:

```text
BP_FLAK1.sklt:
- FORM(SKLT)
- POO2 136
- SEN2 8
- POL2 149
```

```text
BP_FLAK1.base:
- references Skeleton/BP_FLAK1.sklt
- references textures MTL.ILBM and BODEN5.ILBM
- contains ATTS and OLPL
```

Observed mapping:

```text
MTL.ILBM    = 92 ATTS/OLPL entries
BODEN5.ILBM = 57 ATTS/OLPL entries
92 + 57     = 149
SKLT POL2   = 149
```

Strong interpretation:

```text
POL2 = model faces/polygons
ATTS = assigns faces to texture/material
OLPL = 2D/UV/polygon texture list data
```

This is a path toward textured preview in SKLtron, but do not implement unless requested.

Loose-file override idea:

* OpenUA checks loose asset override before SET.BAS
* if override exists and loads, use it
* if missing or fails, fallback to SET.BAS
* useful for SKLtron workflow
* not currently a standalone roadmap unless explicitly reopened
* likely high/XHigh complexity due to loader/VFS/cache implications

---

## 31. Asset and palette rules

IFF/ILB/ILBM editing is fragile.

Common tools:

* GrafX2
* Ultimate Paint
* GIMP
* Audacity

`Data/Fonts` contains HUD/map/radar/icon atlas assets. These are index-sensitive.

`TYPE_NS.FON` maps `type_icon` to HUD/finder icons.

`assign.txt` in `env` can alter folder structure/name mapping. `:` acts like `/`.

Palette slots:

```text
slot0
slot1
slot2
slot3
slot4
slot5
slot6
slot7
```

Do not alter color index 0 in legacy PAL files such as `STANDARD.PAL`, because it can break invisibility/masks and reveal hidden textures.

When converting map/briefing/UI images, preserve indexed palette behavior.

---

## 32. Audio rules

Vanilla Urban Assault sound format:

```text
WAV Microsoft
Mono
22050 Hz
Unsigned 8-bit PCM
```

OpenUA modern audio loader may support:

* MP3
* OGG
* FLAC
* modern WAV

But this depends on runtime/DLL availability.

Do not break vanilla WAV compatibility.

Do not replace the vanilla audio path with modern-only behavior.

---

## 33. Lore / faction identity guidance

Urban Assault lore matters for feature and unit design.

Factions:

```text
Resistance:
- remaining free democracies
- Host Station
- SDU

Ghorkov:
- militarist Eurasian empire
- industrial/military expansion

Taerkasten:
- retrocultist / neo-luddite armed order
- WWII/monastic aesthetics
- slow but tough identity

Mykonians:
- aliens treating Earth as battery/resource
- Parasite
- geometric/crystalline/bio-vein identity

Sulgogars:
- bio-vegetative/parasitic species
- Earth as nursery
- infection/corruption potential

Black Sect:
- clandestine hybrid/stealth faction
- tech theft
- Anvil-class
```

Always distinguish:

* vanilla 1998 canon
* unreleased/official Metropolis Dawn material
* community/fan interpretation

Useful for:

* Tactical Database
* descriptions
* faction identity
* unit role design

---

## 34. Sulg gameplay design direction

Preferred modern Sulg identity:

* not just high DPS aliens
* biological pressure
* infection
* debuffs
* weakness
* mind control / temporary conversion
* battlefield corruption
* attrition
* spores
* slow collapse of enemy formation

Implemented systems already support much of this:

* debuffs
* mind control
* status icons
* AoE
* cluster weapons
* carrier spawn
* damage over time / weakness logic where implemented
* visual FX chains

Avoid turning every Sulg concept into a new engine feature if it can be built with existing systems.

---

## 35. Taerkasten Zeppelin / future unit design note

The Taerkasten Zeppelin is a future unit concept, not an engine roadmap item by itself.

It may combine existing or planned systems:

* `visual_scale`
* attached flaks
* carrier spawn
* power generating vehicles
* radar capability
* status icons
* decoration/chain FX
* damaged system
* possibly future modular huge unit support

Do not list “Carrier Zeppelin Taerkasten” as a separate engine feature.

---

## 36. Water/naval mega-feature

Approved long-term mega-feature.

Umbrella concept:

* water
* rivers
* oceans
* aquatic terrain support
* naval units
* fleets
* water maps

Do not split into separate faction-specific naval roadmap entries.

Likely XHigh/ExtraHigh complexity or beyond.

Do not implement casually.

---

## 37. OpenUA diff/changelog generator

Approved roadmap.

Goal:

Create a Python tool comparing vanilla OpenUA/UA source tree against Matteo’s modified tree and generating a human-readable diff/changelog.

Should ignore:

* build artifacts
* generated files
* irrelevant binary outputs

Purposes:

* reconstruct project history
* identify undocumented engine features
* generate changelog
* update modder feature docs
* summarize engine changes over time

---

## 38. Model complexity guidance

When selecting model effort:

```text
Low:
- documentation-only
- tiny strings
- simple diffs
- applying known patch

Medium:
- localized UI change
- cleanup
- small parser addition
- simple feature in known area

High:
- delicate C++ gameplay behavior
- AI/movement/collision
- multi-file local feature
- SKLT binary editing

XHigh / ExtraHigh:
- reverse engineering
- VFS/resource loader
- SET.BAS override
- textured SKLtron
- water/naval systems
- structural engine changes
```

When work is expensive or risky, prefer analysis first and implementation second.

---

## 39. Handling uncertainty

If unsure:

* inspect code first
* search for similar existing patterns
* make a minimal hypothesis
* avoid broad changes
* ask for clarification only if needed
* state uncertainty in final report

Do not fake knowledge.

Do not say a thing is vanilla or custom unless verified or clearly known.

When an implementation can affect vanilla behavior, explicitly call out the risk.

---

## 40. What not to do

Do not:

* rewrite the renderer casually
* rewrite AI globally
* rewrite the resource loader casually
* replace legacy parser systems without request
* remove old behavior just because it looks ugly
* add global debug spam
* silence important warnings
* change unrelated files
* reformat whole files
* invent new roadmap items without permission
* claim in-game validation without an actual in-game test
* use heavy abstractions where simple fields/functions are enough
* break original Urban Assault data compatibility
* include personal/private user context in project files

---

## 41. Good final answer example

Good final report:

```text
Summary:
Implemented parser fields and default-disabled prototype storage for seek and destroy.

Files modified:
- src/world/protos.h
- src/world/parsers.cpp

Build:
- cmake --build build -j12 passed

Behavior:
- Missing parameters preserve vanilla behavior.
- seek_and_destroy defaults to false.
- No gameplay detonation logic added yet.

Risks:
- None expected at runtime because fields are parsed but unused.

Suggested in-game test:
- Add seek_and_destroy/seek_and_destroy_weapon to one test vehicle and confirm the game still loads.
```

Bad final report:

```text
Done, improved the system.
```

Never give that kind of report.

---

## 42. Final priority rule

The owner values progress, but not at the cost of corrupting the project.

If a feature becomes too messy:

* stop
* explain the problem
* propose rollback
* propose a smaller V1
* do not keep forcing a broken architecture

Small working features beat giant unstable rewrites.

This project is a living restoration/modding effort. Preserve the soul of Urban Assault while extending it carefully.
