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

#include "config.h"
#include "command-acl.h"

#include <guacamole/client.h>
#include <guacamole/mem.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Trims leading and trailing whitespace from a string in-place.
 *
 * @param str
 *     The string to trim.
 *
 * @return
 *     Pointer to the trimmed string (may be different from input).
 */
static char* trim_whitespace(char* str) {
    
    if (str == NULL || *str == '\0')
        return str;
    
    /* Trim leading whitespace */
    while (isspace((unsigned char)*str))
        str++;
    
    if (*str == '\0')
        return str;
    
    /* Trim trailing whitespace */
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    
    end[1] = '\0';
    return str;
}

/**
 * Checks if a string starts with a given prefix.
 *
 * @param str
 *     The string to check.
 *
 * @param prefix
 *     The prefix to look for.
 *
 * @return
 *     true if str starts with prefix, false otherwise.
 */
static bool starts_with(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/**
 * Checks if a command is an alias or function definition.
 *
 * @param command
 *     The command to check.
 *
 * @return
 *     true if the command defines an alias or function, false otherwise.
 */
static bool is_alias_or_function_definition(const char* command) {
    
    if (command == NULL || *command == '\0')
        return false;
    
    /* Skip leading whitespace */
    while (isspace((unsigned char)*command))
        command++;
    
    /* Check for alias definition */
    if (starts_with(command, "alias "))
        return true;
    
    /* Check for function definition */
    if (strstr(command, "function ") != NULL)
        return true;
    
    /* Check for bash function syntax: name() { */
    if (strstr(command, "() {") != NULL)
        return true;
    
    /* Check for source command */
    if (starts_with(command, "source "))
        return true;
    
    /* Check for dot source: ". file" but not ".." */
    if (starts_with(command, ". ") && command[2] != '.')
        return true;
    
    return false;
}

/**
 * Checks if a command matches a pattern (simple substring matching).
 *
 * @param command
 *     The command to check.
 *
 * @param pattern
 *     The pattern to match against.
 *
 * @return
 *     true if the command matches the pattern, false otherwise.
 */
static bool command_matches_pattern(const char* command, const char* pattern) {
    
    if (command == NULL || pattern == NULL)
        return false;
    
    /* Simple substring matching - pattern can appear anywhere in command */
    return (strstr(command, pattern) != NULL);
}

/**
 * Creates a new ACL rule with default values.
 *
 * @return
 *     Pointer to newly allocated rule, or NULL on error.
 */
static guac_ssh_acl_rule* create_rule() {
    guac_ssh_acl_rule* rule = guac_mem_zalloc(sizeof(guac_ssh_acl_rule));
    if (rule != NULL) {
        rule->blacklist = NULL;
        rule->whitelist = NULL;
        rule->blocked_message = strdup(GUAC_SSH_ACL_DEFAULT_MESSAGE);
    }
    return rule;
}

/**
 * Frees an ACL rule.
 *
 * @param rule
 *     The rule to free.
 */
static void free_rule(guac_ssh_acl_rule* rule) {
    if (rule == NULL)
        return;
    
    guac_mem_free(rule->blacklist);
    guac_mem_free(rule->whitelist);
    guac_mem_free(rule->blocked_message);
    guac_mem_free(rule);
}

guac_ssh_acl_config* guac_ssh_acl_load_config(const char* config_path) {
    
    FILE* file = fopen(config_path, "r");
    if (file == NULL)
        return NULL;
    
    guac_ssh_acl_config* config = guac_mem_zalloc(sizeof(guac_ssh_acl_config));
    if (config == NULL) {
        fclose(file);
        return NULL;
    }
    
    /* Allocate arrays for connection and user rules */
    config->connection_rules = guac_mem_zalloc(
        sizeof(guac_ssh_acl_connection_rule) * GUAC_SSH_ACL_MAX_RULES);
    config->user_rules = guac_mem_zalloc(
        sizeof(guac_ssh_acl_user_rule) * GUAC_SSH_ACL_MAX_RULES);
    config->connection_rule_count = 0;
    config->user_rule_count = 0;
    
    char line[4096];
    char current_section[256] = "";
    guac_ssh_acl_rule* current_rule = NULL;
    
    while (fgets(line, sizeof(line), file) != NULL) {
        
        char* trimmed = trim_whitespace(line);
        
        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';')
            continue;
        
        /* Check for section header */
        if (*trimmed == '[') {
            char* end = strchr(trimmed, ']');
            if (end != NULL) {
                *end = '\0';
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                
                /* Create appropriate rule based on section name */
                if (strcmp(current_section, "global") == 0) {
                    if (config->global == NULL)
                        config->global = create_rule();
                    current_rule = config->global;
                }
                else if (starts_with(current_section, "connection:")) {
                    if (config->connection_rule_count < GUAC_SSH_ACL_MAX_RULES) {
                        guac_ssh_acl_connection_rule* conn_rule = 
                            &config->connection_rules[config->connection_rule_count];
                        conn_rule->key = strdup(current_section + 11); /* Skip "connection:" */
                        conn_rule->rule.blacklist = NULL;
                        conn_rule->rule.whitelist = NULL;
                        conn_rule->rule.blocked_message = strdup(GUAC_SSH_ACL_DEFAULT_MESSAGE);
                        current_rule = &conn_rule->rule;
                        config->connection_rule_count++;
                    }
                }
                else if (starts_with(current_section, "user:")) {
                    if (config->user_rule_count < GUAC_SSH_ACL_MAX_RULES) {
                        guac_ssh_acl_user_rule* user_rule = 
                            &config->user_rules[config->user_rule_count];
                        user_rule->username = strdup(current_section + 5); /* Skip "user:" */
                        user_rule->rule.blacklist = NULL;
                        user_rule->rule.whitelist = NULL;
                        user_rule->rule.blocked_message = strdup(GUAC_SSH_ACL_DEFAULT_MESSAGE);
                        current_rule = &user_rule->rule;
                        config->user_rule_count++;
                    }
                }
                continue;
            }
        }
        
        /* Parse key=value pairs */
        if (current_rule != NULL) {
            char* equals = strchr(trimmed, '=');
            if (equals != NULL) {
                *equals = '\0';
                char* key = trim_whitespace(trimmed);
                char* value = trim_whitespace(equals + 1);
                
                if (strcmp(key, "blacklist") == 0 && *value != '\0') {
                    guac_mem_free(current_rule->blacklist);
                    current_rule->blacklist = strdup(value);
                }
                else if (strcmp(key, "whitelist") == 0 && *value != '\0') {
                    guac_mem_free(current_rule->whitelist);
                    current_rule->whitelist = strdup(value);
                }
                else if (strcmp(key, "blocked_message") == 0 && *value != '\0') {
                    guac_mem_free(current_rule->blocked_message);
                    current_rule->blocked_message = strdup(value);
                }
            }
        }
    }
    
    fclose(file);
    return config;
}

guac_ssh_acl_rule* guac_ssh_acl_get_rule(guac_ssh_acl_config* config,
        const char* guacamole_username, const char* hostname,
        const char* ssh_username) {
    
    if (config == NULL)
        return NULL;
    
    /* Priority 1: Check for user-specific rule */
    if (guacamole_username != NULL) {
        for (int i = 0; i < config->user_rule_count; i++) {
            if (strcmp(config->user_rules[i].username, guacamole_username) == 0) {
                return &config->user_rules[i].rule;
            }
        }
    }
    
    /* Priority 2: Check for connection-specific rule */
    if (hostname != NULL) {

        /* Pass 1: exact hostname:username match */
        if (ssh_username != NULL) {
            char connection_key[512];
            snprintf(connection_key, sizeof(connection_key), "%s:%s",
                     hostname, ssh_username);

            for (int i = 0; i < config->connection_rule_count; i++) {
                if (strcmp(config->connection_rules[i].key, connection_key) == 0) {
                    return &config->connection_rules[i].rule;
                }
            }
        }

        /* Pass 2: hostname-only match (rule has no username, applies to any user) */
        for (int i = 0; i < config->connection_rule_count; i++) {
            if (strchr(config->connection_rules[i].key, ':') == NULL &&
                strcmp(config->connection_rules[i].key, hostname) == 0) {
                return &config->connection_rules[i].rule;
            }
        }
    }
    
    /* Priority 3: Return global rule */
    return config->global;
}

bool guac_ssh_acl_check_command(guac_ssh_acl_rule* rule,
        const char* command, guac_client* client) {
    
    if (rule == NULL || command == NULL)
        return true; /* No rule means allow all */
    
    /* Trim command */
    char cmd_copy[8192];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    char* trimmed_cmd = trim_whitespace(cmd_copy);
    
    /* Empty command is always allowed */
    if (*trimmed_cmd == '\0')
        return true;
    
    /* Always block alias and function definitions */
    if (is_alias_or_function_definition(trimmed_cmd)) {
        if (client != NULL)
            guac_client_log(client, GUAC_LOG_WARNING,
                "Blocked alias/function definition: %s", trimmed_cmd);
        return false;
    }
    
    /* Check blacklist first */
    if (rule->blacklist != NULL && *rule->blacklist != '\0') {
        char* blacklist_copy = strdup(rule->blacklist);
        char* token = strtok(blacklist_copy, ",");
        
        while (token != NULL) {
            char* pattern = trim_whitespace(token);
            if (*pattern != '\0' && command_matches_pattern(trimmed_cmd, pattern)) {
                guac_mem_free(blacklist_copy);
                if (client != NULL)
                    guac_client_log(client, GUAC_LOG_WARNING,
                        "Command blocked by blacklist: %s (matched: %s)", 
                        trimmed_cmd, pattern);
                return false;
            }
            token = strtok(NULL, ",");
        }
        guac_mem_free(blacklist_copy);
    }
    
    /* Check whitelist if present */
    if (rule->whitelist != NULL && *rule->whitelist != '\0') {
        char* whitelist_copy = strdup(rule->whitelist);
        char* token = strtok(whitelist_copy, ",");
        bool found = false;
        
        while (token != NULL) {
            char* pattern = trim_whitespace(token);
            if (*pattern != '\0' && command_matches_pattern(trimmed_cmd, pattern)) {
                found = true;
                break;
            }
            token = strtok(NULL, ",");
        }
        guac_mem_free(whitelist_copy);
        
        if (!found) {
            if (client != NULL)
                guac_client_log(client, GUAC_LOG_WARNING,
                    "Command not in whitelist: %s", trimmed_cmd);
            return false;
        }
    }
    
    /* Command is allowed */
    return true;
}

void guac_ssh_acl_free_config(guac_ssh_acl_config* config) {
    
    if (config == NULL)
        return;
    
    /* Free global rule */
    if (config->global != NULL)
        free_rule(config->global);
    
    /* Free connection rules */
    for (int i = 0; i < config->connection_rule_count; i++) {
        guac_mem_free(config->connection_rules[i].key);
        guac_mem_free(config->connection_rules[i].rule.blacklist);
        guac_mem_free(config->connection_rules[i].rule.whitelist);
        guac_mem_free(config->connection_rules[i].rule.blocked_message);
    }
    guac_mem_free(config->connection_rules);
    
    /* Free user rules */
    for (int i = 0; i < config->user_rule_count; i++) {
        guac_mem_free(config->user_rules[i].username);
        guac_mem_free(config->user_rules[i].rule.blacklist);
        guac_mem_free(config->user_rules[i].rule.whitelist);
        guac_mem_free(config->user_rules[i].rule.blocked_message);
    }
    guac_mem_free(config->user_rules);
    
    guac_mem_free(config);
}
