#!/usr/bin/env sh
set -eu

if ! command -v makensis >/dev/null 2>&1; then
  echo "makensis was not found. Install NSIS first." >&2
  exit 1
fi

./scripts/build-mingw.sh
mkdir -p dist
makensis installer/PulseDial.nsi

echo "Release installer: dist/PulseDialSetup-x64.exe"
