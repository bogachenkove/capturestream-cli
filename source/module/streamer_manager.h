#ifndef STREAMER_MANAGER_H
#define STREAMER_MANAGER_H

#include "recorder.h"
#include <pthread.h>

typedef struct streamer_manager
{
    recorder_context **recorders;
    int recorder_count;
    int recorder_capacity;
    pthread_mutex_t manager_lock;
    const void *configuration;
    void (*on_streamer_status_change)(const char *streamer, recorder_state state);
} streamer_manager;

streamer_manager* streamer_manager_create(const void *initial_config);
void streamer_manager_destroy(streamer_manager *manager);
int streamer_manager_reload_configuration(streamer_manager *manager, const void *new_config);
int streamer_manager_add_streamer(streamer_manager *manager, const char *streamer, const char *platform);
int streamer_manager_remove_streamer(streamer_manager *manager, const char *streamer,
                                     const char *platform, int finalize);
int streamer_manager_get_count(streamer_manager *manager);
int streamer_manager_get_status_strings(streamer_manager *manager, char **buffer, int max_count);
int streamer_manager_get_streamer_info(streamer_manager *manager, int index,
                                       char *name, size_t name_size,
                                       char *platform, size_t platform_size,
                                       char *work_dir, size_t dir_size);

#endif