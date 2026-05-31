#!/usr/bin/env bash
#
# Build the Pro Motion NG file-I/O plugins into Windows x64 DLLs on macOS, using
# the mingw-w64 cross-compiler, and optionally install them into a local Pro
# Motion NG install running under Wine/CrossOver.
#
# Why this exists: Pro Motion NG plugins are Windows DLLs. On a Mac there's no
# MSVC, so we cross-compile with mingw-w64. Each DLL is statically linked against
# libgcc/libstdc++ so it needs only system DLLs (KERNEL32 + the UCRT api-ms-win
# forwarders) that Wine already provides.
#
# Plugins handled:
#   c1    - Apple IIgs SHR (raw 32K)     (no external deps)
#   i16   - 16-color Foenix, lzsa2       (links the bundled lzsa lib)
#   i256  - 256-color Foenix, lzsa2      (links the bundled lzsa lib)
#
# Usage:
#   ./build_plugins_macos.sh                  # build all -> <plugin>/build/<name>.dll
#   ./build_plugins_macos.sh c1 i256          # build only the named plugins
#   ./build_plugins_macos.sh --install        # build all, then install
#   ./build_plugins_macos.sh --install i16    # build + install only i16
#   PMNG_PLUGINS="/path/to/Pro Motion NG - V8/plugins" ./build_plugins_macos.sh --install
#
# Requires: brew install mingw-w64
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # file_io_plugin_samples
CXX="${CXX:-x86_64-w64-mingw32-g++}"
CC="${CC:-x86_64-w64-mingw32-gcc}"
OBJDUMP="${CXX%-g++}-objdump"

# Default install target: a Pro Motion NG plugins folder inside a Wine prefix.
# Override with the PMNG_PLUGINS env var if yours lives elsewhere.
PMNG_PLUGINS="${PMNG_PLUGINS:-$HOME/Applications/ProMotionNG/wineprefix/drive_c/Program Files/cosmigo/Pro Motion NG - V8/plugins}"

# --- per-plugin config -----------------------------------------------------
# Fields: dir | output DLL basename | export-shim cpp | file-format cpp | uses lzsa?
plugin_cfg() {
    case "$1" in
        c1)   echo "c1   c1ImgIo   c1ImageIo.cpp   c1_file.cpp   no"  ;;
        i16)  echo "i16  i16ImgIo  i16ImageIo.cpp  16_file.cpp   yes" ;;
        i256) echo "i256 i256ImgIo i256ImageIo.cpp 256_file.cpp  yes" ;;
        *)    return 1 ;;
    esac
}

# --- args ------------------------------------------------------------------
INSTALL=0
TARGETS=()
for a in "$@"; do
    case "$a" in
        --install)   INSTALL=1 ;;
        c1|i16|i256) TARGETS+=("$a") ;;
        *) echo "error: unknown argument '$a' (use --install and/or plugin names: c1 i16 i256)" >&2; exit 2 ;;
    esac
done
[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=(c1 i16 i256)

# --- preflight -------------------------------------------------------------
for tool in "$CXX" "$CC"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not found. Install with:  brew install mingw-w64" >&2; exit 1; }
done

build_one() {
    local name="$1"
    local dir binary shim_cpp fmt_cpp uses_lzsa
    read -r dir binary shim_cpp fmt_cpp uses_lzsa <<<"$(plugin_cfg "$name")"
    local SD="$HERE/$dir"
    local OUT="$SD/build"
    local STAGE="$OUT/obj"
    local DLL="$OUT/$binary.dll"

    echo "==================== $name ===================="
    rm -rf "$STAGE"; mkdir -p "$STAGE"

    # Stage the plugin's own C++ sources + headers flat. The export-shim header
    # (<name>ImageIo.h) includes "..\pluginInterface.h" -- a Windows backslash
    # path to the shared interface header one dir up. Copy that shared header in
    # flat and rewrite the backslash include so it resolves under a Unix
    # toolchain. (Harmless if a plugin already ships a local copy.)
    cp "$SD"/*.cpp "$SD"/*.h "$STAGE"/
    cp "$HERE"/pluginInterface.h "$STAGE"/ 2>/dev/null || true
    perl -0pi -e 's/#include\s+"\.\.[\\\/]+pluginInterface\.h"/#include "pluginInterface.h"/' "$STAGE/${name}ImageIo.h"

    # i16 ships compat.h, which provides static-inline fopen_s / sscanf_s shims
    # for "non-MSVC compilers" (guarded by #ifndef _MSC_VER). mingw is non-MSVC
    # but DOES target Windows, where the real CRT already declares fopen_s -- so
    # the shim collides. Retarget the guard to _WIN32 (defined by both mingw and
    # MSVC) so any Windows build uses the real CRT and only true non-Windows
    # builds get the shim.
    if [[ -f "$STAGE/compat.h" ]]; then
        perl -pi -e 's/#ifndef _MSC_VER/#ifndef _WIN32/' "$STAGE/compat.h"
    fi

    local DEF=(-DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS "-D${name}ImageIO_EXPORTS")
    # Search staged headers first (fixed shim), then lzsa include roots so the
    # plugin's bare includes ("lib.h", "shrink_inmem.h", ...) resolve.
    local INC=(-I"$STAGE")
    if [[ "$uses_lzsa" == yes ]]; then
        INC+=(-I"$SD/lzsa/src" -I"$SD/lzsa/src/libdivsufsort/include")
    fi

    local OBJS=()

    echo "==> compiling C++ ($shim_cpp, $fmt_cpp)"
    "$CXX" -O2 -std=c++17 -municode "${DEF[@]}" "${INC[@]}" -c "$STAGE/$shim_cpp" -o "$STAGE/${shim_cpp%.cpp}.o"
    "$CXX" -O2 -std=c++17 -municode "${DEF[@]}" "${INC[@]}" -c "$STAGE/$fmt_cpp"  -o "$STAGE/${fmt_cpp%.cpp}.o"
    OBJS+=("$STAGE/${shim_cpp%.cpp}.o" "$STAGE/${fmt_cpp%.cpp}.o")

    if [[ "$uses_lzsa" == yes ]]; then
        # lzsa core: every .c in lzsa/src except the command-line driver lzsa.c
        # (it has main()). Plus libdivsufsort's library .c files (NOT its
        # examples/, which also have main()).
        local lzsa_srcs=()
        local f
        for f in "$SD"/lzsa/src/*.c; do
            [[ "$(basename "$f")" == "lzsa.c" ]] && continue
            lzsa_srcs+=("$f")
        done
        for f in "$SD"/lzsa/src/libdivsufsort/lib/*.c; do
            lzsa_srcs+=("$f")
        done

        echo "==> compiling lzsa (${#lzsa_srcs[@]} C files)"
        for f in "${lzsa_srcs[@]}"; do
            local o="$STAGE/lzsa_$(basename "$f" .c).o"
            "$CC" -O2 "${DEF[@]}" "${INC[@]}" -c "$f" -o "$o"
            OBJS+=("$o")
        done
    fi

    echo "==> linking $binary.dll"
    "$CXX" -shared -municode -o "$DLL" \
           "${OBJS[@]}" "$SD/pluginInterface.def" \
           -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup

    # Verify every export the .def promises is actually present.
    local REQ HAVE miss=0 r
    REQ=$(grep -vE 'LIBRARY|EXPORTS' "$SD/pluginInterface.def" | tr -d ' \t\r' | grep -vE '^$')
    HAVE=$("$OBJDUMP" -p "$DLL" | awk '/\[ *[0-9]+\]/{print $NF}')
    for r in $REQ; do echo "$HAVE" | grep -qx "$r" || { echo "  MISSING export: $r"; miss=$((miss+1)); }; done
    [[ "$miss" -ne 0 ]] && { echo "error: $miss required export(s) missing in $binary.dll" >&2; return 1; }

    echo "==> built $DLL"
    file "$DLL"
    echo "    $(echo "$REQ" | wc -w | tr -d ' ') required exports present"

    if [[ "$INSTALL" -eq 1 ]]; then
        [[ -d "$PMNG_PLUGINS" ]] || { echo "error: plugins folder not found: $PMNG_PLUGINS" >&2; echo "       set PMNG_PLUGINS to your Pro Motion NG 'plugins' directory." >&2; return 1; }
        cp "$DLL" "$PMNG_PLUGINS/$binary.dll"
        echo "==> installed to $PMNG_PLUGINS/$binary.dll"
    fi
}

for t in "${TARGETS[@]}"; do build_one "$t"; done

if [[ "$INSTALL" -eq 1 ]]; then
    echo
    echo "All done. Restart Pro Motion NG to pick up the plugin(s)."
fi
