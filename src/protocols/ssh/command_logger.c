#include "command_logger.h"
#include "command-acl.h"

#include <guacamole/user.h>
#include <guacamole/socket.h>
#include <guacamole/client.h>

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

#define ACL_CONFIG_PATH      "/etc/guacamole/command-acl.conf"
#define GUAC_PROPERTIES_PATH "/etc/guacamole/guacamole.properties"

/* -----------------------------------------------------------------------
 * Guacamole properties reader
 * Reads a single key from guacamole.properties (supports both ':' and '='
 * separators).  Returns a malloc'd string the caller must free, or NULL.
 * ---------------------------------------------------------------------- */
static char* read_guac_property(const char* key) {
    FILE* f = fopen(GUAC_PROPERTIES_PATH, "r");
    if (!f) return NULL;

    char line[512];
    char* result = NULL;
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        if (strncmp(p, key, key_len) != 0) continue;

        char* after = p + key_len;
        while (*after == ' ' || *after == '\t') after++;
        if (*after != ':' && *after != '=') continue;

        after++;
        while (*after == ' ' || *after == '\t') after++;

        size_t len = strlen(after);
        while (len > 0 && (after[len-1] == '\n' || after[len-1] == '\r'
                           || after[len-1] == ' '))
            after[--len] = '\0';

        result = strdup(after);
        break;
    }

    fclose(f);
    return result;
}

/* -----------------------------------------------------------------------
 * PostgreSQL connection
 * Opens one connection per command_logger instance (one per SSH session).
 * ---------------------------------------------------------------------- */
static PGconn* connect_to_db(guac_client* client) {
    char* host     = read_guac_property("postgresql-hostname");
    char* port     = read_guac_property("postgresql-port");
    char* dbname   = read_guac_property("postgresql-database");
    char* user     = read_guac_property("postgresql-username");
    char* password = read_guac_property("postgresql-password");

    char conninfo[1024];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s connect_timeout=5",
             host     ? host     : "localhost",
             port     ? port     : "5432",
             dbname   ? dbname   : "guacamole_db",
             user     ? user     : "guacamole",
             password ? password : "");

    free(host); free(port); free(dbname); free(user); free(password);

    PGconn* conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        guac_client_log(client, GUAC_LOG_ERROR,
                        "command_logger: DB connection failed: %s",
                        PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }

    guac_client_log(client, GUAC_LOG_DEBUG,
                    "command_logger: PostgreSQL connection established");
    return conn;
}

/* -----------------------------------------------------------------------
 * Insert one row into command_logs.
 * type:   'normal' | 'dangerous' | 'restricted'
 * status: 'executed' | 'restricted' | 'incomplete'
 * ---------------------------------------------------------------------- */
static void insert_command_log(command_logger* logger,
                                const char* timestamp_str,
                                const char* command,
                                const char* type,
                                const char* status) {
    if (!logger->db_conn) return;

    /* Reconnect if the connection was lost */
    if (PQstatus(logger->db_conn) != CONNECTION_OK) {
        PQreset(logger->db_conn);
        if (PQstatus(logger->db_conn) != CONNECTION_OK) {
            guac_client_log(logger->client, GUAC_LOG_ERROR,
                            "command_logger: DB reconnect failed: %s",
                            PQerrorMessage(logger->db_conn));
            return;
        }
    }

    const char* params[10] = {
        timestamp_str,           /* $1  timestamp                */
        logger->ssh_hostname,    /* $2  asset_ip                 */
        logger->guac_user_id,    /* $3  connection_session_id    */
        logger->session_id,      /* $4  session_id               */
        logger->guac_username,   /* $5  username                 */
        logger->current_path,    /* $6  execution_path           */
        logger->ssh_username,    /* $7  asset_username           */
        command,                 /* $8  command                  */
        type,                    /* $9  type                     */
        status                   /* $10 command_status           */
    };

    const char* sql =
        "INSERT INTO command_logs "
        "(timestamp, asset_ip, connection_session_id, session_id, username, "
        " execution_path, asset_username, command, type, command_status) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) "
        "ON CONFLICT ON CONSTRAINT unique_timestamp_connection_type DO NOTHING";

    PGresult* res = PQexecParams(logger->db_conn, sql,
                                 10, NULL, params, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        guac_client_log(logger->client, GUAC_LOG_ERROR,
                        "command_logger: INSERT failed (%s): %s",
                        type, PQerrorMessage(logger->db_conn));
    }

    PQclear(res);
}

/* -----------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
static void generate_session_id(char* session_id, size_t len,
                                 const char* connection_id,
                                 const char* username, const char* ip) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_info);

    char safe_username[32];
    char safe_ip[20];
    strncpy(safe_username, username, sizeof(safe_username) - 1);
    safe_username[sizeof(safe_username) - 1] = '\0';
    strncpy(safe_ip, ip, sizeof(safe_ip) - 1);
    safe_ip[sizeof(safe_ip) - 1] = '\0';

    snprintf(session_id, len, "%s_%s_%s_%s",
             timestamp, connection_id, safe_username, safe_ip);
}

static void get_client_ip(guac_user* user, char* ip_buffer, size_t buffer_size) {
    if (!user || !ip_buffer || buffer_size == 0) {
        if (ip_buffer && buffer_size > 0) {
            strncpy(ip_buffer, "unknown", buffer_size - 1);
            ip_buffer[buffer_size - 1] = '\0';
        }
        return;
    }

    strncpy(ip_buffer, "unknown", buffer_size - 1);
    ip_buffer[buffer_size - 1] = '\0';

    const char* remote_addr = getenv("GUAC_REMOTE_ADDR");
    if (remote_addr && strlen(remote_addr) > 0
            && strcmp(remote_addr, "0.0.0.0") != 0) {
        strncpy(ip_buffer, remote_addr, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return;
    }

    remote_addr = getenv("REMOTE_ADDR");
    if (remote_addr && strlen(remote_addr) > 0
            && strcmp(remote_addr, "0.0.0.0") != 0) {
        strncpy(ip_buffer, remote_addr, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return;
    }

    if (user->client && user->client->connection_id)
        guac_client_log(user->client, GUAC_LOG_DEBUG,
                        "command_logger: unable to get real IP");
}

/**
 * Checks if command is dangerous. Uses configurable list from acl_config
 * when available, otherwise uses built-in defaults.
 */
static int is_dangerous_command(command_logger* logger, const char* command) {
    if (!command) return 0;
    if (logger->acl_config != NULL)
        return guac_ssh_acl_is_dangerous_command(logger->acl_config, command) ? 1 : 0;
    /* Fallback when no ACL config loaded */
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
    for (int i = 0; dangerous_patterns[i] != NULL; i++)
        if (strstr(command, dangerous_patterns[i]) != NULL)
            return 1;
    return 0;
}

/**
 * Updates logger->current_path by parsing cd commands.
 */
static void update_execution_path(command_logger* logger, const char* cmd) {
    while (isspace((unsigned char)*cmd)) cmd++;
    if (strncmp(cmd, "cd", 2) != 0) return;
    const char* after_cd = cmd + 2;
    if (*after_cd != '\0' && !isspace((unsigned char)*after_cd)) return;
    while (isspace((unsigned char)*after_cd)) after_cd++;

    if (*after_cd == '\0' || strcmp(after_cd, "~") == 0) {
        strcpy(logger->current_path, "~");
        return;
    }
    if (strcmp(after_cd, "-") == 0) return;

    if (after_cd[0] == '/') {
        strncpy(logger->current_path, after_cd,
                sizeof(logger->current_path) - 1);
        logger->current_path[sizeof(logger->current_path) - 1] = '\0';
        return;
    }
    if (after_cd[0] == '~' && after_cd[1] == '/') {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "~/%s", after_cd + 2);
        strncpy(logger->current_path, tmp, sizeof(logger->current_path) - 1);
        logger->current_path[sizeof(logger->current_path) - 1] = '\0';
        return;
    }
    if (strcmp(after_cd, "..") == 0) {
        char* last = strrchr(logger->current_path, '/');
        if (last != NULL && last != logger->current_path)
            *last = '\0';
        else if (strcmp(logger->current_path, "~") != 0)
            strcpy(logger->current_path, "/");
        return;
    }

    /* Relative path */
    {
        size_t cur_len   = strlen(logger->current_path);
        size_t remaining = sizeof(logger->current_path) - cur_len - 1;
        int trailing = (cur_len > 0 &&
                        logger->current_path[cur_len - 1] == '/');
        if (!trailing && remaining > 1) {
            logger->current_path[cur_len]     = '/';
            logger->current_path[cur_len + 1] = '\0';
            remaining--;
        }
        strncat(logger->current_path, after_cd, remaining);
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
command_logger* guac_ssh_command_logger_create(guac_user* user,
                                                const char* ssh_username,
                                                const char* ssh_hostname) {
    command_logger* logger = (command_logger*) calloc(1, sizeof(command_logger));
    if (!logger) return NULL;

    logger->client       = user->client;
    logger->buffer[0]    = '\0';
    logger->buffer_pos   = 0;
    logger->display_buffer[0] = '\0';
    logger->display_pos  = 0;
    logger->cursor_pos   = 0;
    logger->in_tab_completion = 0;
    logger->command_start = time(NULL);

    if (user->user_id && user->user_id[0] != '\0') {
        strncpy(logger->guac_user_id, user->user_id,
                sizeof(logger->guac_user_id) - 1);
        logger->guac_user_id[sizeof(logger->guac_user_id) - 1] = '\0';
    } else {
        strcpy(logger->guac_user_id, "unknown");
    }

    if (user->info.name != NULL && user->info.name[0] != '\0') {
        strncpy(logger->guac_username, user->info.name,
                sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    } else {
        strncpy(logger->guac_username, logger->guac_user_id,
                sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    }

    if (ssh_username && ssh_username[0] != '\0') {
        strncpy(logger->ssh_username, ssh_username,
                sizeof(logger->ssh_username) - 1);
        logger->ssh_username[sizeof(logger->ssh_username) - 1] = '\0';
    } else {
        strcpy(logger->ssh_username, "unknown");
    }

    strcpy(logger->current_path, "~");

    if (user->user_id && user->user_id[0] != '\0') {
        strncpy(logger->connection_id, user->user_id,
                sizeof(logger->connection_id) - 1);
        logger->connection_id[sizeof(logger->connection_id) - 1] = '\0';
    } else {
        time_t now = time(NULL);
        pid_t  pid = getpid();
        snprintf(logger->connection_id, sizeof(logger->connection_id),
                 "%lx-%lx", (unsigned long)now, (unsigned long)pid);
    }

    if (ssh_hostname && ssh_hostname[0] != '\0') {
        strncpy(logger->ssh_hostname, ssh_hostname,
                sizeof(logger->ssh_hostname) - 1);
        logger->ssh_hostname[sizeof(logger->ssh_hostname) - 1] = '\0';
    } else {
        strcpy(logger->ssh_hostname, "unknown");
    }

    get_client_ip(user, logger->remote_ip, sizeof(logger->remote_ip));

    generate_session_id(logger->session_id, sizeof(logger->session_id),
                        logger->connection_id, logger->guac_username,
                        logger->remote_ip);

    logger->acl_config = guac_ssh_acl_load_config(ACL_CONFIG_PATH);
    if (logger->acl_config)
        guac_client_log(user->client, GUAC_LOG_INFO,
                        "command_logger: ACL configuration loaded");

    logger->db_conn = connect_to_db(user->client);
    if (!logger->db_conn) {
        guac_client_log(user->client, GUAC_LOG_WARNING,
                        "command_logger: proceeding without DB logging");
    }

    guac_client_log(user->client, GUAC_LOG_INFO,
                    "command_logger: started — "
                    "guac_user_id=%s guac_username=%s "
                    "ssh_username=%s IP=%s SSH_host=%s",
                    logger->guac_user_id, logger->guac_username,
                    logger->ssh_username, logger->remote_ip,
                    logger->ssh_hostname);

    return logger;
}

void guac_ssh_command_logger_key(command_logger* logger, int keysym,
                                  int pressed) {
    if (!logger || !pressed) return;

    if (keysym == 0xFF0D || keysym == 0x0D || keysym == 0x0A) {
        pthread_mutex_lock(&log_mutex);

        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S%z", tm_info);

        char clean_cmd[CMD_BUFFER_SIZE];
        int  clean_pos = 0;
        for (int i = 0; i < logger->buffer_pos && i < CMD_BUFFER_SIZE - 1; i++) {
            if (logger->buffer[i] == '\t')
                clean_cmd[clean_pos++] = ' ';
            else if (logger->buffer[i] >= 32 || logger->buffer[i] == 0)
                clean_cmd[clean_pos++] = logger->buffer[i];
        }
        clean_cmd[clean_pos] = '\0';

        int is_empty = 1;
        for (int i = 0; i < clean_pos; i++) {
            if (clean_cmd[i] != ' ' && clean_cmd[i] != '\0') {
                is_empty = 0;
                break;
            }
        }

        if (!is_empty) {
            bool is_restricted = false;
            if (logger->acl_config) {
                guac_ssh_acl_rule* rule = guac_ssh_acl_get_rule(
                        logger->acl_config,
                        logger->guac_username,
                        logger->ssh_hostname,
                        logger->ssh_username);
                if (rule && !guac_ssh_acl_check_command(rule, clean_cmd,
                                                         logger->client))
                    is_restricted = true;
            }

            bool is_dangerous = !is_restricted && is_dangerous_command(logger, clean_cmd);

            const char* cmd_type;
            const char* cmd_status;

            if (is_restricted) {
                cmd_type   = "restricted";
                cmd_status = "restricted";
            } else if (is_dangerous) {
                cmd_type   = "dangerous";
                cmd_status = "executed";
            } else {
                cmd_type   = "normal";
                cmd_status = "executed";
            }

            insert_command_log(logger, timestamp, clean_cmd,
                               cmd_type, cmd_status);

            if (!is_restricted)
                update_execution_path(logger, clean_cmd);

            if (is_restricted) {
                openlog("guacamole", LOG_PID | LOG_CONS, LOG_AUTH);
                syslog(LOG_WARNING,
                       "ACL blocked command: guac_username=%s "
                       "ssh_username=%s ip=%s host=%s cmd=%s",
                       logger->guac_username, logger->ssh_username,
                       logger->remote_ip, logger->ssh_hostname, clean_cmd);
                closelog();
            }

            if (is_dangerous) {
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
        memset(logger->buffer, 0, CMD_BUFFER_SIZE);
        logger->buffer_pos = 0;
        return;
    }

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

    if (keysym == 0xFF09) {
        logger->in_tab_completion = 1;
        logger->cursor_pos = logger->buffer_pos;
        return;
    }

    if (keysym >= 0x20 && keysym <= 0x7E) {
        if (logger->buffer_pos < CMD_BUFFER_SIZE - 2) {
            logger->buffer[logger->buffer_pos++] = (char)keysym;
            logger->buffer[logger->buffer_pos]   = '\0';
        }
        if (logger->display_pos < CMD_BUFFER_SIZE - 2) {
            logger->display_buffer[logger->display_pos++] = (char)keysym;
            logger->display_buffer[logger->display_pos]   = '\0';
        }
        logger->in_tab_completion = 0;
    }
}

void guac_ssh_command_logger_output(command_logger* logger,
                                     const char* data, int length) {
    if (!logger || !data || length <= 0) return;
    if (!logger->in_tab_completion) return;
    for (int i = 0; i < length; i++) {
        char c = data[i];
        if (c < 32 && c != '\r' && c != '\n') continue;
        if (c == '\r' || c == '\n') { logger->in_tab_completion = 0; break; }
        if (c >= 32 && c <= 126) {
            if (logger->buffer_pos < CMD_BUFFER_SIZE - 2) {
                logger->buffer[logger->buffer_pos++] = c;
                logger->buffer[logger->buffer_pos]   = '\0';
            }
            if (logger->display_pos < CMD_BUFFER_SIZE - 2) {
                logger->display_buffer[logger->display_pos++] = c;
                logger->display_buffer[logger->display_pos]   = '\0';
            }
        }
    }
}

void guac_ssh_command_logger_flush(command_logger* logger) {
    if (!logger || logger->buffer_pos == 0 || !logger->db_conn) return;

    pthread_mutex_lock(&log_mutex);

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S%z", tm_info);

    char clean_cmd[CMD_BUFFER_SIZE];
    int  clean_pos = 0;
    for (int i = 0; i < logger->buffer_pos && i < CMD_BUFFER_SIZE - 1; i++) {
        if (logger->buffer[i] == '\t')
            clean_cmd[clean_pos++] = ' ';
        else if (logger->buffer[i] >= 32 || logger->buffer[i] == 0)
            clean_cmd[clean_pos++] = logger->buffer[i];
    }
    clean_cmd[clean_pos] = '\0';

    insert_command_log(logger, timestamp, clean_cmd, "normal", "incomplete");

    pthread_mutex_unlock(&log_mutex);

    memset(logger->buffer, 0, CMD_BUFFER_SIZE);
    logger->buffer_pos    = 0;
    logger->display_pos   = 0;
    logger->in_tab_completion = 0;
}

void guac_ssh_command_logger_free(command_logger* logger) {
    if (!logger) return;
    if (logger->buffer_pos > 0 && logger->db_conn)
        guac_ssh_command_logger_flush(logger);
    if (logger->acl_config)
        guac_ssh_acl_free_config(logger->acl_config);
    if (logger->db_conn)
        PQfinish(logger->db_conn);
    free(logger);
}
