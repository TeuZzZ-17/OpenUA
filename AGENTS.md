# AGENTS.md — OpenUA / Urban Assault Project

This file contains stable, generic instructions for AI coding agents working on this repository.
It is intentionally roadmap-neutral: do not assume that any specific feature is pending, completed, or approved unless the current user prompt says so.

## Project Context

This repository is related to OpenUA / UA Source, a modern source port and modding project for Microsoft Urban Assault (1998).

The codebase may contain a mix of:

* C and C++ engine/gameplay code.
* Legacy rendering, UI, input, AI, physics, weapon, vehicle, mission, and resource-loading systems.
* Data-driven configuration files for vehicles, weapons, buildings, levels, UI assets, and modding parameters.
* Python tools or editors related to map editing, asset inspection, or modding utilities.

Treat the project as a legacy game engine with many interconnected systems. Small changes can have side effects.

## Core Rule

Work only on the task explicitly requested in the current prompt.

Do not implement extra features, roadmap items, speculative improvements, cleanups, rewrites, or refactors unless the user specifically asks for them.

If something looks tempting to improve but is outside the task, mention it in the final report instead of changing it.

## Agent Compatibility

These instructions are meant to work with different coding agents, including but not limited to Codex, Claude Code, Copilot-style agents, and local IDE agents.

The repository may be modified by more than one agent over time.
Therefore:

* Keep changes small and easy to review.
* Avoid broad refactors.
* Avoid formatting-only changes in unrelated files.
* Do not rename files, functions, parameters, or data fields unless required.
* Do not overwrite another agent’s unrelated work.
* Do not assume previous agent output is correct; verify it through code inspection, build, and tests when possible.

## Branch and Git Discipline

Use one branch per task when possible.

Recommended branch style:

```bash
git checkout -b agent-short-task-name
```

Before editing, inspect the current diff:

```bash
git status
git diff
```

Do not discard or overwrite user changes unless explicitly instructed.

After editing, provide a final summary listing:

* Files changed.
* What was changed.
* Why it was changed.
* How it was tested.
* Any remaining risks or follow-up suggestions.

Do not commit automatically unless the user specifically asks for a commit.

## Build Expectations

When asked to modify C/C++ code, try to keep the project buildable.

Typical Windows/MSYS2-style build commands may be:

```bash
cmake -G Ninja -B build -S src
cmake --build build -j12
```

If the repository layout differs, inspect existing build files before assuming paths.

If a full build is expensive or unavailable, still run the most relevant lightweight checks possible and clearly report what was or was not tested.

## Coding Style

Prefer conservative, local changes.

Follow the style already used in nearby code:

* Naming conventions.
* Indentation.
* Error handling.
* Logging style.
* Data-loading patterns.
* Existing math/vector/helper utilities.
* Existing UI/render/input conventions.

Do not introduce large new abstractions unless the task clearly requires them.

Avoid modernizing unrelated legacy code just because it looks old.

## Data-Driven Design

OpenUA modding features should usually be configurable through existing data/config patterns when appropriate.

When adding or extending parameters:

* Preserve vanilla/default behavior when the parameter is absent.
* Use safe defaults.
* Avoid breaking old configuration files.
* Prefer optional parameters over mandatory new fields.
* Document newly added parameters near related parsing/loading code if the project already has such comments.
* Reuse existing parsing helpers and config conventions.

Do not silently change the meaning of existing parameters unless the task explicitly requires it.

## Gameplay Safety

When modifying gameplay systems, preserve existing behavior unless the task says otherwise.

Be especially careful with:

* Vehicle lifecycle.
* Weapon spawning and projectile ownership.
* AI target selection.
* Player control.
* Host station behavior.
* Buildings and sector ownership.
* Unit death/despawn/capture.
* Save/load or mission state.
* UI state and input handling.
* Multiplayer/network-sensitive code, if present.

Avoid changes that create uncontrolled spawning, infinite loops, repeated death triggers, invalid owners, dangling pointers, or hidden global state issues.

## Rendering and UI Safety

When modifying rendering or UI:

* Reuse existing assets and rendering helpers where possible.
* Preserve existing layout unless the task asks for a layout change.
* Avoid drawing new UI elements on the minimap unless explicitly requested.
* Keep UI changes readable at original game resolutions.
* Avoid hardcoding asset paths if nearby code uses data-driven paths.
* Avoid expensive per-frame allocations or repeated file loading.

## Asset and File Loading Safety

When working with assets:

* Prefer fallback behavior if an optional asset is missing.
* Do not crash the game because a modded file is absent.
* Preserve vanilla asset loading behavior unless explicitly changing it.
* Do not require new external assets unless the task asks for them.
* If adding support for loose files or overrides, keep fallback to original packed resources.

## Memory, Performance, and Legacy Constraints

This is a legacy-style game codebase. Avoid expensive operations inside hot loops.

Be careful with:

* Per-frame allocations.
* Repeated string parsing.
* Repeated file I/O.
* Unbounded searches over all units/buildings/projectiles.
* Temporary objects in render loops.
* Pointer ownership and object lifetime.

Prefer simple cached values when appropriate.

## Testing Checklist

Use the most relevant checks for the task.

For C/C++ engine changes:

* Configure/build with CMake/Ninja if possible.
* Launch the game if possible.
* Test the touched feature in a small controlled scenario.
* Test at least one vanilla scenario to check regressions.

For config/data changes:

* Verify the file parses.
* Verify missing/new parameters use safe defaults.
* Verify old entries still work.

For Python tools:

* Run the script/tool if possible.
* Check import errors.
* Test the specific UI/action/parser touched.
* Avoid breaking packaged builds.

## Final Response Format

When finished, report in a concise structure:

```text
Summary:
- ...

Files changed:
- path/to/file.cpp
- path/to/file.h

Tests:
- Build: passed/failed/not run
- Runtime test: passed/failed/not run

Notes / risks:
- ...
```

If something failed, be honest. Do not claim success without evidence.

## When Unsure

If the task is ambiguous, choose the smallest safe interpretation and state the assumption.

Ask for clarification only when continuing would likely cause wrong or destructive changes.

If the requested change risks affecting many systems, propose a smaller V1 first.
