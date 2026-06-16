#ifndef GUAC_SSH_COMMAND_LOGGER_H
#define GUAC_SSH_COMMAND_LOGGER_H

#include <guacamole/user.h>
#include <guacamole/client.h>
#include <libpq-fe.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

/* Forward declaration */
struct guac_ssh_acl_config;

/* Circular buffer for command building */
#define CMD_BUFFER_SIZE 4096
#define MAX_CMD_LENGTH 1024
#define SESSION_ID_LEN 256
#define CONNECTION_ID_LEN 64

typedef struct command_logger {
    char buffer[CMD_BUFFER_SIZE];
    int buffer_pos;
    char display_buffer[CMD_BUFFER_SIZE];   /* Track what's displayed (for tab completion) */
    int display_pos;
    int cursor_pos;                         /* Current cursor position in buffer */
    int in_tab_completion;                  /* Flag to track if we're in tab completion */
    time_t command_start;

    /* Guacamole user_id — unique protocol-level ID for the user accessing
     * the asset (user->user_id).  Used as the identity column in the log. */
    char guac_user_id[256];

    /* Human-readable Guacamole username (user->info.name) used exclusively
     * for per-user ACL rule resolution. */
    char guac_username[256];

    /* SSH username used to authenticate against the remote host. */
    char ssh_username[256];

    /* Current working directory on the remote host, updated by tracking
     * cd commands.  Logged as execution_path.  Starts at "~". */
    char current_path[1024];

    char remote_ip[64];
    char connection_id[CONNECTION_ID_LEN];  /* Unique per connection from Guacamole */
    char session_id[SESSION_ID_LEN];        /* Detailed session identifier */
    guac_client* client;                    /* Reference to client for logging and ACL */
    struct guac_ssh_acl_config* acl_config; /* ACL configuration */
    char ssh_hostname[256];                 /* SSH hostname for ACL matching */
    char asset_id[64];                      /* Guacamole asset/connection ID for ACL */
    PGconn* db_conn;                        /* PostgreSQL connection for command_logs */
} command_logger;

// Initialize logger for a user
command_logger* guac_ssh_command_logger_create(guac_user* user, const char* username, 
                                                const char* ssh_hostname,
                                                const char* asset_id);

// Log a keystroke (builds command)
void guac_ssh_command_logger_key(command_logger* logger, int keysym, int pressed);

// Process terminal output (for capturing tab completion)
void guac_ssh_command_logger_output(command_logger* logger, const char* data, int length);

// Flush incomplete command
void guac_ssh_command_logger_flush(command_logger* logger);

// Clean up
void guac_ssh_command_logger_free(command_logger* logger);

#endif
