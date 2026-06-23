#ifndef CONTROL_H
#define CONTROL_H

#include <stddef.h>
#include <pthread.h>

typedef struct control_server
{
    int socket_file_descriptor;
    char socket_path[128];
    void *manager_handle;
    void *config_manager_handle;
    int running;
    pthread_t accept_thread;
} control_server;

control_server* control_server_create(const char *socket_path, void *manager, void *config_mgr);
void control_server_destroy(control_server *server);
int control_server_process_command(control_server *server, const char *command, char *response, size_t response_size);

#endif