/*
 * Simple HTTP Client
 * ------------------
 * Supports:
 * - GET request
 * - POST request
 * Used to test HTTP server
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

#define PORT 8080
#define BUF_SIZE 8192

int main()
{
    int sock;
    struct sockaddr_in server;
    char buffer[BUF_SIZE];

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    /* Server configuration */
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* Connect to server */
    if (connect(sock,
        (struct sockaddr*)&server,
        sizeof(server)) < 0)
    {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected to HTTP server.\n");

    /* -------- CHOOSE REQUEST TYPE -------- */

    printf("\n1. GET Request\n");
    printf("2. POST Request\n");
    printf("Choose option: ");

    int choice;
    scanf("%d", &choice);
    getchar(); // clear newline

    char request[BUF_SIZE];

    if (choice == 1)
    {
        /* GET request */
        sprintf(request,
                "GET / HTTP/1.0\r\n\r\n");
    }
    else if (choice == 2)
    {
        char data[1024];

        printf("Enter POST data: ");
        fgets(data, sizeof(data), stdin);

        data[strcspn(data, "\n")] = 0;

        sprintf(request,
            "POST / HTTP/1.0\r\n"
            "Content-Length: %ld\r\n"
            "\r\n"
            "%s",
            strlen(data), data);
    }
    else
    {
        printf("Invalid choice\n");
        close(sock);
        return 0;
    }

    /* Send request */
    send(sock, request, strlen(request), 0);

    /* Receive response */
    int n = recv(sock,
                 buffer,
                 sizeof(buffer)-1, 0);

    if (n > 0)
    {
        buffer[n] = '\0';
        printf("\n----- SERVER RESPONSE -----\n");
        printf("%s\n", buffer);
    }

    close(sock);
    return 0;
}
