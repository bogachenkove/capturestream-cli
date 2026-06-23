#include "streamer_manager.h"
#include "streamer_list.h"
#include "logger.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

streamer_manager* streamer_manager_create(const void *initial_config)
{
    streamer_manager *manager = calloc(1, sizeof(streamer_manager));
    if (manager == NULL)
    {
        log_write(LOG_ERROR, "streamerManagerCreate: calloc failed");
        return NULL;
    }

    pthread_mutex_init(&manager->manager_lock, NULL);
    manager->configuration = initial_config;
    manager->recorders = NULL;
    manager->recorder_count = 0;
    manager->recorder_capacity = 0;
    manager->on_streamer_status_change = NULL;

    const configuration *configuration_snapshot = (const configuration*)initial_config;

    if (strlen(configuration_snapshot->twitch_streamers) > 0)
    {
        char **names = NULL;
        int name_count = 0;

        if (streamer_list_parse(configuration_snapshot->twitch_streamers, &names, &name_count) == 0)
        {
            for (int streamer_index = 0; streamer_index < name_count; streamer_index++)
            {
                recorder_context *context = recorder_create(names[streamer_index], "twitch",
                                                            configuration_snapshot->base_directory, configuration_snapshot);
                if (context != NULL)
                {
                    if (recorder_start(context) == 0)
                    {
                        if (manager->recorder_count >= manager->recorder_capacity)
                        {
                            int new_capacity = (manager->recorder_capacity == 0) ? 8 : manager->recorder_capacity * 2;
                            recorder_context **new_array = realloc(manager->recorders,
                                                                   sizeof(recorder_context*) * new_capacity);
                            if (new_array == NULL)
                            {
                                log_write(LOG_ERROR, "streamerManagerCreate: realloc failed for %s", names[streamer_index]);
                                recorder_destroy(context);
                                streamer_list_free(names, name_count);
                                pthread_mutex_destroy(&manager->manager_lock);
                                free(manager);
                                return NULL;
                            }
                            manager->recorders = new_array;
                            manager->recorder_capacity = new_capacity;
                        }
                        manager->recorders[manager->recorder_count++] = context;
                        log_write(LOG_INFO, "streamerManagerCreate: added %s", names[streamer_index]);
                    }
                    else
                    {
                        recorder_destroy(context);
                        log_write(LOG_ERROR, "streamerManagerCreate: failed to start recorder for %s", names[streamer_index]);
                    }
                }
                else
                {
                    log_write(LOG_ERROR, "streamerManagerCreate: failed to create recorder for %s", names[streamer_index]);
                }
            }
            streamer_list_free(names, name_count);
        }
    }

    return manager;
}

void streamer_manager_destroy(streamer_manager *manager)
{
    if (manager == NULL)
    {
        return;
    }

    pthread_mutex_lock(&manager->manager_lock);
    for (int recorder_index = 0; recorder_index < manager->recorder_count; recorder_index++)
    {
        recorder_stop(manager->recorders[recorder_index], 1);
        recorder_destroy(manager->recorders[recorder_index]);
    }
    free(manager->recorders);
    manager->recorders = NULL;
    manager->recorder_count = 0;
    manager->recorder_capacity = 0;
    pthread_mutex_unlock(&manager->manager_lock);

    pthread_mutex_destroy(&manager->manager_lock);
    free(manager);
    log_write(LOG_INFO, "streamerManagerDestroy: manager destroyed");
}

int streamer_manager_reload_configuration(streamer_manager *manager, const void *new_config)
{
    if (manager == NULL || new_config == NULL)
    {
        return -1;
    }

    const configuration *configuration_snapshot = (const configuration*)new_config;
    pthread_mutex_lock(&manager->manager_lock);

    char **new_names = NULL;
    int new_count = 0;

    if (strlen(configuration_snapshot->twitch_streamers) > 0)
    {
        if (streamer_list_parse(configuration_snapshot->twitch_streamers, &new_names, &new_count) != 0)
        {
            pthread_mutex_unlock(&manager->manager_lock);
            return -1;
        }
    }

    int *remove_flags = calloc(manager->recorder_count, sizeof(int));
    if (remove_flags == NULL)
    {
        pthread_mutex_unlock(&manager->manager_lock);
        streamer_list_free(new_names, new_count);
        return -1;
    }

    for (int existing_index = 0; existing_index < manager->recorder_count; existing_index++)
    {
        recorder_context *context = manager->recorders[existing_index];
        int match_found = 0;

        for (int new_index = 0; new_index < new_count; new_index++)
        {
            if (strcmp(context->streamer_name, new_names[new_index]) == 0 &&
                strcmp(context->platform_name, "twitch") == 0)
            {
                match_found = 1;
                break;
            }
        }

        if (!match_found)
        {
            remove_flags[existing_index] = 1;
        }
    }

    for (int existing_index = manager->recorder_count - 1; existing_index >= 0; existing_index--)
    {
        if (remove_flags[existing_index])
        {
            recorder_stop(manager->recorders[existing_index], 1);
            recorder_destroy(manager->recorders[existing_index]);

            for (int shift_index = existing_index; shift_index < manager->recorder_count - 1; shift_index++)
            {
                manager->recorders[shift_index] = manager->recorders[shift_index + 1];
            }
            manager->recorder_count--;
        }
    }

    free(remove_flags);

    for (int new_index = 0; new_index < new_count; new_index++)
    {
        int presence = 0;

        for (int existing_index = 0; existing_index < manager->recorder_count; existing_index++)
        {
            if (strcmp(manager->recorders[existing_index]->streamer_name, new_names[new_index]) == 0 &&
                strcmp(manager->recorders[existing_index]->platform_name, "twitch") == 0)
            {
                presence = 1;
                break;
            }
        }

        if (!presence)
        {
            recorder_context *context = recorder_create(new_names[new_index], "twitch",
                                                        configuration_snapshot->base_directory, configuration_snapshot);
            if (context != NULL)
            {
                if (recorder_start(context) == 0)
                {
                    if (manager->recorder_count >= manager->recorder_capacity)
                    {
                        int new_capacity = (manager->recorder_capacity == 0) ? 8 : manager->recorder_capacity * 2;
                        recorder_context **new_array = realloc(manager->recorders,
                                                               sizeof(recorder_context*) * new_capacity);
                        if (new_array == NULL)
                        {
                            log_write(LOG_ERROR, "streamerManagerReload: realloc failed for %s", new_names[new_index]);
                            recorder_destroy(context);
                            continue;
                        }
                        manager->recorders = new_array;
                        manager->recorder_capacity = new_capacity;
                    }
                    manager->recorders[manager->recorder_count++] = context;
                    log_write(LOG_INFO, "streamerManagerReload: added %s", new_names[new_index]);
                }
                else
                {
                    recorder_destroy(context);
                    log_write(LOG_ERROR, "streamerManagerReload: failed to start %s", new_names[new_index]);
                }
            }
            else
            {
                log_write(LOG_ERROR, "streamerManagerReload: failed to create %s", new_names[new_index]);
            }
        }
    }

    streamer_list_free(new_names, new_count);

    for (int recorder_index = 0; recorder_index < manager->recorder_count; recorder_index++)
    {
        recorder_update_configuration(manager->recorders[recorder_index], configuration_snapshot);
    }

    manager->configuration = new_config;
    pthread_mutex_unlock(&manager->manager_lock);

    log_write(LOG_INFO, "streamerManagerReload: reloaded, now %d recorders", manager->recorder_count);
    return 0;
}

int streamer_manager_add_streamer(streamer_manager *manager, const char *streamer, const char *platform)
{
    if (manager == NULL || streamer == NULL || platform == NULL)
    {
        return -1;
    }

    pthread_mutex_lock(&manager->manager_lock);

    int existing_index = streamer_list_find_index(manager, streamer, platform);
    if (existing_index >= 0)
    {
        pthread_mutex_unlock(&manager->manager_lock);
        log_write(LOG_WARN, "streamerManagerAddStreamer: %s/%s already exists", platform, streamer);
        return -1;
    }

    const configuration *configuration_snapshot = (const configuration*)manager->configuration;
    recorder_context *context = recorder_create(streamer, platform, configuration_snapshot->base_directory, configuration_snapshot);
    if (context == NULL)
    {
        pthread_mutex_unlock(&manager->manager_lock);
        return -1;
    }

    if (recorder_start(context) != 0)
    {
        recorder_destroy(context);
        pthread_mutex_unlock(&manager->manager_lock);
        return -1;
    }

    if (manager->recorder_count >= manager->recorder_capacity)
    {
        int new_capacity = (manager->recorder_capacity == 0) ? 8 : manager->recorder_capacity * 2;
        recorder_context **new_array = realloc(manager->recorders, sizeof(recorder_context*) * new_capacity);
        if (new_array == NULL)
        {
            recorder_stop(context, 0);
            recorder_destroy(context);
            pthread_mutex_unlock(&manager->manager_lock);
            return -1;
        }
        manager->recorders = new_array;
        manager->recorder_capacity = new_capacity;
    }

    manager->recorders[manager->recorder_count++] = context;
    pthread_mutex_unlock(&manager->manager_lock);

    log_write(LOG_INFO, "streamerManagerAddStreamer: added %s/%s", platform, streamer);
    return 0;
}

int streamer_manager_remove_streamer(streamer_manager *manager, const char *streamer,
                                     const char *platform, int finalize)
{
    if (manager == NULL || streamer == NULL || platform == NULL)
    {
        return -1;
    }

    pthread_mutex_lock(&manager->manager_lock);

    int existing_index = streamer_list_find_index(manager, streamer, platform);
    if (existing_index < 0)
    {
        pthread_mutex_unlock(&manager->manager_lock);
        return -1;
    }

    recorder_context *context = manager->recorders[existing_index];
    recorder_stop(context, finalize);
    recorder_destroy(context);

    for (int shift_index = existing_index; shift_index < manager->recorder_count - 1; shift_index++)
    {
        manager->recorders[shift_index] = manager->recorders[shift_index + 1];
    }
    manager->recorder_count--;

    pthread_mutex_unlock(&manager->manager_lock);
    log_write(LOG_INFO, "streamerManagerRemoveStreamer: removed %s/%s", platform, streamer);
    return 0;
}

int streamer_manager_get_count(streamer_manager *manager)
{
    if (manager == NULL)
    {
        return 0;
    }

    pthread_mutex_lock(&manager->manager_lock);
    int count = manager->recorder_count;
    pthread_mutex_unlock(&manager->manager_lock);
    return count;
}

int streamer_manager_get_status_strings(streamer_manager *manager, char **buffer, int max_count)
{
    if (manager == NULL || buffer == NULL || max_count <= 0)
    {
        return 0;
    }

    pthread_mutex_lock(&manager->manager_lock);

    int output_count = (manager->recorder_count < max_count) ? manager->recorder_count : max_count;

    for (int status_index = 0; status_index < output_count; status_index++)
    {
        buffer[status_index] = malloc(256);
        if (buffer[status_index] == NULL)
        {
            for (int free_index = 0; free_index < status_index; free_index++)
            {
                free(buffer[free_index]);
                buffer[free_index] = NULL;
            }
            pthread_mutex_unlock(&manager->manager_lock);
            return 0;
        }
        recorder_get_status_string(manager->recorders[status_index], buffer[status_index], 256);
    }

    pthread_mutex_unlock(&manager->manager_lock);
    return output_count;
}

int streamer_manager_get_streamer_info(streamer_manager *manager, int index,
                                       char *name, size_t name_size,
                                       char *platform, size_t platform_size,
                                       char *work_dir, size_t dir_size)
{
    if (manager == NULL || index < 0 || index >= manager->recorder_count)
    {
        return -1;
    }

    recorder_context *context = manager->recorders[index];
    pthread_mutex_lock(&context->context_lock);

    strncpy(name, context->streamer_name, name_size - 1);
    name[name_size - 1] = '\0';

    strncpy(platform, context->platform_name, platform_size - 1);
    platform[platform_size - 1] = '\0';

    strncpy(work_dir, context->work_directory, dir_size - 1);
    work_dir[dir_size - 1] = '\0';

    pthread_mutex_unlock(&context->context_lock);
    return 0;
}