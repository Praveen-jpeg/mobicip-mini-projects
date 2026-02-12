/*
 * CLIENT PROGRAM
 * --------------
 * - Accepts server IP address via command-line argument
 * - Connects to server using TCP sockets
 * - Sends message along with current timestamp
 * - Receives echoed response with message count
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include <stdlib.h>

#define PORT "8080"
#define BUF_SIZE 1024

int main(int argc, char *argv[])
{
    /* Ensure server IP is provided */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sock;
    struct addrinfo hints, *res;

    /* Buffer for sending and receiving */
    char message[BUF_SIZE];
    char buffer[BUF_SIZE * 2];

    /* Prepare hints for getaddrinfo */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // Supports IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    /* Resolve server IP address */
    if (getaddrinfo(argv[1], PORT, &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    /* Create socket */
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        perror("socket");
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }

    /* Connect to server */
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(sock);
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }

    freeaddrinfo(res);

    /* Communication loop */
    while (1)
    {
        printf("Enter message: ");

        /* Read user input */
        if (!fgets(message, BUF_SIZE, stdin))
            break;

        /* If input exceeds buffer size, flush remaining characters */
        if (!strchr(message, '\n')) {
            printf("⚠ Input too long (max %d characters). Truncated.\n", BUF_SIZE - 1);
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF);
        }

        /* Remove newline character */
        size_t len = strlen(message);
        if (len > 0 && message[len - 1] == '\n')
            message[len - 1] = '\0';

        /* Ignore empty messages */
        if (strlen(message) == 0)
            continue;

        /* Exit condition */
        if (strcmp(message, "exit") == 0)
            break;

        /* Generate current timestamp */
        time_t now = time(NULL);
        char *timestamp = ctime(&now);
        timestamp[strlen(timestamp) - 1] = '\0';

        /* Combine timestamp and message using delimiter */
        char send_buf[BUF_SIZE];
        snprintf(send_buf, sizeof(send_buf), "%s|%s", timestamp, message);

        /* Send data to server */
        if (send(sock, send_buf, strlen(send_buf), 0) < 0) {
            perror("send");
            break;
        }

        /* Receive server response */
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            perror("recv");
            break;
        }

        buffer[n] = '\0';
        printf("\n%s\n", buffer);
    }

    close(sock);
    return 0;
}
