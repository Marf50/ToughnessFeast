#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${KENSHILIB_DIR:-}" ]]; then
  echo "Set KENSHILIB_DIR to KenshiLib_Examples_deps root" >&2
  exit 1
fi

if [[ "$(uname -s)" != MINGW* && "$(uname -s)" != MSYS* && "$(uname -s)" != CYGWIN* ]]; then
  if ! command -v cl.exe >/dev/null 2>&1 && [[ -z "${VCINSTALLDIR:-}" ]]; then
    echo "No MSVC detected. Open this project in CLion with a Windows/MSVC toolchain." >&2
    echo "Native Linux build cannot produce a Kenshi-loadable DLL." >&2
    exit 2
  fi
fi

cmake -S . -B cmake-build-release -DKENSHILIB_DIR="$KENSHILIB_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --config Release
echo "DLL: kenshi_mod/ToughnessFeast.dll"
