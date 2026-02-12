/*
 * ECHO SERVER PROGRAM
 * --------------------
 * - Listens for incoming TCP connections
 * - Supports IPv4 and IPv6
 * - Uses select() to handle multiple clients (no threads/fork)
 * - Receives timestamp|message format
 * - Returns formatted response including:
 *      message
 *      timestamp
 *      total messages echoed so far
 */

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
    struct addrinfo hints, *res;
    int listen_fd, client_fd, max_fd;
    int clients[MAX_CLIENTS];
    int message_count = 0;  // Global counter
    fd_set read_fds;
    char buffer[BUF_SIZE];

    /* Prepare address hints */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // Bind to all interfaces

    /* Resolve local address */
    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        perror("getaddrinfo");
        return EXIT_FAILURE;
    }

    /* Create listening socket */
    listen_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_fd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }

    /* Bind socket */
    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        close(listen_fd);
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }

    /* Start listening */
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    /* Initialize client array */
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i] = -1;

    printf("Echo Server running on port %s (select-based)\n", PORT);

    /* Main server loop */
    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        max_fd = listen_fd;

        /* Add active client sockets to fd_set */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] != -1) {
                FD_SET(clients[i], &read_fds);
                if (clients[i] > max_fd)
                    max_fd = clients[i];
            }
        }

        /* Wait for activity */
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        /* New connection request */
        if (FD_ISSET(listen_fd, &read_fds)) {
            client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd >= 0) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i] == -1) {
                        clients[i] = client_fd;
                        break;
                    }
                }
            }
        }

        /* Handle data from clients */
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i] != -1 && FD_ISSET(clients[i], &read_fds))
            {
                int n = recv(clients[i], buffer, BUF_SIZE - 1, 0);

                /* Client disconnected */
                if (n <= 0) {
                    close(clients[i]);
                    clients[i] = -1;
                }
                else {
                    buffer[n] = '\0';

                    /* Copy buffer safely */
                    char data[BUF_SIZE];
                    strncpy(data, buffer, BUF_SIZE - 1);
                    data[BUF_SIZE - 1] = '\0';

                    /* Parse timestamp and message */
                    char *timestamp = strtok(data, "|");
                    char *message   = strtok(NULL, "|");

                    if (!timestamp || !message)
                        continue;

                    /* Remove trailing newline */
                    size_t len = strlen(message);
                    if (len > 0 && message[len - 1] == '\n')
                        message[len - 1] = '\0';

                    /* Ignore empty messages */
                    if (strlen(message) == 0)
                        continue;

                    /* Increment global counter */
                    message_count++;

                    /* Prepare formatted reply */
                    char reply[BUF_SIZE * 2];
                    snprintf(reply, sizeof(reply),
                             "Server Echo:\n"
                             "Message: %s\n"
                             "Timestamp: %s\n"
                             "Total Messages Echoed: %d\n",
                             message, timestamp, message_count);

                    /* Send response */
                    send(clients[i], reply, strlen(reply), 0);
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}
