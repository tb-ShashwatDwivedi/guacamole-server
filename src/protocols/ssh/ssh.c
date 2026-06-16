/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

 #include "config.h"

 #include "argv.h"
 #include "command-acl.h"
 #include "common-ssh/sftp.h"
 #include "common-ssh/ssh.h"
 #include "settings.h"
 #include "sftp.h"
 #include "ssh.h"
 #include "terminal/terminal.h"
 #include "ttymode.h"
 
 #ifdef ENABLE_SSH_AGENT
 #include "ssh_agent.h"
 #endif
 
 #include <libssh2.h>
 #include <libssh2_sftp.h>
 #include <guacamole/client.h>
 #include <guacamole/mem.h>
 #include <guacamole/recording.h>
 #include <guacamole/socket.h>
 #include <guacamole/timestamp.h>
 #include <guacamole/wol-constants.h>
 #include <guacamole/wol.h>
 #include <openssl/err.h>
 #include <openssl/ssl.h>
 
 #ifdef LIBSSH2_USES_GCRYPT
 #include <gcrypt.h>
 #endif
 
 #include <errno.h>
 #include <netdb.h>
 #include <netinet/in.h>
 #include <poll.h>
 #include <pthread.h>
 #include <stdbool.h>
 #include <stddef.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
#include <strings.h>
#include <sys/socket.h>
 #include <sys/time.h>
 
 /**
  * Produces a new user object containing a username and password or private
  * key, prompting the user as necessary to obtain that information.
  *
  * @param client
  *     The Guacamole client containing any existing user data, as well as the
  *     terminal to use when prompting the user.
  *
  * @return
  *     A new user object containing the user's username and other credentials,
  *     or NULL if fails to import key.
  */
 static guac_common_ssh_user* guac_ssh_get_user(guac_client* client) {
 
     guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
     guac_ssh_settings* settings = ssh_client->settings;
 
     guac_common_ssh_user* user;
 
     /* Get username */
     if (settings->username == NULL)
         settings->username = guac_terminal_prompt(ssh_client->term,
                 "Login as: ", true);
 
     /* Create user object from username */
     user = guac_common_ssh_create_user(settings->username);
 
     /* If key specified, import */
     if (settings->key_base64 != NULL) {
 
         guac_client_log(client, GUAC_LOG_DEBUG,
                 "Attempting private key import (WITHOUT passphrase)");
 
         /* Attempt to read key without passphrase */
         if (guac_common_ssh_user_import_key(user,
                     settings->key_base64, NULL)) {
 
             /* Log failure of initial attempt */
             guac_client_log(client, GUAC_LOG_DEBUG,
                     "Initial import failed: %s",
                     guac_common_ssh_key_error());
 
             guac_client_log(client, GUAC_LOG_DEBUG,
                     "Re-attempting private key import (WITH passphrase)");
 
             /* Prompt for passphrase if missing */
             if (settings->key_passphrase == NULL)
                 settings->key_passphrase =
                     guac_terminal_prompt(ssh_client->term,
                             "Key passphrase: ", false);
 
             /* Reattempt import with passphrase */
             if (guac_common_ssh_user_import_key(user,
                         settings->key_base64,
                         settings->key_passphrase)) {
 
                 /* If still failing, give up */
                 guac_client_abort(client,
                         GUAC_PROTOCOL_STATUS_CLIENT_UNAUTHORIZED,
                         "Auth key import failed: %s",
                         guac_common_ssh_key_error());
 
                 guac_common_ssh_destroy_user(user);
                 return NULL;
 
             }
 
         } /* end decrypt key with passphrase */
 
         /* Success */
         guac_client_log(client, GUAC_LOG_INFO,
                 "Auth key successfully imported.");
 
         /* Import public key, if available. */
         if (settings->public_key_base64 != NULL) {
 
             guac_client_log(client, GUAC_LOG_DEBUG,
                     "Attempting public key import");
 
             /* Attempt to read public key */
             if (guac_common_ssh_user_import_public_key(user,
                         settings->public_key_base64)) {
 
                 /* Public key import fails. */
                 guac_client_abort(client,
                        GUAC_PROTOCOL_STATUS_CLIENT_UNAUTHORIZED,
                        "Auth public key import failed: %s",
                         guac_common_ssh_key_error());
 
                 guac_common_ssh_destroy_user(user);
                 return NULL;
 
             }
 
             /* Success */
             guac_client_log(client, GUAC_LOG_INFO,
                     "Auth public key successfully imported.");
 
         }
 
     } /* end if key given */
 
     /* If available, get password from settings */
     else if (settings->password != NULL) {
         guac_common_ssh_user_set_password(user, settings->password);
     }
 
     /* Clear screen of any prompts */
     guac_terminal_printf(ssh_client->term, "\x1B[H\x1B[J");
 
     return user;
 
 }
 
 /**
  * A function used to generate a terminal prompt to gather additional
  * credentials from the guac_client during a connection, and using
  * the specified string to generate the prompt for the user.
  *
  * @param client
  *     The guac_client object associated with the current connection
  *     where additional credentials are required.
  *
  * @param cred_name
  *     The prompt text to display to the screen when prompting for the
  *     additional credentials.
  *
  * @return
  *     The string of credentials gathered from the user.
  */
 static char* guac_ssh_get_credential(guac_client *client, char* cred_name) {
 
     guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
     return guac_terminal_prompt(ssh_client->term, cred_name, false);
 
 }
 
#define CONFIRM_PROMPT "\r\nAre you sure you want to execute this dangerous command? (yes/no): "
#define CONFIRM_CANCELLED "\r\nCommand cancelled.\r\n"
#define SSH_ACL_CONFIG_PATH "/etc/guacamole/command-acl.conf"

/**
 * Writes a readable blocked-command notice to the terminal, avoiding ANSI
 * clear-line sequences that render as garbage in the Guacamole client.
 */
static void ssh_write_blocked_notice(guac_terminal* term, const char* blocked_msg) {

    const char* msg = blocked_msg;

    if (msg == NULL || msg[0] == '\0')
        msg = GUAC_SSH_ACL_DEFAULT_MESSAGE;

    /* Skip leading whitespace/newlines from config — we add our own break. */
    while (*msg == '\r' || *msg == '\n' || *msg == ' ' || *msg == '\t')
        msg++;

    guac_terminal_write(term, "\r\n", 2);
    guac_terminal_write(term, msg, strlen(msg));
    guac_terminal_write(term, "\r\n", 2);
}

void* ssh_input_thread(void* data) {

    guac_client* client = (guac_client*) data;
    guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
    guac_ssh_settings* settings = ssh_client->settings;

    char buffer[8192];
    int bytes_read;

    /* Command buffer for ACL/dangerous checking */
    char command_buffer[8192] = {0};
    int command_len = 0;
    
    bool dangerous_confirm_enabled = (settings->acl_config != NULL &&
            guac_ssh_acl_require_confirmation(settings->acl_config));
    /* Always intercept commands when ACL config file exists so edits take
     * effect without requiring a full session reconnect. */
    bool cmd_check_enabled = (access(SSH_ACL_CONFIG_PATH, R_OK) == 0)
            || dangerous_confirm_enabled;

    while ((bytes_read = guac_terminal_read_stdin(ssh_client->term, buffer, sizeof(buffer))) > 0) {
        
        /* Handle confirmation mode first (yes/no response) */
        if (ssh_client->confirm_pending) {
            for (int i = 0; i < bytes_read; i++) {
                char c = buffer[i];
                if (c == '\r' || c == '\n') {
                    ssh_client->confirm_response[ssh_client->confirm_response_len] = '\0';
                    /* Trim and compare */
                    char* resp = ssh_client->confirm_response;
                    while (*resp == ' ' || *resp == '\t') resp++;
                    bool is_yes = (strcasecmp(resp, "yes") == 0 || strcasecmp(resp, "y") == 0);
                    bool is_no  = (strcasecmp(resp, "no") == 0  || strcasecmp(resp, "n") == 0);
                    
                    /* Echo newline so response appears on its own line */
                    guac_terminal_write(ssh_client->term, "\r\n", 2);
                    
                    if (is_yes) {
                        /* Command is already in remote buffer - just send Enter */
                        pthread_mutex_lock(&(ssh_client->term_channel_lock));
                        libssh2_channel_write(ssh_client->term_channel, "\n", 1);
                        pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                    } else {
                        /* Cancel: send Ctrl+C to clear remote buffer, then show message */
                        const char ctrl_c = '\x03';
                        pthread_mutex_lock(&(ssh_client->term_channel_lock));
                        libssh2_channel_write(ssh_client->term_channel, &ctrl_c, 1);
                        pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                        guac_terminal_write(ssh_client->term, CONFIRM_CANCELLED,
                                sizeof(CONFIRM_CANCELLED) - 1);
                        if (!is_no) {
                            guac_client_log(client, GUAC_LOG_DEBUG,
                                    "Dangerous command confirmation: invalid response '%s'",
                                    ssh_client->confirm_response);
                        }
                    }
                    
                    ssh_client->confirm_pending = false;
                    ssh_client->confirm_response_len = 0;
                    memset(ssh_client->confirm_response, 0, sizeof(ssh_client->confirm_response));
                } else if (c == 0x7F || c == 0x08) {
                    if (ssh_client->confirm_response_len > 0) {
                        ssh_client->confirm_response[--ssh_client->confirm_response_len] = '\0';
                        /* Erase character from display */
                        guac_terminal_write(ssh_client->term, "\b \b", 3);
                    }
                } else if (ssh_client->confirm_response_len < (int)sizeof(ssh_client->confirm_response) - 1
                        && c >= 0x20 && c <= 0x7E) {
                    ssh_client->confirm_response[ssh_client->confirm_response_len++] = c;
                    ssh_client->confirm_response[ssh_client->confirm_response_len] = '\0';
                    /* Echo character so user sees their input */
                    guac_terminal_write(ssh_client->term, &c, 1);
                }
            }
            if (client->state == GUAC_CLIENT_STOPPING)
                break;
            continue;
        }
        
        /* If command checking enabled, buffer input and check commands */
        if (cmd_check_enabled) {
            for (int i = 0; i < bytes_read; i++) {
                char c = buffer[i];
                
                if (c == '\r' || c == '\n') {
                    command_buffer[command_len] = '\0';
                    bool blocked = false;
                    bool needs_confirm = false;
                    char blocked_notice[512];
                    blocked_notice[0] = '\0';
                    int blocked_cmd_len = command_len;
                    
                    if (command_len > 0) {
                        /* Reload ACL from disk on each command so file
                         * updates apply without reconnecting. */
                        guac_ssh_acl_config* fresh_acl =
                            guac_ssh_acl_load_config(SSH_ACL_CONFIG_PATH);
                        if (fresh_acl != NULL) {
                            guac_ssh_acl_rule* rule = guac_ssh_acl_get_rule(
                                    fresh_acl,
                                    settings->guacamole_username,
                                    settings->hostname,
                                    settings->username,
                                    settings->asset_id);
                            if (rule != NULL
                                    && !guac_ssh_acl_check_command(rule,
                                        command_buffer, client)) {
                                blocked = true;
                                if (rule->blocked_message != NULL) {
                                    strncpy(blocked_notice, rule->blocked_message,
                                            sizeof(blocked_notice) - 1);
                                    blocked_notice[sizeof(blocked_notice) - 1] = '\0';
                                }
                            }

                            /* Dangerous command confirmation */
                            if (!blocked && dangerous_confirm_enabled
                                    && guac_ssh_acl_is_dangerous_command(
                                        fresh_acl, command_buffer)) {
                                needs_confirm = true;
                            }

                            guac_ssh_acl_free_config(fresh_acl);
                        }
                        else if (!blocked && dangerous_confirm_enabled
                                && settings->acl_config != NULL
                                && guac_ssh_acl_is_dangerous_command(
                                    settings->acl_config, command_buffer)) {
                            needs_confirm = true;
                        }
                    }
                    
                    if (blocked) {
                        /* Erase the typed command on the remote shell using
                         * backspaces — never send Enter or control chars. */
                        if (blocked_cmd_len > 0) {
                            char bs = (char) settings->backspace;
                            pthread_mutex_lock(&(ssh_client->term_channel_lock));
                            for (int j = 0; j < blocked_cmd_len; j++)
                                libssh2_channel_write(ssh_client->term_channel,
                                        &bs, 1);
                            pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                        }

                        ssh_write_blocked_notice(ssh_client->term,
                                blocked_notice[0] != '\0' ? blocked_notice : NULL);
                        guac_client_log(client, GUAC_LOG_WARNING,
                                "Blocked command for user %s: %s",
                                settings->guacamole_username ?
                                    settings->guacamole_username : "unknown",
                                command_buffer);
                    } else if (needs_confirm) {
                        /* Keep command visible - append prompt on new line.
                         * Command remains in remote buffer (we never sent Enter).
                         * User sees: command\nAre you sure... (yes/no): */
                        guac_terminal_write(ssh_client->term, CONFIRM_PROMPT,
                                sizeof(CONFIRM_PROMPT) - 1);
                        strncpy(ssh_client->confirm_command, command_buffer,
                                sizeof(ssh_client->confirm_command) - 1);
                        ssh_client->confirm_command[sizeof(ssh_client->confirm_command) - 1] = '\0';
                        ssh_client->confirm_pending = true;
                        ssh_client->confirm_response_len = 0;
                        memset(ssh_client->confirm_response, 0, sizeof(ssh_client->confirm_response));
                    } else {
                        /* Command allowed - send Enter to SSH */
                        pthread_mutex_lock(&(ssh_client->term_channel_lock));
                        libssh2_channel_write(ssh_client->term_channel, &c, 1);
                        pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                    }
                    
                    command_len = 0;
                    memset(command_buffer, 0, sizeof(command_buffer));
                } else if (c == 0x7F || c == 0x08) {
                    if (command_len > 0)
                        command_buffer[--command_len] = '\0';
                    pthread_mutex_lock(&(ssh_client->term_channel_lock));
                    libssh2_channel_write(ssh_client->term_channel, &c, 1);
                    pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                } else {
                    if (command_len < (int)sizeof(command_buffer) - 1)
                        command_buffer[command_len++] = c;
                    pthread_mutex_lock(&(ssh_client->term_channel_lock));
                    libssh2_channel_write(ssh_client->term_channel, &c, 1);
                    pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                }
            }
        } else {
            pthread_mutex_lock(&(ssh_client->term_channel_lock));
            libssh2_channel_write(ssh_client->term_channel, buffer, bytes_read);
            pthread_mutex_unlock(&(ssh_client->term_channel_lock));
        }

        if (client->state == GUAC_CLIENT_STOPPING)
            break;
    }

    guac_client_stop(client);
    return NULL;

}
 
 void* ssh_client_thread(void* data) {
 
     guac_client* client = (guac_client*) data;
     guac_ssh_client* ssh_client = (guac_ssh_client*) client->data;
     guac_ssh_settings* settings = ssh_client->settings;
 
     char buffer[8192];
 
     pthread_t input_thread;
 
     /* If Wake-on-LAN is enabled, attempt to wake. */
     if (settings->wol_send_packet) {
 
         /**
          * If wait time is set, send the wake packet and try to connect to the
          * server, failing if the server does not respond.
          */
         if (settings->wol_wait_time > 0) {
             guac_client_log(client, GUAC_LOG_DEBUG, "Sending Wake-on-LAN packet, "
                     "and pausing for %d seconds.", settings->wol_wait_time);
 
             /* Send the Wake-on-LAN request and wait until the server is responsive. */
             if (guac_wol_wake_and_wait(settings->wol_mac_addr,
                     settings->wol_broadcast_addr,
                     settings->wol_udp_port,
                     settings->wol_wait_time,
                     GUAC_WOL_DEFAULT_CONNECT_RETRIES,
                     settings->hostname,
                     settings->port,
                     settings->timeout)) {
                 guac_client_log(client, GUAC_LOG_ERROR, "Failed to send WOL packet or connect to remote server.");
                 return NULL;
             }
         }
 
         /* Just send the packet and continue the connection, or return if failed. */
         else if(guac_wol_wake(settings->wol_mac_addr,
                     settings->wol_broadcast_addr,
                     settings->wol_udp_port)) {
             guac_client_log(client, GUAC_LOG_ERROR, "Failed to send WOL packet.");
             return NULL;
         }
     }
 
     /* Init SSH base libraries */
     if (guac_common_ssh_init(client)) {
         guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                 "SSH library initialization failed");
         return NULL;
     }
 
     char ssh_ttymodes[GUAC_SSH_TTYMODES_SIZE(1)];
 
     /* Set up screen recording, if requested */
     if (settings->recording_path != NULL) {
         ssh_client->recording = guac_recording_create(client,
                 settings->recording_path,
                 settings->recording_name,
                 settings->create_recording_path,
                 !settings->recording_exclude_output,
                 !settings->recording_exclude_mouse,
                 0, /* Touch events not supported */
                 settings->recording_include_keys,
                 settings->recording_write_existing);
     }
 
     /* Create terminal options with required parameters */
     guac_terminal_options* options = guac_terminal_options_create(
             settings->width, settings->height, settings->resolution);
 
     /* Set optional parameters */
     options->disable_copy = settings->disable_copy;
     options->max_scrollback = settings->max_scrollback;
     options->font_name = settings->font_name;
     options->font_size = settings->font_size;
     options->color_scheme = settings->color_scheme;
     options->backspace = settings->backspace;
 
     /* Create terminal */
     ssh_client->term = guac_terminal_create(client, options);
 
     /* Free options struct now that it's been used */
     guac_mem_free(options);
 
     /* Fail if terminal init failed */
     if (ssh_client->term == NULL) {
         guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                 "Terminal initialization failed");
         return NULL;
     }
 
     /* Send current values of exposed arguments to owner only */
     guac_client_for_owner(client, guac_ssh_send_current_argv, ssh_client);
 
     /* Set up typescript, if requested */
     if (settings->typescript_path != NULL) {
         guac_terminal_create_typescript(ssh_client->term,
                 settings->typescript_path,
                 settings->typescript_name,
                 settings->create_typescript_path,
                 settings->typescript_write_existing);
     }
 
     /* Get user and credentials */
     ssh_client->user = guac_ssh_get_user(client);
     if (ssh_client->user == NULL) {
         /* Already aborted within guac_ssh_get_user() */
         return NULL;
     }
 
     /* Ensure connection is kept alive during lengthy connects */
     guac_socket_require_keep_alive(client->socket);
 
     /* Open SSH session */
     ssh_client->session = guac_common_ssh_create_session(client,
             settings->hostname, settings->port, ssh_client->user,
             settings->timeout, settings->server_alive_interval,
             settings->host_key, guac_ssh_get_credential);
     if (ssh_client->session == NULL) {
         /* Already aborted within guac_common_ssh_create_session() */
         return NULL;
     }
 
     pthread_mutex_init(&ssh_client->term_channel_lock, NULL);
 
     /* Open channel for terminal */
     ssh_client->term_channel =
         libssh2_channel_open_session(ssh_client->session->session);
     if (ssh_client->term_channel == NULL) {
         guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_ERROR,
                 "Unable to open terminal channel.");
         return NULL;
     }
 
     /* Set the client timezone */
     if (settings->timezone != NULL) {
         if (libssh2_channel_setenv(ssh_client->term_channel, "TZ",
                     settings->timezone)) {
             guac_client_log(client, GUAC_LOG_WARNING,
                     "Unable to set the timezone: SSH server "
                     "refused to set \"TZ\" variable.");
         }
     }
 
 #ifdef ENABLE_SSH_AGENT
     /* Start SSH agent forwarding, if enabled */
     if (ssh_client->enable_agent) {
         libssh2_session_callback_set(ssh_client->session,
                 LIBSSH2_CALLBACK_AUTH_AGENT, (void*) ssh_auth_agent_callback);
 
         /* Request agent forwarding */
         if (libssh2_channel_request_auth_agent(ssh_client->term_channel))
             guac_client_log(client, GUAC_LOG_ERROR, "Agent forwarding request failed");
         else
             guac_client_log(client, GUAC_LOG_INFO, "Agent forwarding enabled.");
     }
 
     ssh_client->auth_agent = NULL;
 #endif
 
     /* Start SFTP session as well, if enabled */
     if (settings->enable_sftp) {
 
         /* Create SSH session specific for SFTP */
         guac_client_log(client, GUAC_LOG_DEBUG, "Reconnecting for SFTP...");
         ssh_client->sftp_session =
             guac_common_ssh_create_session(client, settings->hostname,
                     settings->port, ssh_client->user, settings->timeout,
                     settings->server_alive_interval, settings->host_key, NULL);
         if (ssh_client->sftp_session == NULL) {
             /* Already aborted within guac_common_ssh_create_session() */
             return NULL;
         }
 
         /* Request SFTP */
         ssh_client->sftp_filesystem = guac_common_ssh_create_sftp_filesystem(
                     ssh_client->sftp_session, settings->sftp_root_directory,
                     NULL, settings->sftp_disable_download,
                     settings->sftp_disable_upload);
 
         /* Expose filesystem to connection owner */
         guac_client_for_owner(client,
                 guac_common_ssh_expose_sftp_filesystem,
                 ssh_client->sftp_filesystem);
 
         /* Init handlers for Guacamole-specific console codes */
         if (!settings->sftp_disable_upload)
             guac_terminal_set_upload_path_handler(ssh_client->term,
                     guac_sftp_set_upload_path);
 
         if (!settings->sftp_disable_download)
             guac_terminal_set_file_download_handler(ssh_client->term,
                     guac_sftp_download_file);
 
         guac_client_log(client, GUAC_LOG_DEBUG, "SFTP session initialized");
 
     }
 
     /* Set up the ttymode array prior to requesting the PTY */
     int ttymodeBytes = guac_ssh_ttymodes_init(ssh_ttymodes,
             GUAC_SSH_TTY_OP_VERASE, settings->backspace, GUAC_SSH_TTY_OP_END);
     if (ttymodeBytes < 1)
         guac_client_log(client, GUAC_LOG_WARNING, "Unable to set TTY modes."
                 "  Backspace may not work as expected.");
 
     /* Request PTY */
     int term_height = guac_terminal_get_rows(ssh_client->term);
     int term_width = guac_terminal_get_columns(ssh_client->term);
     if (libssh2_channel_request_pty_ex(ssh_client->term_channel,
             settings->terminal_type, strlen(settings->terminal_type),
             ssh_ttymodes, ttymodeBytes, term_width, term_height, 0, 0)) {
         guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_ERROR, "Unable to allocate PTY.");
         return NULL;
     }
 
     /* Forward specified locale */
     if (settings->locale != NULL) {
         if (libssh2_channel_setenv(ssh_client->term_channel, "LANG",
                     settings->locale)) {
             guac_client_log(client, GUAC_LOG_WARNING,
                     "Unable to forward locale: SSH server refused to set "
                     "\"LANG\" environment variable.");
         }
     }
 
     /* If a command is specified, run that instead of a shell */
     if (settings->command != NULL) {
         if (libssh2_channel_exec(ssh_client->term_channel, settings->command)) {
             guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_ERROR,
                     "Unable to execute command.");
             return NULL;
         }
     }
 
     /* Otherwise, request a shell */
     else if (libssh2_channel_shell(ssh_client->term_channel)) {
         guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_ERROR,
                 "Unable to associate shell with PTY.");
         return NULL;
     }
 
     /* Logged in */
     guac_client_log(client, GUAC_LOG_INFO, "SSH connection successful.");
     guac_terminal_start(ssh_client->term);
 
     /* Start input thread */
     if (pthread_create(&(input_thread), NULL, ssh_input_thread, (void*) client)) {
         guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR, "Unable to start input thread");
         return NULL;
     }
 
     /* Set non-blocking */
     libssh2_session_set_blocking(ssh_client->session->session, 0);
 
     /* While data available, write to terminal */
     int bytes_read = 0;
     for (;;) {
 
         /* Track total amount of data read */
         int total_read = 0;
 
         /* Timeout for polling socket activity */
         int timeout;
 
         pthread_mutex_lock(&(ssh_client->term_channel_lock));
 
         /* Stop reading at EOF */
         if (libssh2_channel_eof(ssh_client->term_channel)) {
             pthread_mutex_unlock(&(ssh_client->term_channel_lock));
             break;
         }
 
         /* Client is stopping, break the loop */
         if (client->state == GUAC_CLIENT_STOPPING) {
             pthread_mutex_unlock(&(ssh_client->term_channel_lock));
             break;
         }
 
         /* Send keepalive at configured interval */
         if (settings->server_alive_interval > 0) {
             timeout = 0;
             if (libssh2_keepalive_send(ssh_client->session->session, &timeout) > 0) {
                 pthread_mutex_unlock(&(ssh_client->term_channel_lock));
                 break;
             }
             timeout *= 1000;
         }
         /* If keepalive is not configured, sleep for the default of 1 second */
         else
             timeout = GUAC_SSH_DEFAULT_POLL_TIMEOUT;
 
         /* Read terminal data */
         bytes_read = libssh2_channel_read(ssh_client->term_channel,
                 buffer, sizeof(buffer));
 
         pthread_mutex_unlock(&(ssh_client->term_channel_lock));
 
         /* Attempt to write data received. Exit on failure. */
         if (bytes_read > 0) {
             int written = guac_terminal_write(ssh_client->term, buffer, bytes_read);
             if (written < 0)
                 break;
 
             total_read += bytes_read;
         }
 
         else if (bytes_read < 0 && bytes_read != LIBSSH2_ERROR_EAGAIN)
             break;
 
 #ifdef ENABLE_SSH_AGENT
         /* If agent open, handle any agent packets */
         if (ssh_client->auth_agent != NULL) {
             bytes_read = ssh_auth_agent_read(ssh_client->auth_agent);
             if (bytes_read > 0)
                 total_read += bytes_read;
             else if (bytes_read < 0 && bytes_read != LIBSSH2_ERROR_EAGAIN)
                 ssh_client->auth_agent = NULL;
         }
 #endif
 
         /* Wait for more data if reads turn up empty */
         if (total_read == 0) {
 
             /* Wait on the SSH session file descriptor only */
             struct pollfd fds[] = {{
                 .fd      = ssh_client->session->fd,
                 .events  = POLLIN,
                 .revents = 0,
             }};
 
             /* Wait up to computed timeout */
             if (poll(fds, 1, timeout) < 0)
                 break;
 
         }
 
     }
 
     /* Kill client and Wait for input thread to die */
     guac_client_stop(client);
     pthread_join(input_thread, NULL);
 
     pthread_mutex_destroy(&ssh_client->term_channel_lock);
 
     guac_client_log(client, GUAC_LOG_INFO, "SSH connection ended.");
     return NULL;
 
 }