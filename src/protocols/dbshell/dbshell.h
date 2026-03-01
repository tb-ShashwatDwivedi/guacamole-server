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

#ifndef GUAC_DBSHELL_H
#define GUAC_DBSHELL_H

#include <guacamole/client.h>

/**
 * Main client I/O thread entry point.
 *
 * This function:
 *  1. Creates the guac_terminal.
 *  2. Resolves the appropriate CLI binary (mysql, psql, …).
 *  3. Opens a PTY pair via forkpty() and exec()s the CLI in the child.
 *  4. In the parent, reads output from the PTY master and writes it to
 *     guac_terminal; a helper thread reads user keystrokes from guac_terminal
 *     and writes them back to the PTY master.
 *
 * @param data  Pointer to the guac_client for this connection.
 * @return      Always NULL.
 */
void* guac_dbshell_client_thread(void* data);

#endif /* GUAC_DBSHELL_H */
