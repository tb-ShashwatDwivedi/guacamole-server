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

#include "command-acl.h"
#include "command_logger.h"
#include "settings.h"
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/recording.h>

#include <pthread.h>

/**
 * Maximum size of the SQL statement accumulation buffer.
 * Covers even the largest practical multi-line SQL statements.
 */
#define GUAC_DBSHELL_SQL_BUFFER_SIZE 65536

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

    /**
     * ACL configuration loaded from /etc/guacamole/command-acl.conf, or NULL
     * if the file is absent or unparseable. When non-NULL, SQL statements are
     * checked against blacklist/whitelist rules before being forwarded to the
     * database CLI subprocess.
     */
    guac_ssh_acl_config* acl_config;

    /**
     * Accumulation buffer for the current multi-line SQL statement.
     * Characters are appended as the user types and cleared after each
     * complete statement (or on Ctrl+C).
     */
    char sql_buffer[GUAC_DBSHELL_SQL_BUFFER_SIZE];

    /**
     * Number of bytes currently held in sql_buffer.
     */
    int sql_buffer_pos;

    /**
     * Guacamole web-interface username of the connecting user. Populated
     * from user->info.name when the owner joins. Used for ACL rule lookup.
     */
    char guac_username[256];

    /**
     * PostgreSQL command audit logger for completed and blocked SQL statements.
     */
    guac_dbshell_command_logger* cmd_logger;

} guac_dbshell_client;

/**
 * Free handler invoked when the last user disconnects.
 * Cleans up the terminal, PTY, subprocess, and recording.
 */
int guac_dbshell_client_free_handler(guac_client* client);

#endif /* GUAC_DBSHELL_CLIENT_H */
