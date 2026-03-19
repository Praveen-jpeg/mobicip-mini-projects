/*
 * Tic Tac Toe WebSocket Server
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define PORT 9001
#define MAX_CLIENTS 100
#define BUF_SIZE 4096

typedef struct{
    int p1;
    int p2;
    char board[9];
    int turn;
    int active;
    int finished;
}Game;

int clients[MAX_CLIENTS];
Game games[MAX_CLIENTS/2];

int client_count=0;
int game_count=0;

void ws_send(int fd,const char *msg);
int websocket_handshake(int fd,char *req);
int client_in_active_game(int fd);
void notify_waiting_clients(void);

/* Reuse an inactive game slot before growing the game table. */
int alloc_game_slot(){

    for(int i=0;i<game_count;i++)
        if(!games[i].active) return i;

    if(game_count>=MAX_CLIENTS/2) return -1;

    return game_count++;
}

/* Check whether a socket is already assigned to an active game. */
int client_in_active_game(int fd){

    for(int i=0;i<game_count;i++){
        if(!games[i].active) continue;
        if(fd==games[i].p1 || fd==games[i].p2) return 1;
    }

    return 0;
}

/* Tell idle clients they are connected but still waiting to be paired. */
void notify_waiting_clients(void){

    for(int i=0;i<client_count;i++){
        if(client_in_active_game(clients[i])) continue;
        printf("[debug] client fd=%d is waiting for an opponent\n",clients[i]);
        ws_send(clients[i],"WAITING");
    }
}

/* Remove a disconnected client from any active match and notify the opponent. */
void disconnect_from_games(int fd){

    for(int i=0;i<game_count;i++){

        Game *g=&games[i];
        if(!g->active) continue;

        if(fd!=g->p1 && fd!=g->p2) continue;

        int other=(fd==g->p1)?g->p2:g->p1;
        if(other>=0){
            printf("[debug] client fd=%d disconnected, notifying opponent fd=%d\n",fd,other);
            ws_send(other,"OPPONENT_LEFT");
        }

        g->active=0;
        g->finished=1;
        g->p1=-1;
        g->p2=-1;
    }
}

/* -------- SEND WS FRAME -------- */

void ws_send(int fd,const char *msg){

    unsigned char frame[1024];
    int len=strlen(msg);
    if(len<0 || len>125) return;

    frame[0]=0x81;
    frame[1]=len;

    memcpy(frame+2,msg,len);

    send(fd,frame,len+2,MSG_NOSIGNAL);
}

/* -------- HANDSHAKE -------- */

int websocket_handshake(int fd,char *req){

    char *key=strstr(req,"Sec-WebSocket-Key:");
    if(!key) return -1;

    key+=19;
    while(*key==' ') key++;

    char client_key[128];
    sscanf(key,"%s",client_key);

    char combined[256];
    sprintf(combined,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",client_key);

    unsigned char sha1[20];
    SHA1((unsigned char*)combined,strlen(combined),sha1);

    char accept[256];
    EVP_EncodeBlock((unsigned char*)accept,sha1,20);

    char response[512];

    /* Complete the standard WebSocket accept handshake. */
    sprintf(response,
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %s\r\n\r\n",accept);

    if(send(fd,response,strlen(response),MSG_NOSIGNAL)<0) return -1;
    return 0;
}

/* -------- WIN CHECK -------- */

int winner(char *b){

    int w[8][3]={
    {0,1,2},{3,4,5},{6,7,8},
    {0,3,6},{1,4,7},{2,5,8},
    {0,4,8},{2,4,6}};

    for(int i=0;i<8;i++)
        if(b[w[i][0]]!=' ' &&
           b[w[i][0]]==b[w[i][1]] &&
           b[w[i][1]]==b[w[i][2]])
           return 1;

    return 0;
}

int draw(char *b){

    for(int i=0;i<9;i++)
        if(b[i]==' ') return 0;

    return 1;
}

/* -------- RESET GAME -------- */

void reset_game(Game *g){

    for(int i=0;i<9;i++)
        g->board[i]=' ';

    g->turn=0;
    g->finished=0;

    ws_send(g->p1,"RESET");
    ws_send(g->p2,"RESET");

    ws_send(g->p1,"YOUR_TURN");
    ws_send(g->p2,"OPPONENT_TURN");
}

/* -------- PAIR PLAYERS -------- */

void pair_players(){

    while(1){

        int p1=-1;
        int p2=-1;

        for(int i=0;i<client_count;i++){
            if(client_in_active_game(clients[i])) continue;

            if(p1==-1){
                p1=clients[i];
            }else{
                p2=clients[i];
                break;
            }
        }

        if(p1==-1 || p2==-1) break;

        int slot=alloc_game_slot();
        if(slot==-1) break;

        Game *g=&games[slot];

        g->p1=p1;
        g->p2=p2;
        g->active=1;
        g->finished=0;

        ws_send(g->p1,"INFO X");
        ws_send(g->p2,"INFO O");
        reset_game(g);

        printf("[debug] paired game slot=%d p1(fd=%d,X) with p2(fd=%d,O)\n",slot,g->p1,g->p2);
    }

    notify_waiting_clients();
}

/* -------- HANDLE MOVE -------- */

void handle_move(int fd,int pos){

    for(int i=0;i<game_count;i++){

        Game *g=&games[i];

        if(!g->active) continue;
        if(g->finished) continue;

        int player=-1;

        if(fd==g->p1) player=0;
        if(fd==g->p2) player=1;

        if(player==-1) continue;

        if(player!=g->turn) return;
        if(pos<0||pos>8) return;
        if(g->board[pos]!=' ') return;

        /* Player 0 is always X and player 1 is always O. */
        char mark=player==0?'X':'O';

        g->board[pos]=mark;

        char msg[64];
        sprintf(msg,"MOVE %d %c",pos,mark);

        ws_send(g->p1,msg);
        ws_send(g->p2,msg);

        if(winner(g->board)){
            g->finished=1;

            if(player==0){
                ws_send(g->p1,"WIN");
                ws_send(g->p2,"LOSE");
            }else{
                ws_send(g->p2,"WIN");
                ws_send(g->p1,"LOSE");
            }

            return;
        }

        if(draw(g->board)){
            g->finished=1;
            ws_send(g->p1,"DRAW");
            ws_send(g->p2,"DRAW");
            return;
        }

        g->turn^=1;

        if(g->turn==0){
            ws_send(g->p1,"YOUR_TURN");
            ws_send(g->p2,"OPPONENT_TURN");
        }else{
            ws_send(g->p2,"YOUR_TURN");
            ws_send(g->p1,"OPPONENT_TURN");
        }
    }
}

/* -------- RECEIVE FRAME -------- */

int ws_receive(int fd,char *out){

    unsigned char buf[BUF_SIZE];

    int n=recv(fd,buf,sizeof(buf),0);
    if(n<=0) return -1;
    if(n<2) return -1;

    int opcode=buf[0]&0x0F;
    if(opcode==0x8) return -1;
    if(opcode!=0x1) return -1;

    /* This server only accepts short masked text frames from browsers. */
    int masked=buf[1]&0x80;
    int len=buf[1]&127;
    int index=2;

    if(!masked) return -1;
    if(len>=126) return -1;
    if(n<index+4+len) return -1;
    if(len>=255) return -1;

    unsigned char mask[4];
    memcpy(mask,buf+index,4);
    index+=4;

    for(int i=0;i<len;i++)
        out[i]=buf[index+i]^mask[i%4];

    out[len]=0;

    return len;
}

/* -------- MAIN -------- */

int main(){

    int server_fd;
    struct sockaddr_in addr;
    int opt=1;

    fd_set readfds;

    signal(SIGPIPE,SIG_IGN);

    server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        perror("socket");
        return 1;
    }

    setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    addr.sin_addr.s_addr=INADDR_ANY;

    if(bind(server_fd,(struct sockaddr*)&addr,sizeof(addr))<0){
        perror("bind");
        return 1;
    }
    if(listen(server_fd,10)<0){
        perror("listen");
        return 1;
    }

    printf("Server running on port %d\n",PORT);

    while(1){

        FD_ZERO(&readfds);
        FD_SET(server_fd,&readfds);

        int maxfd=server_fd;

        for(int i=0;i<client_count;i++){
            FD_SET(clients[i],&readfds);
            if(clients[i]>maxfd)
                maxfd=clients[i];
        }

        select(maxfd+1,&readfds,NULL,NULL,NULL);

        if(FD_ISSET(server_fd,&readfds)){

            int client_fd=accept(server_fd,NULL,NULL);
            if(client_fd<0) continue;
            printf("[debug] accepted tcp client fd=%d\n",client_fd);

            char buffer[BUF_SIZE];
            int total=0;
            int n=0;

            while(total<BUF_SIZE-1){
                n=recv(client_fd,buffer+total,sizeof(buffer)-1-total,0);
                if(n<=0) break;
                total+=n;
                buffer[total]=0;
                /* Stop reading once we reach the blank line that ends the HTTP headers. */
                if(strstr(buffer,"\r\n\r\n")) break;
            }

            if(total<=0 || websocket_handshake(client_fd,buffer)<0){
                close(client_fd);
                continue;
            }

            if(client_count>=MAX_CLIENTS){
                close(client_fd);
                continue;
            }
            clients[client_count++]=client_fd;
            printf("[debug] websocket handshake complete for fd=%d, total clients=%d\n",client_fd,client_count);

            pair_players();
        }

        for(int i=0;i<client_count;i++){

            int fd=clients[i];

            if(FD_ISSET(fd,&readfds)){

                char msg[256];

                int r=ws_receive(fd,msg);

                if(r<=0){
                    disconnect_from_games(fd);
                    printf("[debug] removing client fd=%d from client list\n",fd);
                    close(fd);
                    clients[i]=clients[--client_count];
                    pair_players();
                    i--;
                    continue;
                }

                if(strncmp(msg,"MOVE",4)==0)
                    handle_move(fd,atoi(msg+5));

                if(strncmp(msg,"NEW_GAME",8)==0){
                    for(int j=0;j<game_count;j++){
                        if(games[j].active && (fd==games[j].p1||fd==games[j].p2))
                            reset_game(&games[j]);
                    }
                }
            }
        }
    }
}
