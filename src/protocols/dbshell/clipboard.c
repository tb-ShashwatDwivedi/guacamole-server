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
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/stream.h>
#include <guacamole/user.h>

int guac_dbshell_clipboard_handler(guac_user* user, guac_stream* stream,
        char* mimetype) {

    guac_client* client          = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;

    /* Clear any previously buffered clipboard content */
    guac_terminal_clipboard_reset(dbshell->term, mimetype);

    /* Wire up per-stream handlers for the incoming clipboard data */
    stream->blob_handler = guac_dbshell_clipboard_blob_handler;
    stream->end_handler  = guac_dbshell_clipboard_end_handler;

    return 0;

}

int guac_dbshell_clipboard_blob_handler(guac_user* user, guac_stream* stream,
        void* data, int length) {

    guac_client* client          = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;

    /* Append this chunk to the terminal's clipboard buffer */
    guac_terminal_clipboard_append(dbshell->term, data, length);

    return 0;

}

int guac_dbshell_clipboard_end_handler(guac_user* user, guac_stream* stream) {
    /* The guac_terminal handles paste internally; nothing more to do here */
    return 0;
}
