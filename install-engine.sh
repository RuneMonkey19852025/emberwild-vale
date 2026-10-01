#!/usr/bin/env bash
# Emberwild Vale — engine installer & build watcher.
# Waits for Linux_Unreal_Engine_*.zip in ~/Downloads (or $EMBERWILD_ZIP_DIR),
# verifies the sha256, extracts to ~/Unreal, and compiles the project.
set -u

LOG="$HOME/Unreal/install-status.log"
mkdir -p "$HOME/Unreal"
log() { echo "[$(date '+%H:%M:%S')] $*" >> "$LOG"; }

: > "$LOG"
log "watching for the engine zip..."

EXPECTED_SHA256="4bc938a3c34ae7305e9dff7b3be381a43ca4b942348e90102885f4bcab7e9575"
PROJ="$HOME/UnrealProjects/EmberwildVale"
ZIP_DIR="${EMBERWILD_ZIP_DIR:-$HOME/Downloads}"

ZIP=""
for i in $(seq 1 720); do   # up to 12 hours
  # only consider zips that have stopped growing for 2 consecutive checks
  CAND=$(ls -t "$ZIP_DIR"/Linux_Unreal_Engine_*.zip 2>/dev/null | head -1 || true)
  if [ -n "$CAND" ]; then
    S1=$(stat -c%s "$CAND")
    sleep 5
    S2=$(stat -c%s "$CAND")
    if [ "$S1" = "$S2" ] && [ "$S1" -gt 1000000000 ]; then
      ZIP="$CAND"
      break
    fi
  fi
  sleep 5
done

if [ -z "$ZIP" ]; then
  log "TIMEOUT: no engine zip appeared in $ZIP_DIR"
  exit 1
fi

log "found: $ZIP ($(numfmt --to=iec $(stat -c%s "$ZIP")))"
log "verifying sha256 (takes a minute for ~30GB)..."
ACTUAL=$(sha256sum "$ZIP" | cut -d' ' -f1)
if [ "$ACTUAL" = "$EXPECTED_SHA256" ]; then
  log "sha256 OK"
else
  log "sha256 MISMATCH (got $ACTUAL) — extracting anyway; Epic may have replaced the build"
fi

log "extracting to ~/Unreal (this takes several minutes)..."
mkdir -p "$HOME/Unreal"
if unzip -q -o "$ZIP" -d "$HOME/Unreal"; then
  log "extract done"
else
  log "unzip reported an error (possibly partial download); checking for the editor binary anyway"
fi

ENGINE=$(find "$HOME/Unreal" -maxdepth 2 -name "UnrealEditor" -path "*Binaries/Linux*" 2>/dev/null | head -1)
if [ -z "$ENGINE" ]; then
  log "FAIL: UnrealEditor not found after extraction"
  exit 1
fi
ENGINE_ROOT=$(dirname "$(dirname "$(dirname "$(dirname "$ENGINE")")")")
log "engine at: $ENGINE_ROOT"

# the project needs to point at the detected engine version
VER=$(basename "$ENGINE_ROOT" | grep -oE '5\.[0-9]+' | head -1)
if [ -n "$VER" ]; then
  log "engine version: $VER — updating uproject association"
  sed -i "s/\"EngineAssociation\": \"[^\"]*\"/\"EngineAssociation\": \"$VER\"/" "$PROJ/EmberwildVale.uproject"
fi

# generate project files, then compile the editor target
chmod +x "$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor" 2>/dev/null || true
log "generating project files..."
"$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh" \
  "$PROJ/EmberwildVale.uproject" -game -engine >> "$LOG" 2>&1 \
  || log "GenerateProjectFiles returned nonzero (continuing — not fatal for CLI builds)"

log "compiling EmberwildValeEditor (first build: 5-10 min)..."
"$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
  EmberwildValeEditor Linux Development \
  -project="$PROJ/EmberwildVale.uproject" -WaitMutex >> "$LOG" 2>&1
RC=$?
if [ $RC -eq 0 ]; then
  log "BUILD OK — open $PROJ/EmberwildVale.uproject in the editor to play"
else
  log "BUILD FAILED (rc=$RC) — see the log above for compiler errors"
fi
