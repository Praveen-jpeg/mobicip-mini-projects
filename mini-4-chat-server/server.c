/* Header usage summary:
   arpa/inet.h   -> sockaddr_in, htons(), INADDR_ANY
   ctype.h       -> tolower() in normalize_username()
   errno.h       -> errno, EINTR while handling select()
   openssl/evp.h -> EVP_EncodeBlock() for WebSocket accept key
   openssl/sha.h -> SHA1() for WebSocket handshake hashing
   signal.h      -> signal(), sig_atomic_t, SIGINT, SIGTERM, SIGPIPE
   stdio.h       -> printf(), perror(), snprintf(), fopen(), fread()
   stdlib.h      -> malloc(), free()
   string.h      -> strlen(), strcmp(), strncpy(), strstr(), memset()
   sys/select.h  -> fd_set, FD_SET(), FD_ISSET(), select()
   sys/socket.h  -> socket(), setsockopt(), bind(), listen(), accept(), recv(), send()
   unistd.h      -> close() */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define SERVER_PORT 8080
#define MAX_CLIENTS 100
#define HTTP_BUFFER 8192
#define WS_BUFFER 4096
#define USERNAME_LEN 31
/* Each connected socket is tracked here. A socket starts as plain HTTP and
   becomes a WebSocket client after a successful upgrade request. */
typedef struct {
    int fd;
    int active;
    int websocket;
    char username[USERNAME_LEN + 1];
    /* Lowercased copy used only for case-insensitive comparisons. */
    char username_key[USERNAME_LEN + 1];
} Client;
static Client clients[MAX_CLIENTS];
static volatile sig_atomic_t keep_running = 1;
/* Keep client-slot cleanup in one place so init/add/close stay consistent. */
static void reset_client_slot(Client *client) {
    client->fd = -1;
    client->active = 0;
    client->websocket = 0;
    client->username[0] = '\0';
    client->username_key[0] = '\0';
}
/* Stop the main loop cleanly when Ctrl+C or termination is received. */
static void handle_signal(int signo) {
    (void)signo;
    keep_running = 0;
}
/* Reset every client slot so the server starts from a known empty state. */
static void init_clients(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        reset_client_slot(&clients[i]);
    }
}
/* Place a newly accepted socket into the first free client slot. */
static int add_client(int client_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            reset_client_slot(&clients[i]);
            clients[i].fd = client_fd;
            clients[i].active = 1;
            return i;
        }
    }
    return -1;
}
/* Remove a client completely and clear all stored state for that slot. */
static void close_client(int index) {
    if (index < 0 || index >= MAX_CLIENTS || !clients[index].active) {
        return;
    }
    close(clients[index].fd);
    reset_client_slot(&clients[index]);
}
static int send_all(int fd, const unsigned char *data, int length) {
    int sent = 0;
    /* Keep sending until the full response/frame has been written. */
    while (sent < length) {
        int chunk = (int)send(fd, data + sent, length - sent, MSG_NOSIGNAL);
        if (chunk <= 0) {
            return -1;
        }
        sent += chunk;
    }
    return 0;
}
typedef struct {
    const char *path;
    const char *file;
    const char *content_type;
} StaticRoute;
/* Static file routes served by this single-process HTTP/WebSocket server. */
static const StaticRoute static_routes[] = {
    {"/", "index.html", "text/html; charset=utf-8"},
    {"/index.html", "index.html", "text/html; charset=utf-8"},
    {"/style.css", "style.css", "text/css; charset=utf-8"},
    {"/client.js", "client.js", "application/javascript; charset=utf-8"},
};
/* Resolve URL path to both the local file and the response content type. */
static int resolve_static_route(const char *path, const char **file,
                                const char **content_type) {
    size_t count = sizeof(static_routes) / sizeof(static_routes[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(path, static_routes[i].path) == 0) {
            *file = static_routes[i].file;
            *content_type = static_routes[i].content_type;
            return 0;
        }
    }
    return -1;
}
/* Build and send a simple HTTP response with headers and optional body. */
static void send_http_response(int fd, int status, const char *status_text,
                               const char *content_type,
                               const unsigned char *body, int body_len) {
    char header[512];
    int header_len = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n\r\n",
        status, status_text, content_type, body_len);
    if (header_len < 0) {
        return;
    }
    send_all(fd, (const unsigned char *)header, header_len);
    if (body_len > 0) {
        send_all(fd, body, body_len);
    }
}
static void serve_file(int fd, const char *path) {
    const char *local_path;
    const char *content_type;
    /* The same server handles both the webpage files and WebSocket traffic. */
    if (resolve_static_route(path, &local_path, &content_type) != 0) {
        const char *body = "404 Not Found\n";
        send_http_response(fd, 404, "Not Found", "text/plain; charset=utf-8",
                           (const unsigned char *)body, (int)strlen(body));
        return;
    }
    FILE *file = fopen(local_path, "rb");
    if (file == NULL) {
        const char *body = "500 Internal Server Error\n";
        send_http_response(fd, 500, "Internal Server Error",
                           "text/plain; charset=utf-8",
                           (const unsigned char *)body, (int)strlen(body));
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return;
    }
    rewind(file);
    unsigned char *body = (unsigned char *)malloc((size_t)size);
    if (body == NULL) {
        fclose(file);
        return;
    }
    if (size > 0 && fread(body, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(body);
        return;
    }
    fclose(file);
    send_http_response(fd, 200, "OK", content_type, body, (int)size);
    free(body);
}
/* Send a WebSocket text frame to the browser. Server frames are not masked. */
static int ws_send_text(int fd, const char *message) {
    unsigned char frame[WS_BUFFER + 16];
    size_t len = strlen(message);
    int pos = 0;
    /* Build a minimal server-to-browser text frame. */
    if (len > WS_BUFFER) {
        return -1;
    }
    frame[pos++] = 0x81;
    if (len <= 125) {
        frame[pos++] = (unsigned char)len;
    } else {
        frame[pos++] = 126;
        frame[pos++] = (unsigned char)((len >> 8) & 0xFF);
        frame[pos++] = (unsigned char)(len & 0xFF);
    }
    memcpy(frame + pos, message, len);
    pos += (int)len;
    return send_all(fd, frame, pos);
}
/* Escape special characters so the message can be embedded safely in JSON. */
static void escape_json(const char *src, char *dest, size_t size) {
    size_t i = 0;
    if (size == 0) {
        return;
    }
    while (*src != '\0' && i + 1 < size) {
        if (*src == '"' || *src == '\\') {
            if (i + 2 >= size) {
                break;
            }
            dest[i++] = '\\';
            dest[i++] = *src++;
            continue;
        }
        if (*src == '\n' || *src == '\r') {
            if (i + 2 >= size) {
                break;
            }
            dest[i++] = '\\';
            dest[i++] = 'n';
            src++;
            continue;
        }
        dest[i++] = *src++;
    }
    dest[i] = '\0';
}
/* Wrap server events into a small JSON object that the browser can parse. */
static int send_json_message(int fd, const char *type, const char *sender,
                             const char *recipient, const char *message) {
    char safe_sender[128];
    char safe_recipient[128];
    char safe_message[WS_BUFFER / 2];
    char payload[WS_BUFFER];
    escape_json(sender ? sender : "", safe_sender, sizeof(safe_sender));
    escape_json(recipient ? recipient : "", safe_recipient,
                sizeof(safe_recipient));
    escape_json(message ? message : "", safe_message, sizeof(safe_message));
    if (snprintf(payload, sizeof(payload),
                 "{\"type\":\"%s\",\"sender\":\"%s\",\"recipient\":\"%s\","
                 "\"message\":\"%s\"}",
                 type ? type : "", safe_sender, safe_recipient,
                 safe_message) >=
        (int)sizeof(payload)) {
        return -1;
    }
    return ws_send_text(fd, payload);
}
/* Small wrapper for common server-origin events with no recipient. */
static int send_event(int fd, const char *type, const char *sender,
                      const char *message) {
    return send_json_message(fd, type, sender, "", message);
}
/* Consistent error payload helper used across command handlers. */
static int send_error(int fd, const char *message) {
    return send_event(fd, "error", "server", message);
}
static void normalize_username(const char *src, char *dest, size_t size) {
    size_t i = 0;
    if (size == 0) {
        return;
    }
    /* Store a lowercase key so username checks ignore letter casing. */
    while (src[i] != '\0' && i + 1 < size) {
        dest[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dest[i] = '\0';
}
/* Find a logged-in client using the normalized username key. */
static Client *find_client_by_name(const char *username) {
    char normalized[USERNAME_LEN + 1];
    normalize_username(username, normalized, sizeof(normalized));
    /* Compare against the normalized key, not the display name. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].websocket &&
            strcmp(clients[i].username_key, normalized) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}
/* Restrict usernames to a safe, simple character set for easier parsing. */
static int valid_username(const char *username) {
    size_t len = strlen(username);
    if (len == 0 || len > USERNAME_LEN) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        char c = username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}
static void broadcast_public_message(const char *sender, const char *message) {
    /* Public chat messages are sent to every logged-in WebSocket client. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].websocket &&
            clients[i].username[0] != '\0') {
            send_event(clients[i].fd, "public", sender, message);
        }
    }
}
/* System messages are used for events such as join/leave notifications. */
static void broadcast_system_message(const char *message) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].websocket) {
            send_event(clients[i].fd, "system", "server", message);
        }
    }
}
/* Remove newline and trailing spaces from incoming text commands. */
static void trim_message(char *text) {
    int len = (int)strlen(text);
    while (len > 0 &&
           (text[len - 1] == '\r' || text[len - 1] == '\n' ||
            text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len--;
    }
}
static int parse_ws_frame(int fd, char *out, size_t out_size) {
    unsigned char header[2];
    unsigned char mask[4];
    unsigned char payload[WS_BUFFER];
    unsigned long long payload_len;
    /* Read and decode a single masked browser-to-server text frame. */
    if (recv(fd, header, 2, MSG_WAITALL) != 2) {
        return -1;
    }
    /* Opcode 0x8 means the client is closing the connection. */
    if ((header[0] & 0x0F) == 0x08) {
        return -1;
    }
    /* This implementation accepts only masked text frames from browsers. */
    if ((header[0] & 0x0F) != 0x01 || (header[1] & 0x80) == 0) {
        return -1;
    }
    /* Small payloads fit directly in the second byte, larger ones use an
       extra 2-byte length field. */
    payload_len = (unsigned long long)(header[1] & 0x7F);
    if (payload_len == 126) {
        unsigned char ext[2];
        if (recv(fd, ext, 2, MSG_WAITALL) != 2) {
            return -1;
        }
        payload_len = ((unsigned long long)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        return -1;
    }
    if (payload_len >= out_size || payload_len >= sizeof(payload)) {
        return -1;
    }
    /* Browser-to-server frames are masked, so read the masking key first. */
    if (recv(fd, mask, 4, MSG_WAITALL) != 4) {
        return -1;
    }
    if ((unsigned long long)recv(fd, payload, payload_len, MSG_WAITALL) !=
        payload_len) {
        return -1;
    }
    /* Unmask the payload to recover the original text command. */
    for (unsigned long long i = 0; i < payload_len; i++) {
        out[i] = (char)(payload[i] ^ mask[i % 4]);
    }
    out[payload_len] = '\0';
    return (int)payload_len;
}
static int do_websocket_handshake(int fd, const char *request) {
    const char *key_header = strstr(request, "Sec-WebSocket-Key:");
    char client_key[128];
    char combined[256];
    unsigned char sha1_hash[20];
    char accept_key[128];
    char response[512];
    /* The browser sends Sec-WebSocket-Key in the HTTP upgrade request. */
    if (key_header == NULL) {
        return -1;
    }
    key_header += strlen("Sec-WebSocket-Key:");
    while (*key_header == ' ') {
        key_header++;
    }
    if (sscanf(key_header, "%127s", client_key) != 1) {
        return -1;
    }
    /* WebSocket handshake: combine client key with the RFC magic string,
       hash it, then return Sec-WebSocket-Accept in the upgrade response. */
    snprintf(combined, sizeof(combined),
             "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", client_key);
    SHA1((const unsigned char *)combined, strlen(combined), sha1_hash);
    EVP_EncodeBlock((unsigned char *)accept_key, sha1_hash, 20);
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept_key);
    return send_all(fd, (const unsigned char *)response, (int)strlen(response));
}
/* Handle the LOGIN|username command from the browser. */
static void handle_login(Client *client, char *username) {
    char joined[128];
    char normalized[USERNAME_LEN + 1];
    trim_message(username);
    /* First successful login claims that username for the whole server,
       regardless of casing. */
    if (!valid_username(username)) {
        send_error(client->fd, "Username must contain only letters, numbers, _ or -");
        return;
    }
    if (find_client_by_name(username) != NULL) {
        send_error(client->fd, "That username is already in use");
        return;
    }
    /* Keep both versions:
       - username: original display name shown in the UI
       - username_key: lowercase form used for uniqueness checks */
    normalize_username(username, normalized, sizeof(normalized));
    strncpy(client->username, username, USERNAME_LEN);
    client->username[USERNAME_LEN] = '\0';
    strncpy(client->username_key, normalized, USERNAME_LEN);
    client->username_key[USERNAME_LEN] = '\0';
    send_event(client->fd, "login", client->username, "You joined the chat");
    snprintf(joined, sizeof(joined), "%s joined the public chat", client->username);
    broadcast_system_message(joined);
}
/* Handle the PUBLIC|message command from the browser. */
static void handle_public_message(Client *client, char *message) {
    trim_message(message);
    /* Users must log in before they can send chat messages. */
    if (client->username[0] == '\0') {
        send_error(client->fd, "Choose a username first");
        return;
    }
    if (message[0] == '\0') {
        return;
    }
    broadcast_public_message(client->username, message);
}
/* Handle the PRIVATE|username|message command from the browser. */
static void handle_private_message(Client *client, char *payload) {
    char *target = payload;
    char *message = strchr(payload, '|');
    Client *recipient_client;
    if (client->username[0] == '\0') {
        send_error(client->fd, "Choose a username first");
        return;
    }
    if (message == NULL) {
        send_error(client->fd, "Private format is PRIVATE|username|message");
        return;
    }
    *message = '\0';
    message++;
    trim_message(target);
    trim_message(message);
    if (target[0] == '\0' || message[0] == '\0') {
        send_error(client->fd, "Private message needs both user and text");
        return;
    }
    recipient_client = find_client_by_name(target);
    if (recipient_client == NULL) {
        send_error(client->fd, "Target user is not online");
        return;
    }
    if (recipient_client == client) {
        send_error(client->fd, "You cannot send a private message to yourself");
        return;
    }
    send_json_message(recipient_client->fd, "private", client->username,
                      recipient_client->username, message);
    /* Mirror the sent message back so the sender also sees it in the chat log. */
    send_json_message(client->fd, "private", client->username,
                      recipient_client->username, message);
}
static void handle_ws_message(int index) {
    char buffer[WS_BUFFER];
    Client *client = &clients[index];
    /* Once upgraded, this socket stops speaking HTTP and only exchanges
       WebSocket frames. */
    if (parse_ws_frame(client->fd, buffer, sizeof(buffer)) <= 0) {
        if (client->username[0] != '\0') {
            char left[128];
            snprintf(left, sizeof(left), "%s left the chat", client->username);
            close_client(index);
            broadcast_system_message(left);
        } else {
            close_client(index);
        }
        return;
    }
    /* Commands use a simple text protocol:
       LOGIN|username
       PUBLIC|message
       PRIVATE|username|message */
    if (strncmp(buffer, "LOGIN|", 6) == 0) {
        handle_login(client, buffer + 6);
        return;
    }
    if (strncmp(buffer, "PUBLIC|", 7) == 0) {
        handle_public_message(client, buffer + 7);
        return;
    }
    if (strncmp(buffer, "PRIVATE|", 8) == 0) {
        handle_private_message(client, buffer + 8);
        return;
    }
    send_error(client->fd, "Unknown command");
}
/* Handle one plain HTTP request before the socket is closed or upgraded. */
static void handle_http_request(int index) {
    char request[HTTP_BUFFER];
    char method[16];
    char path[256];
    int fd = clients[index].fd;
    int bytes_received = (int)recv(fd, request, sizeof(request) - 1, 0);
    /* A disconnected client or bad read means there is nothing to process. */
    if (bytes_received <= 0) {
        close_client(index);
        return;
    }
    request[bytes_received] = '\0';
    /* Normal GET requests serve files; GET /ws upgrades to WebSocket. */
    if (strstr(request, "Upgrade: websocket") != NULL &&
        strstr(request, "GET /ws ") != NULL) {
        if (do_websocket_handshake(fd, request) == 0) {
            clients[index].websocket = 1;
            send_event(fd, "system", "server",
                       "Connected. Enter a username to begin.");
            return;
        }
        close_client(index);
        return;
    }
    /* Parse the request line to get the HTTP method and requested path. */
    if (sscanf(request, "%15s %255s", method, path) != 2) {
        const char *body = "400 Bad Request\n";
        send_http_response(fd, 400, "Bad Request", "text/plain; charset=utf-8",
                           (const unsigned char *)body, (int)strlen(body));
        close_client(index);
        return;
    }
    if (strcmp(method, "GET") != 0) {
        const char *body = "405 Method Not Allowed\n";
        send_http_response(fd, 405, "Method Not Allowed",
                           "text/plain; charset=utf-8",
                           (const unsigned char *)body, (int)strlen(body));
        close_client(index);
        return;
    }
    serve_file(fd, path);
    close_client(index);
}
int main(void) {
    int server_fd;
    int opt = 1;
    struct sockaddr_in server_addr;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    init_clients();
    /* Create the TCP listening socket for both HTTP and WebSocket clients. */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    /* Allow quick restarts without waiting for the old port to fully expire. */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    /* bind() attaches the socket to port 8080 on all network interfaces. */
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    /* listen() turns the socket into a passive socket that accepts clients. */
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    printf("Chat server running on http://127.0.0.1:%d\n", SERVER_PORT);
    while (keep_running) {
        fd_set readfds;
        int max_fd = server_fd;
        /* select() lets one thread watch the listening socket and every
           connected client socket at the same time. */
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd > max_fd) {
                    max_fd = clients[i].fd;
                }
            }
        }
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }
        /* If the listening socket is ready, a new client is waiting. */
        if (FD_ISSET(server_fd, &readfds)) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("accept");
            } else {
                int slot = add_client(client_fd);
                if (slot < 0) {
                    const char *body = "Server is full\n";
                    send_http_response(client_fd, 503, "Service Unavailable",
                                       "text/plain; charset=utf-8",
                                       (const unsigned char *)body,
                                       (int)strlen(body));
                    close(client_fd);
                }
            }
        }
        /* Process whichever connected client sockets became readable. */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active || !FD_ISSET(clients[i].fd, &readfds)) {
                continue;
            }
            /* Before upgrade the socket speaks HTTP; after upgrade it speaks
               only WebSocket frames. */
            if (clients[i].websocket) {
                handle_ws_message(i);
            } else {
                handle_http_request(i);
            }
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            close(clients[i].fd);
        }
    }
    close(server_fd);
    printf("Server stopped.\n");
    return 0;
}
