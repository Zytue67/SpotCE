#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This build script must be run on macOS."
  exit 1
fi

if [[ ! -d .venv ]]; then
  python3 -m venv .venv
fi
source .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r bridge/requirements.txt pyinstaller

rm -rf build dist "SpotCE.spec"

python3 -m PyInstaller \
  --noconfirm \
  --clean \
  --windowed \
  --onedir \
  --name "SpotCE" \
  --osx-bundle-identifier "com.spotce.app" \
  --hidden-import serial.tools.list_ports \
  --collect-all rumps \
  --paths bridge \
  bridge/spotce_menubar.py

DMG_ROOT="build/dmg-root"
rm -rf "$DMG_ROOT"
mkdir -p "$DMG_ROOT"
cp -R "dist/SpotCE.app" "$DMG_ROOT/"
ln -s /Applications "$DMG_ROOT/Applications"

hdiutil create \
  -volname "SpotCE" \
  -srcfolder "$DMG_ROOT" \
  -ov \
  -format UDZO \
  "dist/SpotCE-macOS.dmg"

echo
echo "Built:"
echo "  dist/SpotCE.app"
echo "  dist/SpotCE-macOS.dmg"
echo
echo "Install SpotCE.app in Applications, open it, then choose Start Bridge from the SpotCE menu-bar item."
