#!/bin/bash
# Rebuild the library + harness and re-run the whole verification.
# (cmake is not available in this environment, so the library is compiled
#  directly with gcc/g++ — see ../build/build_lib.sh. The source changes
#  themselves are plain edits to existing files and also build with the
#  normal cmake flow unchanged.)
set -e
cd "$(dirname "$0")"

echo "== building libfluidscore.a =="
/workspace/build/build_lib.sh

INC="-DOSAL_cpp11=1 -I/workspace/build -I/workspace/build/include -I/workspace/include \
     -I/workspace/src -I/workspace/src/synth -I/workspace/src/rvoice \
     -I/workspace/src/utils -I/workspace/src/sfloader -I/workspace/src/bindings"
LIB=/workspace/build/libfluidscore.a

echo "== building space_test =="
gcc -O2 $INC space_test.c -o space_test $LIB -lm -lpthread -lstdc++

echo "== running verification =="
./space_test

echo
echo "Open work/index.html in a browser to see the waveforms."
