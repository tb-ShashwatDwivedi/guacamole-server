# SSH Command ACL Implementation

## Overview

This implementation adds command access control list (ACL) functionality to Apache Guacamole's SSH protocol handler. It allows administrators to restrict which commands users can execute through SSH connections, providing an additional security layer.

## Features

- **Global, Per-Connection, and Per-User Rules**: Define ACL rules at three priority levels
- **Blacklist and Whitelist Modes**: Block specific commands or allow only approved commands
- **Alias/Function Protection**: Automatically blocks attempts to create aliases or functions that could bypass restrictions
- **Real-time Command Filtering**: Commands are checked before being sent to the SSH server
- **Audit Logging**: All blocked commands are logged for security auditing
- **Flexible Configuration**: INI-format configuration file for easy management

## Architecture

### Data Flow

```
User Input → Terminal → ssh_input_thread() → ACL Check → SSH Server
                                   ↓
                            [If Blocked]
                                   ↓
                          Denial Message + Log
```

### Priority Order

Rules are applied in this priority order (highest to lowest):

1. **Per-User Rules** (Guacamole username)
2. **Per-Connection Rules** (SSH hostname:username)
3. **Global Rules** (default for all connections)

## Installation

### 1. Build and Install

The ACL feature is now integrated into guacamole-server. Build as normal:

```bash
cd /opt/guacamole-server-1.6.0
./configure
make
sudo make install
```

### 2. Create Configuration File

Copy the example configuration:

```bash
sudo mkdir -p /etc/guacamole
sudo cp command-acl.conf.example /etc/guacamole/command-acl.conf
sudo chmod 600 /etc/guacamole/command-acl.conf
sudo chown guacd:guacd /etc/guacamole/command-acl.conf
```

### 3. Edit Configuration

Edit `/etc/guacamole/command-acl.conf` to define your rules:

```ini
[global]
blacklist=rm -rf,sudo,shutdown,reboot
blocked_message=\r\n*** Command blocked by security policy ***\r\n

[connection:production-server.example.com:appuser]
whitelist=ls,cd,pwd,cat,grep,tail,ps,top
blocked_message=\r\n*** Production server - restricted access ***\r\n

[user:john]
blacklist=sudo,rm,chmod
blocked_message=\r\n*** User not authorized for this command ***\r\n
```

### 4. Restart guacd

```bash
sudo systemctl restart guacd
```

## Configuration Format

### Section Types

#### Global Rules
```ini
[global]
blacklist=command1,command2,command3
whitelist=
blocked_message=Your custom message here
```

#### Per-Connection Rules
```ini
[connection:hostname:sshusername]
blacklist=command1,command2
whitelist=
blocked_message=Connection-specific message
```

- `hostname`: SSH server hostname (from connection configuration)
- `sshusername`: SSH username used to log in

#### Per-User Rules
```ini
[user:guacamole_username]
blacklist=command1,command2
whitelist=
blocked_message=User-specific message
```

- `guacamole_username`: Username from Guacamole web interface

### Configuration Keys

- **`blacklist`**: Comma-separated list of command patterns to block
- **`whitelist`**: Comma-separated list of command patterns to allow (if set, ONLY these commands are allowed)
- **`blocked_message`**: Custom message displayed when a command is blocked

### Dangerous Command Configuration (Global Only)

- **`dangerous_commands`**: Comma-separated list of patterns for commands considered "dangerous". Used for audit logging and optional confirmation. If not set, built-in defaults apply (rm -rf, dd, shutdown, reboot, etc.).
- **`dangerous_require_confirmation`**: When set to `true` or `yes`, users must type "yes" or "no" before executing any dangerous command. Helps prevent accidental execution.

Example:
```ini
[global]
dangerous_commands=rm -rf,rm -rf /,dd if=/dev/zero,mkfs,shutdown,reboot,halt,kill -9,pkill,DROP DATABASE
dangerous_require_confirmation=true
```

### Pattern Matching

- **Substring matching**: Pattern "rm" matches "rm", "rm -rf", "arm"
- **Case-sensitive**: "RM" and "rm" are different
- **No regex**: Simple substring matching only (for security and performance)

## Usage Examples

### Example 1: Block Dangerous Commands Globally

```ini
[global]
blacklist=rm -rf,sudo,shutdown,reboot,halt,poweroff,dd,fdisk,mkfs
blocked_message=\r\n*** Command blocked by security policy ***\r\n
```

### Example 2: Whitelist-Only Mode for Production

```ini
[connection:prod-db.example.com:readonly]
whitelist=ls,cd,pwd,cat,tail,head,grep,ps,top,mysql
blocked_message=\r\n*** Production database - read-only access ***\r\n
```

### Example 3: User-Specific Restrictions

```ini
[user:intern]
blacklist=sudo,rm,mv,chmod,chown,kill
whitelist=
blocked_message=\r\n*** Intern account - restricted access ***\r\n
```

### Example 4: Mixed Mode (Blacklist + Whitelist)

```ini
[connection:app-server:appuser]
blacklist=rm -rf /,sudo su,dd if=/dev/zero
whitelist=ls,cd,pwd,cat,grep,tail,systemctl status,journalctl
blocked_message=\r\n*** Application server - access denied ***\r\n
```

## Security Considerations

### What This Protects Against

✅ Direct execution of blocked commands  
✅ Alias creation attempts  
✅ Function definitions  
✅ Source file execution  
✅ Accidental dangerous commands  

### What This Does NOT Protect Against

❌ Commands executed through shell scripts already on the server  
❌ Commands using backticks or $() subshells (unless blocked explicitly)  
❌ Environment variable manipulation  
❌ Piped commands that modify behavior  
❌ Binary execution via full paths (unless pattern matches)  

### Defense in Depth

This is **application-level protection**. For comprehensive security, combine with:

1. **SSH Server Restrictions**
   - Use `ForceCommand` in SSH config
   - Implement restricted bash (rbash)
   - Use SSH certificate-based authentication

2. **Kernel-Level Enforcement**
   - Deploy AppArmor profiles
   - Use SELinux policies
   - Implement mandatory access controls (MAC)

3. **File System Security**
   - Restrict file permissions
   - Use read-only mounts where appropriate
   - Implement disk quotas

4. **Monitoring**
   - Enable audit logging (auditd)
   - Monitor guacd logs for blocked commands
   - Set up alerts for suspicious activity

## Logging

Blocked commands are logged at WARNING level in guacd logs:

```
[ssh] WARNING: Blocked command for user john: sudo reboot
[ssh] WARNING: Blocked alias/function definition: alias rm='rm -rf'
```

View logs:
```bash
sudo journalctl -u guacd -f
# or
sudo tail -f /var/log/syslog | grep guacd
```

## Troubleshooting

### Commands Not Being Blocked

1. **Check config file exists and is readable**:
   ```bash
   ls -l /etc/guacamole/command-acl.conf
   ```

2. **Verify file ownership**:
   ```bash
   sudo chown guacd:guacd /etc/guacamole/command-acl.conf
   ```

3. **Check guacd logs for loading errors**:
   ```bash
   sudo journalctl -u guacd | grep -i acl
   ```

4. **Verify rule syntax**:
   - Ensure section names are correct: `[global]`, `[connection:host:user]`, `[user:name]`
   - Check for proper key=value format
   - Verify no extra spaces in section headers

### Rules Not Matching Expected Behavior

1. **Check priority**: User rules override connection rules override global rules
2. **Pattern matching is substring-based**: "rm" matches "arm", "rmdir", etc.
3. **Whitelist mode**: If whitelist is set, ONLY those commands are allowed
4. **Empty commands**: Empty strings are always allowed

### ACL Not Loading

1. **Config file permissions too open**:
   ```bash
   sudo chmod 600 /etc/guacamole/command-acl.conf
   ```

2. **Restart guacd after config changes**:
   ```bash
   sudo systemctl restart guacd
   ```

3. **Check file format**: Ensure INI format is valid (no syntax errors)

## Performance Impact

- **Minimal**: ACL checking adds negligible latency (~microseconds per keystroke)
- **Memory**: ~1-2 KB per connection for command buffering
- **CPU**: Substring matching is O(n*m) but patterns are typically short

## Testing

### Test ACL Configuration

1. **Create a test config**:
   ```ini
   [global]
   blacklist=test_blocked_cmd
   blocked_message=\r\n*** TEST: Command blocked ***\r\n
   ```

2. **Connect via Guacamole SSH**

3. **Try blocked command**:
   ```bash
   test_blocked_cmd
   ```
   Expected: Denial message appears, command not executed

4. **Try allowed command**:
   ```bash
   ls
   ```
   Expected: Command executes normally

5. **Try alias bypass**:
   ```bash
   alias bad='test_blocked_cmd'
   ```
   Expected: Blocked with "alias/function definition" message

### Verify Logging

```bash
sudo journalctl -u guacd -f
# In another terminal, try blocked command
# You should see: WARNING: Blocked command...
```

## File Structure

### New Files Created

```
src/protocols/ssh/
├── command-acl.h          # ACL header with structures and function declarations
├── command-acl.c          # ACL implementation (INI parser, rule matching)
└── Makefile.am            # Updated to build ACL files

Root directory/
├── command-acl.conf.example  # Example configuration file
└── COMMAND-ACL-README.md     # This documentation
```

### Modified Files

```
src/protocols/ssh/
├── settings.h             # Added ACL fields to guac_ssh_settings structure
├── settings.c             # Load ACL config, resolve rules, cleanup
└── ssh.c                  # Integrated ACL checks in ssh_input_thread()
```

## API Reference

### Functions

#### `guac_ssh_acl_load_config()`
```c
guac_ssh_acl_config* guac_ssh_acl_load_config(const char* config_path);
```
Loads ACL configuration from INI file.

#### `guac_ssh_acl_get_rule()`
```c
guac_ssh_acl_rule* guac_ssh_acl_get_rule(
    guac_ssh_acl_config* config,
    const char* guacamole_username,
    const char* hostname,
    const char* ssh_username
);
```
Gets the effective ACL rule based on priority.

#### `guac_ssh_acl_check_command()`
```c
bool guac_ssh_acl_check_command(
    guac_ssh_acl_rule* rule,
    const char* command,
    guac_client* client
);
```
Checks if a command is allowed. Returns true if allowed, false if blocked.

#### `guac_ssh_acl_free_config()`
```c
void guac_ssh_acl_free_config(guac_ssh_acl_config* config);
```
Frees all memory associated with an ACL configuration.

## Contributing

When modifying the ACL implementation:

1. **Maintain backward compatibility**: Missing config file should not break SSH connections
2. **Log security events**: Always log blocked commands
3. **Test thoroughly**: Verify both blacklist and whitelist modes
4. **Update documentation**: Keep this README current
5. **Follow Apache Guacamole coding standards**

## License

This implementation is licensed under the Apache License, Version 2.0, consistent with Apache Guacamole.

## Support

For issues or questions:
- Check guacd logs for errors
- Review this documentation
- Verify configuration file syntax
- Test with simple global rules first

## Version History

- **v1.0** (2026-02-06): Initial implementation
  - Global, per-connection, and per-user rules
  - Blacklist and whitelist modes
  - Alias/function protection
  - INI configuration file support
  - Audit logging

---

**Note**: This feature is a custom extension to Apache Guacamole and may require updates when upgrading to newer versions of guacamole-server.
