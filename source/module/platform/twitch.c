#include "twitch.h"
#include "../logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int twitch_is_stream_live (const char *streamer)
{
 if (streamer == NULL)
 {
		log_write (LOG_ERROR, "twitch_isStreamLive: streamer is NULL");
		return -1;
 }

 char command[512];
 snprintf (command, sizeof (command), "streamlink --json https://www.twitch.tv/%s 2>/dev/null", streamer);

 FILE *file_pointer = popen (command, "r");
 if (file_pointer == NULL)
 {
		log_write (LOG_ERROR, "twitch_isStreamLive: popen failed for %s: %s", streamer, strerror (errno));
		return -1;
 }

 char *buffer = malloc (32768);
 if (buffer == NULL)
 {
		log_write (LOG_ERROR, "twitch_isStreamLive: malloc failed for %s", streamer);
		pclose (file_pointer);
		return -1;
 }

 size_t total_bytes = 0;
 size_t buffer_size = 32768;

 while (total_bytes < buffer_size - 1)
 {
		size_t bytes_read = fread (buffer + total_bytes, 1, buffer_size - total_bytes - 1, file_pointer);
		if (bytes_read == 0)
		{
			break;
		}
		total_bytes += bytes_read;
 }

 buffer[total_bytes] = '\0';

 int status = pclose (file_pointer);

 if (status != 0)
 {
		log_write (LOG_DEBUG, "twitch_isStreamLive: streamlink returned %d for %s", status, streamer);
		free (buffer);
		return 0;
 }

 const char *streams_field = strstr (buffer, "\"streams\"");
 if (streams_field == NULL)
 {
		log_write (LOG_DEBUG, "twitch_isStreamLive: no 'streams' field in response for %s", streamer);
		free (buffer);
		return 0;
 }

 const char *open_brace = strchr (streams_field + 9, '{');
 if (open_brace == NULL)
 {
		log_write (LOG_DEBUG, "twitch_isStreamLive: no opening brace after 'streams' for %s", streamer);
		free (buffer);
		return 0;
 }

 const char *close_brace = strchr (open_brace + 1, '}');
 if (close_brace == NULL)
 {
		log_write (LOG_DEBUG, "twitch_isStreamLive: no closing brace for 'streams' for %s", streamer);
		free (buffer);
		return 0;
 }

 size_t content_length = close_brace - open_brace - 1;
 int is_live = (content_length > 0);

 free (buffer);
 return is_live;
}

void twitch_build_stream_url (const char *streamer, char *buffer, size_t buffer_size)
{
 if (streamer == NULL || buffer == NULL || buffer_size == 0)
 {
		return;
 }

 snprintf (buffer, buffer_size, "https://www.twitch.tv/%s", streamer);
}
