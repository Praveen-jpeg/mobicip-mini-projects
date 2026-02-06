#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/select.h>

#define PORT "8080"
#define BUF_SIZE 1024
#define MAX_CLIENTS FD_SETSIZE

int main()
{
    int listen_fd, client_fd, max_fd;
    int clients[MAX_CLIENTS];
    int message_count = 0;

    struct addrinfo hints, *res;
    fd_set read_fds;
    char buffer[BUF_SIZE];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 + IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, PORT, &hints, &res);

    listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bind(listen_fd, res->ai_addr, res->ai_addrlen);
    listen(listen_fd, 10);
    freeaddrinfo(res);

    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i] = -1;

    printf("Echo Server running on port %s (select-based)\n", PORT);

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        max_fd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] != -1) {
                FD_SET(clients[i], &read_fds);
                if (clients[i] > max_fd)
                    max_fd = clients[i];
            }
        }

        select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        /* New connection */
        if (FD_ISSET(listen_fd, &read_fds)) {
            client_fd = accept(listen_fd, NULL, NULL);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == -1) {
                    clients[i] = client_fd;
                    break;
                }
            }
        }

        /* Handle clients */
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i] != -1 && FD_ISSET(clients[i], &read_fds))
            {
                int n = recv(clients[i], buffer, BUF_SIZE - 1, 0);

                if (n <= 0) {
                    close(clients[i]);
                    clients[i] = -1;
                }
                else {
                    buffer[n] = '\0';

                    char data[BUF_SIZE];
                    strcpy(data, buffer);

                    char *timestamp = strtok(data, "|");
                    char *message   = strtok(NULL, "|");

                    /* Defensive checks */
                    if (timestamp == NULL || message == NULL)
                        continue;

                    /* Trim newline from message */
                    size_t len = strlen(message);
                    if (len > 0 && message[len - 1] == '\n')
                        message[len - 1] = '\0';

                    /* Ignore empty messages */
                    if (strlen(message) == 0)
                        continue;

                    /* Count only valid messages */
                    message_count++;

                    char reply[BUF_SIZE * 2];
                    snprintf(reply, sizeof(reply),
                            "Server Echo:\n"
                            "Message: %s\n"
                            "Timestamp: %s\n"
                            "Total Messages Echoed: %d\n",
                            message, timestamp, message_count);

                    send(clients[i], reply, strlen(reply), 0);
                }

            }
        }
    }
}
