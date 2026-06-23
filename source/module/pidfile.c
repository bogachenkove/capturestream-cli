#include "pidfile.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <signal.h>

int pidfile_create(const char *path)
{
    if (path == NULL)
    {
        log_write(LOG_ERROR, "pidfileCreate: path is NULL");
        return -1;
    }

    int file_descriptor = open(path, O_RDWR | O_CREAT, 0644);
    if (file_descriptor < 0)
    {
        log_write(LOG_ERROR, "pidfileCreate: open(%s) failed: %s", path, strerror(errno));
        return -1;
    }

    if (flock(file_descriptor, LOCK_EX | LOCK_NB) < 0)
    {
        if (errno == EWOULDBLOCK)
        {
            log_write(LOG_WARN, "pidfileCreate: another instance is running (locked)");
        }
        else
        {
            log_write(LOG_ERROR, "pidfileCreate: flock failed: %s", strerror(errno));
        }
        close(file_descriptor);
        return -1;
    }

    char pid_text[32];
    snprintf(pid_text, sizeof(pid_text), "%d\n", getpid());

    if (ftruncate(file_descriptor, 0) < 0)
    {
        log_write(LOG_ERROR, "pidfileCreate: ftruncate failed: %s", strerror(errno));
        flock(file_descriptor, LOCK_UN);
        close(file_descriptor);
        return -1;
    }

    if (write(file_descriptor, pid_text, strlen(pid_text)) != (ssize_t)strlen(pid_text))
    {
        log_write(LOG_ERROR, "pidfileCreate: write failed: %s", strerror(errno));
        flock(file_descriptor, LOCK_UN);
        close(file_descriptor);
        return -1;
    }

    fsync(file_descriptor);
    log_write(LOG_INFO, "pidfileCreate: created %s with PID %d", path, getpid());

    return file_descriptor;
}

void pidfile_remove(int file_descriptor)
{
    if (file_descriptor < 0)
    {
        return;
    }

    flock(file_descriptor, LOCK_UN);
    close(file_descriptor);
    log_write(LOG_INFO, "pidfileRemove: PID file closed and unlocked");
}

int pidfile_is_running(const char *path)
{
    if (path == NULL)
    {
        return -1;
    }

    FILE *file_pointer = fopen(path, "r");
    if (file_pointer == NULL)
    {
        if (errno == ENOENT)
        {
            return 0;
        }
        log_write(LOG_ERROR, "pidfileIsRunning: fopen(%s) failed: %s", path, strerror(errno));
        return -1;
    }

    char pid_text[32];
    if (fgets(pid_text, sizeof(pid_text), file_pointer) == NULL)
    {
        fclose(file_pointer);
        return 0;
    }

    fclose(file_pointer);

    pid_t pid = (pid_t)atoi(pid_text);
    if (pid <= 0)
    {
        return 0;
    }

    if (kill(pid, 0) == 0)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}