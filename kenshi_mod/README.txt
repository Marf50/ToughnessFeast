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
  • Save/load will NOT instantly finish a stump — Feast tracks its own
    growth and writes ToughnessFeast.progress next to the DLL

WHERE TO SEE HEAL TIME
  • Hover the HUNGER bar — full journal: food, status, every limb,
    progress bar, and time remaining (~2h 14m or "eat >200")
  • Hover TOUGHNESS on the character sheet — compact Feast + ETAs
  • Character info / medical panel — Feast lines on the selected person

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
  • Stay fed above 200 (of ~300) for limb growth. Hover Hunger to see
    exactly how long a missing arm/leg will take.
  • Stages = Missing → Stump (food dump + short KO) → weak limb
    (bigger dump + longer KO) → Healing → OK
  • Budding is intentionally slow. A full arm is a long-term recovery.
  • Skeletons cannot feast.

CONFIG
  Edit config.ini next to the DLL. All rates and unlocks are there.
  Set EnableHooks=0 for load-only safe mode.
  Set EnablePanelUi=0 if the medical / info panel lines ever misbehave.
  Set EnableToughnessTooltip=0 to keep only the Hunger journal.
  Set EnableTooltips=0 if tips ever misbehave (gameplay stays on).
  Set DebugLog=0 to quiet the log.

SAFE BY DESIGN
  • No medical writes on the hit / DR path (no mid-combat setLimb)
  • Toughness via getStat (never mis-reads Strength)
  • Race via getRace() virtual (never bad myRace offset)
  • Stage complete only from owned progress (never from loaded flesh)
  • setLimb only at bud→restore; part pointer always re-fetched
  • SEH around race / tip / regrow / panel
