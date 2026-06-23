#include "config_watcher.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/inotify.h>

static void* inotify_thread_function(void *argument);

int config_watcher_start(configuration_manager *manager)
{
    if (manager == NULL)
    {
        return -1;
    }

    manager->inotify_file_descriptor = inotify_init1(IN_CLOEXEC);
    if (manager->inotify_file_descriptor < 0)
    {
        log_write(LOG_ERROR, "configWatcherStart: inotify_init1 failed: %s", strerror(errno));
        return -1;
    }

    manager->inotify_watch_descriptor = inotify_add_watch(
            manager->inotify_file_descriptor,
            manager->current_configuration.configuration_path,
            IN_MODIFY | IN_MOVED_TO | IN_ATTRIB
    );

    if (manager->inotify_watch_descriptor < 0)
    {
        log_write(LOG_ERROR, "configWatcherStart: inotify_add_watch failed for %s: %s",
                  manager->current_configuration.configuration_path, strerror(errno));
        close(manager->inotify_file_descriptor);
        manager->inotify_file_descriptor = -1;
        return -1;
    }

    manager->reload_thread_active = 1;
    if (pthread_create(&manager->reload_thread_identifier, NULL,
                       inotify_thread_function, manager) != 0)
    {
        log_write(LOG_ERROR, "configWatcherStart: pthread_create failed");
        inotify_rm_watch(manager->inotify_file_descriptor, manager->inotify_watch_descriptor);
        close(manager->inotify_file_descriptor);
        manager->inotify_file_descriptor = -1;
        return -1;
    }

    log_write(LOG_INFO, "configWatcherStart: watcher started for %s",
              manager->current_configuration.configuration_path);
    return 0;
}

void config_watcher_stop(configuration_manager *manager)
{
    if (manager == NULL)
    {
        return;
    }

    manager->reload_thread_active = 0;

    pthread_cancel(manager->reload_thread_identifier);

    if (manager->inotify_file_descriptor >= 0)
    {
        inotify_rm_watch(manager->inotify_file_descriptor, manager->inotify_watch_descriptor);
        close(manager->inotify_file_descriptor);
        manager->inotify_file_descriptor = -1;
    }

    pthread_join(manager->reload_thread_identifier, NULL);

    log_write(LOG_INFO, "configWatcherStop: watcher stopped");
}

static void* inotify_thread_function(void *argument)
{
    configuration_manager *manager = (configuration_manager*)argument;
    char buffer[4096];
    ssize_t bytes_read;

    while (manager->reload_thread_active)
    {
        bytes_read = read(manager->inotify_file_descriptor, buffer, sizeof(buffer));
        if (bytes_read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EBADF)
            {
                break;
            }
            log_write(LOG_ERROR, "inotifyThreadFunction: read error: %s", strerror(errno));
            break;
        }

        if (bytes_read == 0)
        {
            break;
        }

        log_write(LOG_DEBUG, "inotifyThreadFunction: event received, reloading");

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        configuration_reload(manager);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    return NULL;
}