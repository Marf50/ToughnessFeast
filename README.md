# ToughnessFeast

RE_Kenshi plugin for Kenshi: toughness past 100, combat soft-cap, food-powered limb regen.

## See heal time in-game

- Hover **Hunger** — full Feast journal with progress bars and time remaining
- Hover **Toughness** — compact Feast + limb ETAs
- Character info / medical panel — Feast lines on the selected person

Loading a save will **not** instantly finish a stump. Progress is stored in `ToughnessFeast.progress` next to the DLL.

## No Windows PC?

**→ Read [NO_WINDOWS_PC.md](NO_WINDOWS_PC.md)**  

Push this folder to GitHub → free Windows Actions runner builds `ToughnessFeast.dll` → download the artifact → drop into Kenshi `mods/`.

## Linux CLion

Open this folder → CMake reload → IDE stubs clear red errors. That does **not** produce a game DLL.

## Windows MSVC (if you ever have one)

```bat
cmake -S . -B build -A x64 -DKENSHILIB_DIR=...\KenshiLib -DBOOST_INCLUDE_DIR=...\boost_1_60_0
cmake --build build --config Release
```
