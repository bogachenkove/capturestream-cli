#ifndef DAEMON_H
#define DAEMON_H

void daemon_start(void);
void daemon_stop(const char *pid_path);
void daemon_status(const char *pid_path);
void daemon_reload(const char *pid_path);

#endif