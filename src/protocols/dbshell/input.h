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

#ifndef GUAC_DBSHELL_INPUT_H
#define GUAC_DBSHELL_INPUT_H

#include <guacamole/user.h>

/**
 * Handles a keyboard event from the connected user and forwards the
 * keystroke to the guac_terminal, which then buffers it for the input thread
 * to relay to the CLI subprocess.
 */
int guac_dbshell_user_key_handler(guac_user* user, int keysym, int pressed);

/**
 * Handles a mouse event from the connected user, forwarding it to the
 * guac_terminal for scroll-wheel and selection support.
 */
int guac_dbshell_user_mouse_handler(guac_user* user,
        int x, int y, int mask);

/**
 * Handles a terminal resize event, updating the guac_terminal dimensions and
 * sending a SIGWINCH / PTY resize request to the CLI subprocess.
 */
int guac_dbshell_user_size_handler(guac_user* user, int width, int height);

#endif /* GUAC_DBSHELL_INPUT_H */
