# Quick Start Guide - Enhanced Command Logging

## What Was Fixed

### 1. Unique Connection IDs ✅
- **Before**: Connection ID and IP showed as `0.0.0.0` for all sessions
- **After**: Each session gets a unique connection ID from Guacamole's `user_id`
- **Format**: `CONN_ID=<unique-user-id>` in every log entry

### 2. Separate Log Files for Restricted Commands ✅
- **Main Log**: `/var/log/guacamole/commands/ssh_commands.log` - All commands
- **Dangerous Log**: `/var/log/guacamole/commands/dangerous_commands.log` - Dangerous patterns (rm -rf, shutdown, etc.)
- **Restricted Log**: `/var/log/guacamole/commands/restricted_commands.log` - ACL-blocked commands

### 3. ACL Integration ✅
- Reads rules from `/etc/guacamole/command-acl.conf`
- Supports user-specific, connection-specific, and global rules
- Logs restricted commands separately

## Testing the Changes

### Step 1: Clear Old Logs (Optional)
```bash
sudo truncate -s 0 /var/log/guacamole/commands/ssh_commands.log
```

### Step 2: Connect via Guacamole
1. Open Guacamole web interface
2. Connect to an SSH server
3. Execute some commands:
   ```bash
   whoami
   ls -la
   pwd
   ```

### Step 3: Verify Logs
```bash
# Check main log - should see CONN_ID and SESSION fields
sudo tail -20 /var/log/guacamole/commands/ssh_commands.log

# Look for SESSION START marker
sudo grep "SESSION START" /var/log/guacamole/commands/ssh_commands.log

# Check unique connection IDs
sudo grep "CONN_ID=" /var/log/guacamole/commands/ssh_commands.log | \
  sed 's/.*CONN_ID=\([^ ]*\).*/\1/' | sort -u
```

### Step 4: Test ACL Rules
1. Try a blacklisted command (configured in `/etc/guacamole/command-acl.conf`):
   ```bash
   sudo apt update  # If 'sudo' is blacklisted
   ```

2. Check restricted commands log:
   ```bash
   sudo cat /var/log/guacamole/commands/restricted_commands.log
   ```

### Step 5: Test Dangerous Command Detection
1. Execute a dangerous command:
   ```bash
   rm -rf /tmp/test_file
   ```

2. Check dangerous commands log:
   ```bash
   sudo cat /var/log/guacamole/commands/dangerous_commands.log
   ```

## Expected Log Format

### Session Start
```
[2026-02-17 17:45:10] === SESSION START === CONN_ID=1234-5678-abcd SESSION=20260217-174510_1234-5678-abcd_root_192.168.1.100 USER=root IP=192.168.1.100 SSH_HOST=192.168.0.19
```

### Command Execution
```
[2026-02-17 17:45:15] CONN_ID=1234-5678-abcd SESSION=20260217-174510_1234-5678-abcd_root_192.168.1.100 USER=root IP=192.168.1.100 CMD=ls -la
```

### Restricted Command
```
[2026-02-17 17:45:20] RESTRICTED: CONN_ID=1234-5678-abcd SESSION=20260217-174510_1234-5678-abcd_root_192.168.1.100 USER=root IP=192.168.1.100 SSH_HOST=192.168.0.19 CMD=sudo apt update
```

### Session End
```
[2026-02-17 17:45:25] === SESSION END === CONN_ID=1234-5678-abcd SESSION=20260217-174510_1234-5678-abcd_root_192.168.1.100 USER=root IP=192.168.1.100
```

## Key Features

### 1. Unique Connection Tracking
- Each connection gets a unique `CONN_ID` from Guacamole
- Session ID includes timestamp, connection ID, username, and IP
- Clear session boundaries with START/END markers

### 2. Multiple Log Files
| File | Purpose | When Written |
|------|---------|--------------|
| `ssh_commands.log` | All commands | Every command execution |
| `dangerous_commands.log` | Dangerous patterns | When dangerous command detected |
| `restricted_commands.log` | ACL violations | When command blocked by ACL |

### 3. ACL Rule Priority
1. **User-specific** (`[user:username]`) - Highest priority
2. **Connection-specific** (`[connection:host:user]`) - Medium priority  
3. **Global** (`[global]`) - Lowest priority

## Troubleshooting

### Issue: Old log format still appearing
**Cause**: Viewing old log entries from before the update  
**Solution**: 
```bash
# Backup old logs
sudo cp /var/log/guacamole/commands/ssh_commands.log{,.old}

# Clear for testing
sudo truncate -s 0 /var/log/guacamole/commands/ssh_commands.log

# Create new SSH connection and test
```

### Issue: IP shows as "unknown"
**Cause**: Environment variables not set for guacd  
**Solution**:
```bash
# Check guacd environment
sudo systemctl show guacd | grep Environment

# Or check guacd logs
sudo journalctl -u guacd -f
```

**Note**: IP address may show as "unknown" if not passed from web application. The connection ID is still unique per session.

### Issue: ACL not working
**Cause**: Config file not found or incorrect format  
**Solution**:
```bash
# Verify config exists
ls -la /etc/guacamole/command-acl.conf

# Check guacd logs for ACL loading
sudo journalctl -u guacd -f | grep -i acl

# Test with example config
sudo cp /opt/guacamole-server-1.6.0/command-acl.conf.example /etc/guacamole/command-acl.conf
sudo systemctl restart guacd
```

### Issue: Log files not created
**Cause**: Permission issues or directory doesn't exist  
**Solution**:
```bash
# Create directory
sudo mkdir -p /var/log/guacamole/commands

# Set permissions
sudo chmod 755 /var/log/guacamole/commands

# Restart guacd
sudo systemctl restart guacd
```

## Verifying Multiple Concurrent Sessions

### Test Scenario
1. Open two browser windows
2. Connect to SSH servers in both (can be same or different)
3. Execute commands in both sessions simultaneously
4. Check logs:

```bash
# View all connection IDs
sudo grep "SESSION START" /var/log/guacamole/commands/ssh_commands.log

# You should see different CONN_ID for each session:
[2026-02-17 18:00:10] === SESSION START === CONN_ID=abc123 ...
[2026-02-17 18:00:15] === SESSION START === CONN_ID=def456 ...
```

## Log Analysis Examples

### Count commands per user
```bash
sudo grep "CONN_ID=" /var/log/guacamole/commands/ssh_commands.log | \
  grep -oP 'USER=\K[^ ]+' | sort | uniq -c
```

### List all restricted command attempts
```bash
sudo grep "RESTRICTED:" /var/log/guacamole/commands/restricted_commands.log | \
  grep -oP 'CMD=\K.*'
```

### Find all sessions for a specific user
```bash
sudo grep "USER=root" /var/log/guacamole/commands/ssh_commands.log | \
  grep "SESSION START"
```

### Check for dangerous commands in last hour
```bash
sudo grep "DANGEROUS:" /var/log/guacamole/commands/dangerous_commands.log | \
  grep "$(date '+%Y-%m-%d %H')"
```

## Files Modified

```
/opt/guacamole-server-1.6.0/src/protocols/ssh/
├── command_logger.h      # Enhanced with connection ID and ACL support
├── command_logger.c      # Main implementation with ACL integration
└── input.c               # Updated to pass SSH hostname

/var/log/guacamole/commands/
├── ssh_commands.log          # All commands (NEW FORMAT)
├── dangerous_commands.log    # Dangerous patterns (NEW)
└── restricted_commands.log   # ACL violations (NEW)

/etc/guacamole/
└── command-acl.conf      # ACL configuration (from example)
```

## Next Steps

1. ✅ Code compiled and installed
2. ✅ guacd service restarted
3. ⏳ **Test with real SSH connection** (Next step for you!)
4. ⏳ Verify unique connection IDs in logs
5. ⏳ Test ACL restrictions
6. ⏳ Configure log rotation

## Support

- Full documentation: `/opt/guacamole-server-1.6.0/COMMAND_LOGGING_IMPROVEMENTS.md`
- ACL example: `/opt/guacamole-server-1.6.0/command-acl.conf.example`
- Test script: `/opt/guacamole-server-1.6.0/test_command_logging.sh`

## Quick Commands Reference

```bash
# View latest commands
sudo tail -f /var/log/guacamole/commands/ssh_commands.log

# Run test script
sudo /opt/guacamole-server-1.6.0/test_command_logging.sh

# Check guacd status
sudo systemctl status guacd

# Restart guacd
sudo systemctl restart guacd

# View guacd logs
sudo journalctl -u guacd -f

# Check ACL config
cat /etc/guacamole/command-acl.conf
```
