#include "command_logger.h"
#include "command-acl.h"

#include <guacamole/user.h>
#include <guacamole/socket.h>
#include <guacamole/client.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <syslog.h>
#include <stdbool.h>
#include <ctype.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Path to ACL configuration file */
#define ACL_CONFIG_PATH "/etc/guacamole/command-acl.conf"

/* Generate a detailed session ID with timestamp and connection info */
static void generate_session_id(char* session_id, size_t len, const char* connection_id, 
                                  const char* username, const char* ip) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_info);
    
    /* Truncate long usernames and IPs to prevent buffer overflow */
    char safe_username[32];
    char safe_ip[20];
    
    strncpy(safe_username, username, sizeof(safe_username) - 1);
    safe_username[sizeof(safe_username) - 1] = '\0';
    
    strncpy(safe_ip, ip, sizeof(safe_ip) - 1);
    safe_ip[sizeof(safe_ip) - 1] = '\0';
    
    snprintf(session_id, len, "%s_%s_%s_%s", 
             timestamp, connection_id, safe_username, safe_ip);
}

/* Get client IP from Guacamole connection info */
static void get_client_ip(guac_user* user, char* ip_buffer, size_t buffer_size) {
    if (!user || !ip_buffer || buffer_size == 0) {
        if (ip_buffer && buffer_size > 0) {
            strncpy(ip_buffer, "unknown", buffer_size - 1);
            ip_buffer[buffer_size - 1] = '\0';
        }
        return;
    }
    
    /* Default to unknown */
    strncpy(ip_buffer, "unknown", buffer_size - 1);
    ip_buffer[buffer_size - 1] = '\0';
    
    /* Method 1: Try to get from environment variables */
    const char* remote_addr = getenv("GUAC_REMOTE_ADDR");
    if (remote_addr && strlen(remote_addr) > 0 && strcmp(remote_addr, "0.0.0.0") != 0) {
        strncpy(ip_buffer, remote_addr, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return;
    }
    
    /* Method 2: Try standard remote address variables */
    remote_addr = getenv("REMOTE_ADDR");
    if (remote_addr && strlen(remote_addr) > 0 && strcmp(remote_addr, "0.0.0.0") != 0) {
        strncpy(ip_buffer, remote_addr, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return;
    }
    
    /* Method 3: Try to extract from client connection string if available */
    if (user->client && user->client->connection_id) {
        /* Log that we're using connection_id as fallback */
        guac_client_log(user->client, GUAC_LOG_DEBUG, 
                       "Unable to get real IP, using connection ID as reference");
    }
}

command_logger* guac_ssh_command_logger_create(guac_user* user, const char* ssh_username,
                                                const char* ssh_hostname) {
    command_logger* logger = (command_logger*) calloc(1, sizeof(command_logger));
    if (!logger) return NULL;

    /* Store client reference */
    logger->client = user->client;

    /* Initialize buffers */
    logger->buffer[0] = '\0';
    logger->buffer_pos = 0;
    logger->display_buffer[0] = '\0';
    logger->display_pos = 0;
    logger->cursor_pos = 0;
    logger->in_tab_completion = 0;
    logger->command_start = time(NULL);

    /* Guacamole user_id — unique protocol-level ID (e.g. "$0a1b2c...").
     * This is the primary identity shown in ssh_commands.log. */
    if (user->user_id && user->user_id[0] != '\0') {
        strncpy(logger->guac_user_id, user->user_id, sizeof(logger->guac_user_id) - 1);
        logger->guac_user_id[sizeof(logger->guac_user_id) - 1] = '\0';
    } else {
        strcpy(logger->guac_user_id, "unknown");
    }

    /* Human-readable Guacamole username (user->info.name = authenticatedUser
     * .getIdentifier() from TunnelRequestService).  This is the primary
     * identity written to ssh_commands.log.  Falls back to guac_user_id so
     * the log always shows a meaningful value rather than "unknown". */
    if (user->info.name != NULL && user->info.name[0] != '\0') {
        strncpy(logger->guac_username, user->info.name, sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    } else {
        /* Fallback: use the unique user_id so the field is never blank */
        strncpy(logger->guac_username, logger->guac_user_id, sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    }

    /* SSH username used to authenticate against the remote host */
    if (ssh_username && ssh_username[0] != '\0') {
        strncpy(logger->ssh_username, ssh_username, sizeof(logger->ssh_username) - 1);
        logger->ssh_username[sizeof(logger->ssh_username) - 1] = '\0';
    } else {
        strcpy(logger->ssh_username, "unknown");
    }

    /* Keep connection_id (= user_id) for session_id generation */
    if (user->user_id && user->user_id[0] != '\0') {
        strncpy(logger->connection_id, user->user_id, sizeof(logger->connection_id) - 1);
        logger->connection_id[sizeof(logger->connection_id) - 1] = '\0';
    } else {
        time_t now = time(NULL);
        pid_t pid = getpid();
        snprintf(logger->connection_id, sizeof(logger->connection_id),
                 "%lx-%lx", (unsigned long)now, (unsigned long)pid);
    }

    /* Copy SSH hostname */
    if (ssh_hostname && ssh_hostname[0] != '\0') {
        strncpy(logger->ssh_hostname, ssh_hostname, sizeof(logger->ssh_hostname) - 1);
        logger->ssh_hostname[sizeof(logger->ssh_hostname) - 1] = '\0';
    } else {
        strcpy(logger->ssh_hostname, "unknown");
    }

    /* Get client IP */
    get_client_ip(user, logger->remote_ip, sizeof(logger->remote_ip));

    /* Generate session ID using the human-readable guac_username so it is
     * meaningful in the log (e.g. 20260301-110243_admin_192.168.1.10). */
    generate_session_id(logger->session_id, sizeof(logger->session_id),
                        logger->connection_id, logger->guac_username, logger->remote_ip);

    /* Load ACL configuration */
    logger->acl_config = guac_ssh_acl_load_config(ACL_CONFIG_PATH);
    if (logger->acl_config) {
        guac_client_log(user->client, GUAC_LOG_INFO,
                        "ACL configuration loaded for command filtering");
    }

    /* Create log directory if it doesn't exist */
    struct stat st = {0};
    if (stat("/var/log/guacamole/commands", &st) == -1) {
        mkdir("/var/log/guacamole/commands", 0755);
    }

    /* Open main log file */
    logger->log_file = fopen("/var/log/guacamole/commands/ssh_commands.log", "a");
    if (!logger->log_file) {
        if (logger->acl_config) {
            guac_ssh_acl_free_config(logger->acl_config);
        }
        free(logger);
        return NULL;
    }

    /* Open alert log file for dangerous commands */
    logger->alert_file = fopen("/var/log/guacamole/commands/dangerous_commands.log", "a");

    /* Open restricted commands log file */
    logger->restricted_file = fopen("/var/log/guacamole/commands/restricted_commands.log", "a");

    guac_client_log(user->client, GUAC_LOG_INFO,
                    "Command logging started - "
                    "guac_user_id: %s, guac_username: %s, "
                    "ssh_username: %s, IP: %s, SSH Host: %s",
                    logger->guac_user_id, logger->guac_username,
                    logger->ssh_username, logger->remote_ip, logger->ssh_hostname);

    return logger;
}

/* Check if command is dangerous */
static int is_dangerous_command(const char* command) {
    if (!command) return 0;
    
    /* List of dangerous command patterns */
    const char* dangerous_patterns[] = {
        "rm -rf", "rm -rf /", "rm -rf *", 
        "dd if=/dev/zero", "mkfs", "format",
        "chmod 777 /", "chown -R", 
        "kill -9", "pkill", "killall",
        "shutdown", "reboot", "halt",
        "iptables -F", "ufw disable",
        "systemctl stop", "service stop",
        "DROP DATABASE", "DROP TABLE", "DELETE FROM",
        NULL
    };
    
    for (int i = 0; dangerous_patterns[i] != NULL; i++) {
        if (strstr(command, dangerous_patterns[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

void guac_ssh_command_logger_key(command_logger* logger, int keysym, int pressed) {
    if (!logger || !pressed) return;
    
    /* Handle Enter key */
    if (keysym == 0xFF0D || keysym == 0x0D || keysym == 0x0A) {
        pthread_mutex_lock(&log_mutex);
        
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        /* Clean the command buffer - replace tabs with spaces for readability */
        char clean_cmd[CMD_BUFFER_SIZE];
        int clean_pos = 0;
        for (int i = 0; i < logger->buffer_pos && i < CMD_BUFFER_SIZE - 1; i++) {
            if (logger->buffer[i] == '\t') {
                clean_cmd[clean_pos++] = ' ';  // Replace tab with space
            } else if (logger->buffer[i] >= 32 || logger->buffer[i] == 0) {
                clean_cmd[clean_pos++] = logger->buffer[i];
            }
        }
        clean_cmd[clean_pos] = '\0';
        
        /* Skip truly empty commands (only spaces) */
        int is_empty = 1;
        for (int i = 0; i < clean_pos; i++) {
            if (clean_cmd[i] != ' ' && clean_cmd[i] != '\0') {
                is_empty = 0;
                break;
            }
        }
        
        if (!is_empty) {

            /* Determine ACL status before writing the main log line so the
             * STATUS column is available in the single unified write. */
            bool is_restricted = false;
            if (logger->acl_config) {
                /* Use the human-readable Guacamole username for per-user ACL
                 * rule resolution (user > connection > global priority). */
                guac_ssh_acl_rule* rule = guac_ssh_acl_get_rule(
                        logger->acl_config,
                        logger->guac_username,   /* Guacamole web username */
                        logger->ssh_hostname,
                        logger->ssh_username);   /* SSH login username */

                if (rule && !guac_ssh_acl_check_command(rule, clean_cmd, logger->client)) {
                    is_restricted = true;
                }
            }

            const char* status = is_restricted ? "RESTRICTED" : "EXECUTED";

            /* Main log — format:
             * timestamp|ssh_host|guac_username|session_id|ip|command|STATUS
             *
             * guac_username : Guacamole web-UI login name (user->info.name,
             *                 set by TunnelRequestService to authenticatedUser
             *                 .getIdentifier()).  Falls back to user->user_id
             *                 if the client does not send a name.
             * session_id   : composite ID for cross-log correlation
             * STATUS       : EXECUTED or RESTRICTED */
            fprintf(logger->log_file, "%s|%s|%s|%s|%s|%s|%s\n",
                    timestamp,
                    logger->ssh_hostname,
                    logger->guac_username,
                    logger->session_id,
                    logger->remote_ip,
                    clean_cmd,
                    status);
            fflush(logger->log_file);

            if (is_restricted) {
                /* Restricted commands log — includes ssh_username for context */
                if (logger->restricted_file) {
                    fprintf(logger->restricted_file,
                            "%s|%s|%s|%s|%s|%s|%s|RESTRICTED\n",
                            timestamp,
                            logger->ssh_hostname,
                            logger->guac_username,
                            logger->session_id,
                            logger->ssh_username,
                            logger->remote_ip,
                            clean_cmd);
                    fflush(logger->restricted_file);
                }

                openlog("guacamole", LOG_PID | LOG_CONS, LOG_AUTH);
                syslog(LOG_WARNING,
                       "ACL blocked command: guac_username=%s "
                       "ssh_username=%s ip=%s host=%s cmd=%s",
                       logger->guac_username,
                       logger->ssh_username, logger->remote_ip,
                       logger->ssh_hostname, clean_cmd);
                closelog();
            }

            /* Dangerous-command alert log (only for commands that were actually
             * executed — already-restricted commands are already logged above). */
            if (!is_restricted && is_dangerous_command(clean_cmd)) {
                if (logger->alert_file) {
                    fprintf(logger->alert_file,
                            "%s|%s|%s|%s|%s|%s|%s|DANGEROUS\n",
                            timestamp,
                            logger->ssh_hostname,
                            logger->guac_username,
                            logger->session_id,
                            logger->ssh_username,
                            logger->remote_ip,
                            clean_cmd);
                    fflush(logger->alert_file);
                }

                openlog("guacamole", LOG_PID | LOG_CONS, LOG_AUTH);
                syslog(LOG_WARNING,
                       "Dangerous command executed: guac_username=%s "
                       "ssh_username=%s ip=%s cmd=%s",
                       logger->guac_username, logger->ssh_username,
                       logger->remote_ip, clean_cmd);
                closelog();
            }
        }
        
        pthread_mutex_unlock(&log_mutex);
        
        /* Reset buffer */
        memset(logger->buffer, 0, CMD_BUFFER_SIZE);
        logger->buffer_pos = 0;
        return;
    }
    
    /* Handle Backspace */
    if (keysym == 0xFF08) {
        if (logger->buffer_pos > 0) {
            logger->buffer_pos--;
            logger->buffer[logger->buffer_pos] = '\0';
        }
        if (logger->display_pos > 0) {
            logger->display_pos--;
            logger->display_buffer[logger->display_pos] = '\0';
        }
        logger->in_tab_completion = 0;
        return;
    }
    
    /* Handle Tab - Mark for completion tracking */
    if (keysym == 0xFF09) {
        /* Mark that tab was pressed - next output from terminal may be completion */
        logger->in_tab_completion = 1;
        /* Save position before tab for potential completion tracking */
        logger->cursor_pos = logger->buffer_pos;
        return;
    }
    
    /* Handle printable characters */
    if (keysym >= 0x20 && keysym <= 0x7E) {
        if (logger->buffer_pos < CMD_BUFFER_SIZE - 2) {
            logger->buffer[logger->buffer_pos++] = (char)keysym;
            logger->buffer[logger->buffer_pos] = '\0';
        }
        if (logger->display_pos < CMD_BUFFER_SIZE - 2) {
            logger->display_buffer[logger->display_pos++] = (char)keysym;
            logger->display_buffer[logger->display_pos] = '\0';
        }
        /* Any printable key after tab means user is typing, completion done */
        logger->in_tab_completion = 0;
    }
}

/* Process terminal output to capture tab completions */
void guac_ssh_command_logger_output(command_logger* logger, const char* data, int length) {
    if (!logger || !data || length <= 0) return;
    
    /* Only process if we're potentially in tab completion */
    if (!logger->in_tab_completion) return;
    
    /* Look for printable characters that might be completion */
    for (int i = 0; i < length; i++) {
        char c = data[i];
        
        /* Skip control characters except newline/carriage return */
        if (c < 32 && c != '\r' && c != '\n') {
            continue;
        }
        
        /* If we get newline/CR, completion is done */
        if (c == '\r' || c == '\n') {
            logger->in_tab_completion = 0;
            break;
        }
        
        /* Add printable characters to both buffers if in completion */
        if (c >= 32 && c <= 126) {
            /* Add to main buffer (this is the completed command) */
            if (logger->buffer_pos < CMD_BUFFER_SIZE - 2) {
                logger->buffer[logger->buffer_pos++] = c;
                logger->buffer[logger->buffer_pos] = '\0';
            }
            
            /* Also track in display buffer */
            if (logger->display_pos < CMD_BUFFER_SIZE - 2) {
                logger->display_buffer[logger->display_pos++] = c;
                logger->display_buffer[logger->display_pos] = '\0';
            }
        }
    }
}

void guac_ssh_command_logger_flush(command_logger* logger) {
    if (!logger || logger->buffer_pos == 0 || !logger->log_file) return;
    
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    /* Clean command for logging */
    char clean_cmd[CMD_BUFFER_SIZE];
    int clean_pos = 0;
    for (int i = 0; i < logger->buffer_pos && i < CMD_BUFFER_SIZE - 1; i++) {
        if (logger->buffer[i] == '\t') {
            clean_cmd[clean_pos++] = ' ';
        } else if (logger->buffer[i] >= 32 || logger->buffer[i] == 0) {
            clean_cmd[clean_pos++] = logger->buffer[i];
        }
    }
    clean_cmd[clean_pos] = '\0';
    
    fprintf(logger->log_file, "%s|%s|%s|%s|%s|%s|%s\n",
            timestamp,
            logger->ssh_hostname,
            logger->guac_username,
            logger->session_id,
            logger->remote_ip,
            clean_cmd,
            "INCOMPLETE");
    fflush(logger->log_file);
    
    pthread_mutex_unlock(&log_mutex);
    
    /* Reset buffers */
    memset(logger->buffer, 0, CMD_BUFFER_SIZE);
    logger->buffer_pos = 0;
    logger->display_pos = 0;
    logger->in_tab_completion = 0;
}

void guac_ssh_command_logger_free(command_logger* logger) {
    if (!logger) return;
    
    /* Flush any incomplete command */
    if (logger->buffer_pos > 0 && logger->log_file) {
        guac_ssh_command_logger_flush(logger);
    }
    
    /* Free ACL configuration */
    if (logger->acl_config) {
        guac_ssh_acl_free_config(logger->acl_config);
    }
    
    /* Close log files */
    if (logger->log_file) {
        fclose(logger->log_file);
    }
    if (logger->alert_file) {
        fclose(logger->alert_file);
    }
    if (logger->restricted_file) {
        fclose(logger->restricted_file);
    }
    
    /* Free logger */
    free(logger);
}
