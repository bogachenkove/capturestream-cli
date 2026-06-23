#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

typedef enum
{
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4
} log_level;

int logger_initialize(log_level level, const char *log_file_path, int use_syslog);
void logger_shutdown(void);
void logger_set_level(log_level level);
void log_write(log_level level, const char *format, ...) __attribute__((format(printf, 2, 3)));

#endif