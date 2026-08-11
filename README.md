TOUGHNESS FEAST  (RE_Kenshi plugin)
====================================

WHAT IT DOES
  • Toughness still rises past 100.
  • Combat DR / wound degen soft-cap at 100 (configurable).
  • Excess toughness → Feast power: while FED, slowly
      - regrow crushed / severed limbs (staged, slow)
      - heal wounds that normally never self-heal
      - paid for with hunger
  • Race unlocks: Hiver ~0, Shek ~50, Human ~75
  • Weak / missing limbs slightly hurt combat until mended
  • Hover HUNGER for the Feast journal (all live stats)

INSTALL
  1. Install RE_Kenshi for Kenshi.
  2. Copy this whole folder to:
       Kenshi/mods/ToughnessFeast/
     so you have:
       mods/ToughnessFeast/ToughnessFeast.dll
       mods/ToughnessFeast/RE_Kenshi.json
       mods/ToughnessFeast/config.ini
       mods/ToughnessFeast/ToughnessFeast.mod
  3. Enable the mod in the Kenshi launcher / mod list.
  4. Launch. Check RE_Kenshi log for "ToughnessFeast: ready".

HOW TO PLAY
  • Train toughness (get hit, fight). Past 100 it still creeps up.
  • Stay fed. Hover the Hunger bar — Feast journal shows unlock,
    power, food use, and every limb stage.
  • Missing arm/leg: stages = MISSING/budding → Fragile → Healing → OK
  • Budding is intentionally slow. A full arm is a long-term recovery.
  • Skeletons cannot feast.

CONFIG
  Edit config.ini next to the DLL. All rates and unlocks are there.
  Set EnableHooks=0 for load-only safe mode.
  Set EnableTooltips=0 if tips ever misbehave (gameplay stays on).
  Set DebugLog=0 to quiet the log.

SAFE BY DESIGN
  • No medical writes on the hit / DR path (no mid-combat setLimb)
  • Toughness via getStat (never mis-reads Strength)
  • Race via getRace() virtual (never bad myRace offset)
  • setLimb only at bud→restore; part pointer always re-fetched
  • SEH around race / tip / regrow
