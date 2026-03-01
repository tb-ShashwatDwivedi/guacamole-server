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

#ifndef GUAC_DBSHELL_CLIENT_H
#define GUAC_DBSHELL_CLIENT_H

#include "settings.h"
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/recording.h>

#include <pthread.h>

/**
 * Per-connection state for the dbshell protocol plugin.
 * Stored in client->data.
 */
typedef struct guac_dbshell_client {

    /**
     * Parsed connection settings received during handshake.
     */
    guac_dbshell_settings* settings;

    /**
     * The terminal that renders the database CLI output to the browser.
     */
    guac_terminal* term;

    /**
     * File descriptor of the master side of the pseudoterminal (PTY).
     * Input typed by the browser user is written here; output from the
     * database CLI subprocess is read from here.
     */
    int pty_fd;

    /**
     * PID of the forked database CLI subprocess.
     */
    pid_t child_pid;

    /**
     * Main I/O thread that ferries data between the PTY and guac_terminal.
     */
    pthread_t client_thread;

    /**
     * Optional session recording, or NULL when recording is not enabled.
     */
    guac_recording* recording;

} guac_dbshell_client;

/**
 * Free handler invoked when the last user disconnects.
 * Cleans up the terminal, PTY, subprocess, and recording.
 */
int guac_dbshell_client_free_handler(guac_client* client);

#endif /* GUAC_DBSHELL_CLIENT_H */
