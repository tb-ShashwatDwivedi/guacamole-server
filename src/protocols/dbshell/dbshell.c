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
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/mem.h>
#include <guacamole/protocol.h>
#include <guacamole/recording.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <pty.h>      /* forkpty() — links against -lutil */
#include <unistd.h>

/* Maximum number of arguments we will ever pass to a CLI binary */
#define GUAC_DBSHELL_MAX_ARGS 32

/* Read buffer size for the PTY I/O loop */
#define GUAC_DBSHELL_BUFFER_SIZE 8192

/* -----------------------------------------------------------------------
 * Resolve default port for a given db_type string.
 * ----------------------------------------------------------------------- */

static const char* guac_dbshell_default_port(const char* db_type) {
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_POSTGRESQL) == 0)
        return GUAC_DBSHELL_DEFAULT_PORT_POSTGRESQL;
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_SQLSERVER) == 0)
        return GUAC_DBSHELL_DEFAULT_PORT_SQLSERVER;
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_MONGODB) == 0)
        return GUAC_DBSHELL_DEFAULT_PORT_MONGODB;
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_ORACLE) == 0)
        return GUAC_DBSHELL_DEFAULT_PORT_ORACLE;
    return GUAC_DBSHELL_DEFAULT_PORT_MYSQL;
}

/* -----------------------------------------------------------------------
 * Build the argv array that will be passed to execvp() in the child.
 * The caller MUST free the returned array with guac_dbshell_free_argv().
 * Returns NULL and logs an error if the db_type is not recognised.
 * ----------------------------------------------------------------------- */

static char** guac_dbshell_build_argv(guac_client* client,
        guac_dbshell_settings* settings) {

    const char* port = (settings->port && settings->port[0] != '\0')
                       ? settings->port
                       : guac_dbshell_default_port(settings->db_type);

    /* Allocate a fixed-size argv; the last entry must remain NULL */
    char** argv = guac_mem_zalloc(sizeof(char*) * GUAC_DBSHELL_MAX_ARGS);
    int i = 0;

    /* ---- MySQL / MariaDB ---- */
    if (strcmp(settings->db_type, GUAC_DBSHELL_TYPE_MYSQL) == 0) {
        argv[i++] = strdup("mysql");
        argv[i++] = strdup("-h"); argv[i++] = strdup(settings->hostname);
        argv[i++] = strdup("-P"); argv[i++] = strdup(port);
        if (settings->username && settings->username[0] != '\0') {
            argv[i++] = strdup("-u"); argv[i++] = strdup(settings->username);
        }
        if (settings->password && settings->password[0] != '\0') {
            /* Inline password flag avoids a shell pipe for secrets */
            char pwflag[512];
            snprintf(pwflag, sizeof(pwflag), "-p%s", settings->password);
            argv[i++] = strdup(pwflag);
        }
        if (settings->database && settings->database[0] != '\0')
            argv[i++] = strdup(settings->database);
    }

    /* ---- PostgreSQL ---- */
    else if (strcmp(settings->db_type, GUAC_DBSHELL_TYPE_POSTGRESQL) == 0) {
        argv[i++] = strdup("psql");
        argv[i++] = strdup("-h"); argv[i++] = strdup(settings->hostname);
        argv[i++] = strdup("-p"); argv[i++] = strdup(port);
        if (settings->username && settings->username[0] != '\0') {
            argv[i++] = strdup("-U"); argv[i++] = strdup(settings->username);
        }
        if (settings->database && settings->database[0] != '\0') {
            argv[i++] = strdup("-d"); argv[i++] = strdup(settings->database);
        }
        /*
         * PGPASSWORD is exported in the child's environment (see
         * guac_dbshell_client_thread) so we do not embed it in the argv.
         */
    }

    /* ---- Microsoft SQL Server (sqlcmd) ---- */
    else if (strcmp(settings->db_type, GUAC_DBSHELL_TYPE_SQLSERVER) == 0) {
        argv[i++] = strdup("sqlcmd");
        argv[i++] = strdup("-S");
        /* sqlcmd uses "host,port" notation */
        char server[512];
        snprintf(server, sizeof(server), "%s,%s", settings->hostname, port);
        argv[i++] = strdup(server);
        if (settings->username && settings->username[0] != '\0') {
            argv[i++] = strdup("-U"); argv[i++] = strdup(settings->username);
        }
        if (settings->password && settings->password[0] != '\0') {
            argv[i++] = strdup("-P"); argv[i++] = strdup(settings->password);
        }
        if (settings->database && settings->database[0] != '\0') {
            argv[i++] = strdup("-d"); argv[i++] = strdup(settings->database);
        }
    }

    /* ---- MongoDB ---- */
    else if (strcmp(settings->db_type, GUAC_DBSHELL_TYPE_MONGODB) == 0) {
        argv[i++] = strdup("mongosh");
        argv[i++] = strdup("--host"); argv[i++] = strdup(settings->hostname);
        argv[i++] = strdup("--port"); argv[i++] = strdup(port);
        if (settings->username && settings->username[0] != '\0') {
            argv[i++] = strdup("-u"); argv[i++] = strdup(settings->username);
        }
        if (settings->password && settings->password[0] != '\0') {
            argv[i++] = strdup("-p"); argv[i++] = strdup(settings->password);
        }
        if (settings->database && settings->database[0] != '\0')
            argv[i++] = strdup(settings->database);
    }

    /* ---- Oracle (sqlplus) ---- */
    else if (strcmp(settings->db_type, GUAC_DBSHELL_TYPE_ORACLE) == 0) {
        argv[i++] = strdup("sqlplus");
        /* sqlplus connection string: user/password@host:port/database */
        char connstr[1024];
        snprintf(connstr, sizeof(connstr), "%s/%s@%s:%s/%s",
                 settings->username  ? settings->username  : "",
                 settings->password  ? settings->password  : "",
                 settings->hostname,
                 port,
                 settings->database ? settings->database : "");
        argv[i++] = strdup(connstr);
    }

    else {
        guac_client_log(client, GUAC_LOG_ERROR,
                "Unsupported db-type: \"%s\".", settings->db_type);
        guac_mem_free(argv);
        return NULL;
    }

    argv[i] = NULL; /* execvp() sentinel */
    return argv;
}

static void guac_dbshell_free_argv(char** argv) {
    if (argv == NULL)
        return;
    for (int i = 0; argv[i] != NULL; i++)
        free(argv[i]);
    guac_mem_free(argv);
}

/* -----------------------------------------------------------------------
 * Input thread: reads user keystrokes from guac_terminal and writes them
 * to the PTY master, so they reach the CLI subprocess's stdin.
 * ----------------------------------------------------------------------- */

static void* guac_dbshell_input_thread(void* data) {

    guac_client* client         = (guac_client*) data;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;

    char buffer[GUAC_DBSHELL_BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = guac_terminal_read_stdin(
                dbshell->term, buffer, sizeof(buffer))) > 0) {

        /* Forward keystrokes to the CLI subprocess via the PTY master */
        int remaining = bytes_read;
        char* ptr     = buffer;
        while (remaining > 0) {
            int written = write(dbshell->pty_fd, ptr, remaining);
            if (written <= 0)
                goto done;
            remaining -= written;
            ptr       += written;
        }

    }

done:
    return NULL;

}

/* -----------------------------------------------------------------------
 * Main client thread: creates the terminal, forks the CLI, and relays
 * its output to the browser via guac_terminal.
 * ----------------------------------------------------------------------- */

void* guac_dbshell_client_thread(void* data) {

    guac_client* client          = (guac_client*) data;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;
    guac_dbshell_settings* settings = dbshell->settings;

    pthread_t input_thread;
    char buffer[GUAC_DBSHELL_BUFFER_SIZE];

    /* ------------------------------------------------------------------ */
    /* 1. Optionally start session recording                               */
    /* ------------------------------------------------------------------ */

    if (settings->recording_path != NULL) {
        dbshell->recording = guac_recording_create(client,
                settings->recording_path,
                settings->recording_name,
                settings->create_recording_path,
                !settings->recording_exclude_output,
                !settings->recording_exclude_mouse,
                0,   /* touch events not supported */
                settings->recording_include_keys,
                settings->recording_write_existing);
    }

    /* ------------------------------------------------------------------ */
    /* 2. Create the guac_terminal (renders output to the browser)        */
    /* ------------------------------------------------------------------ */

    guac_terminal_options* options = guac_terminal_options_create(
            settings->width, settings->height, settings->resolution);

    options->disable_copy    = settings->disable_copy;
    options->max_scrollback  = settings->max_scrollback;
    options->font_name       = settings->font_name;
    options->font_size       = settings->font_size;
    options->color_scheme    = settings->color_scheme;

    dbshell->term = guac_terminal_create(client, options);
    guac_mem_free(options);

    if (dbshell->term == NULL) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                "Terminal initialisation failed");
        return NULL;
    }

    /* Optionally save a typescript of the raw session output */
    if (settings->typescript_path != NULL) {
        guac_terminal_create_typescript(dbshell->term,
                settings->typescript_path,
                settings->typescript_name,
                settings->create_typescript_path,
                settings->typescript_write_existing);
    }

    /* ------------------------------------------------------------------ */
    /* 3. Build the CLI argv and fork a child on a PTY                    */
    /* ------------------------------------------------------------------ */

    char** argv = guac_dbshell_build_argv(client, settings);
    if (argv == NULL) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                "Unsupported database type");
        return NULL;
    }

    struct winsize ws = {
        .ws_row    = (unsigned short) guac_terminal_get_rows(dbshell->term),
        .ws_col    = (unsigned short) guac_terminal_get_columns(dbshell->term),
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    int master_fd;
    pid_t child_pid = forkpty(&master_fd, NULL, NULL, &ws);

    if (child_pid < 0) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                "forkpty() failed: %s", strerror(errno));
        guac_dbshell_free_argv(argv);
        return NULL;
    }

    /* ------------------------------------------------------------------ */
    /* 4. Child: set environment variables and exec the CLI binary        */
    /* ------------------------------------------------------------------ */

    if (child_pid == 0) {

        /*
         * Export PGPASSWORD for psql so the password never appears in
         * the process argument list (which is world-readable via /proc).
         */
        if (strcmp(settings->db_type, GUAC_DBSHELL_TYPE_POSTGRESQL) == 0
                && settings->password && settings->password[0] != '\0') {
            setenv("PGPASSWORD", settings->password, 1);
        }

        /* TERM must be set so interactive CLIs render correctly */
        setenv("TERM", "xterm-256color", 1);

        execvp(argv[0], argv);

        /* If execvp returns, something went wrong */
        fprintf(stderr, "dbshell: exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* ------------------------------------------------------------------ */
    /* 5. Parent: store PTY fd and child PID, release argv                */
    /* ------------------------------------------------------------------ */

    dbshell->pty_fd    = master_fd;
    dbshell->child_pid = child_pid;
    guac_dbshell_free_argv(argv);

    guac_client_log(client, GUAC_LOG_INFO,
            "dbshell: spawned %s client (pid %d)",
            settings->db_type, (int) child_pid);

    /* Allow the terminal to start rendering immediately */
    guac_terminal_start(dbshell->term);

    /* ------------------------------------------------------------------ */
    /* 6. Start the input thread (browser → PTY)                          */
    /* ------------------------------------------------------------------ */

    if (pthread_create(&input_thread, NULL,
                guac_dbshell_input_thread, (void*) client)) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                "Unable to start input thread");
        return NULL;
    }

    /* ------------------------------------------------------------------ */
    /* 7. Main loop: relay PTY output → guac_terminal (PTY → browser)    */
    /* ------------------------------------------------------------------ */

    struct pollfd pfd = { .fd = master_fd, .events = POLLIN };

    for (;;) {

        int result = poll(&pfd, 1, 1000 /* ms */);

        if (result < 0) {
            /* poll() interrupted by a signal — just retry */
            if (errno == EINTR)
                continue;
            break;
        }

        if (result == 0)
            continue; /* timeout — loop again */

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            break; /* PTY hung up — child has exited */

        int bytes_read = read(master_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0)
            break;

        /*
         * Write subprocess output to the terminal.  guac_terminal_write()
         * sends the data over the Guacamole protocol, which is automatically
         * captured by any active guac_recording.
         */
        guac_terminal_write(dbshell->term, buffer, bytes_read);

    }

    /* ------------------------------------------------------------------ */
    /* 8. Teardown: stop the client and join threads                      */
    /* ------------------------------------------------------------------ */

    guac_client_stop(client);
    pthread_join(input_thread, NULL);

    /* Reap the child to avoid zombies */
    waitpid(child_pid, NULL, 0);

    guac_client_log(client, GUAC_LOG_INFO,
            "dbshell: %s session ended.", settings->db_type);
    return NULL;

}
