# Command Logging Format Update - Version 2.0

## Summary of Changes

### 1. Clean CSV-Style Log Format ✅
- Removed SESSION START/END headers
- Implemented pipe-delimited (|) format for easy parsing
- Each command is logged on a single line with all context

### 2. Consistent Field Order ✅
All log files now use the same format:
```
timestamp|ssh_host|connection_id|session_id|user|ip|command|[tag]
```

### 3. Unique Connection Tracking ✅
- Each SSH session gets a unique `connection_id`
- Commands from different concurrent sessions are clearly separated
- No more merged logs from multiple sessions

## New Log Format

### Main Commands Log (`ssh_commands.log`)
**Format**: `timestamp|ssh_host|connection_id|session_id|user|ip|command`

**Example**:
```
2026-02-17 18:10:15|192.168.0.19|@abc123-def456|20260217-181015_@abc123_root_192.168.1.100|root|192.168.1.100|ls -la
2026-02-17 18:10:20|192.168.0.19|@abc123-def456|20260217-181015_@abc123_root_192.168.1.100|root|192.168.1.100|cat /etc/passwd
2026-02-17 18:10:25|192.168.0.13|@xyz789-ghi012|20260217-181020_@xyz789_admin_192.168.1.101|admin|192.168.1.101|whoami
```

### Dangerous Commands Log (`dangerous_commands.log`)
**Format**: `timestamp|ssh_host|connection_id|session_id|user|ip|command|DANGEROUS`

**Example**:
```
2026-02-17 18:15:30|192.168.0.19|@abc123-def456|20260217-181015_@abc123_root_192.168.1.100|root|192.168.1.100|rm -rf /tmp/old_data|DANGEROUS
```

### Restricted Commands Log (`restricted_commands.log`)
**Format**: `timestamp|ssh_host|connection_id|session_id|user|ip|command|RESTRICTED`

**Example**:
```
2026-02-17 18:16:45|192.168.0.19|@abc123-def456|20260217-181015_@abc123_root_192.168.1.100|root|192.168.1.100|sudo apt update|RESTRICTED
```

## Field Descriptions

| Field | Description | Example |
|-------|-------------|---------|
| `timestamp` | Date and time of command execution | `2026-02-17 18:10:15` |
| `ssh_host` | SSH server hostname/IP | `192.168.0.19` |
| `connection_id` | Unique Guacamole connection ID | `@abc123-def456` |
| `session_id` | Detailed session identifier | `20260217-181015_@abc123_root_192.168.1.100` |
| `user` | SSH username | `root` |
| `ip` | Client IP address | `192.168.1.100` or `unknown` |
| `command` | Executed command (tab chars replaced with space) | `ls -la` |
| `tag` | Optional tag (DANGEROUS or RESTRICTED) | `DANGEROUS` |

## Parsing Examples

### Parse with awk
```bash
# Extract all commands by specific user
awk -F'|' '$5 == "root" {print $7}' /var/log/guacamole/commands/ssh_commands.log

# Show commands from specific connection
awk -F'|' '$3 == "@abc123-def456" {print $1, $7}' /var/log/guacamole/commands/ssh_commands.log

# Count commands per SSH host
awk -F'|' '{print $2}' /var/log/guacamole/commands/ssh_commands.log | sort | uniq -c
```

### Parse with Python
```python
import csv

with open('/var/log/guacamole/commands/ssh_commands.log', 'r') as f:
    for line in f:
        fields = line.strip().split('|')
        timestamp, ssh_host, conn_id, session_id, user, ip, command = fields
        print(f"{user}@{ssh_host}: {command}")
```

### Parse with cut
```bash
# Get only commands (field 7)
cut -d'|' -f7 /var/log/guacamole/commands/ssh_commands.log

# Get timestamp and command
cut -d'|' -f1,7 /var/log/guacamole/commands/ssh_commands.log
```

## Concurrent Session Handling

### Problem (Before)
When multiple SSH sessions ran concurrently, commands from different sessions were mixed:
```
[timestamp] USER=root CMD=ls       # Session 1
[timestamp] USER=admin CMD=pwd     # Session 2
[timestamp] USER=root CMD=cd /tmp  # Session 1
```

### Solution (After)
Each command includes its unique connection_id and session_id:
```
2026-02-17 18:10:15|192.168.0.19|@conn1|session1|root|192.168.1.100|ls
2026-02-17 18:10:16|192.168.0.13|@conn2|session2|admin|192.168.1.101|pwd
2026-02-17 18:10:17|192.168.0.19|@conn1|session1|root|192.168.1.100|cd /tmp
```

Now you can easily filter by connection_id to see all commands from a specific session:
```bash
grep '@conn1' /var/log/guacamole/commands/ssh_commands.log
```

## Tab Completion Handling

### Current Limitation

When using tab completion in SSH:
1. User types: `rm -rf ji`
2. User presses Tab
3. Shell completes to: `rm -rf jigar`
4. User presses Enter

**What gets logged**: The command before tab completion was completed by the shell's echo back.

**Example**: If you type `rm -rf ji[TAB]` and it completes to `rm -rf jigar`, the log may show `rm -rf ji` without the completed portion.

### Why This Happens

Tab completion works at the SSH server level:
- User types "ji" and presses Tab
- SSH server sends back completion characters "gar"
- Terminal displays the full "jigar"
- **But**: Completion characters come from SSH server → terminal (not captured as keystrokes)
- Our logger only captures user keystrokes, not server responses

### Workarounds

#### Option 1: Type Full Commands
Instead of using tab completion, type the full command:
```bash
# Instead of: rm -rf ji[TAB]
# Type:       rm -rf jigar
```

#### Option 2: Type a Character After Tab
After tab completion, type and delete a character to ensure the full command is in the buffer:
```bash
rm -rf ji[TAB]      # Completes to "jigar"
rm -rf jigar [SPACE][BACKSPACE]  # Adds space then removes it
[ENTER]             # Now "jigar" is captured
```

#### Option 3: Use Command History Logging (Future Enhancement)
We're working on integrating with terminal history to capture the actual executed commands.

### Future Solution

To properly capture tab completion, we need to:
1. Hook into SSH channel data reception
2. Parse terminal escape sequences
3. Track the current command line state
4. Capture what's actually displayed, not just what's typed

This is planned for a future update.

## Log Rotation

Configure logrotate for the new format:

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
    sharedscripts
    postrotate
        systemctl reload guacd > /dev/null 2>&1 || true
    endscript
}
```

## Querying Logs

### Find all commands from a specific session
```bash
grep "session1" /var/log/guacamole/commands/ssh_commands.log
```

### Find all commands by a user on a specific host
```bash
awk -F'|' '$2 == "192.168.0.19" && $5 == "root" {print $1, $7}' \
    /var/log/guacamole/commands/ssh_commands.log
```

### Show commands in chronological order from multiple sessions
```bash
# Logs are already in chronological order, just view
cat /var/log/guacamole/commands/ssh_commands.log
```

### Export to CSV for analysis
```bash
# Add header
echo "timestamp,ssh_host,connection_id,session_id,user,ip,command" > commands.csv

# Convert pipe-delimited to comma-delimited (escape commands with commas)
sed 's/|/,/g' /var/log/guacamole/commands/ssh_commands.log >> commands.csv
```

### Create session summary
```bash
#!/bin/bash
# summarize_sessions.sh - Summarize commands per session

echo "Session ID | User | SSH Host | Command Count"
echo "-----------|------|----------|---------------"

awk -F'|' '{
    key = $4 "|" $5 "|" $2
    count[key]++
}
END {
    for (k in count) {
        print k " | " count[k]
    }
}' /var/log/guacamole/commands/ssh_commands.log | sort -t'|' -k1
```

## Migration from Old Format

### Old Format (v1)
```
[2026-02-17 17:46:44] CONN_ID=@abc123 SESSION=session1 USER=root IP=192.168.1.100 CMD=ls
[2026-02-17 17:46:40] === SESSION START === CONN_ID=@abc123 ...
```

### New Format (v2)
```
2026-02-17 17:46:44|192.168.0.19|@abc123|session1|root|192.168.1.100|ls
```

### Conversion Script
```bash
#!/bin/bash
# convert_old_logs.sh - Convert v1 format to v2

grep -v "SESSION START\|SESSION END" /var/log/guacamole/commands/ssh_commands.log.old | \
while IFS= read -r line; do
    # Extract fields using regex
    timestamp=$(echo "$line" | grep -oP '\[\K[^]]+')
    conn_id=$(echo "$line" | grep -oP 'CONN_ID=\K[^ ]+')
    session=$(echo "$line" | grep -oP 'SESSION=\K[^ ]+')
    user=$(echo "$line" | grep -oP 'USER=\K[^ ]+')
    ip=$(echo "$line" | grep -oP 'IP=\K[^ ]+')
    cmd=$(echo "$line" | grep -oP 'CMD=\K.*$')
    ssh_host=$(echo "$line" | grep -oP 'SSH_HOST=\K[^ ]+' || echo "unknown")
    
    # Output in new format
    echo "$timestamp|$ssh_host|$conn_id|$session|$user|$ip|$cmd"
done
```

## Testing

### Test 1: Create Two Concurrent Sessions
1. Open two SSH connections through Guacamole
2. In session 1, execute: `ls`, `pwd`, `whoami`
3. In session 2, execute: `date`, `hostname`, `uptime`
4. Check logs:
```bash
tail -20 /var/log/guacamole/commands/ssh_commands.log
```
5. Verify each command has the correct connection_id

### Test 2: Verify Field Separation
```bash
# Count fields - should always be 7 (or 8 with tag)
awk -F'|' '{print NF}' /var/log/guacamole/commands/ssh_commands.log | sort | uniq
```

### Test 3: Parse Logs
```bash
# Extract all unique connection IDs
awk -F'|' '{print $3}' /var/log/guacamole/commands/ssh_commands.log | sort -u
```

## Benefits of New Format

1. **Easy Parsing**: Pipe-delimited format is trivial to parse with standard Unix tools
2. **No Header Noise**: Every line is data, no session markers to filter out
3. **Clear Session Separation**: connection_id makes it easy to group commands by session
4. **Consistent Format**: All three log files use the same structure
5. **Timestamp-First**: Chronological sorting and analysis is straightforward
6. **Scalable**: Format works well with millions of entries
7. **Tool-Friendly**: Compatible with awk, cut, grep, Python csv module, Excel, etc.

## File Locations

- **Main Log**: `/var/log/guacamole/commands/ssh_commands.log`
- **Dangerous Commands**: `/var/log/guacamole/commands/dangerous_commands.log`
- **Restricted Commands**: `/var/log/guacamole/commands/restricted_commands.log`
- **Documentation**: `/opt/guacamole-server-1.6.0/LOG_FORMAT_UPDATE.md`

## Version History

- **v2.0** (2026-02-17): Clean pipe-delimited format, removed headers, fixed concurrent session logging
- **v1.0** (2026-02-17): Initial implementation with connection ID tracking

## Support

For issues or questions:
1. Check guacd logs: `journalctl -u guacd -f`
2. Verify log files: `ls -la /var/log/guacamole/commands/`
3. Test log format: `tail /var/log/guacamole/commands/ssh_commands.log`
