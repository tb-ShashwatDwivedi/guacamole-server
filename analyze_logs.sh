#!/bin/bash

# Command Log Analyzer for Apache Guacamole
# Analyzes the new pipe-delimited log format

LOG_FILE="/var/log/guacamole/commands/ssh_commands.log"
DANGEROUS_LOG="/var/log/guacamole/commands/dangerous_commands.log"
RESTRICTED_LOG="/var/log/guacamole/commands/restricted_commands.log"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

show_usage() {
    echo "Usage: $0 [command] [options]"
    echo ""
    echo "Commands:"
    echo "  summary              Show overall statistics"
    echo "  sessions             List all unique sessions"
    echo "  session <id>         Show all commands for a session"
    echo "  user <username>      Show all commands by a user"
    echo "  host <hostname>      Show all commands to an SSH host"
    echo "  connection <id>      Show all commands for a connection ID"
    echo "  dangerous            Show all dangerous commands"
    echo "  restricted           Show all restricted commands"
    echo "  recent [N]           Show last N commands (default: 20)"
    echo "  concurrent           Show sessions that ran concurrently"
    echo "  export <file>        Export to CSV format"
    echo "  parse                Show parsing examples"
    echo ""
}

check_log_exists() {
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${RED}Error: Log file not found: $LOG_FILE${NC}"
        echo "Please create an SSH connection through Guacamole to generate logs"
        exit 1
    fi
}

show_summary() {
    check_log_exists
    
    echo -e "${BLUE}=== Command Log Summary ===${NC}"
    echo ""
    
    total_commands=$(wc -l < "$LOG_FILE")
    echo -e "Total commands logged: ${GREEN}$total_commands${NC}"
    
    echo ""
    echo -e "${BLUE}Commands by User:${NC}"
    awk -F'|' '{print $5}' "$LOG_FILE" | sort | uniq -c | sort -rn | \
        while read count user; do
            echo "  $user: $count"
        done
    
    echo ""
    echo -e "${BLUE}Commands by SSH Host:${NC}"
    awk -F'|' '{print $2}' "$LOG_FILE" | sort | uniq -c | sort -rn | \
        while read count host; do
            echo "  $host: $count"
        done
    
    echo ""
    echo -e "${BLUE}Unique Sessions:${NC}"
    unique_sessions=$(awk -F'|' '{print $4}' "$LOG_FILE" | sort -u | wc -l)
    echo "  Total: $unique_sessions"
    
    echo ""
    echo -e "${BLUE}Unique Connections:${NC}"
    unique_connections=$(awk -F'|' '{print $3}' "$LOG_FILE" | sort -u | wc -l)
    echo "  Total: $unique_connections"
    
    if [ -f "$DANGEROUS_LOG" ]; then
        echo ""
        dangerous_count=$(wc -l < "$DANGEROUS_LOG")
        echo -e "${RED}Dangerous commands: $dangerous_count${NC}"
    fi
    
    if [ -f "$RESTRICTED_LOG" ]; then
        restricted_count=$(wc -l < "$RESTRICTED_LOG")
        echo -e "${YELLOW}Restricted commands: $restricted_count${NC}"
    fi
}

list_sessions() {
    check_log_exists
    
    echo -e "${BLUE}=== Active Sessions ===${NC}"
    echo ""
    echo -e "Session ID|User|SSH Host|Commands"
    echo "----------|----|--------|--------"
    
    awk -F'|' '{
        key = $4
        if (!(key in user)) {
            user[key] = $5
            host[key] = $2
        }
        count[key]++
    }
    END {
        for (k in count) {
            print k "|" user[k] "|" host[k] "|" count[k]
        }
    }' "$LOG_FILE" | sort | column -t -s'|'
}

show_session() {
    check_log_exists
    local session_id="$1"
    
    if [ -z "$session_id" ]; then
        echo -e "${RED}Error: Session ID required${NC}"
        echo "Usage: $0 session <session_id>"
        exit 1
    fi
    
    echo -e "${BLUE}=== Commands for Session: $session_id ===${NC}"
    echo ""
    
    grep "|$session_id|" "$LOG_FILE" | while IFS='|' read -r timestamp ssh_host conn_id sess_id user ip command; do
        echo -e "${GREEN}[$timestamp]${NC} $command"
    done
}

show_user_commands() {
    check_log_exists
    local username="$1"
    
    if [ -z "$username" ]; then
        echo -e "${RED}Error: Username required${NC}"
        echo "Usage: $0 user <username>"
        exit 1
    fi
    
    echo -e "${BLUE}=== Commands by User: $username ===${NC}"
    echo ""
    
    awk -F'|' -v user="$username" '$5 == user {print $1, $2, $7}' "$LOG_FILE" | \
        while read timestamp host command; do
            echo -e "${GREEN}[$timestamp]${NC} ${YELLOW}$host${NC}: $command"
        done
}

show_host_commands() {
    check_log_exists
    local hostname="$1"
    
    if [ -z "$hostname" ]; then
        echo -e "${RED}Error: Hostname required${NC}"
        echo "Usage: $0 host <hostname>"
        exit 1
    fi
    
    echo -e "${BLUE}=== Commands to SSH Host: $hostname ===${NC}"
    echo ""
    
    awk -F'|' -v host="$hostname" '$2 == host {print $1, $5, $7}' "$LOG_FILE" | \
        while read timestamp user command; do
            echo -e "${GREEN}[$timestamp]${NC} ${YELLOW}$user${NC}: $command"
        done
}

show_connection_commands() {
    check_log_exists
    local conn_id="$1"
    
    if [ -z "$conn_id" ]; then
        echo -e "${RED}Error: Connection ID required${NC}"
        echo "Usage: $0 connection <connection_id>"
        exit 1
    fi
    
    echo -e "${BLUE}=== Commands for Connection: $conn_id ===${NC}"
    echo ""
    
    awk -F'|' -v conn="$conn_id" '$3 == conn {print $1, $7}' "$LOG_FILE" | \
        while read timestamp command; do
            echo -e "${GREEN}[$timestamp]${NC} $command"
        done
}

show_dangerous() {
    if [ ! -f "$DANGEROUS_LOG" ]; then
        echo -e "${YELLOW}No dangerous commands logged yet${NC}"
        return
    fi
    
    echo -e "${RED}=== Dangerous Commands ===${NC}"
    echo ""
    
    while IFS='|' read -r timestamp ssh_host conn_id sess_id user ip command tag; do
        echo -e "${RED}[DANGEROUS]${NC} ${GREEN}$timestamp${NC}"
        echo -e "  User: ${YELLOW}$user${NC} @ ${YELLOW}$ssh_host${NC}"
        echo -e "  Command: ${RED}$command${NC}"
        echo ""
    done < "$DANGEROUS_LOG"
}

show_restricted() {
    if [ ! -f "$RESTRICTED_LOG" ]; then
        echo -e "${YELLOW}No restricted commands logged yet${NC}"
        return
    fi
    
    echo -e "${YELLOW}=== Restricted Commands ===${NC}"
    echo ""
    
    while IFS='|' read -r timestamp ssh_host conn_id sess_id user ip command tag; do
        echo -e "${YELLOW}[RESTRICTED]${NC} ${GREEN}$timestamp${NC}"
        echo -e "  User: ${YELLOW}$user${NC} @ ${YELLOW}$ssh_host${NC}"
        echo -e "  Command: ${YELLOW}$command${NC}"
        echo ""
    done < "$RESTRICTED_LOG"
}

show_recent() {
    check_log_exists
    local count="${1:-20}"
    
    echo -e "${BLUE}=== Last $count Commands ===${NC}"
    echo ""
    
    tail -n "$count" "$LOG_FILE" | while IFS='|' read -r timestamp ssh_host conn_id sess_id user ip command; do
        echo -e "${GREEN}[$timestamp]${NC} ${YELLOW}$user@$ssh_host${NC}: $command"
    done
}

show_concurrent() {
    check_log_exists
    
    echo -e "${BLUE}=== Concurrent Sessions ===${NC}"
    echo ""
    echo "Sessions that had overlapping command execution:"
    echo ""
    
    # This is a simplified check - shows if multiple connection IDs appear in sequence
    awk -F'|' '{
        conn = $3
        if (last_conn != "" && last_conn != conn) {
            concurrent[last_conn] = 1
            concurrent[conn] = 1
        }
        last_conn = conn
    }
    END {
        for (c in concurrent) {
            print c
        }
    }' "$LOG_FILE" | while read conn_id; do
        echo -e "  ${YELLOW}$conn_id${NC}"
    done
}

export_csv() {
    check_log_exists
    local output_file="$1"
    
    if [ -z "$output_file" ]; then
        echo -e "${RED}Error: Output file required${NC}"
        echo "Usage: $0 export <output_file.csv>"
        exit 1
    fi
    
    echo "timestamp,ssh_host,connection_id,session_id,user,ip,command" > "$output_file"
    
    # Convert pipe to comma, but need to handle commands with commas
    sed 's/|/,/g' "$LOG_FILE" >> "$output_file"
    
    echo -e "${GREEN}Exported to: $output_file${NC}"
    echo "Rows: $(wc -l < "$output_file")"
}

show_parsing_examples() {
    cat <<'EOF'
=== Log Parsing Examples ===

The logs use pipe-delimited format:
  timestamp|ssh_host|connection_id|session_id|user|ip|command

Examples:

1. Extract all commands:
   cut -d'|' -f7 /var/log/guacamole/commands/ssh_commands.log

2. Show commands by specific user:
   awk -F'|' '$5 == "root" {print $7}' /var/log/guacamole/commands/ssh_commands.log

3. Count commands per host:
   awk -F'|' '{print $2}' /var/log/guacamole/commands/ssh_commands.log | sort | uniq -c

4. Show all commands from a connection:
   grep '@abc123' /var/log/guacamole/commands/ssh_commands.log

5. Get commands with timestamp:
   awk -F'|' '{print $1, $7}' /var/log/guacamole/commands/ssh_commands.log

6. Find commands containing pattern:
   awk -F'|' '/pattern/ {print $1, $5, $7}' /var/log/guacamole/commands/ssh_commands.log

7. Python parsing:
   import csv
   with open('/var/log/guacamole/commands/ssh_commands.log') as f:
       for line in f:
           ts, host, conn, sess, user, ip, cmd = line.strip().split('|')
           print(f"{user}: {cmd}")

8. Export specific user's commands:
   awk -F'|' '$5 == "admin" {print}' /var/log/guacamole/commands/ssh_commands.log > admin_commands.log

EOF
}

# Main command dispatcher
case "$1" in
    summary)
        show_summary
        ;;
    sessions)
        list_sessions
        ;;
    session)
        show_session "$2"
        ;;
    user)
        show_user_commands "$2"
        ;;
    host)
        show_host_commands "$2"
        ;;
    connection)
        show_connection_commands "$2"
        ;;
    dangerous)
        show_dangerous
        ;;
    restricted)
        show_restricted
        ;;
    recent)
        show_recent "$2"
        ;;
    concurrent)
        show_concurrent
        ;;
    export)
        export_csv "$2"
        ;;
    parse)
        show_parsing_examples
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
