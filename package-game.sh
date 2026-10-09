#!/usr/bin/env bash
# Emberwild Vale — package the UE5 game for Linux (staged + pak'd, self-contained).
# Output: Dist/Linux/EmberwildVale/ — run Dist/Linux/EmberwildVale/EmberwildVale.sh
set -eu

PROJ="$HOME/UnrealProjects/EmberwildVale"
UAT="$HOME/Unreal/Engine/Build/BatchFiles/RunUAT.sh"
LOG="/tmp/emberwild-package.log"

cd "$PROJ"
echo "packaging to $PROJ/Dist/Linux (log: $LOG)"
"$UAT" BuildCookRun -project="$PROJ/EmberwildVale.uproject" \
  -noP4 -platform=Linux -clientconfig=Development -target=EmberwildVale \
  -build -cook -stage -pak -compressed -archive \
  -archivedirectory="$PROJ/Dist" \
  -unattended -utf8output > "$LOG" 2>&1

if grep -qE 'AutomationTool exiting with ExitsCode\(0\)|BUILD SUCCESSFUL' "$LOG"; then
  echo "PACKAGE OK — $(du -sh "$PROJ/Dist/Linux" | cut -f1) at $PROJ/Dist/Linux"
else
  echo "PACKAGE FAILED — see $LOG"
  grep -E 'Error:|Fatal' "$LOG" | tail -10 || true
  exit 1
fi
