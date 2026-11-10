#!/bin/sh
# Regenerate components/hmi/src/ui from the EEZ Studio project without opening the GUI.
#   tools/eez-build.sh [path/to/EEZ-Studio.AppImage]
# Same result as "Build" in EEZ Studio. Needs a desktop session (X11/Wayland) for Electron.
set -e
APPIMAGE="${1:-${EEZ_STUDIO:-$HOME/tools/EEZ-Studio-0.29.0.AppImage}}"
PROJECT="$(cd "$(dirname "$0")/.." && pwd)/components/hmi/OBD_Dash.eez-project"

# VS Code terminals export ELECTRON_RUN_AS_NODE, which turns Electron into plain node.
# APPIMAGE_EXTRACT_AND_RUN: works without FUSE (extracts to /tmp, a few seconds slower).
env -u ELECTRON_RUN_AS_NODE -u ELECTRON_NO_ATTACH_CONSOLE APPIMAGE_EXTRACT_AND_RUN=1 \
    "$APPIMAGE" --no-sandbox --disable-gpu --ozone-platform=x11 --build-project "$PROJECT" 2>&1 \
    | grep -v -e '^\s*$' -e '^/tmp/appimage_extracted' -e 'dbus' -e 'gtk_ui' -e 'vaapi' -e 'libva'
