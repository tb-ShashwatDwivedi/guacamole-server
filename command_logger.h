#ifndef GUAC_SSH_COMMAND_LOGGER_H
#define GUAC_SSH_COMMAND_LOGGER_H

#include <guacamole/user.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

// Circular buffer for command building
#define CMD_BUFFER_SIZE 4096
#define MAX_CMD_LENGTH 1024
#define SESSION_ID_LEN 64

typedef struct command_logger {
    char buffer[CMD_BUFFER_SIZE];
    int buffer_pos;
    time_t command_start;
    char username[256];
    char remote_ip[64];
    char session_id[SESSION_ID_LEN];
    FILE* log_file;
    FILE* alert_file;  // Add this field
} command_logger;

// Initialize logger for a user
command_logger* guac_ssh_command_logger_create(guac_user* user, const char* username);

// Log a keystroke (builds command)
void guac_ssh_command_logger_key(command_logger* logger, int keysym, int pressed);

// Clean up
void guac_ssh_command_logger_free(command_logger* logger);

#endif
