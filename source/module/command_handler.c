#include "command_handler.h"
#include "control.h"
#include "logger.h"
#include "streamer_manager.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND_BUFFER_SIZE 4096

int command_handler_process(control_server *server, const char *command_text,
                            char *response_text, size_t response_size)
{
    if (server == NULL || command_text == NULL || response_text == NULL || response_size == 0)
    {
        return -1;
    }

    char sanitized_command[COMMAND_BUFFER_SIZE];
    strncpy(sanitized_command, command_text, sizeof(sanitized_command) - 1);
    sanitized_command[sizeof(sanitized_command) - 1] = '\0';
    size_t text_length = strlen(sanitized_command);
    if (text_length > 0 && sanitized_command[text_length - 1] == '\n')
    {
        sanitized_command[text_length - 1] = '\0';
    }
    char token_buffer[COMMAND_BUFFER_SIZE];
    strncpy(token_buffer, sanitized_command, sizeof(token_buffer) - 1);
    token_buffer[sizeof(token_buffer) - 1] = '\0';

    log_write(LOG_DEBUG, "commandHandler: received command: %s", sanitized_command);

    char *tokenizer_state;
    char *command_token = strtok_r(token_buffer, " ", &tokenizer_state);

    if (command_token == NULL)
    {
        snprintf(response_text, response_size, "ERROR: empty command\n");
        return -1;
    }

    if (strcmp(command_token, "status") == 0)
    {
        streamer_manager *streamer_manager_handle = (streamer_manager*)server->manager_handle;
        int recorder_count = streamer_manager_get_count(streamer_manager_handle);

        if (recorder_count == 0)
        {
            snprintf(response_text, response_size, "No recorders.\n");
            return 0;
        }

        char **status_lines = malloc(sizeof(char*) * recorder_count);
        if (status_lines == NULL)
        {
            snprintf(response_text, response_size, "ERROR: memory\n");
            return -1;
        }

        int line_count = streamer_manager_get_status_strings(streamer_manager_handle, status_lines, recorder_count);

        if (line_count > 0)
        {
            size_t response_position = 0;
            for (int status_index = 0; status_index < line_count; status_index++)
            {
                response_position += snprintf(response_text + response_position,
                                              response_size - response_position,
                                              "%s\n", status_lines[status_index]);
                free(status_lines[status_index]);
            }
        }

        free(status_lines);
        return 0;
    }
    else if (strcmp(command_token, "stop") == 0)
    {
        snprintf(response_text, response_size, "Stopping all recorders...\n");
        streamer_manager *streamer_manager_handle = (streamer_manager*)server->manager_handle;
        configuration_manager *configuration_manager_handle = (configuration_manager*)server->config_manager_handle;

        const configuration *current_config = configuration_acquire(configuration_manager_handle);
        if (current_config == NULL)
        {
            snprintf(response_text, response_size, "ERROR: cannot acquire configuration\n");
            return -1;
        }

        configuration empty_configuration = *current_config;
        configuration_release(configuration_manager_handle);

        empty_configuration.twitch_streamers[0] = '\0';
        streamer_manager_reload_configuration(streamer_manager_handle, &empty_configuration);
        return 0;
    }
    else if (strcmp(command_token, "reload") == 0)
    {
        configuration_manager *configuration_manager_handle = (configuration_manager*)server->config_manager_handle;
        if (configuration_reload(configuration_manager_handle) == 0)
        {
            snprintf(response_text, response_size, "Configuration reloaded.\n");
            return 0;
        }
        else
        {
            snprintf(response_text, response_size, "ERROR: reload failed.\n");
            return -1;
        }
    }
    else if (strcmp(command_token, "add") == 0)
    {
        char *streamer_name = strtok_r(NULL, " ", &tokenizer_state);
        char *platform_name = strtok_r(NULL, " ", &tokenizer_state);

        if (streamer_name == NULL)
        {
            snprintf(response_text, response_size, "ERROR: missing streamer name\n");
            return -1;
        }

        if (platform_name == NULL)
        {
            platform_name = "twitch";
        }

        streamer_manager *streamer_manager_handle = (streamer_manager*)server->manager_handle;
        if (streamer_manager_add_streamer(streamer_manager_handle, streamer_name, platform_name) == 0)
        {
            snprintf(response_text, response_size, "Added %s/%s.\n", platform_name, streamer_name);
            return 0;
        }
        else
        {
            snprintf(response_text, response_size, "ERROR: failed to add %s/%s\n", platform_name, streamer_name);
            return -1;
        }
    }
    else if (strcmp(command_token, "remove") == 0)
    {
        char *streamer_name = strtok_r(NULL, " ", &tokenizer_state);
        char *platform_name = strtok_r(NULL, " ", &tokenizer_state);

        if (streamer_name == NULL)
        {
            snprintf(response_text, response_size, "ERROR: missing streamer name\n");
            return -1;
        }

        if (platform_name == NULL)
        {
            platform_name = "twitch";
        }

        streamer_manager *streamer_manager_handle = (streamer_manager*)server->manager_handle;
        if (streamer_manager_remove_streamer(streamer_manager_handle, streamer_name, platform_name, 1) == 0)
        {
            snprintf(response_text, response_size, "Removed %s/%s.\n", platform_name, streamer_name);
            return 0;
        }
        else
        {
            snprintf(response_text, response_size, "ERROR: failed to remove %s/%s\n", platform_name, streamer_name);
            return -1;
        }
    }
    else if (strcmp(command_token, "set") == 0)
    {
        char parameter_key[64], parameter_value[64];
        if (command_handler_parse_set(sanitized_command, parameter_key, parameter_value) != 0)
        {
            snprintf(response_text, response_size, "ERROR: invalid set format. Use: set key=value\n");
            return -1;
        }

        configuration_manager *configuration_manager_handle = (configuration_manager*)server->config_manager_handle;
        int assignment_result = 0;
        int integer_value = atoi(parameter_value);
        char *parse_end;
        strtol(parameter_value, &parse_end, 10);

        if (*parse_end == '\0')
        {
            assignment_result = configuration_set_int(configuration_manager_handle, parameter_key, integer_value);
        }
        else
        {
            assignment_result = configuration_set_string(configuration_manager_handle, parameter_key, parameter_value);
        }

        if (assignment_result == 0)
        {
            snprintf(response_text, response_size, "Set %s = %s\n", parameter_key, parameter_value);
        }
        else
        {
            snprintf(response_text, response_size, "ERROR: failed to set %s\n", parameter_key);
        }

        return assignment_result;
    }
    else
    {
        snprintf(response_text, response_size, "ERROR: unknown command: %s\n", command_token);
        return -1;
    }
}

int command_handler_parse_set(const char *command_text, char *parameter_key, char *parameter_value)
{
    if (command_text == NULL || parameter_key == NULL || parameter_value == NULL)
    {
        return -1;
    }

    const char *equals_position = strchr(command_text, '=');
    if (equals_position == NULL)
    {
        return -1;
    }

    const char *key_start = command_text + 4;
    while (*key_start == ' ')
    {
        key_start++;
    }

    size_t key_size = equals_position - key_start;
    if (key_size >= 64)
    {
        return -1;
    }

    strncpy(parameter_key, key_start, key_size);
    parameter_key[key_size] = '\0';

    strncpy(parameter_value, equals_position + 1, 63);
    parameter_value[63] = '\0';

    return 0;
}