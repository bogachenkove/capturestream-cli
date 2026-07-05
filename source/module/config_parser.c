#include "config_parser.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

static int parse_integer (const json_t *object, const char *key, int *destination, int default_value);
static int parse_string (const json_t *object, const char *key, char *destination, size_t max_len, const char *default_value);

int config_parser_load (const char *path, configuration *target_configuration)
{
 json_error_t json_error;
 json_t *root = json_load_file (path, 0, &json_error);

 if (root == NULL)
 {
		log_write (LOG_ERROR, "configParserLoad: json_load_file failed on %s: %s", path, json_error.text);
		return -1;
 }

 if (!json_is_object (root))
 {
		log_write (LOG_ERROR, "configParserLoad: root is not object");
		json_decref (root);
		return -1;
 }

 memset (target_configuration, 0, sizeof (configuration));

 strncpy (target_configuration->base_directory, "/var/recorder", sizeof (target_configuration->base_directory) - 1);
 target_configuration->segment_duration_seconds = 500;
 target_configuration->failure_cooldown_seconds = 60;
 target_configuration->stream_timeout_seconds = 120;
 target_configuration->retry_streams_seconds = 30;
 target_configuration->retry_open_attempts = 5;
 strncpy (target_configuration->disk_mount_point, "/", sizeof (target_configuration->disk_mount_point) - 1);
 target_configuration->disk_free_percent_limit = 5;
 target_configuration->disk_free_absolute_limit_gb = 5;
 target_configuration->disk_check_interval_seconds = 30;
 target_configuration->memory_check_interval_seconds = 10;
 target_configuration->memory_warning_percent = 70;
 target_configuration->memory_critical_percent = 85;
 target_configuration->memory_warning_mb = 500;
 target_configuration->memory_critical_mb = 200;
 target_configuration->subprocess_rss_limit_mb = 2048;
 target_configuration->buffer_max_size_mb = 64;
 target_configuration->log_level = 2;
 target_configuration->concat_max_size_mb = 1950;
 target_configuration->concat_keep_ts = 0;
 target_configuration->concat_scan_interval_seconds = 10;
 target_configuration->concat_force_on_low_disk = 1;
 target_configuration->max_mp4_files_to_keep = 50;
 target_configuration->twitch_streamers[0] = '\0';
 strncpy (target_configuration->stream_quality, "720p", sizeof (target_configuration->stream_quality) - 1);
 target_configuration->stream_quality[sizeof (target_configuration->stream_quality) - 1] = '\0';

 parse_string (root, "base_directory", target_configuration->base_directory, sizeof (target_configuration->base_directory), target_configuration->base_directory);
 parse_integer (root, "segment_duration_seconds", &target_configuration->segment_duration_seconds, target_configuration->segment_duration_seconds);
 parse_integer (root, "failure_cooldown_seconds", &target_configuration->failure_cooldown_seconds, target_configuration->failure_cooldown_seconds);
 parse_integer (root, "stream_timeout_seconds", &target_configuration->stream_timeout_seconds, target_configuration->stream_timeout_seconds);
 parse_integer (root, "retry_streams_seconds", &target_configuration->retry_streams_seconds, target_configuration->retry_streams_seconds);
 parse_integer (root, "retry_open_attempts", &target_configuration->retry_open_attempts, target_configuration->retry_open_attempts);
 parse_string (root, "disk_mount_point", target_configuration->disk_mount_point, sizeof (target_configuration->disk_mount_point), target_configuration->disk_mount_point);
 parse_integer (root, "disk_free_percent_limit", &target_configuration->disk_free_percent_limit, target_configuration->disk_free_percent_limit);
 parse_integer (root, "disk_free_absolute_limit_gb", &target_configuration->disk_free_absolute_limit_gb, target_configuration->disk_free_absolute_limit_gb);
 parse_integer (root, "disk_check_interval_seconds", &target_configuration->disk_check_interval_seconds, target_configuration->disk_check_interval_seconds);
 parse_integer (root, "memory_check_interval_seconds", &target_configuration->memory_check_interval_seconds, target_configuration->memory_check_interval_seconds);
 parse_integer (root, "memory_warning_percent", &target_configuration->memory_warning_percent, target_configuration->memory_warning_percent);
 parse_integer (root, "memory_critical_percent", &target_configuration->memory_critical_percent, target_configuration->memory_critical_percent);
 parse_integer (root, "memory_warning_mb", &target_configuration->memory_warning_mb, target_configuration->memory_warning_mb);
 parse_integer (root, "memory_critical_mb", &target_configuration->memory_critical_mb, target_configuration->memory_critical_mb);
 parse_integer (root, "subprocess_rss_limit_mb", &target_configuration->subprocess_rss_limit_mb, target_configuration->subprocess_rss_limit_mb);
 parse_integer (root, "buffer_max_size_mb", &target_configuration->buffer_max_size_mb, target_configuration->buffer_max_size_mb);
 parse_integer (root, "log_level", &target_configuration->log_level, target_configuration->log_level);
 parse_integer (root, "concat_max_size_mb", &target_configuration->concat_max_size_mb, target_configuration->concat_max_size_mb);
 parse_integer (root, "concat_keep_ts", &target_configuration->concat_keep_ts, target_configuration->concat_keep_ts);
 parse_integer (root, "concat_scan_interval_seconds", &target_configuration->concat_scan_interval_seconds, target_configuration->concat_scan_interval_seconds);
 parse_integer (root, "concat_force_on_low_disk", &target_configuration->concat_force_on_low_disk, target_configuration->concat_force_on_low_disk);
 parse_integer (root, "max_mp4_files_to_keep", &target_configuration->max_mp4_files_to_keep, target_configuration->max_mp4_files_to_keep);
 parse_string (root, "twitch_streamers", target_configuration->twitch_streamers, sizeof (target_configuration->twitch_streamers), "");
 parse_string (root, "stream_quality", target_configuration->stream_quality, sizeof (target_configuration->stream_quality), "720p");

 strncpy (target_configuration->configuration_path, path, sizeof (target_configuration->configuration_path) - 1);
 target_configuration->configuration_path[sizeof (target_configuration->configuration_path) - 1] = '\0';

 json_decref (root);
 log_write (LOG_INFO, "configParserLoad: loaded from %s", path);

 return 0;
}

static int parse_integer (const json_t *object, const char *key, int *destination, int default_value)
{
 json_t *value = json_object_get (object, key);
 if (value == NULL)
 {
		*destination = default_value;
		return 0;
 }

 if (!json_is_integer (value))
 {
		log_write (LOG_WARN, "parseInteger: key '%s' is not integer, using default %d", key, default_value);
		*destination = default_value;
		return -1;
 }

 *destination = (int) json_integer_value (value);
 return 0;
}

static int parse_string (const json_t *object, const char *key, char *destination, size_t max_len, const char *default_value)
{
 json_t *value = json_object_get (object, key);
 if (value == NULL)
 {
		strncpy (destination, default_value, max_len - 1);
		destination[max_len - 1] = '\0';
		return 0;
 }

 if (!json_is_string (value))
 {
		log_write (LOG_WARN, "parseString: key '%s' is not string, using default", key);
		strncpy (destination, default_value, max_len - 1);
		destination[max_len - 1] = '\0';
		return -1;
 }

 const char *source_text = json_string_value (value);
 strncpy (destination, source_text, max_len - 1);
 destination[max_len - 1] = '\0';

 return 0;
}
