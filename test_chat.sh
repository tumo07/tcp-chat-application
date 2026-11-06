#!/usr/bin/env bash
set -e

echo "=========================================================="
echo "      🚀 COMPILING & TESTING TCP CHAT APPLICATION"
echo "=========================================================="

# Build server and client
make clean
make

# Run python verification suite
python3 verify_chat.py

echo ""
echo "=========================================================="
echo "      ✅ ALL TESTS COMPLETED AND VERIFIED!"
echo "=========================================================="
