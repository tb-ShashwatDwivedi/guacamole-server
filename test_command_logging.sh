#!/bin/bash

# Test Script for Enhanced Command Logging
# This script helps verify the command logging improvements

echo "====================================="
echo "Command Logging Test Script"
echo "====================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Please run as root (sudo)${NC}"
    exit 1
fi

echo "1. Checking log directory..."
if [ -d "/var/log/guacamole/commands" ]; then
    echo -e "${GREEN}✓ Log directory exists${NC}"
    ls -lh /var/log/guacamole/commands/
else
    echo -e "${RED}✗ Log directory not found${NC}"
    echo "Creating log directory..."
    mkdir -p /var/log/guacamole/commands
    chmod 755 /var/log/guacamole/commands
fi

echo ""
echo "2. Checking ACL configuration..."
if [ -f "/etc/guacamole/command-acl.conf" ]; then
    echo -e "${GREEN}✓ ACL configuration exists${NC}"
    echo "Current configuration:"
    head -20 /etc/guacamole/command-acl.conf
else
    echo -e "${YELLOW}⚠ ACL configuration not found${NC}"
    echo "You can copy the example: /opt/guacamole-server-1.6.0/command-acl.conf.example"
fi

echo ""
echo "3. Checking guacd service..."
if systemctl is-active --quiet guacd; then
    echo -e "${GREEN}✓ guacd is running${NC}"
    systemctl status guacd --no-pager | head -10
else
    echo -e "${RED}✗ guacd is not running${NC}"
    echo "Starting guacd..."
    systemctl start guacd
fi

echo ""
echo "4. Checking SSH protocol library..."
if [ -f "/usr/local/lib/libguac-client-ssh.so" ]; then
    echo -e "${GREEN}✓ SSH protocol library installed${NC}"
    ls -lh /usr/local/lib/libguac-client-ssh.so*
else
    echo -e "${RED}✗ SSH protocol library not found${NC}"
    echo "Run: cd /opt/guacamole-server-1.6.0/src/protocols/ssh && sudo make install"
fi

echo ""
echo "5. Recent log entries..."
echo ""
echo "=== Main Command Log (last 10 entries) ==="
if [ -f "/var/log/guacamole/commands/ssh_commands.log" ]; then
    tail -10 /var/log/guacamole/commands/ssh_commands.log
else
    echo "No entries yet"
fi

echo ""
echo "=== Dangerous Commands Log ==="
if [ -f "/var/log/guacamole/commands/dangerous_commands.log" ]; then
    if [ -s "/var/log/guacamole/commands/dangerous_commands.log" ]; then
        tail -10 /var/log/guacamole/commands/dangerous_commands.log
    else
        echo "No dangerous commands logged yet"
    fi
else
    echo "Log file not created yet"
fi

echo ""
echo "=== Restricted Commands Log ==="
if [ -f "/var/log/guacamole/commands/restricted_commands.log" ]; then
    if [ -s "/var/log/guacamole/commands/restricted_commands.log" ]; then
        tail -10 /var/log/guacamole/commands/restricted_commands.log
    else
        echo "No restricted commands logged yet"
    fi
else
    echo "Log file not created yet"
fi

echo ""
echo "6. Checking for unique connection IDs..."
if [ -f "/var/log/guacamole/commands/ssh_commands.log" ]; then
    echo "Unique Connection IDs found:"
    grep "CONN_ID=" /var/log/guacamole/commands/ssh_commands.log | \
        sed 's/.*CONN_ID=\([^ ]*\).*/\1/' | sort -u | head -10
    
    echo ""
    echo "Session count:"
    grep "=== SESSION START ===" /var/log/guacamole/commands/ssh_commands.log | wc -l
else
    echo "No log file yet - connect via SSH through Guacamole to generate logs"
fi

echo ""
echo "7. Checking for IP addresses (should not be 0.0.0.0)..."
if [ -f "/var/log/guacamole/commands/ssh_commands.log" ]; then
    echo "IP addresses found:"
    grep "IP=" /var/log/guacamole/commands/ssh_commands.log | \
        sed 's/.*IP=\([^ ]*\).*/\1/' | sort -u
else
    echo "No log file yet"
fi

echo ""
echo "====================================="
echo "Test Complete"
echo "====================================="
echo ""
echo "To test the functionality:"
echo "1. Connect to an SSH server through Guacamole"
echo "2. Execute some commands"
echo "3. Run this script again to see the logged commands"
echo "4. Try executing a dangerous command (e.g., 'sudo shutdown')"
echo "5. Check if ACL rules are being enforced"
echo ""
echo "Log files location: /var/log/guacamole/commands/"
echo "Documentation: /opt/guacamole-server-1.6.0/COMMAND_LOGGING_IMPROVEMENTS.md"
