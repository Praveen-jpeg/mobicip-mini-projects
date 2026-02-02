#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 8080
#define BUF_SIZE 1024

int main()
{
    int sock;
    struct sockaddr_in server;
    char message[BUF_SIZE], buffer[BUF_SIZE * 2];

    sock = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(sock, (struct sockaddr*)&server, sizeof(server));

    while (1)
    {
        printf("Enter message: ");
        fgets(message, BUF_SIZE, stdin);

        if (strncmp(message, "exit", 4) == 0)
            break;

        // Get timestamp
        time_t now;
        time(&now);
        char *time_str = ctime(&now);
        time_str[strlen(time_str)-1] = '\0'; // remove newline

        // Format: timestamp|message
        char send_buf[BUF_SIZE];
        snprintf(send_buf, sizeof(send_buf), "%s|%s", time_str, message);

        send(sock, send_buf, strlen(send_buf), 0);

        memset(buffer, 0, sizeof(buffer));
        recv(sock, buffer, sizeof(buffer), 0);

        printf("\n%s\n", buffer);
    }

    close(sock);
    return 0;
}
