#include "control.h"
#include "command_handler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define CONTROL_BUFFER_SIZE 4096

static void* accept_thread_function(void *argument);

control_server* control_server_create(const char *socket_path, void *manager, void *config_mgr)
{
    if (socket_path == NULL || manager == NULL || config_mgr == NULL)
    {
        log_write(LOG_ERROR, "controlServerCreate: invalid arguments");
        return NULL;
    }

    control_server *server = calloc(1, sizeof(control_server));
    if (server == NULL)
    {
        log_write(LOG_ERROR, "controlServerCreate: calloc failed");
        return NULL;
    }

    strncpy(server->socket_path, socket_path, sizeof(server->socket_path) - 1);
    server->socket_path[sizeof(server->socket_path) - 1] = '\0';

    server->manager_handle = manager;
    server->config_manager_handle = config_mgr;
    server->running = 1;

    int socket_descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_descriptor < 0)
    {
        log_write(LOG_ERROR, "controlServerCreate: socket failed: %s", strerror(errno));
        free(server);
        return NULL;
    }

    unlink(socket_path);

    struct sockaddr_un socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sun_family = AF_UNIX;
    strncpy(socket_address.sun_path, socket_path, sizeof(socket_address.sun_path) - 1);

    if (bind(socket_descriptor, (struct sockaddr*)&socket_address, sizeof(socket_address)) < 0)
    {
        log_write(LOG_ERROR, "controlServerCreate: bind failed: %s", strerror(errno));
        close(socket_descriptor);
        free(server);
        return NULL;
    }

    if (listen(socket_descriptor, 5) < 0)
    {
        log_write(LOG_ERROR, "controlServerCreate: listen failed: %s", strerror(errno));
        close(socket_descriptor);
        unlink(socket_path);
        free(server);
        return NULL;
    }

    server->socket_file_descriptor = socket_descriptor;

    if (pthread_create(&server->accept_thread, NULL, accept_thread_function, server) != 0)
    {
        log_write(LOG_ERROR, "controlServerCreate: pthread_create failed");
        close(socket_descriptor);
        unlink(socket_path);
        free(server);
        return NULL;
    }

    log_write(LOG_INFO, "controlServerCreate: server listening on %s", socket_path);
    return server;
}

void control_server_destroy(control_server *server)
{
    if (server == NULL)
    {
        return;
    }

    server->running = 0;

    if (server->socket_file_descriptor >= 0)
    {
        shutdown(server->socket_file_descriptor, SHUT_RDWR);
        close(server->socket_file_descriptor);
        server->socket_file_descriptor = -1;
    }

    unlink(server->socket_path);

    pthread_cancel(server->accept_thread);
    pthread_join(server->accept_thread, NULL);

    free(server);
    log_write(LOG_INFO, "controlServerDestroy: server destroyed");
}

int control_server_process_command(control_server *server, const char *command, char *response, size_t response_size)
{
    if (server == NULL || command == NULL || response == NULL)
    {
        return -1;
    }

    return command_handler_process(server, command, response, response_size);
}

static void* accept_thread_function(void *argument)
{
    control_server *server = (control_server*)argument;

    while (server->running)
    {
        int client_descriptor = accept(server->socket_file_descriptor, NULL, NULL);
        if (client_descriptor < 0)
        {
            if (errno == EINTR || errno == EBADF || errno == EINVAL || !server->running)
            {
                break;
            }
            log_write(LOG_ERROR, "acceptThread: accept failed: %s", strerror(errno));
            continue;
        }

        char buffer[CONTROL_BUFFER_SIZE];
        ssize_t bytes_received = read(client_descriptor, buffer, sizeof(buffer) - 1);
        if (bytes_received < 0)
        {
            log_write(LOG_ERROR, "acceptThread: read error: %s", strerror(errno));
            close(client_descriptor);
            continue;
        }

        buffer[bytes_received] = '\0';

        char response_text[CONTROL_BUFFER_SIZE];
        int result = command_handler_process(server, buffer, response_text, sizeof(response_text));

        if (result == 0)
        {
            write(client_descriptor, response_text, strlen(response_text));
        }
        else
        {
            const char *error_message = "ERROR: command failed\n";
            write(client_descriptor, error_message, strlen(error_message));
        }

        close(client_descriptor);
    }

    return NULL;
}