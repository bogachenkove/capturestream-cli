#include "module/app.h"
#include "module/daemon.h"
#include "module/cli_setup.h"
#include "module/socket_client.h"
#include "module/logger.h"
#include "module/monitor.h"
#include "module/streamer_manager.h"
#include "module/control.h"
#include "module/ui.h"
#include "module/pidfile.h"
#include "module/utils.h"
#include "module/assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>

int main(int argc, char **argv)
{
    app_get_executable_directory(argv[0]);

    if (geteuid() == 0)
    {
        fprintf(stderr, "Error: This program must NOT be run as root.\n");
        exit(EXIT_FAILURE);
    }

    char config_path[PATH_MAX + 64] = {0};
    char pid_path[PATH_MAX + 64] = {0};
    char socket_path[PATH_MAX + 64] = {0};
    char log_path[PATH_MAX + 64] = {0};

    if (snprintf(config_path, sizeof(config_path), "%s/config.json", executable_directory) >= (int)sizeof(config_path))
    {
        fprintf(stderr, "Config path too long\n");
        exit(EXIT_FAILURE);
    }

    if (snprintf(pid_path, sizeof(pid_path), "%s/recorder.pid", executable_directory) >= (int)sizeof(pid_path))
    {
        fprintf(stderr, "PID path too long\n");
        exit(EXIT_FAILURE);
    }

    if (snprintf(socket_path, sizeof(socket_path), "%s/recorder.sock", executable_directory) >= (int)sizeof(socket_path))
    {
        fprintf(stderr, "Socket path too long\n");
        exit(EXIT_FAILURE);
    }

    if (snprintf(log_path, sizeof(log_path), "%s/logs/recorder.log", executable_directory) >= (int)sizeof(log_path))
    {
        fprintf(stderr, "Log path too long\n");
        exit(EXIT_FAILURE);
    }

    char log_directory[PATH_MAX + 64];
    if (snprintf(log_directory, sizeof(log_directory), "%s/logs", executable_directory) >= (int)sizeof(log_directory))
    {
        fprintf(stderr, "Log directory path too long\n");
        exit(EXIT_FAILURE);
    }

    if (utils_mkdir_recursive(log_directory, 0755) != 0)
    {
        fprintf(stderr, "Failed to create log directory %s\n", log_directory);
        exit(EXIT_FAILURE);
    }

    char *set_value = NULL;
    cli_action action = cli_parse_arguments(argc, argv, &set_value);

    if (action == CLI_ACTION_HELP)
    {
        cli_print_help();
        exit(EXIT_SUCCESS);
    }
    else if (action == CLI_ACTION_STOP)
    {
        daemon_stop(pid_path);
        exit(EXIT_SUCCESS);
    }
    else if (action == CLI_ACTION_STATUS)
    {
        daemon_status(pid_path);
        exit(EXIT_SUCCESS);
    }
    else if (action == CLI_ACTION_RELOAD)
    {
        daemon_reload(pid_path);
        exit(EXIT_SUCCESS);
    }
    else if (action == CLI_ACTION_SET)
    {
        if (socket_client_set_config(socket_path, set_value) != 0)
        {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    else if (action == CLI_ACTION_ERROR)
    {
        cli_print_help();
        exit(EXIT_FAILURE);
    }

    int daemon_mode = (action == CLI_ACTION_START);

    if (utils_file_exists(config_path) != 1)
    {
        printf("Configuration file not found. Starting interactive setup...\n");
        cli_interactive_config_setup(config_path);
        printf("Configuration created. Starting recorder...\n");
    }

    if (daemon_mode)
    {
        daemon_start();
    }

    if (logger_initialize(LOG_INFO, log_path, 0) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        exit(EXIT_FAILURE);
    }

    log_write(LOG_INFO, "=== capturestream-cli starting ===");

    if (pidfile_is_running(pid_path) == 1)
    {
        log_write(LOG_ERROR, "Another instance is running (PID file exists)");
        logger_shutdown();
        fprintf(stderr, "Another instance is running. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    global_pid_file_descriptor = pidfile_create(pid_path);
    if (global_pid_file_descriptor < 0)
    {
        log_write(LOG_ERROR, "Failed to create PID file");
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    global_configuration_manager = configuration_create(config_path);
    if (global_configuration_manager == NULL)
    {
        log_write(LOG_ERROR, "Failed to load configuration from %s", config_path);
        pidfile_remove(global_pid_file_descriptor);
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    const configuration *configuration_snapshot = configuration_acquire(global_configuration_manager);
    if (configuration_snapshot == NULL)
    {
        log_write(LOG_ERROR, "Configuration is NULL");
        configuration_destroy(global_configuration_manager);
        pidfile_remove(global_pid_file_descriptor);
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    logger_set_level((log_level)configuration_snapshot->log_level);

    if (app_create_directories(configuration_snapshot) != 0)
    {
        log_write(LOG_ERROR, "Failed to create directories");
        configuration_release(global_configuration_manager);
        configuration_destroy(global_configuration_manager);
        pidfile_remove(global_pid_file_descriptor);
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    configuration_release(global_configuration_manager);
    configuration_set_change_callback(global_configuration_manager, app_on_configuration_change);

    global_monitor_handle = monitor_create(configuration_snapshot);
    if (global_monitor_handle == NULL)
    {
        log_write(LOG_ERROR, "Failed to create monitor");
        configuration_destroy(global_configuration_manager);
        pidfile_remove(global_pid_file_descriptor);
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    monitor_set_callback(global_monitor_handle, NULL);

    global_streamer_manager = streamer_manager_create(configuration_snapshot);
    if (global_streamer_manager == NULL)
    {
        log_write(LOG_ERROR, "Failed to create streamer manager");
        monitor_destroy(global_monitor_handle);
        configuration_destroy(global_configuration_manager);
        pidfile_remove(global_pid_file_descriptor);
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    global_assembler = assembler_create(global_streamer_manager, global_configuration_manager);
    if (global_assembler == NULL)
    {
        log_write(LOG_WARN, "Failed to create assembler, continuing without MP4 assembly");
    }

    global_control_server = control_server_create(socket_path,
                                                  global_streamer_manager,
                                                  global_configuration_manager);
    if (global_control_server == NULL)
    {
        log_write(LOG_ERROR, "Failed to create control server");
        if (global_assembler != NULL) assembler_destroy(global_assembler);
        streamer_manager_destroy(global_streamer_manager);
        monitor_destroy(global_monitor_handle);
        configuration_destroy(global_configuration_manager);
        pidfile_remove(global_pid_file_descriptor);
        logger_shutdown();
        exit(EXIT_FAILURE);
    }

    if (!daemon_mode)
    {
        if (isatty(STDOUT_FILENO))
        {
            if (ui_initialize(global_streamer_manager, global_configuration_manager) != 0)
            {
                log_write(LOG_WARN, "UI initialization failed, continuing without UI");
            }
        }
        else
        {
            log_write(LOG_INFO, "Not a terminal, UI disabled");
        }
    }

    struct sigaction signal_action;
    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = app_signal_handler;
    sigemptyset(&signal_action.sa_mask);

    sigaction(SIGTERM, &signal_action, NULL);
    sigaction(SIGINT, &signal_action, NULL);
    sigaction(SIGHUP, &signal_action, NULL);

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    pthread_sigmask(SIG_SETMASK, &signal_mask, NULL);

    log_write(LOG_INFO, "Recorder started successfully. PID: %d", getpid());

    while (!global_shutdown_requested)
    {
        sleep(1);
    }

    app_shutdown_program();
    log_write(LOG_INFO, "Recorder stopped.");
    logger_shutdown();

    return 0;
}