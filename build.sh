#!/usr/bin/env bash
# IRONCORE : Deep Forge - build.
#   ./build.sh          compile le moteur C en WebAssembly -> web/game.js + web/game.wasm (emcc requis)
#   ./build.sh test     compile et lance les tests natifs C (gcc requis, avec ASAN/UBSAN)
#   ./build.sh serve    lance un serveur local sur http://localhost:8080 (python3 requis)
set -euo pipefail
cd "$(dirname "$0")"

case "${1:-wasm}" in
  wasm)
    command -v emcc >/dev/null || { echo "emcc introuvable. Voir README.md (Codespaces: ./setup-emsdk.sh)"; exit 1; }
    emcc src/map.c src/path.c src/sim.c src/combat.c src/api.c \
      -std=c11 -O3 -Wall -Wextra \
      -sMODULARIZE=1 -sEXPORT_NAME=IroncoreModule -sENVIRONMENT=web \
      -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216 -sSTACK_SIZE=1048576 \
      -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32 \
      -sFILESYSTEM=0 -sASSERTIONS=0 \
      -o web/game.js
    ls -la web/game.js web/game.wasm
    echo "OK -> lancez ./build.sh serve"
    ;;
  test)
    gcc -std=c11 -g -O1 -fsanitize=address,undefined -Wall -Wextra src/*.c tests/test_core.c -lm -o /tmp/ironcore_test
    /tmp/ironcore_test
    ;;
  serve)
    echo "http://localhost:8080  (Ctrl+C pour arreter)"
    cd web && python3 -m http.server 8080
    ;;
  *) echo "usage: $0 [wasm|test|serve]"; exit 1 ;;
esac
