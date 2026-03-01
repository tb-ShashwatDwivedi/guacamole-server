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
#include "input.h"
#include "terminal/terminal.h"

#include <guacamole/client.h>
#include <guacamole/recording.h>
#include <guacamole/user.h>

#include <sys/ioctl.h>
#include <pty.h>

int guac_dbshell_user_key_handler(guac_user* user, int keysym, int pressed) {

    guac_client* client          = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;
    guac_terminal* term          = dbshell->term;

    /* Record key event if recording is active */
    if (dbshell->recording != NULL)
        guac_recording_report_key(dbshell->recording, keysym, pressed);

    /* Ignore input until the terminal is ready */
    if (term == NULL)
        return 0;

    guac_terminal_send_key(term, keysym, pressed);
    return 0;

}

int guac_dbshell_user_mouse_handler(guac_user* user, int x, int y, int mask) {

    guac_client* client          = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;
    guac_terminal* term          = dbshell->term;

    /* Record mouse position in the session recording if active */
    if (dbshell->recording != NULL)
        guac_recording_report_mouse(dbshell->recording, x, y, mask);

    /* Ignore input until the terminal is ready */
    if (term == NULL)
        return 0;

    guac_terminal_send_mouse(term, user, x, y, mask);
    return 0;

}

int guac_dbshell_user_size_handler(guac_user* user, int width, int height) {

    guac_client* client          = user->client;
    guac_dbshell_client* dbshell = (guac_dbshell_client*) client->data;
    guac_terminal* term          = dbshell->term;

    /* Ignore resize until the terminal is ready */
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
