#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

typedef enum
{
 MONITOR_EVENT_DISK_WARNING,
 MONITOR_EVENT_DISK_CRITICAL,
 MONITOR_EVENT_MEMORY_WARNING,
 MONITOR_EVENT_MEMORY_CRITICAL,
 MONITOR_EVENT_SUBPROCESS_RSS_EXCEEDED
} monitor_event_type;

typedef struct
{
 uint64_t disk_free_bytes;
 uint64_t disk_total_bytes;
 int disk_free_percent;
 uint64_t memory_available_bytes;
 uint64_t memory_total_bytes;
 int memory_used_percent;
 uint64_t swap_total_bytes;
 uint64_t swap_used_bytes;
} resource_status;

typedef void (*monitor_event_callback) (monitor_event_type event, const void *event_data);

void *monitor_create (const void *config);
void monitor_destroy (void *monitor_handle);
void monitor_set_callback (void *monitor_handle, monitor_event_callback callback);
resource_status monitor_get_status (void *monitor_handle);
void monitor_check_now (void *monitor_handle);

#endif
