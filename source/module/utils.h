#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

size_t utils_strlcpy (char *dest, const char *src, size_t size);
size_t utils_strlcat (char *dest, const char *src, size_t size);
void utils_strtolower (char *text);
void utils_strtrim (char *text);
const char *utils_current_time_string (void);
void utils_timestamp_filename (char *buffer, size_t size, const char *prefix);
int utils_file_exists (const char *path);
int utils_mkdir_recursive (const char *path, mode_t mode);

#endif
