#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <pthread.h>
#include <jansson.h>
#include <sys/inotify.h>

typedef struct
{
    char base_directory[256];
    int segment_duration_seconds;
    int failure_cooldown_seconds;
    int stream_timeout_seconds;
    int retry_streams_seconds;
    int retry_open_attempts;
    char disk_mount_point[64];
    int disk_free_percent_limit;
    int disk_free_absolute_limit_gb;
    int disk_check_interval_seconds;
    int memory_check_interval_seconds;
    int memory_warning_percent;
    int memory_critical_percent;
    int memory_warning_mb;
    int memory_critical_mb;
    int subprocess_rss_limit_mb;
    int buffer_max_size_mb;
    int log_level;
    int concat_max_size_mb;
    int concat_keep_ts;
    int concat_scan_interval_seconds;
    int concat_force_on_low_disk;
    int max_mp4_files_to_keep;
    char twitch_streamers[2048];
    char configuration_path[256];
    char stream_quality[32];
} configuration;

typedef struct
{
    configuration current_configuration;
    pthread_rwlock_t configuration_lock;
    int inotify_file_descriptor;
    int inotify_watch_descriptor;
    pthread_t reload_thread_identifier;
    volatile int reload_thread_active;
    void (*on_change_callback)(const configuration*);
} configuration_manager;

configuration_manager* configuration_create(const char *path);
void configuration_destroy(configuration_manager *manager);
int configuration_reload(configuration_manager *manager);
void configuration_set_change_callback(configuration_manager *manager,
                                       void (*callback)(const configuration*));
const configuration* configuration_acquire(configuration_manager *manager);
void configuration_release(configuration_manager *manager);
int configuration_set_int(configuration_manager *manager, const char *key, int value);
int configuration_set_string(configuration_manager *manager, const char *key, const char *value);

#endif