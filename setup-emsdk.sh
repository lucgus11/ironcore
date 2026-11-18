#!/usr/bin/env bash
# Installe Emscripten dans ~/emsdk (une seule fois, ~2 min). Utile dans GitHub Codespaces.
set -euo pipefail
if [ ! -d "$HOME/emsdk" ]; then git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"; fi
cd "$HOME/emsdk" && ./emsdk install latest && ./emsdk activate latest
echo
echo "Fait. Dans CHAQUE nouveau terminal, executez d'abord :  source ~/emsdk/emsdk_env.sh"
