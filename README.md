# ToughnessFeast

**RE_Kenshi** plugin for *Kenshi*: toughness past 100, combat bonuses soft-capped, food-powered flesh / limb regeneration.

## Download the DLL (no Windows PC needed)

1. Open the **[Actions](../../actions)** tab on this repo
2. Open the latest green **Build Windows DLL** run
3. Download the **`ToughnessFeast-mod`** artifact
4. Copy contents into:

```text
[Kenshi]/mods/ToughnessFeast/
```

Linux / Proton example:

```text
~/.steam/steam/steamapps/common/Kenshi/mods/ToughnessFeast/
```

Enable **RE_Kenshi** + **ToughnessFeast** in the Mods menu.

If Actions has not run yet: **Actions → Build Windows DLL → Run workflow**.

## What the mod does

| Toughness | Effect |
| --- | --- |
| 1–100 | Vanilla DR & wound degen |
| ≥ 75 (configurable) | Food-powered regen of flesh, stun, and normally unhealable wounds |
| > 100 | Combat bonuses **stop** scaling; extra toughness only deepens food regen |

Edit `config.ini` after install.

## Build locally (Windows MSVC)

See `NO_WINDOWS_PC.md` and `OPEN_IN_CLION.md`. Requires [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps) with Git LFS.

## Requirements

- [RE_Kenshi](https://www.nexusmods.com/kenshi/mods/847) 0.3.1+
- Kenshi (Steam)

Not affiliated with Lo-Fi Games.
