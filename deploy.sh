#!/usr/bin/env bash
# Copy the built plugin to the MPC and register it in MPC.settings if it isn't yet.
#   ./deploy.sh [--yes] [ssh-host]   (default host: mpcx, see ~/.ssh/config)
# Copying needs no restart: remove the plugin from its track and insert it again to load a new .so.
# Registering (first install only) stops and restarts the MPC app, so it asks first.
set -euo pipefail
cd "$(dirname "$0")"
YES=0
[ "${1:-}" = --yes ] && { YES=1; shift; }
HOST="${1:-mpcx}"
NAME="Padbangers - VST - Overcast"
SO=overcast.so
DEST="/sdcard/Synths/$NAME"
SETTINGS=/media/az01-internal/Settings/MPC/MPC.settings

[ -f "build/$SO" ] || { echo "build/$SO missing: run ./build.sh" >&2; exit 1; }

# 1. files: skin folder + .so, staged next to the live copy and swapped in with mv
tar -C build/skin -cf - "$NAME" | ssh "$HOST" "rm -rf '$DEST.new' && mkdir -p /tmp/overcast && tar -C /tmp/overcast -xf - && mv '/tmp/overcast/$NAME' '$DEST.new'"
scp -q "build/$SO" "$HOST:/tmp/overcast/$SO"
# An unchanged .so keeps its file (same inode), so a skin-only update doesn't look like a stale build below.
ssh "$HOST" "if [ -f '$DEST/$SO' ] && [ \"\$(md5sum < '$DEST/$SO')\" = \"\$(md5sum < '/tmp/overcast/$SO')\" ]; then mv '$DEST/$SO' '$DEST.new/$SO'; else cp '/tmp/overcast/$SO' '$DEST.new/$SO'; fi && rm -rf '$DEST.old' && { [ -d '$DEST' ] && mv '$DEST' '$DEST.old' || true; } && mv '$DEST.new' '$DEST' && rm -rf '$DEST.old' /tmp/overcast"
LOCAL_MD5=$(md5 -q "build/$SO" 2>/dev/null || md5sum "build/$SO" | cut -d' ' -f1)
REMOTE_MD5=$(ssh "$HOST" "md5sum '$DEST/$SO'" | cut -d' ' -f1)
[ "$LOCAL_MD5" = "$REMOTE_MD5" ] || { echo "md5 mismatch after copy" >&2; exit 1; }
echo "copied to $DEST ($LOCAL_MD5)"

# 2. plugin list entry (once)
if ssh "$HOST" "grep -q 'file=\"$DEST/$SO\"' '$SETTINGS'"; then
  # MPC keeps a plugin's .so loaded while any instance exists (undo history included), so re-inserting may not
  # load the new build. Compare the file MPC has mapped with the one just copied.
  if ssh "$HOST" "P=\$(pidof MPC) && grep -q '$SO' /proc/\$P/maps && ! grep '$SO' /proc/\$P/maps | grep -q \$(stat -c %i '$DEST/$SO')"; then
    echo "MPC still runs an older $SO: save the project and restart the MPC app (systemctl restart acvs) to load this build"
  else
    echo "already registered: remove the plugin from its track and insert it again to load this build"
  fi
  exit 0
fi
if [ $YES = 0 ]; then
  ok=
  read -r -p "Register the plugin? This stops and restarts the MPC app (save your project first) [y/N] " ok || true
  [ "$ok" = y ] || [ "$ok" = Y ] || { echo "not registered (run again, or with --yes)"; exit 0; }
fi
ENTRY=$(cat build/pluginlist-entry.xml)
ssh "$HOST" "sh -s" <<EOF
set -e
systemctl stop acvs
trap 'systemctl start acvs' EXIT
i=0; while pidof MPC >/dev/null && [ \$i -lt 30 ]; do sleep 1; i=\$((i + 1)); done
cp '$SETTINGS' '$SETTINGS.bak-overcast-'\$(date +%Y%m%d-%H%M%S)
cat > /tmp/overcast-entry.xml <<'XML'
$ENTRY
XML
# same edit as the catalog installers: add the entry to <VALUE name="pluginList-arm"><KNOWNPLUGINS>
awk -v entryfile=/tmp/overcast-entry.xml '
  BEGIN { while ((getline l < entryfile) > 0) entry = entry l }
  /<VALUE name="pluginList-arm">/ { inlist = 1 }
  inlist && /<\/KNOWNPLUGINS>/ && !done { ind = \$0; sub(/<.*/, "", ind); print ind "  " entry; done = 1; inlist = 0 }
  /<\/PROPERTIES>/ && !done { print "  <VALUE name=\"pluginList-arm\">"; print "    <KNOWNPLUGINS>"; print "      " entry; print "    </KNOWNPLUGINS>"; print "  </VALUE>"; done = 1 }
  { print }' '$SETTINGS' > '$SETTINGS.new'
[ "\$(grep -c 'file="$DEST/$SO"' '$SETTINGS.new')" = 1 ] || { rm -f '$SETTINGS.new'; echo "settings edit failed" >&2; exit 1; }
mv '$SETTINGS.new' '$SETTINGS'
sync
rm -f /tmp/overcast-entry.xml
echo "registered; starting MPC"
EOF
