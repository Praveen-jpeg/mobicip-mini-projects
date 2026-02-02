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

void* handle_client(void* socket_desc)
{
    int client_sock = *(int*)socket_desc;
    char buffer[BUF_SIZE];
    char reply[BUF_SIZE * 2];

    while (1)
    {
        memset(buffer, 0, BUF_SIZE);
        int read_size = recv(client_sock, buffer, BUF_SIZE, 0);

        if (read_size <= 0)
            break;

        // Increase global message counter safely
        pthread_mutex_lock(&lock);
        message_count++;
        int current_count = message_count;
        pthread_mutex_unlock(&lock);

        // buffer format: <timestamp>|<message>
        char *timestamp = strtok(buffer, "|");
        char *message = strtok(NULL, "|");

        snprintf(reply, sizeof(reply),
                 "Server Echo:\nMessage: %s\nTimestamp: %s\nTotal Messages: %d\n\n",
                 message, timestamp, current_count);

        send(client_sock, reply, strlen(reply), 0);
    }

    close(client_sock);
    free(socket_desc);
    return NULL;
}

int main()
{
    int server_fd, client_sock;
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

    while ((client_sock = accept(server_fd, (struct sockaddr*)&client, &c)))
    {
        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        pthread_create(&thread_id, NULL, handle_client, (void*)new_sock);
        pthread_detach(thread_id);
    }

    close(server_fd);
    pthread_mutex_destroy(&lock);
    return 0;
}
