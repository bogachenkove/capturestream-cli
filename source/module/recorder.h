#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

typedef enum
{
    RECORDER_STATE_IDLE,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_ERROR,
    RECORDER_STATE_STOPPED
} recorder_state;

typedef struct recorder_context
{
    char streamer_name[64];
    char platform_name[16];
    char work_directory[256];
    char log_file_path[256];
    int segment_duration_seconds;
    int failure_cooldown_seconds;
    int stream_timeout_seconds;
    int retry_streams_seconds;
    int retry_open_attempts;
    char stream_quality[32];
    recorder_state state;
    atomic_int stop_requested;
    pthread_t thread_id;
    pid_t child_pid;
    time_t last_start_time;
    int consecutive_failures;
    pthread_mutex_t context_lock;
    void *parent_manager;
} recorder_context;

recorder_context* recorder_create(const char *streamer, const char *platform,
                                  const char *base_dir, const void *config);
int recorder_start(recorder_context *context);
void recorder_stop(recorder_context *context, int finalize);
void recorder_destroy(recorder_context *context);
void recorder_update_configuration(recorder_context *context, const void *new_config);
recorder_state recorder_get_state(recorder_context *context);
void recorder_get_status_string(recorder_context *context, char *buffer, size_t buffer_size);

#endif