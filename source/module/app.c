#include "app.h"
#include "logger.h"
#include "monitor.h"
#include "ui.h"
#include "pidfile.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <limits.h>

configuration_manager *global_configuration_manager = NULL;
streamer_manager *global_streamer_manager = NULL;
control_server *global_control_server = NULL;
void *global_monitor_handle = NULL;
assembler_context *global_assembler = NULL;
int global_pid_file_descriptor = -1;
volatile int global_shutdown_requested = 0;
char executable_directory[PATH_MAX + 1] = {0};

void app_signal_handler(int signal_number)
{
    if (signal_number == SIGHUP)
    {
        if (global_configuration_manager != NULL)
        {
            log_write(LOG_INFO, "Received SIGHUP, reloading configuration");
            configuration_reload(global_configuration_manager);
        }
        return;
    }

    global_shutdown_requested = 1;
}

void app_shutdown_program(void)
{
    log_write(LOG_INFO, "Shutting down...");

    ui_shutdown();

    if (global_assembler != NULL)
    {
        assembler_finalize_all(global_assembler);
        assembler_destroy(global_assembler);
        global_assembler = NULL;
    }

    if (global_control_server != NULL)
    {
        control_server_destroy(global_control_server);
        global_control_server = NULL;
    }

    if (global_streamer_manager != NULL)
    {
        streamer_manager_destroy(global_streamer_manager);
        global_streamer_manager = NULL;
    }

    if (global_monitor_handle != NULL)
    {
        monitor_destroy(global_monitor_handle);
        global_monitor_handle = NULL;
    }

    if (global_configuration_manager != NULL)
    {
        configuration_destroy(global_configuration_manager);
        global_configuration_manager = NULL;
    }

    if (global_pid_file_descriptor >= 0)
    {
        pidfile_remove(global_pid_file_descriptor);
        global_pid_file_descriptor = -1;
    }

    log_write(LOG_INFO, "Shutdown complete.");
}

int app_create_directories(const configuration *cfg)
{
    if (cfg == NULL)
    {
        return -1;
    }

    if (utils_mkdir_recursive(cfg->base_directory, 0755) != 0)
    {
        log_write(LOG_ERROR, "Failed to create base directory %s", cfg->base_directory);
        return -1;
    }

    return 0;
}

void app_get_executable_directory(const char *argv0)
{
    char path_buffer[PATH_MAX + 1];
    char path_copy[PATH_MAX + 1];
    ssize_t path_length = readlink("/proc/self/exe", path_buffer, sizeof(path_buffer) - 1);

    if (path_length != -1)
    {
        path_buffer[path_length] = '\0';
        strncpy(path_copy, path_buffer, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        char *directory_path = dirname(path_copy);
        strncpy(executable_directory, directory_path, sizeof(executable_directory) - 1);
        executable_directory[sizeof(executable_directory) - 1] = '\0';
    }
    else
    {
        strncpy(path_copy, argv0, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        char *directory_path = dirname(path_copy);
        strncpy(executable_directory, directory_path, sizeof(executable_directory) - 1);
        executable_directory[sizeof(executable_directory) - 1] = '\0';
    }
}

void app_on_configuration_change(const configuration *new_config)
{
    if (global_streamer_manager != NULL)
    {
        streamer_manager_reload_configuration(global_streamer_manager, new_config);
    }

    if (global_assembler != NULL)
    {
        assembler_update_config(global_assembler, new_config);
    }
}