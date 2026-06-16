#include "config.h"

#include "command_logger.h"
#include "ssh.h"
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/recording.h>
#include <guacamole/user.h>
#include <guacamole/socket.h>
#include <guacamole/protocol.h>

#include <libssh2.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*
 * Command logger key for thread-local storage
 */
static pthread_key_t command_logger_key;
static int command_logger_key_initialized = 0;

/*
 * Free function for command logger
 */
static void command_logger_free(void* data) {
    if (data) {
        guac_ssh_command_logger_free((command_logger*)data);
    }
}

/*
 * Initialize command logger key
 */
static void command_logger_initialize() {
    if (!command_logger_key_initialized) {
        pthread_key_create(&command_logger_key, command_logger_free);
        command_logger_key_initialized = 1;
    }
}

int guac_ssh_user_mouse_handler(guac_user* user, int x, int y, int mask) {

    guac_client* client = user->client;
    guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
    guac_terminal* term = ssh_client->term;

    /* Skip if terminal not yet ready */
    if (term == NULL)
        return 0;

    /* Report mouse position within recording */
    if (ssh_client->recording != NULL)
        guac_recording_report_mouse(ssh_client->recording, x, y, mask);

    /* Send mouse event */
    guac_terminal_send_mouse(term, user, x, y, mask);
    return 0;
}

int guac_ssh_user_key_handler(guac_user* user, int keysym, int pressed) {

    guac_client* client = user->client;
    guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
    guac_terminal* term = ssh_client->term;

    /* Skip if terminal not yet ready */
    if (term == NULL)
        return 0;

    /* Initialize command logger if needed */
    command_logger_initialize();

    /* Get or create command logger for this user */
    command_logger* logger = (command_logger*) pthread_getspecific(command_logger_key);

    if (logger == NULL) {
        /* Try to get the actual username and hostname from the connection */
        const char* username = "unknown";
        const char* ssh_hostname = "unknown";
        
        /* Get SSH username from settings, then SSH client user structure */
        if (ssh_client && ssh_client->settings && ssh_client->settings->username
                && ssh_client->settings->username[0] != '\0') {
            username = ssh_client->settings->username;
            guac_client_log(client, GUAC_LOG_DEBUG,
                          "Command logger: Got username '%s' from settings", username);
        }
        else if (ssh_client && ssh_client->user && ssh_client->user->username) {
            username = ssh_client->user->username;
            guac_client_log(client, GUAC_LOG_DEBUG, 
                          "Command logger: Got username '%s' from SSH user", username);
        } else {
            guac_client_log(client, GUAC_LOG_DEBUG, 
                          "Command logger: Using default username 'unknown'");
        }
        
        /* Get SSH hostname and asset ID from settings */
        const char* asset_id = NULL;
        if (ssh_client && ssh_client->settings && ssh_client->settings->hostname) {
            ssh_hostname = ssh_client->settings->hostname;
            guac_client_log(client, GUAC_LOG_DEBUG, 
                          "Command logger: Got SSH hostname '%s' from settings", ssh_hostname);
        } else {
            guac_client_log(client, GUAC_LOG_DEBUG, 
                          "Command logger: Using default hostname 'unknown'");
        }

        if (ssh_client && ssh_client->settings && ssh_client->settings->asset_id
                && ssh_client->settings->asset_id[0] != '\0') {
            asset_id = ssh_client->settings->asset_id;
            guac_client_log(client, GUAC_LOG_DEBUG,
                          "Command logger: Got asset ID '%s' from settings", asset_id);
        }
        
        /* Create new logger with username, hostname, and asset ID */
        logger = guac_ssh_command_logger_create(user, username, ssh_hostname, asset_id);
        
        if (logger) {
            pthread_setspecific(command_logger_key, logger);
            guac_client_log(client, GUAC_LOG_INFO, 
                          "Command logging started for user: %s on %s", 
                          username, ssh_hostname);
        }
    }

    /* Log the keystroke (only if pressed, not released) */
    if (logger && pressed) {
        guac_ssh_command_logger_key(logger, keysym, pressed);
    }

    /* Report key state within recording */
    if (ssh_client->recording != NULL)
        guac_recording_report_key(ssh_client->recording,
                keysym, pressed);

    /* Send key */
    guac_terminal_send_key(term, keysym, pressed);
    return 0;
}

int guac_ssh_user_size_handler(guac_user* user, int width, int height) {

    /* Get terminal */
    guac_client* client = user->client;
    guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
    guac_terminal* terminal = ssh_client->term;

    /* Skip if terminal not yet ready */
    if (terminal == NULL)
        return 0;

    /* Resize terminal */
    guac_terminal_resize(terminal, width, height);

    /* Update SSH pty size if connected */
    if (ssh_client->term_channel != NULL) {
        pthread_mutex_lock(&(ssh_client->term_channel_lock));
        libssh2_channel_request_pty_size(ssh_client->term_channel,
                guac_terminal_get_columns(terminal),
                guac_terminal_get_rows(terminal));
        pthread_mutex_unlock(&(ssh_client->term_channel_lock));
    }

    return 0;
}
