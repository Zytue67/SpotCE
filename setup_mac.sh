#!/bin/bash
set -e
cd "$(dirname "$0")"

python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r bridge/requirements.txt

echo
echo "SpotCE Mac setup complete."
echo "Terminal bridge: source .venv/bin/activate && python3 bridge/spotce_bridge.py"
echo "Menu-bar app (source): source .venv/bin/activate && python3 bridge/spotce_menubar.py"
echo "Build DMG: ./build_mac_app.sh"
