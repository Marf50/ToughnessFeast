@echo off
setlocal
cd /d "%~dp0\.."

if "%KENSHILIB_DIR%"=="" (
  echo Set KENSHILIB_DIR first, e.g.:
  echo   set KENSHILIB_DIR=C:\deps\KenshiLib_Examples_deps
  exit /b 1
)

cmake -S . -B cmake-build-release -A x64 -DKENSHILIB_DIR="%KENSHILIB_DIR%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build cmake-build-release --config Release
if errorlevel 1 exit /b 1

echo.
echo DLL: kenshi_mod\ToughnessFeast.dll
echo Copy kenshi_mod\ to [Kenshi]\mods\ToughnessFeast\
