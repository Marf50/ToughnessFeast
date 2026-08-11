# Fix CLion “Cannot find Windows.h / Debug.h / kenshi” on Linux

Those errors happen when CLion uses **only Linux system includes**.  
This project is set up so **Linux CLion indexes cleanly** via IDE stubs.

## Do this after extracting a new zip

1. Open the folder that contains **`CMakeLists.txt`**  
   (must also contain `third_party/` and `src/`).
2. **Tools → CMake → Reset Cache and Reload Project**
3. Confirm the CMake tool window says something like:
   ```text
   IDE mode (non-MSVC): using third_party/kenshi-ide-stubs
   ```
4. If still red: **File → Invalidate Caches → Invalidate and Restart**

## What each error meant

| Error | Cause | Fix in this project |
| --- | --- | --- |
| `Windows.h` | No WinSDK on Linux | `third_party/win32-stubs` + IDE mode |
| `Debug.h`, `core/`, `kenshi/` | KenshiLib not on include path | `TOUGHNESSFEAST_LINUX_IDE` → `kenshi_ide_stubs.h` |
| `MAX_PATH` | Windows macro | defined in stubs |

You should **not** need `-DKENSHILIB_DIR` just to edit on Linux.

## Real DLL (Windows only)

A loadable RE_Kenshi plugin still needs:

- Windows + **MSVC** toolchain (toolset **v100** preferred)
- Real `KenshiLib.lib` via [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps) + **git lfs pull**
- CMake on Windows will use `third_party/KenshiLib/Include` and link the `.lib`

Linux **Build** only checks that the IDE stub compile works — it will **not** load in Kenshi.
