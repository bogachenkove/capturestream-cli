#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

size_t utils_strlcpy(char *dest, const char *src, size_t size)
{
    if (dest == NULL || src == NULL || size == 0)
    {
        return 0;
    }

    size_t src_len = strlen(src);
    size_t copy_len = (src_len < size - 1) ? src_len : size - 1;

    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';

    return src_len;
}

size_t utils_strlcat(char *dest, const char *src, size_t size)
{
    if (dest == NULL || src == NULL || size == 0)
    {
        return 0;
    }

    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    size_t free_space = size - dest_len - 1;

    if (free_space > src_len)
    {
        free_space = src_len;
    }

    memcpy(dest + dest_len, src, free_space);
    dest[dest_len + free_space] = '\0';

    return dest_len + src_len;
}

void utils_strtolower(char *text)
{
    if (text == NULL)
    {
        return;
    }

    for ( ; *text; text++)
    {
        *text = tolower((unsigned char)*text);
    }
}

void utils_strtrim(char *text)
{
    if (text == NULL)
    {
        return;
    }

    char *start = text;
    while (*start && isspace((unsigned char)*start))
    {
        start++;
    }

    if (start != text)
    {
        memmove(text, start, strlen(start) + 1);
    }

    char *end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end))
    {
        end--;
    }

    *(end + 1) = '\0';
}

const char* utils_current_time_string(void)
{
    static char buffer[32];
    time_t now = time(NULL);
    struct tm tm_now;

    localtime_r(&now, &tm_now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_now);

    return buffer;
}

void utils_timestamp_filename(char *buffer, size_t size, const char *prefix)
{
    if (buffer == NULL || size == 0)
    {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;

    localtime_r(&now, &tm_now);

    char time_part[32];
    strftime(time_part, sizeof(time_part), "%Y%m%d_%H%M%S", &tm_now);

    snprintf(buffer, size, "%s_%s.ts", prefix ? prefix : "recording", time_part);
}

int utils_file_exists(const char *path)
{
    if (path == NULL)
    {
        return -1;
    }

    struct stat file_status;

    if (stat(path, &file_status) == 0)
    {
        return 1;
    }
    else if (errno == ENOENT)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int utils_mkdir_recursive(const char *path, mode_t mode)
{
    if (path == NULL)
    {
        return -1;
    }

    char path_copy[PATH_MAX];
    utils_strlcpy(path_copy, path, sizeof(path_copy));

    size_t length = strlen(path_copy);
    if (length == 0)
    {
        return -1;
    }

    if (path_copy[length - 1] == '/')
    {
        path_copy[length - 1] = '\0';
    }

    for (char *separator = path_copy + 1; *separator; separator++)
    {
        if (*separator == '/')
        {
            *separator = '\0';
            if (mkdir(path_copy, mode) != 0 && errno != EEXIST)
            {
                return -1;
            }
            *separator = '/';
        }
    }

    if (mkdir(path_copy, mode) != 0 && errno != EEXIST)
    {
        return -1;
    }

    return 0;
}