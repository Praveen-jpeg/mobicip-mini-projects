/*
 * Tic Tac Toe WebSocket Server
 * TURN BASED VERSION (FINAL FIX)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define PORT 9000
#define MAX_CLIENTS 100

typedef struct {
    int p1, p2;
    char board[9];
    int turn; // 0 = p1 , 1 = p2
} Game;

int clients[MAX_CLIENTS];
Game games[MAX_CLIENTS/2];

int client_count=0;
int game_count=0;

/* ================= SEND FRAME ================= */

void ws_send(int fd,const char *msg)
{
    unsigned char frame[1024];
    int len=strlen(msg);

    frame[0]=0x81;
    frame[1]=len;

    memcpy(frame+2,msg,len);
    send(fd,frame,len+2,0);
}

/* ================= HANDSHAKE ================= */

void websocket_handshake(int fd,char *req)
{
    char *key=strstr(req,"Sec-WebSocket-Key:");
    key+=19;
    while(*key==' ') key++;

    char client_key[128];
    sscanf(key,"%s",client_key);

    char combined[256];
    sprintf(combined,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
            client_key);

    unsigned char sha1[20];
    SHA1((unsigned char*)combined,strlen(combined),sha1);

    char accept[256];
    EVP_EncodeBlock((unsigned char*)accept,sha1,20);

    char response[512];
    sprintf(response,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept);

    send(fd,response,strlen(response),0);
}

/* ================= GAME CHECK ================= */

int winner(char *b)
{
    int w[8][3]={
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };

    for(int i=0;i<8;i++)
        if(b[w[i][0]]!=' ' &&
           b[w[i][0]]==b[w[i][1]] &&
           b[w[i][1]]==b[w[i][2]])
            return 1;
    return 0;
}

int draw(char *b)
{
    for(int i=0;i<9;i++)
        if(b[i]==' ') return 0;
    return 1;
}

/* ================= PAIR ================= */

void pair_players()
{
    if(client_count%2!=0) return;

    Game *g=&games[game_count++];

    g->p1=clients[client_count-2];
    g->p2=clients[client_count-1];

    for(int i=0;i<9;i++) g->board[i]=' ';
    g->turn=0;

    ws_send(g->p1,"START X");
    ws_send(g->p2,"START O");

    ws_send(g->p1,"YOUR_TURN");
    ws_send(g->p2,"OPPONENT_TURN");

    printf("Game paired\n");
}

/* ================= HANDLE MOVE ================= */

void handle_move(int fd,int pos)
{
    for(int i=0;i<game_count;i++)
    {
        Game *g=&games[i];

        int player=-1;
        if(fd==g->p1) player=0;
        if(fd==g->p2) player=1;
        if(player==-1) continue;

        if(player!=g->turn) return;
        if(pos<0||pos>8) return;
        if(g->board[pos]!=' ') return;

        char mark = player==0?'X':'O';
        g->board[pos]=mark;

        char msg[64];
        sprintf(msg,"MOVE %d %c",pos,mark);

        ws_send(g->p1,msg);
        ws_send(g->p2,msg);

        if(winner(g->board)){
            ws_send(g->p1,"WIN");
            ws_send(g->p2,"LOSE");
            return;
        }

        if(draw(g->board)){
            ws_send(g->p1,"DRAW");
            ws_send(g->p2,"DRAW");
            return;
        }

        /* SWITCH TURN */
        g->turn ^=1;

        if(g->turn==0){
            ws_send(g->p1,"YOUR_TURN");
            ws_send(g->p2,"OPPONENT_TURN");
        }else{
            ws_send(g->p2,"YOUR_TURN");
            ws_send(g->p1,"OPPONENT_TURN");
        }
    }
}

/* ================= RECEIVE ================= */

int ws_receive(int fd,char *out)
{
    unsigned char buf[2048];
    int n=recv(fd,buf,sizeof(buf),0);
    if(n<=0) return -1;

    int len=buf[1]&127;
    int index=2;

    unsigned char mask[4];
    memcpy(mask,buf+index,4);
    index+=4;

    for(int i=0;i<len;i++)
        out[i]=buf[index+i]^mask[i%4];

    out[len]=0;
    return len;
}

/* ================= MAIN ================= */

int main()
{
    int server_fd,client_fd;
    struct sockaddr_in addr;

    server_fd=socket(AF_INET,SOCK_STREAM,0);

    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    addr.sin_addr.s_addr=INADDR_ANY;

    bind(server_fd,(struct sockaddr*)&addr,sizeof(addr));
    listen(server_fd,10);

    printf("Server running on %d\n",PORT);

    while(1)
    {
        client_fd=accept(server_fd,NULL,NULL);

        char buffer[4096];
        int n=recv(client_fd,buffer,sizeof(buffer)-1,0);
        buffer[n]=0;

        websocket_handshake(client_fd,buffer);

        clients[client_count++]=client_fd;
        pair_players();

        if(fork()==0)
        {
            char msg[256];

            while(1)
            {
                int r=ws_receive(client_fd,msg);
                if(r<=0) break;

                if(strncmp(msg,"MOVE",4)==0)
                    handle_move(client_fd,atoi(msg+5));
            }

            close(client_fd);
            exit(0);
        }
    }
}