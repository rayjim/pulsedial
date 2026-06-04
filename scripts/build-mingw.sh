#!/usr/bin/env sh
set -eu

mkdir -p build

x86_64-w64-mingw32-g++ \
  -std=c++20 \
  -O2 \
  -municode \
  -mwindows \
  -static-libstdc++ \
  -static-libgcc \
  -DWIN32_LEAN_AND_MEAN \
  -DNOMINMAX \
  -DUNICODE \
  -D_UNICODE \
  src/main.cpp \
  -o build/PulseDial.exe \
  -ld2d1 \
  -ldwrite \
  -ldwmapi \
  -lshell32 \
  -lole32 \
  -lgdi32 \
  -luser32
