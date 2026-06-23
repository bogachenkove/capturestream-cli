#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stddef.h>

typedef struct control_server control_server;

int command_handler_process(control_server *server, const char *command_text,
                            char *response_text, size_t response_size);
int command_handler_parse_set(const char *command_text, char *parameter_key, char *parameter_value);

#endif