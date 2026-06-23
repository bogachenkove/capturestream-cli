#include "assembler.h"
#include "logger.h"
#include "utils.h"
#include "streamer_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

typedef struct streamer_assembly_state
{
    char streamer_name[64];
    char platform_name[16];
    char work_directory[256];
    char **pending_files;
    int pending_count;
    size_t pending_size;
    char last_file[256];
} streamer_assembly_state;

struct assembler_context
{
    streamer_manager *streamer_manager;
    configuration_manager *config_manager;
    pthread_t thread;
    volatile int running;
    volatile int finalize_requested;
    pthread_mutex_t lock;
    configuration current_config;
    streamer_assembly_state **states;
    int state_count;
    int state_capacity;
};

static void* assembler_thread_function(void *argument);
static void process_streamer(assembler_context *context, streamer_assembly_state *assembly_state, int finalize);
static void scan_directory(streamer_assembly_state *assembly_state);
static int concat_files(streamer_assembly_state *assembly_state, char **file_list, int file_count, const char *output_path);
static void free_state(streamer_assembly_state *assembly_state);
static void update_states(assembler_context *context);

assembler_context* assembler_create(streamer_manager *manager, configuration_manager *config_manager)
{
    assembler_context *context = calloc(1, sizeof(assembler_context));
    if (context == NULL)
    {
        log_write(LOG_ERROR, "assemblerCreate: calloc failed");
        return NULL;
    }

    context->streamer_manager = manager;
    context->config_manager = config_manager;
    context->running = 1;
    context->finalize_requested = 0;

    pthread_mutex_init(&context->lock, NULL);

    const configuration *configuration_snapshot = configuration_acquire(config_manager);
    if (configuration_snapshot != NULL)
    {
        context->current_config = *configuration_snapshot;
        configuration_release(config_manager);
    }

    context->states = NULL;
    context->state_count = 0;
    context->state_capacity = 0;

    update_states(context);

    if (pthread_create(&context->thread, NULL, assembler_thread_function, context) != 0)
    {
        log_write(LOG_ERROR, "assemblerCreate: pthread_create failed");
        free(context);
        return NULL;
    }

    log_write(LOG_INFO, "assemblerCreate: assembler started");
    return context;
}

void assembler_destroy(assembler_context *context)
{
    if (context == NULL)
    {
        return;
    }

    context->running = 0;
    pthread_join(context->thread, NULL);

    for (int state_position = 0; state_position < context->state_count; state_position++)
    {
        free_state(context->states[state_position]);
    }

    free(context->states);
    pthread_mutex_destroy(&context->lock);
    free(context);

    log_write(LOG_INFO, "assemblerDestroy: assembler destroyed");
}

void assembler_finalize_all(assembler_context *context)
{
    if (context == NULL)
    {
        return;
    }

    context->finalize_requested = 1;
}

void assembler_update_config(assembler_context *context, const configuration *new_config)
{
    if (context == NULL || new_config == NULL)
    {
        return;
    }

    pthread_mutex_lock(&context->lock);
    context->current_config = *new_config;
    pthread_mutex_unlock(&context->lock);

    log_write(LOG_INFO, "assemblerUpdateConfig: config updated");
}

static void* assembler_thread_function(void *argument)
{
    assembler_context *context = (assembler_context*)argument;

    while (context->running)
    {
        if (context->finalize_requested)
        {
            log_write(LOG_INFO, "assemblerThread: finalizing all streamers");
            pthread_mutex_lock(&context->lock);

            for (int state_position = 0; state_position < context->state_count; state_position++)
            {
                process_streamer(context, context->states[state_position], 1);
            }

            pthread_mutex_unlock(&context->lock);
            break;
        }

        pthread_mutex_lock(&context->lock);
        configuration configuration_copy = context->current_config;
        pthread_mutex_unlock(&context->lock);

        update_states(context);

        pthread_mutex_lock(&context->lock);

        for (int state_position = 0; state_position < context->state_count; state_position++)
        {
            process_streamer(context, context->states[state_position], 0);
        }

        pthread_mutex_unlock(&context->lock);

        for (int wait_counter = 0; wait_counter < configuration_copy.concat_scan_interval_seconds && context->running; wait_counter++)
        {
            sleep(1);
        }
    }

    return NULL;
}

static void update_states(assembler_context *context)
{
    streamer_manager *streamer_manager_handle = context->streamer_manager;
    int total_recorders = streamer_manager_get_count(streamer_manager_handle);

    pthread_mutex_lock(&context->lock);

    for (int state_position = 0; state_position < context->state_count; state_position++)
    {
        free_state(context->states[state_position]);
    }

    free(context->states);
    context->states = NULL;
    context->state_count = 0;
    context->state_capacity = 0;

    if (total_recorders > 0)
    {
        context->states = calloc(total_recorders, sizeof(streamer_assembly_state*));
        context->state_capacity = total_recorders;

        for (int streamer_position = 0; streamer_position < total_recorders; streamer_position++)
        {
            streamer_assembly_state *assembly_state = calloc(1, sizeof(streamer_assembly_state));
            if (assembly_state == NULL)
            {
                continue;
            }

            char streamer_name[64], platform_name[16], work_directory[256];
            if (streamer_manager_get_streamer_info(streamer_manager_handle, streamer_position,
                                                   streamer_name, sizeof(streamer_name),
                                                   platform_name, sizeof(platform_name),
                                                   work_directory, sizeof(work_directory)) == 0)
            {
                strncpy(assembly_state->streamer_name, streamer_name, sizeof(assembly_state->streamer_name) - 1);
                assembly_state->streamer_name[sizeof(assembly_state->streamer_name) - 1] = '\0';

                strncpy(assembly_state->platform_name, platform_name, sizeof(assembly_state->platform_name) - 1);
                assembly_state->platform_name[sizeof(assembly_state->platform_name) - 1] = '\0';

                strncpy(assembly_state->work_directory, work_directory, sizeof(assembly_state->work_directory) - 1);
                assembly_state->work_directory[sizeof(assembly_state->work_directory) - 1] = '\0';

                assembly_state->pending_files = NULL;
                assembly_state->pending_count = 0;
                assembly_state->pending_size = 0;
                assembly_state->last_file[0] = '\0';

                context->states[context->state_count++] = assembly_state;
            }
            else
            {
                free(assembly_state);
            }
        }
    }

    pthread_mutex_unlock(&context->lock);
}

static void process_streamer(assembler_context *context, streamer_assembly_state *assembly_state, int finalize)
{
    if (assembly_state == NULL)
    {
        return;
    }

    scan_directory(assembly_state);

    if (assembly_state->pending_count == 0)
    {
        return;
    }

    int processing_total = assembly_state->pending_count;
    if (!finalize && assembly_state->pending_count > 1)
    {
        processing_total = assembly_state->pending_count - 1;
    }

    if (processing_total == 0)
    {
        return;
    }

    int maximum_size_megabytes = context->current_config.concat_max_size_mb;
    size_t maximum_bytes = (size_t)maximum_size_megabytes * 1024 * 1024;
    size_t accumulated_size = 0;
    int start_position = 0;
    char **processing_list = assembly_state->pending_files;
    int total_to_process = processing_total;

    for (int file_position = 0; file_position < total_to_process; file_position++)
    {
        struct stat file_status;
        char complete_path[512];
        snprintf(complete_path, sizeof(complete_path), "%s/%s",
                 assembly_state->work_directory, processing_list[file_position]);

        if (stat(complete_path, &file_status) != 0)
        {
            continue;
        }

        accumulated_size += file_status.st_size;

        if (accumulated_size >= maximum_bytes && (file_position - start_position + 1) > 1)
        {
            char output_path[1024];
            char *initial_file = processing_list[start_position];
            char base_name[256];
            strncpy(base_name, initial_file, sizeof(base_name) - 1);
            base_name[sizeof(base_name) - 1] = '\0';

            char *extension_position = strrchr(base_name, '.');
            if (extension_position)
            {
                *extension_position = '\0';
            }

            snprintf(output_path, sizeof(output_path), "%s/%s.mp4",
                     assembly_state->work_directory, base_name);

            if (concat_files(assembly_state, &processing_list[start_position],
                            file_position - start_position + 1, output_path) == 0)
            {
                if (!context->current_config.concat_keep_ts)
                {
                    for (int removal_position = start_position; removal_position <= file_position; removal_position++)
                    {
                        char file_path[512];
                        snprintf(file_path, sizeof(file_path), "%s/%s",
                                 assembly_state->work_directory, processing_list[removal_position]);
                        unlink(file_path);
                    }
                }

                for (int removal_position = start_position; removal_position <= file_position; removal_position++)
                {
                    free(assembly_state->pending_files[removal_position]);
                    assembly_state->pending_files[removal_position] = NULL;
                }

                int removal_total = file_position - start_position + 1;
                for (int shift_position = file_position + 1; shift_position < assembly_state->pending_count; shift_position++)
                {
                    assembly_state->pending_files[shift_position - removal_total] = assembly_state->pending_files[shift_position];
                }

                assembly_state->pending_count -= removal_total;
                total_to_process -= removal_total;
                file_position = start_position - 1;
                accumulated_size = 0;
                continue;
            }
            else
            {
                log_write(LOG_ERROR, "processStreamer: concat failed for %s", output_path);
                accumulated_size = 0;
                start_position = file_position + 1;
                continue;
            }
        }
    }

    if (finalize && assembly_state->pending_count > 0)
    {
        char output_path[1024];
        char *initial_file = assembly_state->pending_files[0];
        char base_name[256];
        strncpy(base_name, initial_file, sizeof(base_name) - 1);
        base_name[sizeof(base_name) - 1] = '\0';

        char *extension_position = strrchr(base_name, '.');
        if (extension_position)
        {
            *extension_position = '\0';
        }

        snprintf(output_path, sizeof(output_path), "%s/%s.mp4",
                 assembly_state->work_directory, base_name);

        if (concat_files(assembly_state, assembly_state->pending_files,
                        assembly_state->pending_count, output_path) == 0)
        {
            if (!context->current_config.concat_keep_ts)
            {
                for (int removal_position = 0; removal_position < assembly_state->pending_count; removal_position++)
                {
                    char file_path[512];
                    snprintf(file_path, sizeof(file_path), "%s/%s",
                             assembly_state->work_directory, assembly_state->pending_files[removal_position]);
                    unlink(file_path);
                }
            }

            for (int removal_position = 0; removal_position < assembly_state->pending_count; removal_position++)
            {
                free(assembly_state->pending_files[removal_position]);
                assembly_state->pending_files[removal_position] = NULL;
            }

            assembly_state->pending_count = 0;
        }
    }
}

static void scan_directory(streamer_assembly_state *assembly_state)
{
    if (assembly_state == NULL)
    {
        return;
    }

    struct dirent **directory_entries;
    int entry_total = scandir(assembly_state->work_directory, &directory_entries, NULL, alphasort);

    if (entry_total < 0)
    {
        return;
    }

    for (int pending_position = 0; pending_position < assembly_state->pending_count; pending_position++)
    {
        free(assembly_state->pending_files[pending_position]);
    }

    free(assembly_state->pending_files);
    assembly_state->pending_files = NULL;
    assembly_state->pending_count = 0;
    assembly_state->pending_size = 0;

    char name_prefix[256];
    snprintf(name_prefix, sizeof(name_prefix), "%s_%s_",
             assembly_state->platform_name, assembly_state->streamer_name);

    for (int entry_position = 0; entry_position < entry_total; entry_position++)
    {
        if (directory_entries[entry_position]->d_type == DT_REG)
        {
            const char *entry_name = directory_entries[entry_position]->d_name;
            if (strncmp(entry_name, name_prefix, strlen(name_prefix)) == 0)
            {
                char *file_extension = strrchr(entry_name, '.');
                if (file_extension && strcmp(file_extension, ".ts") == 0)
                {
                    assembly_state->pending_files = realloc(assembly_state->pending_files,
                                                            (assembly_state->pending_count + 1) * sizeof(char*));
                    assembly_state->pending_files[assembly_state->pending_count] = strdup(entry_name);
                    assembly_state->pending_count++;
                }
            }
        }
        free(directory_entries[entry_position]);
    }

    free(directory_entries);
}

static int concat_files(streamer_assembly_state *assembly_state, char **file_list, int file_count, const char *output_path)
{
    if (file_count == 0)
    {
        return -1;
    }

    char list_file_path[512];
    snprintf(list_file_path, sizeof(list_file_path), "%s/ffmpeg_list_%d.txt",
             assembly_state->work_directory, getpid());

    FILE *list_file_handle = fopen(list_file_path, "w");
    if (list_file_handle == NULL)
    {
        log_write(LOG_ERROR, "concatFiles: cannot create list file %s", list_file_path);
        return -1;
    }

    for (int file_position = 0; file_position < file_count; file_position++)
    {
        fprintf(list_file_handle, "file '%s/%s'\n",
                assembly_state->work_directory, file_list[file_position]);
    }

    fclose(list_file_handle);

    char shell_command[2048];
    snprintf(shell_command, sizeof(shell_command),
             "ffmpeg -f concat -safe 0 -i %s -c copy -movflags +faststart \"%s\" -y </dev/null",
             list_file_path, output_path);

    int result = system(shell_command);
    if (result != 0)
    {
        log_write(LOG_ERROR, "concatFiles: ffmpeg failed for %s (ret=%d)", output_path, result);
        unlink(list_file_path);
        return -1;
    }

    unlink(list_file_path);
    log_write(LOG_INFO, "concatFiles: created %s", output_path);
    return 0;
}

static void free_state(streamer_assembly_state *assembly_state)
{
    if (assembly_state == NULL)
    {
        return;
    }

    for (int pending_position = 0; pending_position < assembly_state->pending_count; pending_position++)
    {
        free(assembly_state->pending_files[pending_position]);
    }

    free(assembly_state->pending_files);
    free(assembly_state);
}