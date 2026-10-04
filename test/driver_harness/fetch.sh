#!/usr/bin/env bash
# Fetch the pinned meshtastic/firmware source used by the T2 driver harness.
set -euo pipefail

REV=727d8c31dcd2a8e577b672bc5da394a88751bee5
URL=https://github.com/meshtastic/firmware.git
VENDOR="$(cd "$(dirname "$0")" && pwd)/vendor"
DEST="$VENDOR/meshtastic-firmware"
# nanopb version pinned by that Meshtastic revision (platformio.ini)
NANOPB_TAG=0.4.92

if [ ! -f "$VENDOR/nanopb/pb.h" ]; then
    rm -rf "$VENDOR/nanopb"
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$NANOPB_TAG" https://github.com/nanopb/nanopb.git "$VENDOR/nanopb"
fi

if [ -d "$DEST/.git" ] && [ "$(git -C "$DEST" rev-parse HEAD)" = "$REV" ]; then
    exit 0
fi

rm -rf "$DEST"
mkdir -p "$DEST"
git -C "$DEST" init -q
git -C "$DEST" remote add origin "$URL"
git -C "$DEST" fetch -q --depth 1 origin "$REV" || git -C "$DEST" fetch -q origin
git -C "$DEST" checkout -q "$REV"
git -C "$DEST" submodule update -q --init --depth 1 protobufs
