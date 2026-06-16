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

#ifndef GUAC_DBSHELL_SETTINGS_H
#define GUAC_DBSHELL_SETTINGS_H

#include <guacamole/user.h>
#include <stdbool.h>

/**
 * Supported database types for the dbshell protocol.
 */
#define GUAC_DBSHELL_TYPE_MYSQL       "mysql"
#define GUAC_DBSHELL_TYPE_POSTGRESQL  "postgresql"
#define GUAC_DBSHELL_TYPE_SQLSERVER   "sqlserver"
#define GUAC_DBSHELL_TYPE_MONGODB     "mongodb"
#define GUAC_DBSHELL_TYPE_ORACLE      "oracle"

/**
 * Default values.
 */
#define GUAC_DBSHELL_DEFAULT_PORT_MYSQL      "3306"
#define GUAC_DBSHELL_DEFAULT_PORT_POSTGRESQL "5432"
#define GUAC_DBSHELL_DEFAULT_PORT_SQLSERVER  "1433"
#define GUAC_DBSHELL_DEFAULT_PORT_MONGODB    "27017"
#define GUAC_DBSHELL_DEFAULT_PORT_ORACLE     "1521"

#define GUAC_DBSHELL_DEFAULT_FONT_NAME   "monospace"
#define GUAC_DBSHELL_DEFAULT_FONT_SIZE   12
#define GUAC_DBSHELL_DEFAULT_COLOR_SCHEME ""
#define GUAC_DBSHELL_DEFAULT_SCROLLBACK  1000
#define GUAC_DBSHELL_DEFAULT_TYPESCRIPT_NAME "typescript"
#define GUAC_DBSHELL_DEFAULT_RECORDING_NAME  "recording"

/**
 * All settings applicable to a single dbshell connection.
 */
typedef struct guac_dbshell_settings {

    /**
     * The type of database to connect to: "mysql", "postgresql",
     * "sqlserver", "mongodb", or "oracle".
     */
    char* db_type;

    /**
     * The hostname or IP address of the database server.
     */
    char* hostname;

    /**
     * The TCP port to connect to on the database server.
     * If NULL, the default port for the selected db_type is used.
     */
    char* port;

    /**
     * The username to authenticate with.
     */
    char* username;

    /**
     * The password to authenticate with. This value is passed to the
     * CLI client and is intentionally kept in memory only for the
     * duration of process startup.
     */
    char* password;

    /**
     * The name of the database (schema) to connect to, if applicable.
     */
    char* database;

    /**
     * Whether this connection is read-only and user input should be dropped.
     */
    bool read_only;

    /**
     * The name of the font to use within the terminal display.
     */
    char* font_name;

    /**
     * The point size of the font to use within the terminal display.
     */
    int font_size;

    /**
     * The color scheme string accepted by guac_terminal_parse_color_scheme().
     */
    char* color_scheme;

    /**
     * The desired width of the terminal display in pixels.
     */
    int width;

    /**
     * The desired height of the terminal display in pixels.
     */
    int height;

    /**
     * The desired display resolution in DPI.
     */
    int resolution;

    /**
     * The maximum number of rows to keep in the terminal scrollback buffer.
     */
    int max_scrollback;

    /**
     * Whether outbound clipboard access (terminal → client) should be blocked.
     */
    bool disable_copy;

    /**
     * Whether inbound clipboard access (client → terminal) should be blocked.
     */
    bool disable_paste;

    /**
     * Path where session typescripts should be saved, or NULL if disabled.
     */
    char* typescript_path;

    /**
     * Base filename for saved typescripts.
     */
    char* typescript_name;

    /**
     * Whether the typescript directory should be created automatically.
     */
    bool create_typescript_path;

    /**
     * Whether existing typescript files should be appended to.
     */
    bool typescript_write_existing;

    /**
     * Path where screen recordings should be saved, or NULL if disabled.
     */
    char* recording_path;

    /**
     * Base filename for saved screen recordings.
     */
    char* recording_name;

    /**
     * Whether the recording directory should be created automatically.
     */
    bool create_recording_path;

    /**
     * Whether graphical output should be excluded from the session recording.
     */
    bool recording_exclude_output;

    /**
     * Whether mouse state should be excluded from the session recording.
     */
    bool recording_exclude_mouse;

    /**
     * Whether key events should be included in the session recording.
     */
    bool recording_include_keys;

    /**
     * Whether existing recording files should be appended to.
     */
    bool recording_write_existing;

    /**
     * Guacamole connection/asset identifier for per-asset ACL lookup.
     */
    char* asset_id;

} guac_dbshell_settings;

/**
 * NULL-terminated array of accepted client argument names, in positional order.
 * The order here MUST match the IDX_* enum in settings.c.
 */
extern const char* GUAC_DBSHELL_CLIENT_ARGS[];

/**
 * Parses all arguments received during the Guacamole protocol handshake and
 * returns a newly allocated settings object. Returns NULL on parse failure.
 */
guac_dbshell_settings* guac_dbshell_parse_args(guac_user* user,
        int argc, const char** argv);

/**
 * Frees a settings object previously allocated by guac_dbshell_parse_args().
 */
void guac_dbshell_settings_free(guac_dbshell_settings* settings);

#endif /* GUAC_DBSHELL_SETTINGS_H */
