#ifndef WORLD_CLONEBALANCE_H_INCLUDED
#define WORLD_CLONEBALANCE_H_INCLUDED

#include "protos.h" // for World::TVisualTint

// NC_STACK_ypabact lives in the global namespace; only a forward declaration is
// needed here so the helper API stays decoupled from the heavy actor header.
class NC_STACK_ypabact;

namespace World
{

// OpenUA custom: "Black Sect imperfect grey clone" runtime balance.
//
// Black Sect units (owner/faction 5) play like slightly degraded grey clones of
// the prototype they share with the other factions. Every malus below is applied
// ONLY to the *live runtime* values of eligible owner-5 actors:
//   * it never mutates the shared vehicle or weapon prototypes;
//   * it never affects the same prototype when used by another owner;
//   * it is recomputed from the unmodified base/prototype values on every query,
//     so it can never compound across save/load/respawn (no stored malus).
//
// Energy / max HP is intentionally left untouched, because energy is also tied to
// unit cost / economy. See the call sites in ypabact.cpp for the exact effective
// values that are adjusted (defense, shot_time, outgoing damage, force, maxrot,
// sound pitch) and the grey identity tint applied at render time.
namespace CloneBalance
{
    // Black Sect faction/owner id (the imperfect grey clones).
    constexpr int OWNER_BLACK_SECT = 5;

    // Read the game.black_sect_clone_* keys from System::IniConf and cache them.
    // Call once after nucleus.ini has been parsed. Idempotent: safe to re-run.
    void Init();

    // Master switch (game.black_sect_clone_balance). False = vanilla behavior.
    bool Enabled();

    // Multipliers derived from game.black_sect_clone_malus_percent (default 5):
    //   DownFactor()       = 1 - p/100  (0.95) -> effective defense, outgoing
    //                                             damage, force, maxrot, sound pitch
    //   AttackTimeFactor() = 1 + p/100  (1.05) -> shot_time / cooldown (slower fire)
    float DownFactor();
    float AttackTimeFactor();

    // Grey clone identity tint (game.black_sect_clone_tint), normalized to 0..1.
    const TVisualTint &Tint();

    // The single gate every runtime malus funnels through. True only when:
    //   * the clone balance is enabled, AND
    //   * the actor is a Black Sect (owner 5) actor, AND
    //   * the actor is an actual combat UNIT (tank/flyer/ufo/car/gun).
    // Exempt actors (return false even for owner 5):
    //   * the Host Station (BACT_TYPES_ROBO) — the faction command base, not a clone;
    //   * projectiles (BACT_TYPES_MISSLE) — they keep vanilla flight/pitch/visuals.
    //     The reduced firing rate lives on the firing unit (shot_time) and the
    //     outgoing-damage malus is charged to the emitter unit in ModifyEnergy.
    bool IsCloneActor(const NC_STACK_ypabact *bact);
}

}

#endif // WORLD_CLONEBALANCE_H_INCLUDED
