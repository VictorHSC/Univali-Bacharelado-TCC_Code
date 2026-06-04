#!/usr/bin/env bash
# Compila o solver CSPP. Funciona a partir de qualquer caminho.
# Uso: ./build.sh        (gera build/cspp)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
clang++ -std=c++17 -O3 -march=native -pthread -I src -o build/cspp $(find src -name '*.cpp')
echo "OK -> build/cspp"