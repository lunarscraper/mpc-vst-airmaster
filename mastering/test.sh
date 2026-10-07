#!/usr/bin/env bash
# Offline x86 test (ASan/UBSan) of mastering_vst.cpp, then a CPU benchmark (-O2): run after build.sh
# (needs build/params.h, build/popup.h). Prints PASSED/FAILED; exit code follows.
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/b -w /b gcc:12 bash -euc '
  mkdir -p build/x86
  SAN="-fsanitize=address,undefined"
  g++ -O0 -g $SAN -std=c++17 -w -fPIC -shared -Ibuild -I. -o build/x86/mastering-x86.so mastering_vst.cpp Mastering.cpp MasteringProc.cpp -lpthread
  g++ -O0 -g $SAN -std=c++17 -DPRESET_ENV=MASTERING_PRESETS -DEXPECT_PARAMS=6 -o build/x86/host_test host_test.cpp -ldl
  ASAN_OPTIONS=detect_leaks=0 ./build/x86/host_test ./build/x86/mastering-x86.so
  g++ -O2 -std=c++17 -w -fPIC -shared -fvisibility=hidden -Ibuild -I. -o build/x86/mastering-o2.so mastering_vst.cpp Mastering.cpp MasteringProc.cpp -lpthread
  g++ -O2 -std=c++17 -DPRESET_ENV=MASTERING_PRESETS -DEXPECT_PARAMS=6 -o build/x86/bench host_test.cpp -ldl
  ./build/x86/bench ./build/x86/mastering-o2.so bench
'
