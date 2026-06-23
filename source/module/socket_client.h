#ifndef SOCKET_CLIENT_H
#define SOCKET_CLIENT_H

#include <stddef.h>

int socket_client_send_command(const char *socket_path, const char *command,
                               char *response, size_t response_size);
int socket_client_set_config(const char *socket_path, const char *key_value);

#endif