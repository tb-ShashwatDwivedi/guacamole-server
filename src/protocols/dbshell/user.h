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

#ifndef GUAC_DBSHELL_USER_H
#define GUAC_DBSHELL_USER_H

#include <guacamole/client.h>
#include <guacamole/user.h>

/**
 * Called when a new user joins the connection.
 *
 * For the connection owner (first user) this function:
 *  - parses the connection arguments supplied during the handshake,
 *  - stores the settings in the per-client struct, and
 *  - spawns the main I/O thread (guac_dbshell_client_thread).
 *
 * For subsequent joining users the terminal display state is synchronised.
 * Input handlers are registered unless the connection is read-only.
 */
int guac_dbshell_user_join_handler(guac_user* user, int argc, char** argv);

/**
 * Called just before pending users are promoted to full users.
 * Synchronises the current terminal state to all pending users.
 */
int guac_dbshell_join_pending_handler(guac_client* client);

/**
 * Called when a user leaves the connection.
 * Removes the user from the terminal and frees their settings copy.
 */
int guac_dbshell_user_leave_handler(guac_user* user);

#endif /* GUAC_DBSHELL_USER_H */
