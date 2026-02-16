/*
 * HTTP Web Server using epoll
 * Supports:
 * - HTTP/1.0
 * - GET
 * - POST
 * - Basic multipart upload
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>

#define PORT 8080
#define MAX_EVENTS 1024
#define BUF_SIZE 8192

#define WWW_ROOT "/var/www"
#define UPLOAD_DIR "/var/www/uploads"

void set_nonblocking(int fd)
{
    fcntl(fd, F_SETFL,
          fcntl(fd, F_GETFL) | O_NONBLOCK);
}

/* send simple HTTP response */
void send_response(int fd, const char *status,
                   const char *type,
                   const char *body)
{
    char header[1024];

    sprintf(header,
        "HTTP/1.0 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n\r\n",
        status, type, strlen(body));

    send(fd, header, strlen(header), 0);
    send(fd, body, strlen(body), 0);
}

/* GET handling */
void handle_get(int fd, char *path)
{
    char fullpath[512];

    if (strcmp(path, "/") == 0)
        strcpy(path, "/index.html");

    sprintf(fullpath, "%s%s", WWW_ROOT, path);

    FILE *file = fopen(fullpath, "rb");

    if (!file) {
        send_response(fd, "404 Not Found",
                      "text/plain",
                      "File Not Found");
        return;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *content = malloc(size);
    fread(content, 1, size, file);
    fclose(file);

    char header[1024];
    sprintf(header,
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: text/html\r\n\r\n",
        size);

    send(fd, header, strlen(header), 0);
    send(fd, content, size, 0);

    free(content);
}

/* POST handling (basic upload) */
void handle_post(int fd, char *req)
{
    char *body = strstr(req, "\r\n\r\n");
    if (!body) return;

    body += 4;

    char filename[256];
    sprintf(filename,
            "%s/upload_%ld.txt",
            UPLOAD_DIR, time(NULL));

    FILE *f = fopen(filename, "w");
    if (f) {
        fwrite(body, 1, strlen(body), f);
        fclose(f);
    }

    send_response(fd, "200 OK",
                  "text/plain",
                  "Upload Successful");
}

/* parse request */
void handle_client(int fd)
{
    char buffer[BUF_SIZE];

    int n = recv(fd, buffer,
                 sizeof(buffer)-1, 0);

    if (n <= 0) {
        close(fd);
        return;
    }

    buffer[n] = '\0';

    char method[16], path[256];
    sscanf(buffer, "%s %s", method, path);

    if (strcmp(method, "GET") == 0)
        handle_get(fd, path);

    else if (strcmp(method, "POST") == 0)
        handle_post(fd, buffer);

    else
        send_response(fd, "400 Bad Request",
                      "text/plain",
                      "Unsupported Method");

    close(fd);
}

int main()
{
    int server_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd,
         (struct sockaddr*)&addr,
         sizeof(addr));

    listen(server_fd, SOMAXCONN);

    set_nonblocking(server_fd);

    int epfd = epoll_create1(0);

    struct epoll_event ev, events[MAX_EVENTS];

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    epoll_ctl(epfd, EPOLL_CTL_ADD,
              server_fd, &ev);

    printf("HTTP Server running on %d\n", PORT);

    while (1)
    {
        int n = epoll_wait(epfd,
                           events,
                           MAX_EVENTS,
                           -1);

        for (int i = 0; i < n; i++)
        {
            if (events[i].data.fd == server_fd)
            {
                int client =
                    accept(server_fd,
                           NULL, NULL);

                set_nonblocking(client);

                ev.events = EPOLLIN;
                ev.data.fd = client;

                epoll_ctl(epfd,
                          EPOLL_CTL_ADD,
                          client,
                          &ev);
            }
            else
            {
                handle_client(
                    events[i].data.fd);
            }
        }
    }

    return 0;
}
