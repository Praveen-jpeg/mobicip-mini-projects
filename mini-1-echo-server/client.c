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
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
        return 1;
    }

    int sock;
    struct addrinfo hints, *res;
    char message[BUF_SIZE], buffer[BUF_SIZE * 2];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;       // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(argv[1], PORT, &hints, &res);

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    while (1)
    {
        printf("Enter message: ");
        fgets(message, BUF_SIZE, stdin);
        
        size_t len = strlen(message);
        if (len > 0 && message[len - 1] == '\n')
            message[len - 1] = '\0';

        // Ignore empty message after trimming
        if (strlen(message) == 0)
            continue;

        if (strncmp(message, "exit", 4) == 0)
            break;

        time_t now = time(NULL);
        char *ts = ctime(&now);
        ts[strlen(ts) - 1] = '\0';

        char send_buf[BUF_SIZE];
        snprintf(send_buf, sizeof(send_buf), "%s|%s", ts, message);

        send(sock, send_buf, strlen(send_buf), 0);

        memset(buffer, 0, sizeof(buffer));
        recv(sock, buffer, sizeof(buffer), 0);

        printf("\n%s\n", buffer);
    }

    close(sock);
    return 0;
}
