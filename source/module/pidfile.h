#ifndef PIDFILE_H
#define PIDFILE_H

int pidfile_create (const char *path);
void pidfile_remove (int fd);
int pidfile_is_running (const char *path);

#endif
