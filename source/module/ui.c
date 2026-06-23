#include "ui.h"
#include "streamer_manager.h"
#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>

static void *ui_manager_pointer = NULL;
static void *ui_config_pointer = NULL;
static int ui_running_flag = 0;
static pthread_t ui_thread;
static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* ui_thread_function(void *argument);
static int is_terminal(void);
static void clear_screen(void);
static void print_colored(const char *message_text, int color_identifier);
static void get_terminal_size(int *terminal_rows, int *terminal_columns);

int ui_initialize(void *manager_ptr, void *config_ptr)
{
    if (manager_ptr == NULL || config_ptr == NULL)
    {
        log_write(LOG_ERROR, "uiInitialize: invalid arguments");
        return -1;
    }

    pthread_mutex_lock(&ui_mutex);

    if (ui_running_flag)
    {
        pthread_mutex_unlock(&ui_mutex);
        log_write(LOG_WARN, "uiInitialize: already running");
        return -1;
    }

    ui_manager_pointer = manager_ptr;
    ui_config_pointer = config_ptr;
    ui_running_flag = 1;

    pthread_mutex_unlock(&ui_mutex);

    if (pthread_create(&ui_thread, NULL, ui_thread_function, NULL) != 0)
    {
        log_write(LOG_ERROR, "uiInitialize: pthread_create failed");
        ui_running_flag = 0;
        return -1;
    }

    log_write(LOG_INFO, "uiInitialize: UI thread started");
    return 0;
}

void ui_shutdown(void)
{
    pthread_mutex_lock(&ui_mutex);

    if (!ui_running_flag)
    {
        pthread_mutex_unlock(&ui_mutex);
        return;
    }

    ui_running_flag = 0;
    pthread_mutex_unlock(&ui_mutex);

    pthread_join(ui_thread, NULL);
    clear_screen();
    printf("UI stopped.\n");
    fflush(stdout);

    log_write(LOG_INFO, "uiShutdown: UI stopped");
}

void ui_refresh(void)
{
}

static void* ui_thread_function(void *argument)
{
    (void)argument;

    int terminal_rows = 25, terminal_columns = 80;
    int terminal_detected = is_terminal();

    if (!terminal_detected)
    {
        log_write(LOG_INFO, "uiThread: not a terminal, UI disabled");
        return NULL;
    }

    struct termios previous_terminal_state, updated_terminal_state;
    tcgetattr(STDOUT_FILENO, &previous_terminal_state);
    updated_terminal_state = previous_terminal_state;
    updated_terminal_state.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDOUT_FILENO, TCSANOW, &updated_terminal_state);

    printf("\033[?25l");
    clear_screen();

    while (ui_running_flag)
    {
        get_terminal_size(&terminal_rows, &terminal_columns);

        printf("\033[H");
        printf("\033[1;37m=== Stream Recorder Status ===\033[0m\n");

        configuration_manager *configuration_manager_handle = (configuration_manager*)ui_config_pointer;
        const configuration *configuration_snapshot = configuration_acquire(configuration_manager_handle);

        if (configuration_snapshot != NULL)
        {
            printf("Base dir: %s\n", configuration_snapshot->base_directory);
            printf("Segment: %d s, Cooldown: %d s\n",
                   configuration_snapshot->segment_duration_seconds,
                   configuration_snapshot->failure_cooldown_seconds);
            printf("Disk limit: %d%% or %d GB\n",
                   configuration_snapshot->disk_free_percent_limit,
                   configuration_snapshot->disk_free_absolute_limit_gb);
            printf("Memory limit: %d%% critical\n", configuration_snapshot->memory_critical_percent);
            configuration_release(configuration_manager_handle);
        }
        else
        {
            printf("Config: unavailable\n");
        }

        printf("\n");

        streamer_manager *streamer_manager_handle = (streamer_manager*)ui_manager_pointer;
        int recorder_count = streamer_manager_get_count(streamer_manager_handle);

        if (recorder_count == 0)
        {
            printf("No streamers configured.\n");
        }
        else
        {
            char **status_lines = malloc(sizeof(char*) * recorder_count);
            if (status_lines != NULL)
            {
                int line_count = streamer_manager_get_status_strings(streamer_manager_handle, status_lines, recorder_count);

                for (int line_position = 0; line_position < line_count; line_position++)
                {
                    if (strstr(status_lines[line_position], "RECORDING") != NULL)
                    {
                        print_colored(status_lines[line_position], 32);
                    }
                    else if (strstr(status_lines[line_position], "ERROR") != NULL)
                    {
                        print_colored(status_lines[line_position], 31);
                    }
                    else if (strstr(status_lines[line_position], "IDLE") != NULL)
                    {
                        print_colored(status_lines[line_position], 33);
                    }
                    else
                    {
                        printf("%s\n", status_lines[line_position]);
                    }
                    free(status_lines[line_position]);
                }
                free(status_lines);
            }
        }

        time_t current_time = time(NULL);
        struct tm *time_components = localtime(&current_time);
        char time_text[32];
        strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", time_components);

        printf("\n\033[2mLast update: %s\033[0m", time_text);
        fflush(stdout);

        for (int wait_counter = 0; wait_counter < 2 && ui_running_flag; wait_counter++)
        {
            sleep(1);
        }
    }

    printf("\033[?25h");
    tcsetattr(STDOUT_FILENO, TCSANOW, &previous_terminal_state);
    clear_screen();

    return NULL;
}

static int is_terminal(void)
{
    return isatty(STDOUT_FILENO);
}

static void clear_screen(void)
{
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void print_colored(const char *message_text, int color_identifier)
{
    printf("\033[%dm%s\033[0m\n", color_identifier, message_text);
}

static void get_terminal_size(int *terminal_rows, int *terminal_columns)
{
    struct winsize window_dimensions;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_dimensions) == 0)
    {
        *terminal_rows = window_dimensions.ws_row;
        *terminal_columns = window_dimensions.ws_col;
    }
    else
    {
        *terminal_rows = 25;
        *terminal_columns = 80;
    }
}