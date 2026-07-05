#include "config.h"
#include "config_parser.h"
#include "config_watcher.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

configuration_manager *configuration_create (const char *path)
{
 if (path == NULL)
 {
		log_write (LOG_ERROR, "configurationCreate: path is NULL");
		return NULL;
 }

 configuration_manager *manager = calloc (1, sizeof (configuration_manager));
 if (manager == NULL)
 {
		log_write (LOG_ERROR, "configurationCreate: calloc failed");
		return NULL;
 }

 if (pthread_rwlock_init (&manager->configuration_lock, NULL) != 0)
 {
		log_write (LOG_ERROR, "configurationCreate: pthread_rwlock_init failed");
		free (manager);
		return NULL;
 }

 strncpy (manager->current_configuration.configuration_path, path, sizeof (manager->current_configuration.configuration_path) - 1);
 manager->current_configuration.configuration_path[sizeof (manager->current_configuration.configuration_path) - 1] = '\0';

 if (config_parser_load (path, &manager->current_configuration) != 0)
 {
		log_write (LOG_ERROR, "configurationCreate: initial load failed from %s", path);
		pthread_rwlock_destroy (&manager->configuration_lock);
		free (manager);
		return NULL;
 }

 manager->on_change_callback = NULL;
 manager->inotify_file_descriptor = -1;

 if (config_watcher_start (manager) != 0)
 {
		log_write (LOG_ERROR, "configurationCreate: watcher start failed");
		pthread_rwlock_destroy (&manager->configuration_lock);
		free (manager);
		return NULL;
 }

 log_write (LOG_INFO, "configurationCreate: manager created with path %s", path);
 return manager;
}

void configuration_destroy (configuration_manager *manager)
{
 if (manager == NULL)
 {
		return;
 }

 config_watcher_stop (manager);

 pthread_rwlock_destroy (&manager->configuration_lock);
 free (manager);
 log_write (LOG_INFO, "configurationDestroy: manager destroyed");
}

int configuration_reload (configuration_manager *manager)
{
 if (manager == NULL)
 {
		return -1;
 }

 configuration new_config;
 if (config_parser_load (manager->current_configuration.configuration_path, &new_config) != 0)
 {
		log_write (LOG_ERROR, "configurationReload: load failed");
		return -1;
 }

 pthread_rwlock_wrlock (&manager->configuration_lock);
 manager->current_configuration = new_config;
 pthread_rwlock_unlock (&manager->configuration_lock);

 log_write (LOG_INFO, "configurationReload: configuration reloaded successfully");

 if (manager->on_change_callback != NULL)
 {
		manager->on_change_callback (&manager->current_configuration);
 }

 return 0;
}

void configuration_set_change_callback (configuration_manager *manager, void (*callback) (const configuration *))
{
 if (manager != NULL)
 {
		manager->on_change_callback = callback;
 }
}

const configuration *configuration_acquire (configuration_manager *manager)
{
 if (manager == NULL)
 {
		return NULL;
 }

 pthread_rwlock_rdlock (&manager->configuration_lock);
 return &manager->current_configuration;
}

void configuration_release (configuration_manager *manager)
{
 if (manager != NULL)
 {
		pthread_rwlock_unlock (&manager->configuration_lock);
 }
}

int configuration_set_int (configuration_manager *manager, const char *key, int value)
{
 if (manager == NULL || key == NULL)
 {
		return -1;
 }

 pthread_rwlock_wrlock (&manager->configuration_lock);
 configuration *target_configuration = &manager->current_configuration;
 int match_found = 0;

 if (strcmp (key, "segment_duration_seconds") == 0)
 {
		target_configuration->segment_duration_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "failure_cooldown_seconds") == 0)
 {
		target_configuration->failure_cooldown_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "stream_timeout_seconds") == 0)
 {
		target_configuration->stream_timeout_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "retry_streams_seconds") == 0)
 {
		target_configuration->retry_streams_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "retry_open_attempts") == 0)
 {
		target_configuration->retry_open_attempts = value;
		match_found = 1;
 }
 else if (strcmp (key, "disk_free_percent_limit") == 0)
 {
		target_configuration->disk_free_percent_limit = value;
		match_found = 1;
 }
 else if (strcmp (key, "disk_free_absolute_limit_gb") == 0)
 {
		target_configuration->disk_free_absolute_limit_gb = value;
		match_found = 1;
 }
 else if (strcmp (key, "disk_check_interval_seconds") == 0)
 {
		target_configuration->disk_check_interval_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "memory_check_interval_seconds") == 0)
 {
		target_configuration->memory_check_interval_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "memory_warning_percent") == 0)
 {
		target_configuration->memory_warning_percent = value;
		match_found = 1;
 }
 else if (strcmp (key, "memory_critical_percent") == 0)
 {
		target_configuration->memory_critical_percent = value;
		match_found = 1;
 }
 else if (strcmp (key, "memory_warning_mb") == 0)
 {
		target_configuration->memory_warning_mb = value;
		match_found = 1;
 }
 else if (strcmp (key, "memory_critical_mb") == 0)
 {
		target_configuration->memory_critical_mb = value;
		match_found = 1;
 }
 else if (strcmp (key, "subprocess_rss_limit_mb") == 0)
 {
		target_configuration->subprocess_rss_limit_mb = value;
		match_found = 1;
 }
 else if (strcmp (key, "buffer_max_size_mb") == 0)
 {
		target_configuration->buffer_max_size_mb = value;
		match_found = 1;
 }
 else if (strcmp (key, "log_level") == 0)
 {
		target_configuration->log_level = value;
		match_found = 1;
 }
 else if (strcmp (key, "concat_max_size_mb") == 0)
 {
		target_configuration->concat_max_size_mb = value;
		match_found = 1;
 }
 else if (strcmp (key, "concat_keep_ts") == 0)
 {
		target_configuration->concat_keep_ts = value;
		match_found = 1;
 }
 else if (strcmp (key, "concat_scan_interval_seconds") == 0)
 {
		target_configuration->concat_scan_interval_seconds = value;
		match_found = 1;
 }
 else if (strcmp (key, "concat_force_on_low_disk") == 0)
 {
		target_configuration->concat_force_on_low_disk = value;
		match_found = 1;
 }
 else if (strcmp (key, "max_mp4_files_to_keep") == 0)
 {
		target_configuration->max_mp4_files_to_keep = value;
		match_found = 1;
 }

 pthread_rwlock_unlock (&manager->configuration_lock);

 if (match_found)
 {
		log_write (LOG_INFO, "configurationSetInt: %s = %d", key, value);
		if (manager->on_change_callback != NULL)
		{
			manager->on_change_callback (&manager->current_configuration);
		}
		return 0;
 }
 else
 {
		log_write (LOG_WARN, "configurationSetInt: unknown key %s", key);
		return -1;
 }
}

int configuration_set_string (configuration_manager *manager, const char *key, const char *value)
{
 if (manager == NULL || key == NULL || value == NULL)
 {
		return -1;
 }

 pthread_rwlock_wrlock (&manager->configuration_lock);
 configuration *target_configuration = &manager->current_configuration;
 int match_found = 0;

 if (strcmp (key, "base_directory") == 0)
 {
		strncpy (target_configuration->base_directory, value, sizeof (target_configuration->base_directory) - 1);
		target_configuration->base_directory[sizeof (target_configuration->base_directory) - 1] = '\0';
		match_found = 1;
 }
 else if (strcmp (key, "disk_mount_point") == 0)
 {
		strncpy (target_configuration->disk_mount_point, value, sizeof (target_configuration->disk_mount_point) - 1);
		target_configuration->disk_mount_point[sizeof (target_configuration->disk_mount_point) - 1] = '\0';
		match_found = 1;
 }
 else if (strcmp (key, "twitch_streamers") == 0)
 {
		strncpy (target_configuration->twitch_streamers, value, sizeof (target_configuration->twitch_streamers) - 1);
		target_configuration->twitch_streamers[sizeof (target_configuration->twitch_streamers) - 1] = '\0';
		match_found = 1;
 }
 else if (strcmp (key, "stream_quality") == 0)
 {
		strncpy (target_configuration->stream_quality, value, sizeof (target_configuration->stream_quality) - 1);
		target_configuration->stream_quality[sizeof (target_configuration->stream_quality) - 1] = '\0';
		match_found = 1;
 }

 pthread_rwlock_unlock (&manager->configuration_lock);

 if (match_found)
 {
		log_write (LOG_INFO, "configurationSetString: %s = %s", key, value);
		if (manager->on_change_callback != NULL)
		{
			manager->on_change_callback (&manager->current_configuration);
		}
		return 0;
 }
 else
 {
		log_write (LOG_WARN, "configurationSetString: unknown key %s", key);
		return -1;
 }
}
