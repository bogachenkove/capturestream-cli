#ifndef APP_H
#define APP_H

#include "config.h"
#include "streamer_manager.h"
#include "control.h"
#include "assembler.h"
#include <limits.h>

extern configuration_manager *global_configuration_manager;
extern streamer_manager *global_streamer_manager;
extern control_server *global_control_server;
extern void *global_monitor_handle;
extern assembler_context *global_assembler;
extern int global_pid_file_descriptor;
extern volatile int global_shutdown_requested;
extern char executable_directory[PATH_MAX + 1];

void app_signal_handler (int signal_number);
void app_shutdown_program (void);
int app_create_directories (const configuration *cfg);
void app_get_executable_directory (const char *argv0);
void app_on_configuration_change (const configuration *new_config);

#endif
