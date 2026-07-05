#include "cli_setup.h"
#include "config.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

cli_action cli_parse_arguments (int argc, char **argv, char **set_value)
{
 if (argc <= 1)
 {
		return CLI_ACTION_FOREGROUND;
 }

 if (strcmp (argv[1], "--help") == 0 || strcmp (argv[1], "-h") == 0)
 {
		return CLI_ACTION_HELP;
 }
 else if (strcmp (argv[1], "start") == 0)
 {
		return CLI_ACTION_START;
 }
 else if (strcmp (argv[1], "stop") == 0)
 {
		return CLI_ACTION_STOP;
 }
 else if (strcmp (argv[1], "status") == 0)
 {
		return CLI_ACTION_STATUS;
 }
 else if (strcmp (argv[1], "reload") == 0)
 {
		return CLI_ACTION_RELOAD;
 }
 else if (strcmp (argv[1], "set") == 0)
 {
		if (argc < 3)
		{
			fprintf (stderr, "Usage: %s set key=value\n", argv[0]);
			return CLI_ACTION_ERROR;
		}
		if (strchr (argv[2], '=') == NULL)
		{
			fprintf (stderr, "Invalid format. Use: key=value\n");
			return CLI_ACTION_ERROR;
		}
		*set_value = argv[2];
		return CLI_ACTION_SET;
 }
 else
 {
		fprintf (stderr, "Unknown command: %s\n", argv[1]);
		return CLI_ACTION_ERROR;
 }
}

void cli_print_help (void)
{
 printf ("Usage: %s [COMMAND]\n", "capturestream-cli");
 printf ("Commands:\n");
 printf ("  (no command)  Run in foreground with interactive UI (debug mode)\n");
 printf ("  start         Start as a daemon (background)\n");
 printf ("  stop          Stop the running daemon\n");
 printf ("  status        Show daemon status (running or not)\n");
 printf ("  reload        Reload configuration file (SIGHUP)\n");
 printf ("  set key=value Change configuration parameter on the fly\n");
 printf ("  --help, -h    Show this help message\n");
 printf ("Configuration:\n");
 printf ("  If config.json does not exist, an interactive setup will run on first start.\n");
 printf ("  Configuration file is stored in the same directory as the executable.\n");
 printf ("  Logs and PID file are also stored locally.\n");
 printf ("Note: This program must NOT be run as root.\n");
}

void cli_interactive_config_setup (const char *config_path)
{
 configuration default_configuration;
 memset (&default_configuration, 0, sizeof (configuration));

 utils_strlcpy (default_configuration.base_directory, "./recordings", sizeof (default_configuration.base_directory));
 default_configuration.segment_duration_seconds = 500;
 default_configuration.failure_cooldown_seconds = 60;
 default_configuration.stream_timeout_seconds = 120;
 default_configuration.retry_streams_seconds = 30;
 default_configuration.retry_open_attempts = 5;
 utils_strlcpy (default_configuration.disk_mount_point, "/", sizeof (default_configuration.disk_mount_point));
 default_configuration.disk_free_percent_limit = 5;
 default_configuration.disk_free_absolute_limit_gb = 5;
 default_configuration.disk_check_interval_seconds = 30;
 default_configuration.memory_check_interval_seconds = 10;
 default_configuration.memory_warning_percent = 70;
 default_configuration.memory_critical_percent = 85;
 default_configuration.memory_warning_mb = 500;
 default_configuration.memory_critical_mb = 200;
 default_configuration.subprocess_rss_limit_mb = 2048;
 default_configuration.buffer_max_size_mb = 64;
 default_configuration.log_level = 2;
 default_configuration.concat_max_size_mb = 1950;
 default_configuration.concat_keep_ts = 0;
 default_configuration.concat_scan_interval_seconds = 10;
 default_configuration.concat_force_on_low_disk = 1;
 default_configuration.max_mp4_files_to_keep = 50;
 default_configuration.twitch_streamers[0] = '\0';
 utils_strlcpy (default_configuration.stream_quality, "720p", sizeof (default_configuration.stream_quality));

 printf ("\n=== Interactive configuration setup ===\n");
 printf ("Press Enter to accept default values shown in brackets.\n");

 char user_input[1024];

 printf ("Base directory for recordings [%s]: ", default_configuration.base_directory);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		user_input[strcspn (user_input, "\n")] = '\0';
		utils_strlcpy (default_configuration.base_directory, user_input, sizeof (default_configuration.base_directory));
 }

 printf ("Segment duration in seconds [%d]: ", default_configuration.segment_duration_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.segment_duration_seconds = atoi (user_input);
 }

 printf ("Failure cooldown in seconds [%d]: ", default_configuration.failure_cooldown_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.failure_cooldown_seconds = atoi (user_input);
 }

 printf ("Stream timeout in seconds [%d]: ", default_configuration.stream_timeout_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.stream_timeout_seconds = atoi (user_input);
 }

 printf ("Retry streams interval in seconds [%d]: ", default_configuration.retry_streams_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.retry_streams_seconds = atoi (user_input);
 }

 printf ("Retry open attempts [%d]: ", default_configuration.retry_open_attempts);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.retry_open_attempts = atoi (user_input);
 }

 printf ("Disk mount point [%s]: ", default_configuration.disk_mount_point);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		user_input[strcspn (user_input, "\n")] = '\0';
		utils_strlcpy (default_configuration.disk_mount_point, user_input, sizeof (default_configuration.disk_mount_point));
 }

 printf ("Disk free percent limit [%d]: ", default_configuration.disk_free_percent_limit);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.disk_free_percent_limit = atoi (user_input);
 }

 printf ("Disk free absolute limit in GB [%d]: ", default_configuration.disk_free_absolute_limit_gb);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.disk_free_absolute_limit_gb = atoi (user_input);
 }

 printf ("Disk check interval in seconds [%d]: ", default_configuration.disk_check_interval_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.disk_check_interval_seconds = atoi (user_input);
 }

 printf ("Memory check interval in seconds [%d]: ", default_configuration.memory_check_interval_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.memory_check_interval_seconds = atoi (user_input);
 }

 printf ("Memory warning percent [%d]: ", default_configuration.memory_warning_percent);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.memory_warning_percent = atoi (user_input);
 }

 printf ("Memory critical percent [%d]: ", default_configuration.memory_critical_percent);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.memory_critical_percent = atoi (user_input);
 }

 printf ("Memory warning in MB [%d]: ", default_configuration.memory_warning_mb);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.memory_warning_mb = atoi (user_input);
 }

 printf ("Memory critical in MB [%d]: ", default_configuration.memory_critical_mb);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.memory_critical_mb = atoi (user_input);
 }

 printf ("Subprocess RSS limit in MB [%d]: ", default_configuration.subprocess_rss_limit_mb);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.subprocess_rss_limit_mb = atoi (user_input);
 }

 printf ("Buffer max size in MB [%d]: ", default_configuration.buffer_max_size_mb);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.buffer_max_size_mb = atoi (user_input);
 }

 printf ("Log level (0=DEBUG,1=INFO,2=WARN,3=ERROR,4=FATAL) [%d]: ", default_configuration.log_level);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.log_level = atoi (user_input);
 }

 printf ("MP4 concatenation max size in MB [%d]: ", default_configuration.concat_max_size_mb);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.concat_max_size_mb = atoi (user_input);
 }

 printf ("Keep TS files after concatenation (0=delete,1=keep) [%d]: ", default_configuration.concat_keep_ts);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.concat_keep_ts = atoi (user_input);
 }

 printf ("Concatenation scan interval in seconds [%d]: ", default_configuration.concat_scan_interval_seconds);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.concat_scan_interval_seconds = atoi (user_input);
 }

 printf ("Force concatenation on low disk (0=no,1=yes) [%d]: ", default_configuration.concat_force_on_low_disk);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.concat_force_on_low_disk = atoi (user_input);
 }

 printf ("Max MP4 files to keep [%d]: ", default_configuration.max_mp4_files_to_keep);
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		default_configuration.max_mp4_files_to_keep = atoi (user_input);
 }

 printf ("Twitch streamers (comma-separated, e.g. forsen,shroud) []: ");
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		user_input[strcspn (user_input, "\n")] = '\0';
		utils_strlcpy (default_configuration.twitch_streamers, user_input, sizeof (default_configuration.twitch_streamers));
 }

 printf ("Stream quality (best, 1080p, 720p, 480p, 360p, etc.) [720p]: ");
 if (fgets (user_input, sizeof (user_input), stdin) && user_input[0] != '\n')
 {
		user_input[strcspn (user_input, "\n")] = '\0';
		utils_strlcpy (default_configuration.stream_quality, user_input, sizeof (default_configuration.stream_quality));
 }

 json_t *json_root = json_object ();
 json_object_set_new (json_root, "base_directory", json_string (default_configuration.base_directory));
 json_object_set_new (json_root, "segment_duration_seconds", json_integer (default_configuration.segment_duration_seconds));
 json_object_set_new (json_root, "failure_cooldown_seconds", json_integer (default_configuration.failure_cooldown_seconds));
 json_object_set_new (json_root, "stream_timeout_seconds", json_integer (default_configuration.stream_timeout_seconds));
 json_object_set_new (json_root, "retry_streams_seconds", json_integer (default_configuration.retry_streams_seconds));
 json_object_set_new (json_root, "retry_open_attempts", json_integer (default_configuration.retry_open_attempts));
 json_object_set_new (json_root, "disk_mount_point", json_string (default_configuration.disk_mount_point));
 json_object_set_new (json_root, "disk_free_percent_limit", json_integer (default_configuration.disk_free_percent_limit));
 json_object_set_new (json_root, "disk_free_absolute_limit_gb", json_integer (default_configuration.disk_free_absolute_limit_gb));
 json_object_set_new (json_root, "disk_check_interval_seconds", json_integer (default_configuration.disk_check_interval_seconds));
 json_object_set_new (json_root, "memory_check_interval_seconds", json_integer (default_configuration.memory_check_interval_seconds));
 json_object_set_new (json_root, "memory_warning_percent", json_integer (default_configuration.memory_warning_percent));
 json_object_set_new (json_root, "memory_critical_percent", json_integer (default_configuration.memory_critical_percent));
 json_object_set_new (json_root, "memory_warning_mb", json_integer (default_configuration.memory_warning_mb));
 json_object_set_new (json_root, "memory_critical_mb", json_integer (default_configuration.memory_critical_mb));
 json_object_set_new (json_root, "subprocess_rss_limit_mb", json_integer (default_configuration.subprocess_rss_limit_mb));
 json_object_set_new (json_root, "buffer_max_size_mb", json_integer (default_configuration.buffer_max_size_mb));
 json_object_set_new (json_root, "log_level", json_integer (default_configuration.log_level));
 json_object_set_new (json_root, "concat_max_size_mb", json_integer (default_configuration.concat_max_size_mb));
 json_object_set_new (json_root, "concat_keep_ts", json_integer (default_configuration.concat_keep_ts));
 json_object_set_new (json_root, "concat_scan_interval_seconds", json_integer (default_configuration.concat_scan_interval_seconds));
 json_object_set_new (json_root, "concat_force_on_low_disk", json_integer (default_configuration.concat_force_on_low_disk));
 json_object_set_new (json_root, "max_mp4_files_to_keep", json_integer (default_configuration.max_mp4_files_to_keep));
 json_object_set_new (json_root, "twitch_streamers", json_string (default_configuration.twitch_streamers));
 json_object_set_new (json_root, "stream_quality", json_string (default_configuration.stream_quality));

 if (json_dump_file (json_root, config_path, JSON_INDENT (4)) != 0)
 {
		fprintf (stderr, "Failed to write configuration to %s\n", config_path);
		json_decref (json_root);
		exit (EXIT_FAILURE);
 }

 json_decref (json_root);
 printf ("\nConfiguration saved to %s\n", config_path);
}
