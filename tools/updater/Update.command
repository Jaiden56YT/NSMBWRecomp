#!/bin/bash
# NSMBW Online updater (Mac). Double-click it in Finder. It checks the builds repo's latest release
# and, when it is newer than build.txt, replaces "NSMBW Online.app" and the guide files. The disc
# folder (your game) and the app's UserData (saves, settings, controls) are never touched.
#
# The builds repo is private: you need a GitHub account the owner invited, and a token from it
# (github.com -> Settings -> Developer settings -> Personal access tokens -> Tokens (classic) ->
# Generate new token, tick "repo"). The first run asks for it and keeps it in the app's UserData.
set -u
cd "$(dirname "$0")" || exit 1
HERE="$PWD"
REPO="$(sed -n 's/^repo *= *//p' update.cfg 2>/dev/null | head -1)"
ASSET_PREFIX="NSMBW-Online-8P-Mac-"
USERDATA="$HERE/NSMBW Online.app/Contents/MacOS/UserData"
TOKEN_FILE="$USERDATA/github_token.txt"
CURRENT="$(head -1 build.txt 2>/dev/null)"

fail() { echo; echo "Update failed: $*"; echo; read -r -p "Press Return to close." _; exit 1; }
[ -n "$REPO" ] || fail "update.cfg has no 'repo = owner/name' line."
mkdir -p "$USERDATA"
TOKEN="${NSMBW_UPDATE_TOKEN:-$(cat "$TOKEN_FILE" 2>/dev/null)}"
if [ -z "$TOKEN" ]; then
    echo "First update: paste your GitHub token (it is not shown) and press Return."
    read -r -s TOKEN; echo
    [ -n "$TOKEN" ] || fail "no token given."
    printf '%s' "$TOKEN" > "$TOKEN_FILE"; chmod 600 "$TOKEN_FILE"
fi

echo "Checking for updates (you have: ${CURRENT:-unknown})..."
TMP="$(mktemp -d)" || fail "no temporary folder."
trap 'rm -rf "$TMP"' EXIT
CODE=$(curl -sS -o "$TMP/release.json" -w '%{http_code}' -H "Authorization: Bearer $TOKEN" \
    -H "Accept: application/vnd.github+json" "https://api.github.com/repos/$REPO/releases/latest") || fail "no connection to GitHub."
if [ "$CODE" = 401 ] || [ "$CODE" = 404 ]; then
    rm -f "$TOKEN_FILE"
    fail "GitHub refused the token or you have no access to $REPO (ask to be invited). Run Update again to enter a new token."
fi
[ "$CODE" = 200 ] || fail "GitHub answered $CODE."
TAG=$(plutil -extract tag_name raw -o - "$TMP/release.json") || fail "unreadable release."
if [ "$TAG" = "$CURRENT" ]; then echo "You already have the latest version ($TAG)."; read -r -p "Press Return to close." _; exit 0; fi

# Find this platform's zip among the release's assets.
ASSET_ID=""; NAME=""
N=$(plutil -extract assets raw -o - "$TMP/release.json" 2>/dev/null || echo 0)
for ((i = 0; i < N; i++)); do
    n=$(plutil -extract "assets.$i.name" raw -o - "$TMP/release.json")
    case "$n" in "$ASSET_PREFIX"*.zip) NAME="$n"; ASSET_ID=$(plutil -extract "assets.$i.id" raw -o - "$TMP/release.json");; esac
done
[ -n "$ASSET_ID" ] || fail "release $TAG has no Mac download."

echo "Downloading $NAME..."
curl -sSL --fail -H "Authorization: Bearer $TOKEN" -H "Accept: application/octet-stream" \
    -o "$TMP/new.zip" "https://api.github.com/repos/$REPO/releases/assets/$ASSET_ID" || fail "download failed."
ditto -x -k "$TMP/new.zip" "$TMP/x" || fail "the download is not a valid zip."
NEW="$(find "$TMP/x" -maxdepth 1 -mindepth 1 -type d | head -1)"
[ -d "$NEW/NSMBW Online.app" ] || fail "the download has no NSMBW Online.app."
if pgrep -f "$HERE/NSMBW Online.app/Contents/MacOS/NSMBWCompiled" >/dev/null; then fail "close the game first."; fi

# Keep UserData, replace the rest of the app and the guide files.
ditto "$USERDATA" "$TMP/keep/UserData"
rm -rf "$HERE/NSMBW Online.app"
ditto "$NEW/NSMBW Online.app" "$HERE/NSMBW Online.app"
rm -rf "$HERE/NSMBW Online.app/Contents/MacOS/UserData"
ditto "$TMP/keep/UserData" "$HERE/NSMBW Online.app/Contents/MacOS/UserData"
xattr -dr com.apple.quarantine "$HERE/NSMBW Online.app" 2>/dev/null
# Putting UserData back changes the bundle: sign it again (ad hoc, as the download was).
codesign --force --deep -s - "$HERE/NSMBW Online.app" >/dev/null 2>&1
for f in "START HERE.html" "ABOUT THE MOD.txt" "update.cfg" "build.txt"; do
    [ -e "$NEW/$f" ] && cp "$NEW/$f" "$HERE/$f"
done
echo "Updated to $TAG. Your game files, saves and settings were kept."
# This script is running: its new copy goes in under another name and is renamed over it (bash
# keeps reading the old file), as the very last step.
if [ -e "$NEW/Update.command" ]; then
    cp "$NEW/Update.command" "$HERE/.Update.command.new" && chmod +x "$HERE/.Update.command.new" &&
        mv -f "$HERE/.Update.command.new" "$HERE/Update.command"
fi
read -r -p "Press Return to close." _
exit 0
