#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <syslog.h>
#include <pthread.h>
#include <stdarg.h>

static FILE *log_file_handle = NULL;
static log_level current_log_level = LOG_INFO;
static int syslog_enabled = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static char log_path[512] = {0};

static const char *color_table[] =
{
    [LOG_DEBUG] = "\033[36m",
    [LOG_INFO]  = "\033[32m",
    [LOG_WARN]  = "\033[33m",
    [LOG_ERROR] = "\033[31m",
    [LOG_FATAL] = "\033[35m"
};

static const char *color_reset = "\033[0m";

static const int syslog_priority_map[] =
{
    [LOG_DEBUG] = LOG_DEBUG,
    [LOG_INFO]  = LOG_INFO,
    [LOG_WARN]  = LOG_WARNING,
    [LOG_ERROR] = LOG_ERR,
    [LOG_FATAL] = LOG_CRIT
};

int logger_initialize(log_level level, const char *log_file_path, int use_syslog)
{
    pthread_mutex_lock(&log_mutex);

    current_log_level = level;
    syslog_enabled = use_syslog;

    if (log_file_path != NULL)
    {
        strncpy(log_path, log_file_path, sizeof(log_path) - 1);
        log_path[sizeof(log_path) - 1] = '\0';

        log_file_handle = fopen(log_file_path, "a");
        if (log_file_handle == NULL)
        {
            pthread_mutex_unlock(&log_mutex);
            return -1;
        }

        setvbuf(log_file_handle, NULL, _IOLBF, 0);
    }
    else
    {
        log_path[0] = '\0';
        log_file_handle = NULL;
    }

    if (use_syslog)
    {
        openlog("recorder", LOG_PID | LOG_CONS, LOG_DAEMON);
    }

    pthread_mutex_unlock(&log_mutex);
    return 0;
}

void logger_shutdown(void)
{
    pthread_mutex_lock(&log_mutex);

    if (log_file_handle != NULL)
    {
        fclose(log_file_handle);
        log_file_handle = NULL;
    }

    if (syslog_enabled)
    {
        closelog();
        syslog_enabled = 0;
    }

    pthread_mutex_unlock(&log_mutex);
}

void logger_set_level(log_level level)
{
    pthread_mutex_lock(&log_mutex);
    current_log_level = level;
    pthread_mutex_unlock(&log_mutex);
}

void log_write(log_level level, const char *format, ...)
{
    if (level < current_log_level)
    {
        return;
    }

    pthread_mutex_lock(&log_mutex);

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_buffer[32];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm_now);

    static const char *level_names[] =
    {
        [LOG_DEBUG] = "DEBUG",
        [LOG_INFO]  = "INFO",
        [LOG_WARN]  = "WARN",
        [LOG_ERROR] = "ERROR",
        [LOG_FATAL] = "FATAL"
    };

    const char *level_name = (level <= LOG_FATAL) ? level_names[level] : "UNKNOWN";

    va_list args;
    va_start(args, format);

    char message[4096];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (log_file_handle != NULL)
    {
        fprintf(log_file_handle, "[%s] [%s] %s\n", time_buffer, level_name, message);
        fflush(log_file_handle);
    }

    int is_terminal = isatty(STDERR_FILENO);
    if (is_terminal)
    {
        fprintf(stderr, "%s[%s] [%s]%s %s\n",
                color_table[level], time_buffer, level_name, color_reset, message);
    }
    else
    {
        fprintf(stderr, "[%s] [%s] %s\n", time_buffer, level_name, message);
    }

    fflush(stderr);

    if (syslog_enabled)
    {
        int syslog_priority = (level <= LOG_FATAL) ? syslog_priority_map[level] : LOG_NOTICE;
        syslog(syslog_priority, "%s", message);
    }

    pthread_mutex_unlock(&log_mutex);
}