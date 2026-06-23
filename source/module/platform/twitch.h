#ifndef PLATFORM_TWITCH_H
#define PLATFORM_TWITCH_H

#include <stddef.h>

int twitch_is_stream_live(const char *streamer);
void twitch_build_stream_url(const char *streamer, char *buffer, size_t buffer_size);

#endif