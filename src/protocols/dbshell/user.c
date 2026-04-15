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

#include "client.h"
#include "clipboard.h"
#include "dbshell.h"
#include "input.h"
#include "settings.h"
#include "terminal/terminal.h"
#include "user.h"

#include <guacamole/client.h>
#include <guacamole/socket.h>
#include <guacamole/user.h>

#include <pthread.h>
#include <string.h>

int guac_dbshell_user_join_handler(guac_user* user, int argc, char** argv) {

    guac_client* client          = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;

    /* Parse the connection arguments provided during handshake */
    guac_dbshell_settings* settings = guac_dbshell_parse_args(user,
            argc, (const char**) argv);

    if (settings == NULL) {
        guac_user_log(user, GUAC_LOG_INFO,
                "dbshell: badly formatted client arguments.");
        return 1;
    }

    /* Store settings on the user so they can be freed on leave */
    user->data = settings;

    /* The connection owner starts the main I/O thread */
    if (user->owner) {
        dbshell->settings = settings;

        /* Capture Guacamole username for ACL rule lookup */
        if (user->info.name != NULL && user->info.name[0] != '\0') {
            strncpy(dbshell->guac_username, user->info.name,
                    sizeof(dbshell->guac_username) - 1);
            dbshell->guac_username[sizeof(dbshell->guac_username) - 1] = '\0';
        }

        if (pthread_create(&dbshell->client_thread, NULL,
                    guac_dbshell_client_thread, (void*) client)) {
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "dbshell: unable to start client thread");
            return 1;
        }
    }

    /* Register input handlers unless the connection is read-only */
    if (!settings->read_only) {
        user->key_handler   = guac_dbshell_user_key_handler;
        user->mouse_handler = guac_dbshell_user_mouse_handler;
        user->size_handler  = guac_dbshell_user_size_handler;

        if (!settings->disable_paste)
            user->clipboard_handler = guac_dbshell_clipboard_handler;
    }

    return 0;

}

int guac_dbshell_join_pending_handler(guac_client* client) {

    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;

    /* Sync current terminal state to all pending (not-yet-promoted) users */
    if (dbshell->term != NULL) {
        guac_socket* broadcast_socket = client->pending_socket;
        guac_terminal_sync_users(dbshell->term, client, broadcast_socket);
        guac_socket_flush(broadcast_socket);
    }

    return 0;

}

int guac_dbshell_user_leave_handler(guac_user* user) {

    guac_dbshell_client* dbshell =
        (guac_dbshell_client*) user->client->data;

    /* Remove the user from the terminal (stops their display updates) */
    if (dbshell->term != NULL)
        guac_terminal_remove_user(dbshell->term, user);

    /* Free the per-user settings copy (owner settings live in client->data) */
    if (!user->owner) {
        guac_dbshell_settings* settings =
            (guac_dbshell_settings*) user->data;
        guac_dbshell_settings_free(settings);
    }

    return 0;

}
