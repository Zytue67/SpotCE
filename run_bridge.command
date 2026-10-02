#!/bin/bash
cd "$(dirname "$0")"
if [ ! -f .venv/bin/activate ]; then
  echo "SpotCE has not been set up yet."
  echo "Run ./setup_mac.sh in Terminal first."
  echo
  read -n 1 -s -r -p "Press any key to close..."
  exit 1
fi
source .venv/bin/activate
python3 bridge/spotce_bridge.py
status=$?
echo
read -n 1 -s -r -p "Press any key to close..."
exit $status
