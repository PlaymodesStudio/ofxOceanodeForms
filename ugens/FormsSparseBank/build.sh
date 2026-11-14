#!/bin/bash
# Builds FormsSparseBank.scx (Forms Box's oscillator bank), universal (arm64 + x86_64).
#   ugens/FormsSparseBank/build.sh [path to the SuperCollider source]
# Install: copy FormsSparseBank.scx and FormsSparseBank.sc into a folder of the app's
# data/Supercollider/Extensions (the server loads the .scx; the .sc is for compiling formsBox.scd).
set -e
cd "$(dirname "$0")"
SC="${1:-$HOME/Documents/SC/supercollider}"
clang++ -std=c++17 -O3 -bundle -undefined dynamic_lookup \
    -arch arm64 -arch x86_64 -mmacosx-version-min=10.15 \
    -I"$SC/include/plugin_interface" -I"$SC/include/common" -I"$SC/common" \
    FormsSparseBank.cpp -o FormsSparseBank.scx
# ad-hoc signature: macOS kills scsynth loading an unsigned / stale plugin (exit 137)
codesign -f -s - FormsSparseBank.scx
echo "built $(pwd)/FormsSparseBank.scx"
