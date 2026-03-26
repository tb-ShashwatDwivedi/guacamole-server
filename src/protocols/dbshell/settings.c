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

#include "settings.h"

#include <guacamole/mem.h>
#include <guacamole/user.h>

#include <stdlib.h>
#include <string.h>

/* Client plugin arguments — positional order MUST match IDX_* enum below */
const char* GUAC_DBSHELL_CLIENT_ARGS[] = {
    "db-type",
    "hostname",
    "port",
    "username",
    "password",
    "database",
    "read-only",
    "font-name",
    "font-size",
    "color-scheme",
    "width",
    "height",
    "resolution",
    "scrollback",
    "disable-copy",
    "disable-paste",
    "typescript-path",
    "typescript-name",
    "create-typescript-path",
    "typescript-write-existing",
    "recording-path",
    "recording-name",
    "create-recording-path",
    "recording-exclude-output",
    "recording-exclude-mouse",
    "recording-include-keys",
    "recording-write-existing",
    NULL
};

enum DBSHELL_ARGS_IDX {
    IDX_DB_TYPE,
    IDX_HOSTNAME,
    IDX_PORT,
    IDX_USERNAME,
    IDX_PASSWORD,
    IDX_DATABASE,
    IDX_READ_ONLY,
    IDX_FONT_NAME,
    IDX_FONT_SIZE,
    IDX_COLOR_SCHEME,
    IDX_WIDTH,
    IDX_HEIGHT,
    IDX_RESOLUTION,
    IDX_SCROLLBACK,
    IDX_DISABLE_COPY,
    IDX_DISABLE_PASTE,
    IDX_TYPESCRIPT_PATH,
    IDX_TYPESCRIPT_NAME,
    IDX_CREATE_TYPESCRIPT_PATH,
    IDX_TYPESCRIPT_WRITE_EXISTING,
    IDX_RECORDING_PATH,
    IDX_RECORDING_NAME,
    IDX_CREATE_RECORDING_PATH,
    IDX_RECORDING_EXCLUDE_OUTPUT,
    IDX_RECORDING_EXCLUDE_MOUSE,
    IDX_RECORDING_INCLUDE_KEYS,
    IDX_RECORDING_WRITE_EXISTING,
    DBSHELL_ARGS_COUNT
};

guac_dbshell_settings* guac_dbshell_parse_args(guac_user* user,
        int argc, const char** argv) {

    if (argc != DBSHELL_ARGS_COUNT) {
        guac_user_log(user, GUAC_LOG_WARNING,
                "Wrong number of arguments: expected %i, got %i.",
                DBSHELL_ARGS_COUNT, argc);
        return NULL;
    }

    guac_dbshell_settings* settings =
        guac_mem_zalloc(sizeof(guac_dbshell_settings));

    settings->db_type = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_DB_TYPE, GUAC_DBSHELL_TYPE_MYSQL);

    settings->hostname = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_HOSTNAME, "localhost");

    settings->port = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_PORT, "");

    settings->username = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_USERNAME, "");

    settings->password = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_PASSWORD, "");

    settings->database = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_DATABASE, "");

    settings->read_only = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_READ_ONLY, false);

    settings->font_name = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_FONT_NAME,
            GUAC_DBSHELL_DEFAULT_FONT_NAME);

    settings->font_size = guac_user_parse_args_int(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_FONT_SIZE,
            GUAC_DBSHELL_DEFAULT_FONT_SIZE);

    settings->color_scheme = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_COLOR_SCHEME,
            GUAC_DBSHELL_DEFAULT_COLOR_SCHEME);

    settings->width = guac_user_parse_args_int(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_WIDTH, 1024);

    settings->height = guac_user_parse_args_int(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_HEIGHT, 768);

    settings->resolution = guac_user_parse_args_int(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RESOLUTION, 96);

    settings->max_scrollback = guac_user_parse_args_int(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_SCROLLBACK,
            GUAC_DBSHELL_DEFAULT_SCROLLBACK);

    settings->disable_copy = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_DISABLE_COPY, false);

    settings->disable_paste = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_DISABLE_PASTE, false);

    settings->typescript_path = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_TYPESCRIPT_PATH, NULL);

    settings->typescript_name = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_TYPESCRIPT_NAME,
            GUAC_DBSHELL_DEFAULT_TYPESCRIPT_NAME);

    settings->create_typescript_path = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_CREATE_TYPESCRIPT_PATH, false);

    settings->typescript_write_existing = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_TYPESCRIPT_WRITE_EXISTING,
            false);

    settings->recording_path = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RECORDING_PATH, NULL);

    settings->recording_name = guac_user_parse_args_string(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RECORDING_NAME,
            GUAC_DBSHELL_DEFAULT_RECORDING_NAME);

    settings->create_recording_path = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_CREATE_RECORDING_PATH, false);

    settings->recording_exclude_output = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RECORDING_EXCLUDE_OUTPUT,
            false);

    settings->recording_exclude_mouse = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RECORDING_EXCLUDE_MOUSE,
            false);

    settings->recording_include_keys = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RECORDING_INCLUDE_KEYS,
            true);

    settings->recording_write_existing = guac_user_parse_args_boolean(user,
            GUAC_DBSHELL_CLIENT_ARGS, argv, IDX_RECORDING_WRITE_EXISTING,
            false);

    return settings;

}

void guac_dbshell_settings_free(guac_dbshell_settings* settings) {
    if (settings == NULL)
        return;

    guac_mem_free(settings->db_type);
    guac_mem_free(settings->hostname);
    guac_mem_free(settings->port);
    guac_mem_free(settings->username);
    guac_mem_free(settings->password);
    guac_mem_free(settings->database);
    guac_mem_free(settings->font_name);
    guac_mem_free(settings->color_scheme);
    guac_mem_free(settings->typescript_path);
    guac_mem_free(settings->typescript_name);
    guac_mem_free(settings->recording_path);
    guac_mem_free(settings->recording_name);
    guac_mem_free(settings);
}
