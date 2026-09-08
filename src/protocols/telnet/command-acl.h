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

#ifndef GUAC_SSH_COMMAND_ACL_H
#define GUAC_SSH_COMMAND_ACL_H

#include "config.h"

#include <guacamole/client.h>

#include <stdbool.h>

/**
 * Maximum number of connection-specific and user-specific ACL rules.
 */
#define GUAC_SSH_ACL_MAX_RULES 128

/**
 * Default blocked message if not specified in config.
 */
#define GUAC_SSH_ACL_DEFAULT_MESSAGE "\r\n*** Command blocked by security policy ***\r\n"

/**
 * ACL rule structure containing blacklist, whitelist, and blocked message.
 */
typedef struct guac_ssh_acl_rule {

    /**
     * Comma-separated list of blocked command patterns.
     */
    char* blacklist;

    /**
     * Comma-separated list of allowed command patterns. If set, only these
     * commands are allowed (whitelist mode).
     */
    char* whitelist;

    /**
     * Message to display when a command is blocked.
     */
    char* blocked_message;

} guac_ssh_acl_rule;

/**
 * Asset-specific ACL rule with key.
 */
typedef struct guac_ssh_acl_asset_rule {

    /**
     * Asset identifier (Guacamole connection ID).
     */
    char* asset_id;

    /**
     * The ACL rule for this asset.
     */
    guac_ssh_acl_rule rule;

} guac_ssh_acl_asset_rule;

/**
 * Connection-specific ACL rule with key.
 */
typedef struct guac_ssh_acl_connection_rule {

    /**
     * Key in one of four formats:
     *   "hostname"                          — matches any SSH user on that host
     *   "hostname:sshusername"              — matches a specific SSH login user
     *   "hostname:guacusername"             — matches a specific Guacamole account user
     *   "hostname:username:asset_id"        — matches host + user + asset
     *
     * Lookup priority (highest first):
     *   1. hostname:ssh_username:asset_id
     *   2. hostname:guacamole_username:asset_id
     *   3. asset_id (via asset_rules)
     *   4. hostname:ssh_username
     *   5. hostname:guacamole_username
     *   6. hostname (no username)
     */
    char* key;

    /**
     * The ACL rule for this connection.
     */
    guac_ssh_acl_rule rule;

} guac_ssh_acl_connection_rule;

/**
 * User-specific ACL rule with key.
 */
typedef struct guac_ssh_acl_user_rule {

    /**
     * Guacamole username.
     */
    char* username;

    /**
     * The ACL rule for this user.
     */
    guac_ssh_acl_rule rule;

} guac_ssh_acl_user_rule;

/**
 * Complete ACL configuration loaded from file.
 */
typedef struct guac_ssh_acl_config {

    /**
     * Global ACL rule applied to all connections.
     */
    guac_ssh_acl_rule* global;

    /**
     * Array of connection-specific rules.
     */
    guac_ssh_acl_connection_rule* connection_rules;

    /**
     * Number of connection-specific rules.
     */
    int connection_rule_count;

    /**
     * Array of asset-specific rules.
     */
    guac_ssh_acl_asset_rule* asset_rules;

    /**
     * Number of asset-specific rules.
     */
    int asset_rule_count;

    /**
     * Array of user-specific rules.
     */
    guac_ssh_acl_user_rule* user_rules;

    /**
     * Number of user-specific rules.
     */
    int user_rule_count;

    /**
     * Comma-separated list of dangerous command patterns (global only).
     * If NULL, built-in default patterns are used.
     */
    char* dangerous_commands;

    /**
     * If true, prompt "Are you sure? (yes/no)" before executing dangerous commands.
     */
    bool dangerous_require_confirmation;

} guac_ssh_acl_config;

/**
 * Loads ACL configuration from the specified file.
 *
 * @param config_path
 *     Path to the ACL configuration file (INI format).
 *
 * @return
 *     Pointer to loaded configuration, or NULL if file doesn't exist or
 *     parsing fails. Caller must free with guac_ssh_acl_free_config().
 */
guac_ssh_acl_config* guac_ssh_acl_load_config(const char* config_path);

/**
 * Gets the effective ACL rule for a connection based on priority:
 * user-specific > connection-specific > global.
 *
 * @param config
 *     The loaded ACL configuration.
 *
 * @param guacamole_username
 *     Guacamole web interface username, or NULL.
 *
 * @param hostname
 *     SSH server hostname.
 *
 * @param ssh_username
 *     SSH username.
 *
 * @param asset_id
 *     Guacamole connection/asset identifier, or NULL.
 *
 * @return
 *     Pointer to the applicable rule, or NULL if no rules apply.
 *     The returned pointer is owned by the config and should not be freed.
 */
guac_ssh_acl_rule* guac_ssh_acl_get_rule(guac_ssh_acl_config* config,
        const char* guacamole_username, const char* hostname,
        const char* ssh_username, const char* asset_id);

/**
 * Checks if a command is allowed based on the given ACL rule.
 *
 * @param rule
 *     The ACL rule to check against.
 *
 * @param command
 *     The command string to validate.
 *
 * @param client
 *     The guac_client for logging purposes.
 *
 * @return
 *     true if the command is allowed, false if it should be blocked.
 */
bool guac_ssh_acl_check_command(guac_ssh_acl_rule* rule,
        const char* command, guac_client* client);

/**
 * Checks if a command matches dangerous patterns (configurable or built-in).
 *
 * @param config
 *     The loaded ACL configuration (may be NULL).
 *
 * @param command
 *     The command string to check.
 *
 * @return
 *     true if the command is dangerous, false otherwise.
 */
bool guac_ssh_acl_is_dangerous_command(guac_ssh_acl_config* config,
        const char* command);

/**
 * Returns whether confirmation is required before executing dangerous commands.
 *
 * @param config
 *     The loaded ACL configuration (may be NULL).
 *
 * @return
 *     true if confirmation prompt should be shown, false otherwise.
 */
bool guac_ssh_acl_require_confirmation(guac_ssh_acl_config* config);

/**
 * Frees all memory associated with an ACL configuration.
 *
 * @param config
 *     The configuration to free.
 */
void guac_ssh_acl_free_config(guac_ssh_acl_config* config);

#endif
