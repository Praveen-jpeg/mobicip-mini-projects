#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8080
#define BUF_SIZE 1024

int message_count = 0;
pthread_mutex_t lock;

struct client_info {
    int sock;
    SSL *ssl;
};

void* handle_client(void *arg)
{
    struct client_info *info = (struct client_info*)arg;
    SSL *ssl = info->ssl;
    int client = info->sock;
    free(info);

    char buffer[BUF_SIZE];
    char reply[BUF_SIZE * 2];

    while (1)
    {
        memset(buffer, 0, BUF_SIZE);
        int bytes = SSL_read(ssl, buffer, BUF_SIZE - 1);
        if (bytes <= 0) break;

        pthread_mutex_lock(&lock);
        message_count++;
        int count = message_count;
        pthread_mutex_unlock(&lock);

        char data[BUF_SIZE];
        strcpy(data, buffer);

        char *timestamp = strtok(data, "|");
        char *message = strtok(NULL, "|");

        snprintf(reply, sizeof(reply),
                 "Secure Echo:\nMessage: %sTimestamp: %s\nTotal Messages: %d\n\n",
                 message, timestamp, count);

        SSL_write(ssl, reply, strlen(reply));
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client);
    return NULL;
}

int main()
{
    SSL_library_init();
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());

    SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM);

    pthread_mutex_init(&lock, NULL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("🔐 Secure Echo Server running on port %d\n", PORT);

    while (1)
    {
        int client = accept(server_fd, NULL, NULL);

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);
        SSL_accept(ssl);

        struct client_info *info = malloc(sizeof(struct client_info));
        info->sock = client;
        info->ssl = ssl;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, info);
        pthread_detach(tid);
    }
}
