#include "recorder.h"
#include "logger.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#define FFMPEG_SHUTDOWN_TIMEOUT_US 2000000

static void* recorder_thread_function(void *argument);
static pid_t launch_streamlink_and_ffmpeg(recorder_context *context, const char *stream_url);
static int is_stream_live(const char *streamer, const char *platform);
static void handle_child_exit(recorder_context *context, pid_t process_id, int status);
static const char* build_quality_chain(const char *requested_quality);
static int validate_streamer_name(const char *name);
static void escape_shell_argument(const char *input, char *output, size_t output_size);

recorder_context* recorder_create(const char *streamer, const char *platform,
                                  const char *base_dir, const void *config)
{
    if (streamer == NULL || platform == NULL || base_dir == NULL || config == NULL)
    {
        log_write(LOG_ERROR, "recorderCreate: invalid arguments");
        return NULL;
    }

    if (!validate_streamer_name(streamer))
    {
        log_write(LOG_ERROR, "recorderCreate: invalid streamer name '%s'", streamer);
        return NULL;
    }

    if (!validate_streamer_name(platform))
    {
        log_write(LOG_ERROR, "recorderCreate: invalid platform name '%s'", platform);
        return NULL;
    }

    recorder_context *context = calloc(1, sizeof(recorder_context));
    if (context == NULL)
    {
        log_write(LOG_ERROR, "recorderCreate: calloc failed");
        return NULL;
    }

    strncpy(context->streamer_name, streamer, sizeof(context->streamer_name) - 1);
    context->streamer_name[sizeof(context->streamer_name) - 1] = '\0';

    strncpy(context->platform_name, platform, sizeof(context->platform_name) - 1);
    context->platform_name[sizeof(context->platform_name) - 1] = '\0';

    snprintf(context->work_directory, sizeof(context->work_directory), "%s/%s_%s",
             base_dir, platform, streamer);

    if (mkdir(context->work_directory, 0755) != 0 && errno != EEXIST)
    {
        log_write(LOG_ERROR, "recorderCreate: mkdir failed for %s: %s",
                  context->work_directory, strerror(errno));
        free(context);
        return NULL;
    }

    snprintf(context->log_file_path, sizeof(context->log_file_path), "%s/%s_%s.log",
             base_dir, platform, streamer);

    const configuration *configuration_snapshot = (const configuration*)config;
    context->segment_duration_seconds = configuration_snapshot->segment_duration_seconds;
    context->failure_cooldown_seconds = configuration_snapshot->failure_cooldown_seconds;
    context->stream_timeout_seconds = configuration_snapshot->stream_timeout_seconds;
    context->retry_streams_seconds = configuration_snapshot->retry_streams_seconds;
    context->retry_open_attempts = configuration_snapshot->retry_open_attempts;
    strncpy(context->stream_quality, configuration_snapshot->stream_quality, sizeof(context->stream_quality) - 1);
    context->stream_quality[sizeof(context->stream_quality) - 1] = '\0';

    context->state = RECORDER_STATE_IDLE;
    atomic_init(&context->stop_requested, 0);
    context->child_pid = -1;
    context->last_start_time = 0;
    context->consecutive_failures = 0;
    context->parent_manager = NULL;

    pthread_mutex_init(&context->context_lock, NULL);

    log_write(LOG_INFO, "recorderCreate: created for %s/%s with quality %s",
              platform, streamer, context->stream_quality);
    return context;
}

int recorder_start(recorder_context *context)
{
    if (context == NULL)
    {
        return -1;
    }

    pthread_mutex_lock(&context->context_lock);
    if (context->state != RECORDER_STATE_IDLE && context->state != RECORDER_STATE_STOPPED)
    {
        pthread_mutex_unlock(&context->context_lock);
        log_write(LOG_WARN, "recorderStart: recorder already running for %s", context->streamer_name);
        return -1;
    }

    atomic_store(&context->stop_requested, 0);
    context->state = RECORDER_STATE_IDLE;
    pthread_mutex_unlock(&context->context_lock);

    if (pthread_create(&context->thread_id, NULL, recorder_thread_function, context) != 0)
    {
        log_write(LOG_ERROR, "recorderStart: pthread_create failed for %s", context->streamer_name);
        return -1;
    }

    log_write(LOG_INFO, "recorderStart: thread started for %s", context->streamer_name);
    return 0;
}

void recorder_stop(recorder_context *context, int finalize)
{
    if (context == NULL)
    {
        return;
    }

    pthread_mutex_lock(&context->context_lock);
    if (atomic_load(&context->stop_requested))
    {
        pthread_mutex_unlock(&context->context_lock);
        return;
    }

    atomic_store(&context->stop_requested, 1);
    context->state = RECORDER_STATE_STOPPED;
    pthread_mutex_unlock(&context->context_lock);

    if (context->child_pid > 0)
    {
        kill(context->child_pid, SIGTERM);
        int status;
        pid_t waited = waitpid(context->child_pid, &status, WNOHANG);
        if (waited == 0)
        {
            usleep(FFMPEG_SHUTDOWN_TIMEOUT_US);
            waited = waitpid(context->child_pid, &status, WNOHANG);
            if (waited == 0)
            {
                kill(context->child_pid, SIGKILL);
                waitpid(context->child_pid, NULL, 0);
            }
        }
        context->child_pid = -1;
    }

    pthread_join(context->thread_id, NULL);
    log_write(LOG_INFO, "recorderStop: recorder stopped for %s", context->streamer_name);
    (void)finalize;
}

void recorder_destroy(recorder_context *context)
{
    if (context == NULL)
    {
        return;
    }

    pthread_mutex_destroy(&context->context_lock);
    free(context);
    log_write(LOG_INFO, "recorderDestroy: context destroyed");
}

void recorder_update_configuration(recorder_context *context, const void *new_config)
{
    if (context == NULL || new_config == NULL)
    {
        return;
    }

    const configuration *configuration_snapshot = (const configuration*)new_config;
    pthread_mutex_lock(&context->context_lock);
    context->segment_duration_seconds = configuration_snapshot->segment_duration_seconds;
    context->failure_cooldown_seconds = configuration_snapshot->failure_cooldown_seconds;
    context->stream_timeout_seconds = configuration_snapshot->stream_timeout_seconds;
    context->retry_streams_seconds = configuration_snapshot->retry_streams_seconds;
    context->retry_open_attempts = configuration_snapshot->retry_open_attempts;
    strncpy(context->stream_quality, configuration_snapshot->stream_quality, sizeof(context->stream_quality) - 1);
    context->stream_quality[sizeof(context->stream_quality) - 1] = '\0';
    pthread_mutex_unlock(&context->context_lock);

    log_write(LOG_INFO, "recorderUpdateConfiguration: updated for %s, quality=%s",
              context->streamer_name, context->stream_quality);
}

recorder_state recorder_get_state(recorder_context *context)
{
    if (context == NULL)
    {
        return RECORDER_STATE_STOPPED;
    }

    pthread_mutex_lock(&context->context_lock);
    recorder_state state = context->state;
    pthread_mutex_unlock(&context->context_lock);
    return state;
}

void recorder_get_status_string(recorder_context *context, char *buffer, size_t buffer_size)
{
    if (context == NULL || buffer == NULL || buffer_size == 0)
    {
        return;
    }

    pthread_mutex_lock(&context->context_lock);

    const char *state_name = "UNKNOWN";
    switch (context->state)
    {
        case RECORDER_STATE_IDLE:       state_name = "IDLE"; break;
        case RECORDER_STATE_RECORDING:  state_name = "RECORDING"; break;
        case RECORDER_STATE_ERROR:      state_name = "ERROR"; break;
        case RECORDER_STATE_STOPPED:    state_name = "STOPPED"; break;
    }

    snprintf(buffer, buffer_size, "%s/%s: %s (failures: %d)",
             context->platform_name, context->streamer_name, state_name, context->consecutive_failures);

    pthread_mutex_unlock(&context->context_lock);
}

static int validate_streamer_name(const char *name)
{
    if (name == NULL || name[0] == '\0')
    {
        return 0;
    }

    size_t length = strlen(name);
    if (length > 60)
    {
        return 0;
    }

    for (const char *character = name; *character != '\0'; character++)
    {
        char character_value = *character;
        if (!((character_value >= 'a' && character_value <= 'z') ||
              (character_value >= 'A' && character_value <= 'Z') ||
              (character_value >= '0' && character_value <= '9') ||
              character_value == '_'))
        {
            return 0;
        }
    }

    return 1;
}

static void escape_shell_argument(const char *input, char *output, size_t output_size)
{
    if (input == NULL || output == NULL || output_size < 3)
    {
        if (output != NULL && output_size > 0)
        {
            output[0] = '\0';
        }
        return;
    }

    size_t position = 0;
    output[position++] = '\'';

    for (const char *character = input; *character != '\0' && position < output_size - 4; character++)
    {
        if (*character == '\'')
        {
            output[position++] = '\'';
            output[position++] = '\\';
            output[position++] = '\'';
            output[position++] = '\'';
        }
        else
        {
            output[position++] = *character;
        }
    }

    if (position < output_size - 1)
    {
        output[position++] = '\'';
    }
    output[position] = '\0';
}

static int is_stream_live(const char *streamer, const char *platform)
{
    (void)platform;

    char escaped_streamer[128];
    escape_shell_argument(streamer, escaped_streamer, sizeof(escaped_streamer));

    char command[512];
    snprintf(command, sizeof(command),
             "streamlink --json https://www.twitch.tv/%s 2>/dev/null", escaped_streamer);

    FILE *file_pointer = popen(command, "r");
    if (file_pointer == NULL)
    {
        log_write(LOG_ERROR, "isStreamLive: popen failed for %s", streamer);
        return 0;
    }

    char *buffer = malloc(32768);
    if (buffer == NULL)
    {
        log_write(LOG_ERROR, "isStreamLive: malloc failed for %s", streamer);
        pclose(file_pointer);
        return 0;
    }

    size_t total_bytes = 0;
    size_t buffer_size = 32768;

    while (total_bytes < buffer_size - 1)
    {
        size_t bytes_read = fread(buffer + total_bytes, 1, buffer_size - total_bytes - 1, file_pointer);
        if (bytes_read == 0)
        {
            break;
        }
        total_bytes += bytes_read;
    }

    buffer[total_bytes] = '\0';

    int status = pclose(file_pointer);

    if (status != 0)
    {
        log_write(LOG_DEBUG, "isStreamLive: streamlink returned non-zero (%d) for %s", status, streamer);
        free(buffer);
        return 0;
    }

    const char *streams_field = strstr(buffer, "\"streams\"");
    if (streams_field == NULL)
    {
        log_write(LOG_DEBUG, "isStreamLive: no 'streams' field in response for %s", streamer);
        free(buffer);
        return 0;
    }

    const char *open_brace = strchr(streams_field + 9, '{');
    if (open_brace == NULL)
    {
        log_write(LOG_DEBUG, "isStreamLive: no opening brace after 'streams' for %s", streamer);
        free(buffer);
        return 0;
    }

    const char *close_brace = strchr(open_brace + 1, '}');
    if (close_brace == NULL)
    {
        log_write(LOG_DEBUG, "isStreamLive: no closing brace for 'streams' for %s", streamer);
        free(buffer);
        return 0;
    }

    size_t content_length = close_brace - open_brace - 1;
    int is_live = (content_length > 0);

    if (is_live)
    {
        log_write(LOG_DEBUG, "isStreamLive: %s is LIVE (streams content length: %zu)", streamer, content_length);
    }
    else
    {
        log_write(LOG_DEBUG, "isStreamLive: %s is OFFLINE (empty streams object)", streamer);
    }

    free(buffer);
    return is_live;
}

static const char* build_quality_chain(const char *requested_quality)
{
    if (requested_quality == NULL || requested_quality[0] == '\0')
    {
        return "best";
    }

    if (strcmp(requested_quality, "1080p") == 0)
    {
        return "1080p,1080p60,720p,720p60,480p,360p,160p";
    }
    else if (strcmp(requested_quality, "720p") == 0)
    {
        return "720p,720p60,480p,360p,160p";
    }
    else if (strcmp(requested_quality, "480p") == 0)
    {
        return "480p,480p60,360p,360p60,160p";
    }
    else if (strcmp(requested_quality, "360p") == 0)
    {
        return "360p,360p60,160p";
    }
    else if (strcmp(requested_quality, "160p") == 0)
    {
        return "160p,worst";
    }
    else if (strcmp(requested_quality, "best") == 0)
    {
        return "best";
    }
    else if (strcmp(requested_quality, "worst") == 0)
    {
        return "worst";
    }

    return requested_quality;
}

static pid_t launch_streamlink_and_ffmpeg(recorder_context *context, const char *stream_url)
{
    int pipe_descriptor[2];
    if (pipe(pipe_descriptor) != 0)
    {
        log_write(LOG_ERROR, "launchStreamlinkAndFfmpeg: pipe failed");
        return -1;
    }

    pid_t process_id = fork();
    if (process_id < 0)
    {
        close(pipe_descriptor[0]);
        close(pipe_descriptor[1]);
        log_write(LOG_ERROR, "launchStreamlinkAndFfmpeg: fork failed");
        return -1;
    }

    if (process_id == 0)
    {
        close(pipe_descriptor[0]);
        if (dup2(pipe_descriptor[1], STDOUT_FILENO) < 0)
        {
            perror("dup2 stdout");
            _exit(1);
        }
        close(pipe_descriptor[1]);

        const char *quality_chain = build_quality_chain(context->stream_quality);

        char escaped_url[512];
        char escaped_log[512];
        char escaped_work_dir[512];
        char escaped_platform[64];
        char escaped_streamer[128];

        escape_shell_argument(stream_url, escaped_url, sizeof(escaped_url));
        escape_shell_argument(context->log_file_path, escaped_log, sizeof(escaped_log));
        escape_shell_argument(context->work_directory, escaped_work_dir, sizeof(escaped_work_dir));
        escape_shell_argument(context->platform_name, escaped_platform, sizeof(escaped_platform));
        escape_shell_argument(context->streamer_name, escaped_streamer, sizeof(escaped_streamer));

        char command[4096];
        int written = snprintf(command, sizeof(command),
                 "streamlink --stdout --ipv4 --stream-timeout %d --retry-streams %d --retry-open %d %s %s 2>>%s | "
                 "ffmpeg -hide_banner -loglevel warning -fflags +genpts -flags +low_delay "
                 "-probesize 4M -analyzeduration 4M -i - -map 0:v -map 0:a -c copy "
                 "-f segment -segment_format mpegts -segment_time %d -segment_time_delta 0.1 "
                 "-reset_timestamps 0 -strftime 1 -avoid_negative_ts make_zero "
                 "%s/%s_%s_%%Y-%%m-%%d_%%H-%%M-%%S_%%03d.ts 2>>%s",
                 context->stream_timeout_seconds,
                 context->retry_streams_seconds,
                 context->retry_open_attempts,
                 escaped_url,
                 quality_chain,
                 escaped_log,
                 context->segment_duration_seconds,
                 escaped_work_dir,
                 escaped_platform,
                 escaped_streamer,
                 escaped_log);

        if (written < 0 || (size_t)written >= sizeof(command))
        {
            log_write(LOG_ERROR, "launchStreamlinkAndFfmpeg: command too long for %s", context->streamer_name);
            _exit(1);
        }

        log_write(LOG_INFO, "launchStreamlinkAndFfmpeg: using quality chain '%s' for %s",
                  quality_chain, context->streamer_name);

        execl("/bin/sh", "sh", "-c", command, (char*)NULL);
        perror("execl");
        _exit(1);
    }

    close(pipe_descriptor[1]);
    close(pipe_descriptor[0]);
    return process_id;
}

static void* recorder_thread_function(void *argument)
{
    recorder_context *context = (recorder_context*)argument;
    char stream_url[256];
    snprintf(stream_url, sizeof(stream_url), "https://www.twitch.tv/%s", context->streamer_name);

    while (!atomic_load(&context->stop_requested))
    {
        if (!is_stream_live(context->streamer_name, context->platform_name))
        {
            pthread_mutex_lock(&context->context_lock);
            context->state = RECORDER_STATE_IDLE;
            pthread_mutex_unlock(&context->context_lock);

            log_write(LOG_DEBUG, "recorderThread: %s offline, waiting %d s",
                      context->streamer_name, context->retry_streams_seconds);

            for (int wait_second = 0;
                 wait_second < context->retry_streams_seconds && !atomic_load(&context->stop_requested);
                 wait_second++)
            {
                sleep(1);
            }
            continue;
        }

        log_write(LOG_INFO, "recorderThread: %s is live, starting recording with quality '%s'",
                  context->streamer_name, context->stream_quality);

        pthread_mutex_lock(&context->context_lock);
        context->state = RECORDER_STATE_RECORDING;
        context->last_start_time = time(NULL);
        context->consecutive_failures = 0;
        pthread_mutex_unlock(&context->context_lock);

        pid_t child_pid = launch_streamlink_and_ffmpeg(context, stream_url);
        if (child_pid < 0)
        {
            log_write(LOG_ERROR, "recorderThread: failed to launch recorder for %s", context->streamer_name);

            pthread_mutex_lock(&context->context_lock);
            context->state = RECORDER_STATE_ERROR;
            context->consecutive_failures++;
            int cooldown = context->failure_cooldown_seconds;
            pthread_mutex_unlock(&context->context_lock);

            for (int wait_second = 0;
                 wait_second < cooldown && !atomic_load(&context->stop_requested);
                 wait_second++)
            {
                sleep(1);
            }
            continue;
        }

        pthread_mutex_lock(&context->context_lock);
        context->child_pid = child_pid;
        pthread_mutex_unlock(&context->context_lock);

        int status;
        pid_t waited = waitpid(child_pid, &status, 0);
        if (waited > 0)
        {
            handle_child_exit(context, child_pid, status);
        }

        pthread_mutex_lock(&context->context_lock);
        context->child_pid = -1;
        pthread_mutex_unlock(&context->context_lock);

        if (atomic_load(&context->stop_requested))
        {
            break;
        }

        time_t now = time(NULL);
        time_t duration = now - context->last_start_time;

        if (duration < 10)
        {
            log_write(LOG_INFO, "recorderThread: %s recording ended quickly (%ld s), waiting",
                      context->streamer_name, duration);

            for (int wait_second = 0;
                 wait_second < context->retry_streams_seconds && !atomic_load(&context->stop_requested);
                 wait_second++)
            {
                sleep(1);
            }
        }
        else
        {
            log_write(LOG_INFO, "recorderThread: %s recording finished after %ld s",
                      context->streamer_name, duration);
            sleep(context->retry_streams_seconds);
        }
    }

    log_write(LOG_INFO, "recorderThread: thread exiting for %s", context->streamer_name);
    return NULL;
}

static void handle_child_exit(recorder_context *context, pid_t process_id, int status)
{
    if (WIFEXITED(status))
    {
        int exit_code = WEXITSTATUS(status);
        log_write(LOG_DEBUG, "recorderThread: child %d exited with code %d for %s",
                  process_id, exit_code, context->streamer_name);

        if (exit_code != 0)
        {
            pthread_mutex_lock(&context->context_lock);
            context->consecutive_failures++;
            context->state = RECORDER_STATE_ERROR;
            pthread_mutex_unlock(&context->context_lock);
        }
    }
    else if (WIFSIGNALED(status))
    {
        int signal_number = WTERMSIG(status);
        log_write(LOG_WARN, "recorderThread: child %d killed by signal %d for %s",
                  process_id, signal_number, context->streamer_name);

        pthread_mutex_lock(&context->context_lock);
        context->consecutive_failures++;
        context->state = RECORDER_STATE_ERROR;
        pthread_mutex_unlock(&context->context_lock);
    }
}