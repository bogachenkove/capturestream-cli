#include "monitor.h"
#include "logger.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <stdatomic.h>

typedef struct
{
 const void *configuration_reference;
 monitor_event_callback event_callback;
 pthread_t disk_thread;
 pthread_t memory_thread;
 atomic_int running_flag;
 resource_status current_status;
 pthread_mutex_t status_lock;
 char disk_mount_point[64];
 int disk_check_interval;
 int memory_check_interval;
 int disk_free_percent_limit;
 int disk_free_absolute_limit_gb;
 int memory_warning_percent;
 int memory_critical_percent;
 int memory_warning_mb;
 int memory_critical_mb;
 int subprocess_rss_limit_mb;
} monitor_context;

static void *disk_monitor_thread (void *argument);
static void *memory_monitor_thread (void *argument);
static int check_disk_space (monitor_context *context, resource_status *status);
static int check_memory (monitor_context *context, resource_status *status);
static void update_status (monitor_context *context, const resource_status *new_status);

void *monitor_create (const void *config)
{
 monitor_context *context = calloc (1, sizeof (monitor_context));
 if (context == NULL)
 {
		log_write (LOG_ERROR, "monitorCreate: calloc failed");
		return NULL;
 }

 const configuration *configuration_snapshot = (const configuration *) config;

 strncpy (context->disk_mount_point, configuration_snapshot->disk_mount_point, sizeof (context->disk_mount_point) - 1);
 context->disk_mount_point[sizeof (context->disk_mount_point) - 1] = '\0';
 if (context->disk_mount_point[0] == '\0')
 {
		strncpy (context->disk_mount_point, "/", sizeof (context->disk_mount_point) - 1);
		context->disk_mount_point[sizeof (context->disk_mount_point) - 1] = '\0';
 }

 context->disk_check_interval = configuration_snapshot->disk_check_interval_seconds;
 context->memory_check_interval = configuration_snapshot->memory_check_interval_seconds;
 context->disk_free_percent_limit = configuration_snapshot->disk_free_percent_limit;
 context->disk_free_absolute_limit_gb = configuration_snapshot->disk_free_absolute_limit_gb;
 context->memory_warning_percent = configuration_snapshot->memory_warning_percent;
 context->memory_critical_percent = configuration_snapshot->memory_critical_percent;
 context->memory_warning_mb = configuration_snapshot->memory_warning_mb;
 context->memory_critical_mb = configuration_snapshot->memory_critical_mb;
 context->subprocess_rss_limit_mb = configuration_snapshot->subprocess_rss_limit_mb;
 context->configuration_reference = config;
 context->event_callback = NULL;

 atomic_init (&context->running_flag, 1);
 pthread_mutex_init (&context->status_lock, NULL);
 memset (&context->current_status, 0, sizeof (resource_status));

 if (pthread_create (&context->disk_thread, NULL, disk_monitor_thread, context) != 0)
 {
		log_write (LOG_ERROR, "monitorCreate: failed to create disk thread");
		pthread_mutex_destroy (&context->status_lock);
		free (context);
		return NULL;
 }

 if (pthread_create (&context->memory_thread, NULL, memory_monitor_thread, context) != 0)
 {
		log_write (LOG_ERROR, "monitorCreate: failed to create memory thread");
		atomic_store (&context->running_flag, 0);
		pthread_cancel (context->disk_thread);
		pthread_join (context->disk_thread, NULL);
		pthread_mutex_destroy (&context->status_lock);
		free (context);
		return NULL;
 }

 log_write (LOG_INFO, "monitorCreate: monitor started, watching disk at %s", context->disk_mount_point);
 return context;
}

void monitor_destroy (void *monitor_handle)
{
 if (monitor_handle == NULL)
 {
		return;
 }

 monitor_context *context = (monitor_context *) monitor_handle;
 atomic_store (&context->running_flag, 0);
 pthread_cancel (context->disk_thread);
 pthread_cancel (context->memory_thread);
 pthread_join (context->disk_thread, NULL);
 pthread_join (context->memory_thread, NULL);
 pthread_mutex_destroy (&context->status_lock);
 free (context);
 log_write (LOG_INFO, "monitorDestroy: monitor destroyed");
}

void monitor_set_callback (void *monitor_handle, monitor_event_callback callback)
{
 if (monitor_handle == NULL)
 {
		return;
 }

 monitor_context *context = (monitor_context *) monitor_handle;
 context->event_callback = callback;
}

resource_status monitor_get_status (void *monitor_handle)
{
 resource_status status = {0};
 if (monitor_handle == NULL)
 {
		return status;
 }

 monitor_context *context = (monitor_context *) monitor_handle;
 pthread_mutex_lock (&context->status_lock);
 status = context->current_status;
 pthread_mutex_unlock (&context->status_lock);
 return status;
}

void monitor_check_now (void *monitor_handle)
{
 if (monitor_handle == NULL)
 {
		return;
 }

 monitor_context *context = (monitor_context *) monitor_handle;
 resource_status status;

 if (check_disk_space (context, &status) == 0)
 {
		update_status (context, &status);
 }

 if (check_memory (context, &status) == 0)
 {
		update_status (context, &status);
 }
}

static int check_disk_space (monitor_context *context, resource_status *status)
{
 struct statvfs filesystem_status;
 if (statvfs (context->disk_mount_point, &filesystem_status) != 0)
 {
		log_write (LOG_ERROR, "checkDiskSpace: statvfs failed for %s: %s", context->disk_mount_point, strerror (errno));
		return -1;
 }

 uint64_t total = (uint64_t) filesystem_status.f_frsize * filesystem_status.f_blocks;
 uint64_t free_bytes = (uint64_t) filesystem_status.f_frsize * filesystem_status.f_bavail;
 int free_percent = (total > 0) ? (int) ((free_bytes * 100) / total) : 0;

 status->disk_total_bytes = total;
 status->disk_free_bytes = free_bytes;
 status->disk_free_percent = free_percent;

 int critical = 0;
 if (free_percent <= context->disk_free_percent_limit ||
					(free_bytes / (1024 * 1024 * 1024)) <= (uint64_t) context->disk_free_absolute_limit_gb)
 {
		critical = 1;
 }

 if (context->event_callback != NULL)
 {
		if (critical)
		{
			context->event_callback (MONITOR_EVENT_DISK_CRITICAL, NULL);
		}
		else if (free_percent <= context->disk_free_percent_limit + 5)
		{
			context->event_callback (MONITOR_EVENT_DISK_WARNING, NULL);
		}
 }

 return 0;
}

static int check_memory (monitor_context *context, resource_status *status)
{
 struct sysinfo system_info;
 if (sysinfo (&system_info) != 0)
 {
		log_write (LOG_ERROR, "checkMemory: sysinfo failed: %s", strerror (errno));
		return -1;
 }

 uint64_t total_ram = (uint64_t) system_info.totalram * system_info.mem_unit;
 uint64_t free_ram = (uint64_t) system_info.freeram * system_info.mem_unit;
 uint64_t total_swap = (uint64_t) system_info.totalswap * system_info.mem_unit;
 uint64_t used_swap = (uint64_t) (system_info.totalswap - system_info.freeswap) * system_info.mem_unit;
 int used_percent = (total_ram > 0) ? (int) (((total_ram - free_ram) * 100) / total_ram) : 0;

 status->memory_total_bytes = total_ram;
 status->memory_available_bytes = free_ram;
 status->memory_used_percent = used_percent;
 status->swap_total_bytes = total_swap;
 status->swap_used_bytes = used_swap;

 int critical = 0;
 int warning = 0;

 if (used_percent >= context->memory_critical_percent ||
					(free_ram / (1024 * 1024)) <= (uint64_t) context->memory_critical_mb)
 {
		critical = 1;
 }
 else if (used_percent >= context->memory_warning_percent ||
										(free_ram / (1024 * 1024)) <= (uint64_t) context->memory_warning_mb)
 {
		warning = 1;
 }

 if (context->event_callback != NULL)
 {
		if (critical)
		{
			context->event_callback (MONITOR_EVENT_MEMORY_CRITICAL, NULL);
		}
		else if (warning)
		{
			context->event_callback (MONITOR_EVENT_MEMORY_WARNING, NULL);
		}
 }

 return 0;
}

static void update_status (monitor_context *context, const resource_status *new_status)
{
 pthread_mutex_lock (&context->status_lock);
 context->current_status = *new_status;
 pthread_mutex_unlock (&context->status_lock);
}

static void *disk_monitor_thread (void *argument)
{
 monitor_context *context = (monitor_context *) argument;
 resource_status status;

 while (atomic_load (&context->running_flag))
 {
		if (check_disk_space (context, &status) == 0)
		{
			update_status (context, &status);
		}
		sleep (context->disk_check_interval);
 }

 return NULL;
}

static void *memory_monitor_thread (void *argument)
{
 monitor_context *context = (monitor_context *) argument;
 resource_status status;

 while (atomic_load (&context->running_flag))
 {
		if (check_memory (context, &status) == 0)
		{
			update_status (context, &status);
		}
		sleep (context->memory_check_interval);
 }

 return NULL;
}
