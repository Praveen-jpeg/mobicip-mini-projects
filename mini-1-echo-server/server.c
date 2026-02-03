#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUF_SIZE 1024

int message_count = 0;
pthread_mutex_t lock;

struct client_info {
    int sock;
    char ip[INET_ADDRSTRLEN];
};

void* handle_client(void* arg)
{
    struct client_info *info = (struct client_info*)arg;
    int client_sock = info->sock;
    char client_ip[INET_ADDRSTRLEN];
    strcpy(client_ip, info->ip);
    free(info);

    char buffer[BUF_SIZE];
    char reply[BUF_SIZE * 2];

    while (1)
    {
        memset(buffer, 0, BUF_SIZE);
        int read_size = recv(client_sock, buffer, BUF_SIZE - 1, 0);

        if (read_size <= 0)
            break;

        pthread_mutex_lock(&lock);
        message_count++;
        int current_count = message_count;
        pthread_mutex_unlock(&lock);

        char data[BUF_SIZE];
        strcpy(data, buffer);

        char *timestamp = strtok(data, "|");
        char *message = strtok(NULL, "|");

        snprintf(reply, sizeof(reply),
                 "Server Echo:\nMessage: %sTimestamp: %s\nTotal Messages: %d\nClient IP: %s\n\n",
                 message, timestamp, current_count, client_ip);

        send(client_sock, reply, strlen(reply), 0);
    }

    close(client_sock);
    return NULL;
}

int main()
{
    int server_fd;
    struct sockaddr_in server, client;
    socklen_t c = sizeof(client);

    pthread_mutex_init(&lock, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server, sizeof(server));
    listen(server_fd, 5);

    printf("Echo Server running on port %d...\n", PORT);

    while (1)
    {
        int client_sock = accept(server_fd, (struct sockaddr*)&client, &c);

        struct client_info *info = malloc(sizeof(struct client_info));
        info->sock = client_sock;
        inet_ntop(AF_INET, &client.sin_addr, info->ip, INET_ADDRSTRLEN);

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, info);
        pthread_detach(thread_id);
    }

    close(server_fd);
    pthread_mutex_destroy(&lock);
    return 0;
}
