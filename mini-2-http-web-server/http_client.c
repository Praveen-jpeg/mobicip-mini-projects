#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int main()
{
    int sock = socket(AF_INET,
                      SOCK_STREAM, 0);

    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr =
        inet_addr("127.0.0.1");

    connect(sock,
        (struct sockaddr*)&server,
        sizeof(server));

    char request[] =
        "GET / HTTP/1.0\r\n\r\n";

    send(sock, request,
         strlen(request), 0);

    char buffer[4096];
    int n = recv(sock,
                 buffer,
                 sizeof(buffer)-1, 0);

    buffer[n] = '\0';

    printf("%s\n", buffer);

    close(sock);
}
