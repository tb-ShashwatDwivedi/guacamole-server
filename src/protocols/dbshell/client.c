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
#include "dbshell.h"
#include "settings.h"
#include "user.h"
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/mem.h>
#include <guacamole/recording.h>

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Entry point called by guacd immediately after loading this plugin via
 * dlopen(). Initialises per-connection state and wires up all handlers.
 */
int guac_client_init(guac_client* client) {

    /* Declare accepted connection parameters */
    client->args = GUAC_DBSHELL_CLIENT_ARGS;

    /* Allocate and zero-initialise per-connection state */
    guac_dbshell_client* dbshell_client =
        guac_mem_zalloc(sizeof(guac_dbshell_client));

    dbshell_client->pty_fd   = -1;
    dbshell_client->child_pid = -1;

    client->data = dbshell_client;

    /* Wire up lifecycle handlers */
    client->join_handler         = guac_dbshell_user_join_handler;
    client->join_pending_handler = guac_dbshell_join_pending_handler;
    client->free_handler         = guac_dbshell_client_free_handler;
    client->leave_handler        = guac_dbshell_user_leave_handler;

    return 0;

}

int guac_dbshell_client_free_handler(guac_client* client) {

    guac_dbshell_client* dbshell_client =
        (guac_dbshell_client*) client->data;

    /* Close the PTY master — this causes the child process to receive SIGHUP */
    if (dbshell_client->pty_fd != -1) {
        close(dbshell_client->pty_fd);
        dbshell_client->pty_fd = -1;
    }

    /* Wait for the main I/O thread to finish */
    if (dbshell_client->child_pid != -1)
        pthread_join(dbshell_client->client_thread, NULL);

    /* Free the session recording if one was created */
    if (dbshell_client->recording != NULL)
        guac_recording_free(dbshell_client->recording);

    /* Free the terminal */
    guac_terminal_free(dbshell_client->term);

    /* Free connection settings */
    guac_dbshell_settings_free(dbshell_client->settings);

    guac_mem_free(dbshell_client);
    return 0;

}
