#!/bin/sh
# Builds and runs every unit test of the portable core (needs only g++).
set -e
cd "$(dirname "$0")/.."
CORE="src/core/Gen.cpp src/core/Audio.cpp src/core/Sim.cpp src/core/Patterns.cpp src/core/Decor.cpp src/core/LevelString.cpp src/core/Studio.cpp"
OUT=${TMPDIR:-/tmp}
for t in audio sim gen studio pack; do
  g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/ais_test_$t" tests/test_$t.cpp $CORE
done
"$OUT/ais_test_audio" | tail -1
"$OUT/ais_test_sim" | tail -1
"$OUT/ais_test_gen" | tail -1
"$OUT/ais_test_studio" 3 | grep -E "^result|^fallback|^cancel"
"$OUT/ais_test_pack" | head -1
python3 tests/check_pack.py
echo "ALL CORE TESTS FINISHED"
