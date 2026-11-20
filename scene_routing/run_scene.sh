#!/usr/bin/env bash
#
# Build and run the four-space (cave / wooden hall / yard / well head)
# scene routing scenario against the modified FluidSynth, then refresh
# the data behind the local HTML report.
#
# Usage:  ./run_scene.sh [path-to-fluidsynth-build-dir]
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${1:-$ROOT/build3}"
SR="$ROOT/sf2/VintageDreamsWaves-v2.sf2"

if [[ ! -f "$BUILD/src/libfluidsynth.so" ]]; then
    echo "error: no libfluidsynth.so in $BUILD/src" >&2
    echo "build FluidSynth first (see README.md)" >&2
    exit 1
fi

CC="${CC:-gcc}"
PKG="$(pkg-config --cflags glib-2.0 2>/dev/null || true)"

"$CC" -I"$BUILD/include" -I"$ROOT/include" $PKG -O2 -Wall -Wextra \
      "$HERE/scene_render.c" -o "$HERE/scene_render" \
      -L"$BUILD/src" -lfluidsynth -lglib-2.0 -lm \
      -Wl,-rpath,"$BUILD/src"

# The harness looks for the soundfont at /workspace/sf2 and at a
# relative sf2/ path; run from the repo root so the latter resolves.
cd "$ROOT"
"$HERE/scene_render"

# keep the report data next to the HTML page
mv -f "$ROOT/scene_report_data.json" "$ROOT/scene_report_data.js" "$HERE/" 2>/dev/null || true

echo
echo "report: open $HERE/scene_report.html in a browser (no server needed)"
