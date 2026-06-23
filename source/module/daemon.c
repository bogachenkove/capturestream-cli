#include "daemon.h"
#include "logger.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

void daemon_start(void)
{
    pid_t process_identifier = fork();
    if (process_identifier < 0)
    {
        log_write(LOG_ERROR, "Fork failed");
        exit(EXIT_FAILURE);
    }

    if (process_identifier > 0)
    {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0)
    {
        log_write(LOG_ERROR, "setsid failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    process_identifier = fork();
    if (process_identifier < 0)
    {
        log_write(LOG_ERROR, "Second fork failed");
        exit(EXIT_FAILURE);
    }

    if (process_identifier > 0)
    {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int file_descriptor = open("/dev/null", O_RDWR);
    dup2(file_descriptor, STDIN_FILENO);
    dup2(file_descriptor, STDOUT_FILENO);
    dup2(file_descriptor, STDERR_FILENO);

    if (file_descriptor > STDERR_FILENO)
    {
        close(file_descriptor);
    }

    log_write(LOG_INFO, "Daemon started with PID %d", getpid());
}

void daemon_stop(const char *pid_path)
{
    if (utils_file_exists(pid_path) != 1)
    {
        fprintf(stderr, "PID file not found. Is the daemon running?\n");
        exit(EXIT_FAILURE);
    }

    FILE *file_pointer = fopen(pid_path, "r");
    if (file_pointer == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char pid_text[32];
    if (fgets(pid_text, sizeof(pid_text), file_pointer) == NULL)
    {
        fclose(file_pointer);
        fprintf(stderr, "Failed to read PID\n");
        exit(EXIT_FAILURE);
    }

    fclose(file_pointer);

    pid_t process_identifier = atoi(pid_text);
    if (process_identifier <= 0)
    {
        fprintf(stderr, "Invalid PID\n");
        exit(EXIT_FAILURE);
    }

    if (kill(process_identifier, SIGTERM) == 0)
    {
        printf("Sent SIGTERM to PID %d\n", process_identifier);
    }
    else
    {
        perror("kill");
        exit(EXIT_FAILURE);
    }
}

void daemon_status(const char *pid_path)
{
    if (utils_file_exists(pid_path) != 1)
    {
        printf("Daemon is not running (PID file not found)\n");
        return;
    }

    FILE *file_pointer = fopen(pid_path, "r");
    if (file_pointer == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char pid_text[32];
    if (fgets(pid_text, sizeof(pid_text), file_pointer) == NULL)
    {
        fclose(file_pointer);
        printf("Daemon is not running (PID file empty)\n");
        return;
    }

    fclose(file_pointer);

    pid_t process_identifier = atoi(pid_text);
    if (process_identifier <= 0)
    {
        printf("Daemon is not running (invalid PID)\n");
        return;
    }

    if (kill(process_identifier, 0) == 0)
    {
        printf("Daemon is running with PID %d\n", process_identifier);
    }
    else
    {
        printf("Daemon is not running (stale PID file)\n");
    }
}

void daemon_reload(const char *pid_path)
{
    if (utils_file_exists(pid_path) != 1)
    {
        fprintf(stderr, "PID file not found. Is the daemon running?\n");
        exit(EXIT_FAILURE);
    }

    FILE *file_pointer = fopen(pid_path, "r");
    if (file_pointer == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char pid_text[32];
    if (fgets(pid_text, sizeof(pid_text), file_pointer) == NULL)
    {
        fclose(file_pointer);
        fprintf(stderr, "Failed to read PID\n");
        exit(EXIT_FAILURE);
    }

    fclose(file_pointer);

    pid_t process_identifier = atoi(pid_text);
    if (process_identifier <= 0)
    {
        fprintf(stderr, "Invalid PID\n");
        exit(EXIT_FAILURE);
    }

    if (kill(process_identifier, SIGHUP) == 0)
    {
        printf("Sent SIGHUP to PID %d to reload configuration\n", process_identifier);
    }
    else
    {
        perror("kill");
        exit(EXIT_FAILURE);
    }
}