#ifndef GUAC_DBSHELL_COMMAND_LOGGER_H
#define GUAC_DBSHELL_COMMAND_LOGGER_H

#include "command-acl.h"

#include <guacamole/client.h>
#include <guacamole/user.h>
#include <libpq-fe.h>

#define GUAC_DBSHELL_CMD_LOG_SESSION_ID_LEN 256
#define GUAC_DBSHELL_CMD_LOG_CONNECTION_ID_LEN 64
#define GUAC_DBSHELL_CMD_LOG_EXEC_PATH_LEN 64

typedef struct guac_dbshell_command_logger {

    guac_client* client;
    PGconn* db_conn;

    char guac_user_id[256];
    char guac_username[256];
    char db_username[256];
    char asset_ip[256];
    char execution_path[GUAC_DBSHELL_CMD_LOG_EXEC_PATH_LEN];
    char connection_id[GUAC_DBSHELL_CMD_LOG_CONNECTION_ID_LEN];
    char session_id[GUAC_DBSHELL_CMD_LOG_SESSION_ID_LEN];
    char remote_ip[64];

} guac_dbshell_command_logger;

guac_dbshell_command_logger* guac_dbshell_command_logger_create(
        guac_user* user,
        const char* guac_username,
        const char* db_username,
        const char* hostname,
        const char* db_type,
        guac_ssh_acl_config* acl_config);

void guac_dbshell_command_logger_log(
        guac_dbshell_command_logger* logger,
        const char* command,
        const char* type,
        const char* command_status);

void guac_dbshell_command_logger_flush(
        guac_dbshell_command_logger* logger,
        const char* pending_command);

void guac_dbshell_command_logger_free(guac_dbshell_command_logger* logger);

#endif /* GUAC_DBSHELL_COMMAND_LOGGER_H */
