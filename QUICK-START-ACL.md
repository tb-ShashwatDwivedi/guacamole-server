# Quick Start: SSH Command ACL

## 🚀 5-Minute Setup

### Step 1: Build and Install (5 minutes)

```bash
cd /opt/guacamole-server-1.6.0

# Configure
./configure

# Build
make -j$(nproc)

# Install (requires root)
sudo make install

# Update library cache
sudo ldconfig
```

### Step 2: Create Configuration (2 minutes)

```bash
# Create directory
sudo mkdir -p /etc/guacamole

# Create basic config
sudo tee /etc/guacamole/command-acl.conf > /dev/null <<'EOF'
[global]
blacklist=rm -rf,sudo,shutdown,reboot,halt,poweroff,init,dd,fdisk,mkfs
blocked_message=\r\n*** Command blocked by security policy ***\r\n
EOF

# Set permissions
sudo chmod 600 /etc/guacamole/command-acl.conf
sudo chown guacd:guacd /etc/guacamole/command-acl.conf
```

### Step 3: Restart guacd (30 seconds)

```bash
# Restart the daemon
sudo systemctl restart guacd

# Check status
sudo systemctl status guacd

# View logs
sudo journalctl -u guacd -n 50
```

### Step 4: Test It! (2 minutes)

1. **Connect to an SSH server through Guacamole web interface**

2. **Try a blocked command**:
   ```bash
   sudo reboot
   ```
   ✅ Should see: `*** Command blocked by security policy ***`

3. **Try an allowed command**:
   ```bash
   ls -la
   ```
   ✅ Should work normally

4. **Try alias bypass**:
   ```bash
   alias badcmd='sudo reboot'
   ```
   ✅ Should be blocked: "alias/function definition"

## 📋 Common Configurations

### Configuration 1: Block Only Dangerous Commands

```ini
[global]
blacklist=rm -rf,sudo,shutdown,reboot,dd,fdisk,mkfs,userdel,useradd
blocked_message=\r\n*** Command blocked ***\r\n
```

### Configuration 2: Allow Only Safe Commands (Whitelist Mode)

```ini
[global]
whitelist=ls,cd,pwd,cat,grep,tail,head,less,more,ps,top,df,du,free
blocked_message=\r\n*** Command not in whitelist ***\r\n
```

### Configuration 3: Per-Connection Rules

```ini
[global]
blacklist=sudo,shutdown,reboot
blocked_message=\r\n*** Command blocked ***\r\n

[connection:production-db.example.com:dbadmin]
whitelist=ls,cd,cat,tail,mysql,mysqldump,psql,pg_dump
blocked_message=\r\n*** Production DB - restricted access ***\r\n
```

### Configuration 4: Per-User Rules

```ini
[global]
blacklist=rm -rf,sudo,shutdown
blocked_message=\r\n*** Command blocked ***\r\n

[user:admin]
blacklist=rm -rf /
blocked_message=\r\n*** Even admins can't do that ***\r\n

[user:intern]
whitelist=ls,cd,pwd,cat,grep
blocked_message=\r\n*** Intern - read-only access ***\r\n
```

## 🔍 Verify It's Working

### Check Configuration Loaded

```bash
# Connect via SSH through Guacamole and run a blocked command
# Then check logs:
sudo journalctl -u guacd -n 20 | grep -i "command acl\|blocked"
```

Expected output:
```
Feb 06 10:30:15 guacd[1234]: Command ACL enabled for this connection
Feb 06 10:30:45 guacd[1234]: Blocked command for user john: sudo reboot
```

### Test All Protection Types

1. **Direct command**: `sudo reboot` → ❌ Blocked
2. **Alias creation**: `alias bad='sudo reboot'` → ❌ Blocked  
3. **Function creation**: `function bad() { sudo reboot; }` → ❌ Blocked
4. **Source file**: `source dangerous.sh` → ❌ Blocked
5. **Allowed command**: `ls -la` → ✅ Works

## 🛠️ Troubleshooting

### Problem: ACL Not Working

**Solution 1: Check config file exists**
```bash
ls -l /etc/guacamole/command-acl.conf
```

**Solution 2: Verify permissions**
```bash
sudo chmod 600 /etc/guacamole/command-acl.conf
sudo chown guacd:guacd /etc/guacamole/command-acl.conf
```

**Solution 3: Restart guacd**
```bash
sudo systemctl restart guacd
```

### Problem: Commands Still Going Through

**Check logs for errors:**
```bash
sudo journalctl -u guacd -f
```

**Verify INI syntax:**
```bash
cat /etc/guacamole/command-acl.conf
```

Ensure:
- Section headers: `[global]`, `[connection:host:user]`, `[user:name]`
- Key=value format (no spaces around `=`)
- No special characters in values (except in blocked_message)

### Problem: Too Restrictive

**Option 1: Temporarily disable**
```bash
sudo mv /etc/guacamole/command-acl.conf /etc/guacamole/command-acl.conf.disabled
sudo systemctl restart guacd
```

**Option 2: Make less restrictive**
```bash
sudo nano /etc/guacamole/command-acl.conf
# Remove some items from blacklist or comment out whitelist
sudo systemctl restart guacd
```

## 📊 Monitoring

### Real-time Log Monitoring

```bash
# Watch for blocked commands
sudo journalctl -u guacd -f | grep -i blocked
```

### Daily Summary

```bash
# Count blocked commands today
sudo journalctl -u guacd --since today | grep -c "Blocked command"

# List unique blocked commands
sudo journalctl -u guacd --since today | grep "Blocked command" | awk -F': ' '{print $4}' | sort | uniq -c
```

### Alert on Blocked Commands

Create `/usr/local/bin/check-guacd-blocks.sh`:
```bash
#!/bin/bash
RECENT_BLOCKS=$(sudo journalctl -u guacd --since "5 minutes ago" | grep -c "Blocked command")
if [ "$RECENT_BLOCKS" -gt 10 ]; then
    echo "ALERT: $RECENT_BLOCKS commands blocked in last 5 minutes!"
    # Send email, webhook, etc.
fi
```

Add to cron:
```bash
*/5 * * * * /usr/local/bin/check-guacd-blocks.sh
```

## 🎯 Best Practices

### 1. Start Conservative
```ini
[global]
blacklist=rm -rf,sudo,shutdown,reboot
# Test this first, then add more
```

### 2. Use Whitelist for Critical Systems
```ini
[connection:production-db:admin]
whitelist=ls,cd,cat,tail,mysql,mysqldump
# Only these commands allowed
```

### 3. Test Before Production
- Set up test server
- Configure ACL rules
- Test all user workflows
- Verify no legitimate commands blocked

### 4. Document Your Rules
```ini
# Block system modification commands
# Added: 2026-02-06 by admin
# Reason: Security policy v2.1
[global]
blacklist=rm -rf,sudo,shutdown
```

### 5. Regular Review
- Monthly: Review blocked command logs
- Quarterly: Update rules based on usage
- Annually: Security audit of ACL config

## 📚 Next Steps

1. ✅ **Basic setup complete** - ACL is now active
2. 📖 Read [COMMAND-ACL-README.md](COMMAND-ACL-README.md) for full documentation
3. 📝 Review [command-acl.conf.example](command-acl.conf.example) for more examples
4. 🔒 Combine with AppArmor/SELinux for defense in depth
5. 📊 Set up monitoring and alerting
6. 🧪 Test disaster recovery scenarios

## 💡 Quick Reference

### Config File Location
```
/etc/guacamole/command-acl.conf
```

### Restart Command
```bash
sudo systemctl restart guacd
```

### View Logs
```bash
sudo journalctl -u guacd -f
```

### Test Connection
```
Guacamole Web UI → Connect to SSH → Try blocked command
```

### Priority Order
```
User Rules > Connection Rules > Global Rules
```

---

**🎉 You're all set!** Your SSH connections now have command ACL protection.
