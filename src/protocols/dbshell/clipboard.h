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

#ifndef GUAC_DBSHELL_CLIPBOARD_H
#define GUAC_DBSHELL_CLIPBOARD_H

#include <guacamole/stream.h>
#include <guacamole/user.h>

/**
 * Clipboard stream handler — invoked when the browser sends clipboard data
 * to the terminal.  Resets the terminal clipboard and wires up blob/end
 * handlers on the stream.
 */
int guac_dbshell_clipboard_handler(guac_user* user, guac_stream* stream,
        char* mimetype);

/**
 * Clipboard blob handler — appends a chunk of clipboard data received from
 * the browser to the terminal's internal clipboard buffer.
 */
int guac_dbshell_clipboard_blob_handler(guac_user* user, guac_stream* stream,
        void* data, int length);

/**
 * Clipboard end handler — called when the browser finishes sending clipboard
 * data.  No action is needed; the terminal handles the data internally.
 */
int guac_dbshell_clipboard_end_handler(guac_user* user, guac_stream* stream);

#endif /* GUAC_DBSHELL_CLIPBOARD_H */
