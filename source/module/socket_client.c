#include "socket_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int socket_client_send_command (const char *socket_path, const char *command_text, char *response, size_t response_size)
{
 int socket_descriptor = socket (AF_UNIX, SOCK_STREAM, 0);
 if (socket_descriptor < 0)
 {
		perror ("socket");
		return -1;
 }

 struct sockaddr_un socket_address;
 memset (&socket_address, 0, sizeof (socket_address));
 socket_address.sun_family = AF_UNIX;
 snprintf (socket_address.sun_path, sizeof (socket_address.sun_path), "%s", socket_path);

 if (connect (socket_descriptor, (struct sockaddr *) &socket_address, sizeof (socket_address)) < 0)
 {
		perror ("connect");
		close (socket_descriptor);
		fprintf (stderr, "Failed to connect to control socket. Is the daemon running?\n");
		return -1;
 }

 if (write (socket_descriptor, command_text, strlen (command_text)) < 0)
 {
		perror ("write");
		close (socket_descriptor);
		return -1;
 }

 ssize_t bytes_received = read (socket_descriptor, response, response_size - 1);
 if (bytes_received < 0)
 {
		perror ("read");
		close (socket_descriptor);
		return -1;
 }

 response[bytes_received] = '\0';
 close (socket_descriptor);
 return 0;
}

int socket_client_set_config (const char *socket_path, const char *key_value)
{
 char command_text[256];
 snprintf (command_text, sizeof (command_text), "set %s\n", key_value);

 char response[4096];
 if (socket_client_send_command (socket_path, command_text, response, sizeof (response)) != 0)
 {
		return -1;
 }

 printf ("%s", response);
 return 0;
}
