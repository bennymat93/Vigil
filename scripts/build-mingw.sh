#!/usr/bin/env bash
# Cross-compile Vigil.exe for Windows x64 from Linux/macOS using MinGW-w64.
#   sudo apt-get install mingw-w64         # Debian/Ubuntu
#   brew install mingw-w64                 # macOS
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
mkdir -p "$BUILD"

CXX="${CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"

# Regenerate icons if Pillow is available (optional).
if command -v python3 >/dev/null && python3 -c "import PIL" 2>/dev/null; then
    python3 "$ROOT/scripts/make_icons.py" || true
fi

# Compile resources (run from src so relative icon paths resolve).
( cd "$SRC" && "$WINDRES" vigil.rc -O coff -o "$BUILD/vigil_res.o" )

# Link the executable: static, GUI subsystem, Unicode entry point.
"$CXX" -municode -O2 -s -mwindows -static -static-libgcc -static-libstdc++ \
    -o "$BUILD/Vigil.exe" \
    "$SRC/main.cpp" "$BUILD/vigil_res.o" \
    -lshell32 -lole32 -ladvapi32 -luser32

echo "Built: $BUILD/Vigil.exe"
ls -la "$BUILD/Vigil.exe"
