#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PORT 8080
#define BUF_SIZE 1024

int main()
{
    SSL_library_init();
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_connect(ssl);

    char message[BUF_SIZE], buffer[BUF_SIZE * 2];

    while (1)
    {
        printf("Enter message: ");
        fgets(message, BUF_SIZE, stdin);

        if (strncmp(message, "exit", 4) == 0)
            break;

        time_t now = time(NULL);
        char *time_str = ctime(&now);
        time_str[strlen(time_str)-1] = '\0';

        char send_buf[BUF_SIZE];
        snprintf(send_buf, sizeof(send_buf), "%s|%s", time_str, message);

        SSL_write(ssl, send_buf, strlen(send_buf));

        memset(buffer, 0, sizeof(buffer));
        SSL_read(ssl, buffer, sizeof(buffer));

        printf("\n%s\n", buffer);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
}
