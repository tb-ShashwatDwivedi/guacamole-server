#include "command_logger.h"

#include <guacamole/client.h>
#include <guacamole/user.h>

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t dbshell_log_mutex = PTHREAD_MUTEX_INITIALIZER;

#define GUAC_PROPERTIES_PATH "/etc/guacamole/guacamole.properties"
#define DBSHELL_CMD_CLEAN_BUFFER_SIZE 65536

static char* read_guac_property(const char* key) {
    FILE* f = fopen(GUAC_PROPERTIES_PATH, "r");
    if (!f)
        return NULL;

    char line[512];
    char* result = NULL;
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\0' || *p == '\n')
            continue;

        if (strncmp(p, key, key_len) != 0)
            continue;

        char* after = p + key_len;
        while (*after == ' ' || *after == '\t')
            after++;
        if (*after != ':' && *after != '=')
            continue;

        after++;
        while (*after == ' ' || *after == '\t')
            after++;

        size_t len = strlen(after);
        while (len > 0 && (after[len - 1] == '\n' || after[len - 1] == '\r'
                           || after[len - 1] == ' '))
            after[--len] = '\0';

        result = strdup(after);
        break;
    }

    fclose(f);
    return result;
}

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

    free(host);
    free(port);
    free(dbname);
    free(user);
    free(password);

    PGconn* conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        guac_client_log(client, GUAC_LOG_ERROR,
                "dbshell command_logger: DB connection failed: %s",
                PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }

    guac_client_log(client, GUAC_LOG_DEBUG,
            "dbshell command_logger: PostgreSQL connection established");
    return conn;
}

static void insert_command_log(guac_dbshell_command_logger* logger,
        const char* timestamp_str, const char* command,
        const char* type, const char* status) {

    if (!logger || !logger->db_conn || !command)
        return;

    if (PQstatus(logger->db_conn) != CONNECTION_OK) {
        PQreset(logger->db_conn);
        if (PQstatus(logger->db_conn) != CONNECTION_OK) {
            guac_client_log(logger->client, GUAC_LOG_ERROR,
                    "dbshell command_logger: DB reconnect failed: %s",
                    PQerrorMessage(logger->db_conn));
            return;
        }
    }

    const char* params[10] = {
        timestamp_str,
        logger->asset_ip,
        logger->guac_user_id,
        logger->session_id,
        logger->guac_username,
        logger->execution_path,
        logger->db_username,
        command,
        type,
        status
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
                "dbshell command_logger: INSERT failed (%s): %s",
                type, PQerrorMessage(logger->db_conn));
    }

    PQclear(res);
}

static void generate_session_id(char* session_id, size_t len,
        const char* connection_id, const char* username, const char* ip) {

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
    if (!ip_buffer || buffer_size == 0)
        return;

    strncpy(ip_buffer, "unknown", buffer_size - 1);
    ip_buffer[buffer_size - 1] = '\0';

    if (!user)
        return;

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

    if (user->client)
        guac_client_log(user->client, GUAC_LOG_DEBUG,
                "dbshell command_logger: unable to get real IP");
}

static int is_command_empty(const char* command) {
    if (!command)
        return 1;

    for (const char* p = command; *p != '\0'; p++) {
        if (!isspace((unsigned char) *p))
            return 0;
    }

    return 1;
}

static void clean_command(const char* command, char* clean_cmd, size_t clean_size) {
    size_t clean_pos = 0;

    if (!command || !clean_cmd || clean_size == 0) {
        if (clean_cmd && clean_size > 0)
            clean_cmd[0] = '\0';
        return;
    }

    for (const char* p = command; *p != '\0' && clean_pos < clean_size - 1; p++) {
        if (*p == '\t')
            clean_cmd[clean_pos++] = ' ';
        else if (*p >= 32 || *p == 0)
            clean_cmd[clean_pos++] = *p;
    }

    clean_cmd[clean_pos] = '\0';
}

static void log_command_internal(guac_dbshell_command_logger* logger,
        const char* command, const char* type, const char* command_status) {

    if (!logger || !logger->db_conn || is_command_empty(command))
        return;

    char clean_cmd[DBSHELL_CMD_CLEAN_BUFFER_SIZE];
    clean_command(command, clean_cmd, sizeof(clean_cmd));

    if (is_command_empty(clean_cmd))
        return;

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S%z", tm_info);

    pthread_mutex_lock(&dbshell_log_mutex);
    insert_command_log(logger, timestamp, clean_cmd, type, command_status);
    pthread_mutex_unlock(&dbshell_log_mutex);

    if (strcmp(type, "restricted") == 0) {
        openlog("guacamole", LOG_PID | LOG_CONS, LOG_AUTH);
        syslog(LOG_WARNING,
                "dbshell ACL blocked command: guac_username=%s "
                "db_username=%s ip=%s host=%s cmd=%s",
                logger->guac_username, logger->db_username,
                logger->remote_ip, logger->asset_ip, clean_cmd);
        closelog();
    }
    else if (strcmp(type, "dangerous") == 0) {
        openlog("guacamole", LOG_PID | LOG_CONS, LOG_AUTH);
        syslog(LOG_WARNING,
                "dbshell dangerous command executed: guac_username=%s "
                "db_username=%s ip=%s cmd=%s",
                logger->guac_username, logger->db_username,
                logger->remote_ip, clean_cmd);
        closelog();
    }
}

guac_dbshell_command_logger* guac_dbshell_command_logger_create(
        guac_user* user,
        const char* guac_username,
        const char* db_username,
        const char* hostname,
        const char* db_type,
        guac_ssh_acl_config* acl_config) {

    if (!user || !user->client)
        return NULL;

    guac_dbshell_command_logger* logger =
        (guac_dbshell_command_logger*) calloc(1, sizeof(guac_dbshell_command_logger));

    if (!logger)
        return NULL;

    logger->client = user->client;
    (void) acl_config;

    if (user->user_id && user->user_id[0] != '\0') {
        strncpy(logger->guac_user_id, user->user_id,
                sizeof(logger->guac_user_id) - 1);
        logger->guac_user_id[sizeof(logger->guac_user_id) - 1] = '\0';
    }
    else {
        strcpy(logger->guac_user_id, "unknown");
    }

    if (guac_username && guac_username[0] != '\0') {
        strncpy(logger->guac_username, guac_username,
                sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    }
    else if (user->info.name != NULL && user->info.name[0] != '\0') {
        strncpy(logger->guac_username, user->info.name,
                sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    }
    else {
        strncpy(logger->guac_username, logger->guac_user_id,
                sizeof(logger->guac_username) - 1);
        logger->guac_username[sizeof(logger->guac_username) - 1] = '\0';
    }

    if (db_username && db_username[0] != '\0') {
        strncpy(logger->db_username, db_username,
                sizeof(logger->db_username) - 1);
        logger->db_username[sizeof(logger->db_username) - 1] = '\0';
    }
    else {
        strcpy(logger->db_username, "unknown");
    }

    if (hostname && hostname[0] != '\0') {
        strncpy(logger->asset_ip, hostname,
                sizeof(logger->asset_ip) - 1);
        logger->asset_ip[sizeof(logger->asset_ip) - 1] = '\0';
    }
    else {
        strcpy(logger->asset_ip, "unknown");
    }

    if (db_type && db_type[0] != '\0')
        snprintf(logger->execution_path, sizeof(logger->execution_path),
                "dbshell:%s", db_type);
    else
        strcpy(logger->execution_path, "dbshell:unknown");

    if (user->user_id && user->user_id[0] != '\0') {
        strncpy(logger->connection_id, user->user_id,
                sizeof(logger->connection_id) - 1);
        logger->connection_id[sizeof(logger->connection_id) - 1] = '\0';
    }
    else {
        time_t now = time(NULL);
        pid_t pid = getpid();
        snprintf(logger->connection_id, sizeof(logger->connection_id),
                "%lx-%lx", (unsigned long) now, (unsigned long) pid);
    }

    get_client_ip(user, logger->remote_ip, sizeof(logger->remote_ip));

    generate_session_id(logger->session_id, sizeof(logger->session_id),
            logger->connection_id, logger->guac_username, logger->remote_ip);

    logger->db_conn = connect_to_db(user->client);
    if (!logger->db_conn) {
        guac_client_log(user->client, GUAC_LOG_WARNING,
                "dbshell command_logger: proceeding without DB logging");
    }

    guac_client_log(user->client, GUAC_LOG_INFO,
            "dbshell command_logger: started — "
            "guac_user_id=%s guac_username=%s db_username=%s "
            "host=%s execution_path=%s",
            logger->guac_user_id, logger->guac_username,
            logger->db_username, logger->asset_ip, logger->execution_path);

    return logger;
}

void guac_dbshell_command_logger_log(
        guac_dbshell_command_logger* logger,
        const char* command,
        const char* type,
        const char* command_status) {

    log_command_internal(logger, command, type, command_status);
}

void guac_dbshell_command_logger_flush(
        guac_dbshell_command_logger* logger,
        const char* pending_command) {

    if (!logger || !pending_command || pending_command[0] == '\0')
        return;

    log_command_internal(logger, pending_command, "normal", "incomplete");
}

void guac_dbshell_command_logger_free(guac_dbshell_command_logger* logger) {
    if (!logger)
        return;

    if (logger->db_conn)
        PQfinish(logger->db_conn);

    free(logger);
}
