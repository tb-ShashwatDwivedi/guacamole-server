#!/bin/bash

# Comprehensive Test Script for Command Logging v3.0

echo "============================================"
echo "Command Logging v3.0 - Feature Test Script"
echo "============================================"
echo ""
echo "This script will guide you through testing all features."
echo "You need to run commands in an SSH session through Guacamole,"
echo "then this script will verify they were captured correctly."
echo ""

LOG_FILE="/var/log/guacamole/commands/ssh_commands.log"

# Check if logs exist
if [ ! -f "$LOG_FILE" ]; then
    echo "⚠️  No log file found yet."
    echo "Please connect via Guacamole SSH and run some commands first."
    exit 1
fi

echo "📋 Test Checklist:"
echo ""

# Test 1: Tab Completion
echo "1️⃣  TAB COMPLETION TEST"
echo "   In your SSH session, type: cd /v[TAB]ar/l[TAB]og"
echo "   (Let Tab auto-complete the path)"
echo ""
read -p "   Have you done this? (y/n) " answer
if [ "$answer" = "y" ]; then
    if grep -q "cd /var/log" "$LOG_FILE"; then
        echo "   ✅ PASS: Full path 'cd /var/log' found in logs!"
    else
        echo "   ❌ FAIL: Tab completion not fully captured"
    fi
fi
echo ""

# Test 2: Arrow Key History
echo "2️⃣  ARROW KEY HISTORY TEST"
echo "   In your SSH session:"
echo "   - Type: ls -la"
echo "   - Press Enter"
echo "   - Press ↑ (up arrow) to recall"
echo "   - Press Enter again"
echo ""
read -p "   Have you done this? (y/n) " answer
if [ "$answer" = "y" ]; then
    count=$(grep -c "ls -la" "$LOG_FILE")
    if [ "$count" -ge 2 ]; then
        echo "   ✅ PASS: Command logged multiple times ($count times)"
    else
        echo "   ❌ FAIL: History recall not captured"
    fi
fi
echo ""

# Test 3: Copy-Paste
echo "3️⃣  COPY-PASTE TEST"
echo "   Copy this command: echo 'This is a pasted command test'"
echo "   Paste it in your SSH session and press Enter"
echo ""
read -p "   Have you done this? (y/n) " answer
if [ "$answer" = "y" ]; then
    if grep -q "This is a pasted command test" "$LOG_FILE"; then
        echo "   ✅ PASS: Pasted command captured!"
    else
        echo "   ❌ FAIL: Paste not captured"
    fi
fi
echo ""

# Test 4: Password Protection  
echo "4️⃣  PASSWORD PROTECTION TEST"
echo "   In your SSH session, run: sudo ls"
echo "   Enter your password when prompted"
echo ""
read -p "   Have you done this? (y/n) " answer
if [ "$answer" = "y" ]; then
    if grep -q "sudo ls" "$LOG_FILE"; then
        echo "   ✅ Command 'sudo ls' logged"
        # Check if any line after sudo contains only non-printable or short text
        # (passwords are usually short and shouldn't appear)
        if grep -A1 "sudo ls" "$LOG_FILE" | tail -1 | grep -qE "^\w{1,20}$" 2>/dev/null; then
            echo "   ⚠️  WARNING: Possible password in log - check manually"
        else
            echo "   ✅ PASS: Password appears to be protected!"
        fi
    else
        echo "   ❌ FAIL: Command not logged"
    fi
fi
echo ""

# Test 5: Concurrent Sessions
echo "5️⃣  CONCURRENT SESSIONS TEST"
echo "   Open TWO SSH connections through Guacamole"
echo "   In session 1: run 'whoami'"  
echo "   In session 2: run 'hostname'"
echo ""
read -p "   Have you done this? (y/n) " answer
if [ "$answer" = "y" ]; then
    conn_ids=$(awk -F'|' '{print $3}' "$LOG_FILE" | sort -u | wc -l)
    if [ "$conn_ids" -ge 2 ]; then
        echo "   ✅ PASS: Multiple connection IDs found ($conn_ids unique)"
    else
        echo "   ⚠️  Only 1 connection ID - test with 2 simultaneous sessions"
    fi
fi
echo ""

echo "============================================"
echo "📊 Log Analysis"
echo "============================================"
echo ""

total_commands=$(wc -l < "$LOG_FILE")
echo "Total commands logged: $total_commands"
echo ""

echo "Recent commands:"
tail -5 "$LOG_FILE" | while IFS='|' read -r timestamp host conn session user ip cmd; do
    echo "  [$timestamp] $user: $cmd"
done

echo ""
echo "============================================"
echo "For detailed analysis, run:"
echo "  sudo /opt/guacamole-server-1.6.0/analyze_logs.sh summary"
echo "============================================"
