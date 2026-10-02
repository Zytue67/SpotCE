#!/bin/bash
set -e
cd "$(dirname "$0")/calculator"
make clean
make

echo
echo "Built calculator program: calculator/bin/SPOTCE.8xp"
