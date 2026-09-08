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
#include "command-acl.h"
#include "command_logger.h"
#include "input.h"
#include "settings.h"
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/recording.h>
#include <guacamole/user.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <pty.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * SQL statement buffer helpers
 * ----------------------------------------------------------------------- */

/**
 * Appends a single character to the SQL statement buffer.
 * Silently drops the character if the buffer is full.
 */
static void sql_buf_append(guac_dbshell_client* dbshell, char c) {
    if (dbshell->sql_buffer_pos < GUAC_DBSHELL_SQL_BUFFER_SIZE - 1) {
        dbshell->sql_buffer[dbshell->sql_buffer_pos++] = c;
        dbshell->sql_buffer[dbshell->sql_buffer_pos]   = '\0';
    }
}

/**
 * Removes the last character from the SQL buffer (handles backspace).
 */
static void sql_buf_backspace(guac_dbshell_client* dbshell) {
    if (dbshell->sql_buffer_pos > 0)
        dbshell->sql_buffer[--dbshell->sql_buffer_pos] = '\0';
}

/**
 * Clears the SQL statement buffer entirely (Ctrl+C or after a complete
 * statement has been forwarded).
 */
static void sql_buf_reset(guac_dbshell_client* dbshell) {
    dbshell->sql_buffer_pos = 0;
    dbshell->sql_buffer[0]  = '\0';
}

/* -----------------------------------------------------------------------
 * Statement completion detection
 *
 * Returns true when the accumulated buffer represents a complete, ready-to-
 * execute statement for the given database type.  Completion is declared
 * when the last meaningful (non-whitespace) character matches the
 * db-specific terminator:
 *
 *   mysql / postgresql / sqlserver / oracle / mongodb : ';'
 *   postgresql                                        : '\g' (backslash-g)
 *   sqlserver                                         : lone 'GO' line
 *   oracle                                            : lone '/' line
 *
 * MongoDB statements technically execute on Enter when the JavaScript
 * expression is syntactically complete (balanced braces/parens), but
 * detecting that reliably requires a JS parser.  Using ';' for MongoDB
 * still catches the most dangerous operations (e.g. db.users.drop();).
 * ----------------------------------------------------------------------- */

static bool dbshell_is_complete_statement(const char* db_type,
        const char* buf, int buf_len) {

    if (buf_len <= 0)
        return false;

    /* Locate last non-whitespace byte */
    int end = buf_len - 1;
    while (end >= 0 && (buf[end] == ' '  || buf[end] == '\t' ||
                        buf[end] == '\n' || buf[end] == '\r'))
        end--;

    if (end < 0)
        return false;

    /* All DB types: semicolon always terminates */
    if (buf[end] == ';')
        return true;

    /* PostgreSQL: \g meta-command */
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_POSTGRESQL) == 0
            && end >= 1 && buf[end] == 'g' && buf[end - 1] == '\\')
        return true;

    /* SQL Server (sqlcmd): lone GO line, case-insensitive */
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_SQLSERVER) == 0) {
        int line_start = 0;
        for (int i = end; i >= 0; i--)
            if (buf[i] == '\n') { line_start = i + 1; break; }
        const char* ll = buf + line_start;
        while (*ll == ' ' || *ll == '\t') ll++;
        if (strncasecmp(ll, "go", 2) == 0) {
            const char* after = ll + 2;
            while (*after == ' ' || *after == '\t') after++;
            if (*after == '\0' || *after == '\n' || *after == '\r')
                return true;
        }
    }

    /* Oracle (sqlplus): lone '/' line executes a PL/SQL block */
    if (strcmp(db_type, GUAC_DBSHELL_TYPE_ORACLE) == 0) {
        int line_start = 0;
        for (int i = end; i >= 0; i--)
            if (buf[i] == '\n') { line_start = i + 1; break; }
        const char* ll = buf + line_start;
        while (*ll == ' ' || *ll == '\t') ll++;
        if (*ll == '/' && (ll[1] == '\0' || ll[1] == ' ' ||
                           ll[1] == '\t'  || ll[1] == '\n'))
            return true;
    }

    return false;
}

/* -----------------------------------------------------------------------
 * ACL enforcement
 *
 * Checks the accumulated sql_buffer against the loaded ACL rules.
 * Returns true if the statement is ALLOWED, false if it must be blocked.
 *
 * When blocked:
 *   1. Ctrl+C (0x03) is written directly to the PTY master so the database
 *      CLI cancels its current input line and returns to its prompt.
 *   2. The configured (or default) blocked_message is written to the
 *      Guacamole terminal so the user sees the rejection notice.
 *   3. The SQL buffer is reset.
 * ----------------------------------------------------------------------- */

static bool dbshell_acl_check_and_block(guac_client* client,
        guac_dbshell_client* dbshell) {

    if (dbshell->acl_config == NULL)
        return true; /* no ACL config → allow everything */

    if (dbshell->sql_buffer_pos == 0)
        return true; /* empty buffer → nothing to check */

    guac_dbshell_settings* settings = dbshell->settings;
    const char* hostname   = (settings && settings->hostname)
                             ? settings->hostname : "";
    const char* db_user    = (settings && settings->username)
                             ? settings->username : "";
    const char* guac_user  = (dbshell->guac_username[0] != '\0')
                             ? dbshell->guac_username : NULL;
    const char* asset_id   = (settings && settings->asset_id
                              && settings->asset_id[0] != '\0')
                             ? settings->asset_id : NULL;

    guac_ssh_acl_rule* rule = guac_ssh_acl_get_rule(
            dbshell->acl_config, guac_user, hostname, db_user, asset_id);

    if (rule == NULL)
        return true; /* no matching rule → allow */

    if (guac_ssh_acl_check_command(rule, dbshell->sql_buffer, client))
        return true; /* rule permits the statement */

    /* --- BLOCKED --- */
    const char* msg = (rule->blocked_message != NULL && rule->blocked_message[0] != '\0')
                      ? rule->blocked_message
                      : "\r\n*** SQL command blocked by security policy ***\r\n";

    /* Cancel the current input line in the database CLI via Ctrl+C.
     * Return value intentionally ignored: a failed write simply means the
     * child process has already exited, which is harmless. */
    if (dbshell->pty_fd != -1) {
        char ctrl_c = 0x03;
        ssize_t unused_rc = write(dbshell->pty_fd, &ctrl_c, 1);
        (void) unused_rc;
    }

    /* Display the block notice in the browser terminal */
    guac_terminal_write(dbshell->term, msg, strlen(msg));

    guac_client_log(client, GUAC_LOG_WARNING,
            "dbshell: ACL blocked SQL statement — "
            "guac_user=\"%s\" db_user=\"%s\" host=\"%s\" stmt=\"%.120s\"",
            guac_user  ? guac_user : "(none)",
            db_user,
            hostname,
            dbshell->sql_buffer);

    if (dbshell->cmd_logger != NULL)
        guac_dbshell_command_logger_log(dbshell->cmd_logger,
                dbshell->sql_buffer, "restricted", "restricted");

    sql_buf_reset(dbshell);
    return false;
}

/* -----------------------------------------------------------------------
 * Input event handlers
 * ----------------------------------------------------------------------- */

int guac_dbshell_user_key_handler(guac_user* user, int keysym, int pressed) {

    guac_client*          client  = user->client;
    guac_dbshell_client*  dbshell = (guac_dbshell_client*) client->data;
    guac_terminal*        term    = dbshell->term;

    /* Record key event if session recording is active */
    if (dbshell->recording != NULL)
        guac_recording_report_key(dbshell->recording, keysym, pressed);

    /* Ignore all input until the terminal is ready */
    if (term == NULL)
        return 0;

    /* Only process key-press events; releases go straight through */
    if (!pressed) {
        guac_terminal_send_key(term, keysym, pressed);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* Ctrl+C — reset the SQL buffer and forward the signal                */
    /* ------------------------------------------------------------------ */
    if (keysym == 0x03 || keysym == 0xFF03) {
        sql_buf_reset(dbshell);
        guac_terminal_send_key(term, keysym, pressed);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* Backspace — remove the last character from the SQL buffer          */
    /* ------------------------------------------------------------------ */
    if (keysym == 0xFF08) {
        sql_buf_backspace(dbshell);
        guac_terminal_send_key(term, keysym, pressed);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* Printable ASCII — append to SQL buffer                             */
    /* ------------------------------------------------------------------ */
    if (keysym >= 0x20 && keysym <= 0x7E)
        sql_buf_append(dbshell, (char) keysym);

    /* ------------------------------------------------------------------ */
    /* Enter — ACL check then forward (or suppress if blocked)            */
    /* ------------------------------------------------------------------ */
    if (keysym == 0xFF0D || keysym == 0x0D) {

        /* Append the newline to the buffer before checking */
        sql_buf_append(dbshell, '\n');

        /* Run ACL check; returns false and handles cleanup when blocked */
        if (!dbshell_acl_check_and_block(client, dbshell))
            return 0; /* Enter suppressed — statement blocked */

        guac_dbshell_settings* settings = dbshell->settings;
        if (settings != NULL &&
                dbshell_is_complete_statement(settings->db_type,
                        dbshell->sql_buffer, dbshell->sql_buffer_pos)) {

            if (dbshell->cmd_logger != NULL) {
                const char* cmd_type = guac_ssh_acl_is_dangerous_command(
                        dbshell->acl_config, dbshell->sql_buffer)
                    ? "dangerous" : "normal";
                guac_dbshell_command_logger_log(dbshell->cmd_logger,
                        dbshell->sql_buffer, cmd_type, "executed");
            }

            sql_buf_reset(dbshell);
        }
    }

    /* Forward keystroke to the terminal / PTY */
    guac_terminal_send_key(term, keysym, pressed);
    return 0;

}

int guac_dbshell_user_mouse_handler(guac_user* user, int x, int y, int mask) {

    guac_client*         client  = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;
    guac_terminal*       term    = dbshell->term;

    if (dbshell->recording != NULL)
        guac_recording_report_mouse(dbshell->recording, x, y, mask);

    if (term == NULL)
        return 0;

    guac_terminal_send_mouse(term, user, x, y, mask);
    return 0;

}

int guac_dbshell_user_size_handler(guac_user* user, int width, int height) {

    guac_client*         client  = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;
    guac_terminal*       term    = dbshell->term;

    if (term == NULL)
        return 0;

    /* Resize the guac_terminal rendering layer */
    guac_terminal_resize(term, width, height);

    /* Propagate the new dimensions to the CLI subprocess via PTY ioctl */
    if (dbshell->pty_fd != -1) {
        struct winsize ws = {
            .ws_row    = (unsigned short) guac_terminal_get_rows(term),
            .ws_col    = (unsigned short) guac_terminal_get_columns(term),
            .ws_xpixel = 0,
            .ws_ypixel = 0
        };
        ioctl(dbshell->pty_fd, TIOCSWINSZ, &ws);
    }

    return 0;

}
