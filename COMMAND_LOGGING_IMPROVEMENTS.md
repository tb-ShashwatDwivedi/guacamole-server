# Command Logging Improvements - Implementation Summary

## Overview
Enhanced the Apache Guacamole SSH command logging system to capture unique connection IDs per session and implement ACL-based restricted command logging.

## Changes Made

### 1. Enhanced Connection Tracking

#### Problem
- Connection ID and IP address were showing as `0.0.0.0` and not updating per connection
- Session IDs were not uniquely identifying connections

#### Solution
- **Unique Connection ID**: Now uses Guacamole's built-in `user->user_id` which is guaranteed unique per connection
- **Improved IP Detection**: Enhanced IP address extraction with multiple fallback methods:
  - `GUAC_REMOTE_ADDR` environment variable
  - `REMOTE_ADDR` environment variable  
  - Connection ID as fallback reference
- **Detailed Session ID**: Generated using format `YYYYMMDD-HHMMSS_CONNECTION-ID_USERNAME_IP`

#### Files Modified
- `src/protocols/ssh/command_logger.h`
- `src/protocols/ssh/command_logger.c`
- `src/protocols/ssh/input.c`

### 2. ACL-Based Restricted Command Logging

#### Problem
- No separation between dangerous commands and ACL-restricted commands
- All blocked commands were logged to the same file

#### Solution
- **Integrated ACL Configuration**: Command logger now loads and checks ACL rules from `/etc/guacamole/command-acl.conf`
- **Separate Log Files**: 
  - `ssh_commands.log` - All commands executed
  - `dangerous_commands.log` - Commands matching dangerous patterns (rm -rf, dd, shutdown, etc.)
  - `restricted_commands.log` - Commands blocked by ACL rules (blacklist/whitelist)
- **Priority-Based ACL**: Checks rules in order: user-specific > connection-specific > global

#### Configuration Files
- `/etc/guacamole/command-acl.conf` - Active ACL configuration
- `/opt/guacamole-server-1.6.0/command-acl.conf.example` - Example configuration with detailed comments

### 3. Enhanced Log Format

#### Old Format
```
[2026-02-17 16:47:44] USER=appadmin IP=0.0.0.0 CMD=clear
```

#### New Format
```
[2026-02-17 17:45:10] === SESSION START === CONN_ID=$USER_ID SESSION=20260217-174510_$USER_ID_appadmin_192.168.1.100 USER=appadmin IP=192.168.1.100 SSH_HOST=192.168.0.19

[2026-02-17 17:45:15] CONN_ID=$USER_ID SESSION=20260217-174510_$USER_ID_appadmin_192.168.1.100 USER=appadmin IP=192.168.1.100 CMD=ls -la

[2026-02-17 17:45:20] CONN_ID=$USER_ID SESSION=20260217-174510_$USER_ID_appadmin_192.168.1.100 USER=appadmin IP=192.168.1.100 CMD=sudo apt update

[2026-02-17 17:45:25] === SESSION END === CONN_ID=$USER_ID SESSION=20260217-174510_$USER_ID_appadmin_192.168.1.100 USER=appadmin IP=192.168.1.100
```

#### Restricted Commands Log Format
```
[2026-02-17 17:45:20] RESTRICTED: CONN_ID=$USER_ID SESSION=20260217-174510_$USER_ID_appadmin_192.168.1.100 USER=appadmin IP=192.168.1.100 SSH_HOST=192.168.0.19 CMD=sudo apt update
```

## Log Files Location

All log files are created in `/var/log/guacamole/commands/`:

1. **ssh_commands.log** - Complete command history
   - Contains all commands executed
   - Includes session start/end markers
   - Unique connection ID per session

2. **dangerous_commands.log** - Potentially harmful commands
   - Commands matching dangerous patterns
   - Examples: `rm -rf`, `dd if=/dev/zero`, `shutdown`, `mkfs`
   - Also logged to syslog with LOG_WARNING level

3. **restricted_commands.log** - ACL-blocked commands
   - Commands blocked by ACL blacklist
   - Commands not in ACL whitelist (if enabled)
   - Includes SSH hostname for connection-specific rules

## ACL Configuration

### Configuration File: `/etc/guacamole/command-acl.conf`

The ACL system supports three levels of rules (in priority order):

1. **User-specific rules** - Based on Guacamole web username
   ```ini
   [user:developer]
   blacklist=sudo,shutdown,reboot,rm -rf
   blocked_message=\r\n*** Developer access - command blocked ***\r\n
   ```

2. **Connection-specific rules** - Based on SSH hostname:username
   ```ini
   [connection:192.168.0.19:root]
   blacklist=ping,sudo
   blocked_message=\r\n*** Command blocked on this connection ***\r\n
   ```

3. **Global rules** - Apply to all connections
   ```ini
   [global]
   blacklist=shutdown,reboot,rm -rf
   blocked_message=\r\n*** Command blocked ***\r\n
   ```

### ACL Features

- **Blacklist Mode**: Block specific command patterns
- **Whitelist Mode**: Only allow specific commands (if whitelist is set)
- **Pattern Matching**: Simple substring matching (case-sensitive)
- **Custom Messages**: Display custom blocked messages to users
- **Empty Commands**: Always allowed regardless of rules

## Testing

### Test 1: Verify Unique Connection IDs
1. Create two SSH connections through Guacamole
2. Execute commands in both sessions
3. Check `/var/log/guacamole/commands/ssh_commands.log`
4. Verify each session has a different CONN_ID and SESSION ID

### Test 2: Verify ACL Restrictions
1. Configure ACL rules in `/etc/guacamole/command-acl.conf`
2. Connect via SSH through Guacamole
3. Try to execute a blacklisted command
4. Verify command is logged in `/var/log/guacamole/commands/restricted_commands.log`

### Test 3: Verify Dangerous Command Detection
1. Execute a dangerous command (e.g., `rm -rf /tmp/test`)
2. Check `/var/log/guacamole/commands/dangerous_commands.log`
3. Verify command is logged with DANGEROUS tag

### Test 4: Verify IP Address Capture
1. Connect from a known IP address
2. Execute commands
3. Verify IP is correctly captured in logs (not 0.0.0.0)

## Environment Variables for IP Detection

To ensure proper IP address capture, set these environment variables in guacd:

1. **Edit guacd service**: `/etc/systemd/system/guacd.service.d/override.conf`
   ```ini
   [Service]
   Environment="GUAC_REMOTE_ADDR=%i"
   ```

2. **Or pass from web application**: Set environment variables when starting guacd

## Compilation and Installation

```bash
# Navigate to SSH protocol directory
cd /opt/guacamole-server-1.6.0/src/protocols/ssh

# Compile
make

# Install
sudo make install

# Restart guacd
sudo systemctl restart guacd

# Verify service is running
sudo systemctl status guacd
```

## Code Structure

### Header Files
- `command_logger.h` - Logger structure and function declarations
- `command-acl.h` - ACL configuration structures (existing)

### Implementation Files
- `command_logger.c` - Main logging implementation
  - Connection ID generation from user->user_id
  - Session ID generation with timestamp
  - IP address detection with fallbacks
  - ACL integration and checking
  - Multiple log file management
  
- `input.c` - Keyboard input handler
  - Logger initialization per user
  - SSH hostname extraction
  - Logger lifecycle management

## Key Improvements

1. ✅ **Unique Connection IDs**: Uses Guacamole's guaranteed-unique user_id
2. ✅ **Better IP Detection**: Multiple fallback methods for IP extraction
3. ✅ **Session Tracking**: Clear session start/end markers in logs
4. ✅ **ACL Integration**: Automatic command filtering based on configuration
5. ✅ **Separate Log Files**: Different files for normal, dangerous, and restricted commands
6. ✅ **Comprehensive Logging**: Includes connection ID, session ID, username, IP, and SSH hostname
7. ✅ **Syslog Integration**: Critical events also logged to system log

## Security Considerations

1. **Log File Permissions**: Ensure only authorized users can read command logs
   ```bash
   sudo chmod 640 /var/log/guacamole/commands/*.log
   sudo chown guacd:adm /var/log/guacamole/commands/*.log
   ```

2. **ACL Configuration**: Protect ACL configuration file
   ```bash
   sudo chmod 600 /etc/guacamole/command-acl.conf
   sudo chown guacd:guacd /etc/guacamole/command-acl.conf
   ```

3. **Log Rotation**: Configure logrotate for command logs
   ```bash
   # Create /etc/logrotate.d/guacamole-commands
   /var/log/guacamole/commands/*.log {
       daily
       rotate 30
       compress
       delaycompress
       missingok
       notifempty
       create 640 guacd adm
   }
   ```

## Troubleshooting

### Issue: IP still shows as "unknown"
- Check environment variables: `env | grep -i remote`
- Verify guacd service configuration
- Check guacd logs: `journalctl -u guacd -f`

### Issue: ACL rules not working
- Verify ACL config file exists: `ls -la /etc/guacamole/command-acl.conf`
- Check guacd logs for ACL loading messages
- Verify config file syntax (use example as reference)

### Issue: Logs not being created
- Check directory permissions: `ls -la /var/log/guacamole/commands/`
- Verify guacd user has write access
- Check disk space: `df -h /var/log`

### Issue: Compilation errors
- Ensure all dependencies installed: `libguac-dev`, `libssh2-1-dev`
- Run `make clean` before rebuilding
- Check for syntax errors in modified files

## Future Enhancements

1. **Real-time IP Extraction**: Direct socket-based IP extraction
2. **Command Execution Prevention**: Block commands before execution (currently logs only)
3. **Web Dashboard**: Real-time command monitoring interface
4. **Advanced Pattern Matching**: Regex support in ACL rules
5. **Command Analytics**: Statistics and reporting on command usage
6. **Integration with SIEM**: Export logs to security monitoring systems

## References

- Apache Guacamole Documentation: https://guacamole.apache.org/doc/gug/
- Command ACL README: `/opt/guacamole-server-1.6.0/COMMAND-ACL-README.md`
- ACL Example Config: `/opt/guacamole-server-1.6.0/command-acl.conf.example`

## Author & Version

- **Implementation Date**: February 17, 2026
- **Version**: 1.0
- **Guacamole Server Version**: 1.6.0
- **Platform**: Linux (tested on Ubuntu/Debian)
