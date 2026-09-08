#!/bin/bash
# Verify that a production TB-PAM / Guacamole deployment has everything
# required for per-asset command ACL blocking to work.
set -euo pipefail

SSH_PLUGIN="${SSH_PLUGIN:-/usr/local/lib/libguac-client-ssh.so}"
JDBC_JAR="${JDBC_JAR:-/etc/guacamole/extensions/guacamole-auth-jdbc-postgresql-1.6.0.jar}"
ACL_CONFIG="${ACL_CONFIG:-/etc/guacamole/command-acl.conf}"

fail=0

check() {
    if "$@"; then
        echo "  OK: $*"
    else
        echo "  FAIL: $*"
        fail=1
    fi
}

echo "=== Command ACL production readiness check ==="
echo

echo "[1] SSH plugin ($SSH_PLUGIN)"
if [ ! -f "$SSH_PLUGIN" ]; then
    echo "  FAIL: SSH plugin not found"
    fail=1
else
    grep -Fq "asset-id" <(strings "$SSH_PLUGIN" 2>/dev/null) \
        && echo "  OK: plugin contains asset-id support" \
        || { echo "  FAIL: plugin missing asset-id — rebuild guacamole-server and redeploy libguac-client-ssh.so"; fail=1; }
    grep -Fq "expected_key" <(strings "$SSH_PLUGIN" 2>/dev/null) \
        && echo "  OK: plugin contains ACL lookup logging" \
        || { echo "  FAIL: plugin missing ACL code — rebuild from current guacamole-server source"; fail=1; }
fi
echo

echo "[2] JDBC extension ($JDBC_JAR)"
if [ ! -f "$JDBC_JAR" ]; then
    echo "  FAIL: JDBC JAR not found"
    fail=1
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    (cd "$tmp" && jar xf "$JDBC_JAR" guacamole-auth-jdbc-base-1.6.0.jar 2>/dev/null) || true
    base="$tmp/guacamole-auth-jdbc-base-1.6.0.jar"
    if [ -f "$base" ]; then
        grep -Fq "asset-id" <(unzip -p "$base" \
            org/apache/guacamole/auth/jdbc/tunnel/AbstractGuacamoleTunnelService.class \
            2>/dev/null | strings) \
            && echo "  OK: JDBC extension passes asset-id to guacd" \
            || { echo "  FAIL: JDBC JAR missing asset-id — rebuild guacamole-client and redeploy extension"; fail=1; }
    else
        grep -Fq "asset-id" <(strings "$JDBC_JAR" 2>/dev/null) \
            && echo "  OK: JDBC extension passes asset-id to guacd" \
            || { echo "  FAIL: JDBC JAR missing asset-id"; fail=1; }
    fi
fi
echo

echo "[3] ACL config ($ACL_CONFIG)"
if [ ! -f "$ACL_CONFIG" ]; then
    echo "  WARN: config file missing (blocking disabled until created)"
else
    check test -r "$ACL_CONFIG"
    echo "  Sections found:"
    grep -E '^\[(connection|asset|user|global):' "$ACL_CONFIG" 2>/dev/null | head -10 || true
    if grep -q '^\[asset:' "$ACL_CONFIG" 2>/dev/null; then
        echo "  OK: [asset:ID] sections present (matched after server fix)"
    fi
    if grep -qE '^\[connection:[^:]+:[^:]+:[^]]+\]' "$ACL_CONFIG" 2>/dev/null; then
        echo "  OK: 4-part [connection:host:user:asset_id] sections present"
    fi
fi
echo

echo "[4] guacd service"
if systemctl is-active --quiet guacd 2>/dev/null; then
    echo "  OK: guacd is running"
    echo "  Recent ACL log lines:"
    journalctl -u guacd --no-pager -n 50 2>/dev/null \
        | grep -E "Command ACL|expected_key|asset_id" | tail -5 || echo "  (none — connect to an SSH asset and retry)"
else
    echo "  WARN: guacd not running or not managed by systemd"
fi
echo

if [ "$fail" -eq 0 ]; then
    echo "=== All critical checks passed ==="
    exit 0
else
    echo "=== Some checks FAILED — see above ==="
    echo
    echo "Production build must deploy BOTH:"
    echo "  1. guacamole-server  -> libguac-client-ssh.so (make && make install)"
    echo "  2. guacamole-client  -> guacamole-auth-jdbc-postgresql-*.jar"
    echo "Then restart: systemctl restart guacd tomcat"
    exit 1
fi
