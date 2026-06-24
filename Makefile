SRCDIR = source
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -I$(SRCDIR)/module -MMD
CFLAGS += -Wno-format-truncation
LDFLAGS = -ljansson -pthread

SRCS = $(SRCDIR)/main.c \
    $(SRCDIR)/module/app.c \
    $(SRCDIR)/module/daemon.c \
    $(SRCDIR)/module/cli_setup.c \
    $(SRCDIR)/module/socket_client.c \
    $(SRCDIR)/module/config.c \
    $(SRCDIR)/module/config_parser.c \
    $(SRCDIR)/module/config_watcher.c \
    $(SRCDIR)/module/logger.c \
    $(SRCDIR)/module/monitor.c \
    $(SRCDIR)/module/streamer_manager.c \
    $(SRCDIR)/module/streamer_list.c \
    $(SRCDIR)/module/recorder.c \
    $(SRCDIR)/module/control.c \
    $(SRCDIR)/module/command_handler.c \
    $(SRCDIR)/module/ui.c \
    $(SRCDIR)/module/pidfile.c \
    $(SRCDIR)/module/utils.c \
    $(SRCDIR)/module/platform/twitch.c \
    $(SRCDIR)/module/assembler.c

OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)
TARGET = capturestream-cli

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)

.PHONY: all clean