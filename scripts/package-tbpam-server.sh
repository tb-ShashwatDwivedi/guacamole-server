#!/bin/bash
# Build guacamole-server and stage binaries for TB-PAM tbpam-core/server/ layout.
#
# Usage:
#   ./scripts/package-tbpam-server.sh [DEST_DIR]
#
# Example:
#   ./scripts/package-tbpam-server.sh /opt/tbPAM-UBUNTU-2.0.9-P1-2026-06-26/tbpam-core/server
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/dist/tbpam-server}"
STAGE="$DEST/usr/local/lib"
BINDIR="$DEST/usr/local/sbin"

echo "Building guacamole-server in $ROOT ..."
cd "$ROOT"
make -j"$(nproc)" 2>&1
make install DESTDIR="$DEST/staging" 2>&1

echo "Staging plugins to TB-PAM layout ($STAGE) ..."
mkdir -p "$STAGE" "$BINDIR"

# TB-PAM install extracts usr/ to / — plugins live in /usr/local/lib/
for plugin in libguac-client-ssh libguac-client-rdp libguac-client-vnc \
              libguac-client-dbshell libguac-client-telnet libguac-client-kubernetes \
              libguac libguac-terminal; do
    for f in "$DEST/staging/usr/local/lib/${plugin}.so"*; do
        [ -e "$f" ] || continue
        cp -a "$f" "$STAGE/"
    done
done

if [ -f "$DEST/staging/usr/local/sbin/guacd" ]; then
    cp -a "$DEST/staging/usr/local/sbin/guacd" "$BINDIR/"
fi

rm -rf "$DEST/staging"

echo "Verifying staged SSH plugin ..."
grep -Fq "asset-id" <(strings "$STAGE/libguac-client-ssh.so" 2>/dev/null) \
    || { echo "ERROR: staged SSH plugin missing asset-id"; exit 1; }
grep -Fq "expected_key" <(strings "$STAGE/libguac-client-ssh.so" 2>/dev/null) \
    || { echo "ERROR: staged SSH plugin missing ACL code"; exit 1; }

echo "Done. Copy $DEST/usr into your TB-PAM package and rebuild the installer."
echo "Also rebuild and deploy guacamole-auth-jdbc-postgresql JAR from guacamole-client."
