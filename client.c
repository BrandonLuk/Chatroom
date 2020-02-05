#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "terminal.h"

#define PORT "54060"
#define MAXDATASIZE 512

char terminal_buf[MAXDATASIZE];
int terminal_buf_len;

void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_msg(int sockfd, char *buf, int buf_nbytes)
{
    send(sockfd, buf, buf_nbytes, 0);
}

void login_to_server(int sockfd, char* buf)
{
    int nbytes;
    char confirmation_buf[2];

    // See if the server has enough room
    nbytes = recv(sockfd, buf, 256, 0);
    if(buf[0] != '0')
    {
        write(STDOUT_FILENO, buf, nbytes);
        exit(0);
    }

    // Send confirmation message to server
    send(sockfd, "1", 2, 0);

    // Answer server's query for username
    nbytes = recv(sockfd, buf, 256, 0);
    write(STDOUT_FILENO, buf, nbytes);
    gets(buf);
    send(sockfd, buf, strlen(buf)+1, 0);

    // Answer server's query for color
    do{
        nbytes = recv(sockfd, buf, 256, 0);
        write(STDOUT_FILENO, buf, nbytes);
        gets(buf);
        send(sockfd, buf, strlen(buf)+1, 0);
        recv(sockfd, confirmation_buf, 2, 0);
    }
    while(confirmation_buf[0] == '0');

    // Send confirmation message to server
    send(sockfd, "1", 2, 0);

    // Recieve server's joining confirmation message
    nbytes = recv(sockfd, buf, 256, 0);
    write(STDOUT_FILENO, buf, nbytes);

    write(STDOUT_FILENO, "\n", 1);
}

int main(int argc, char* argv[])
{
    fd_set master;
    fd_set read_fds;
    int fdmax;

    int sockfd;

    char buf[MAXDATASIZE];
    int nbytes;

    int i, rv;
    char c;

    struct addrinfo hints, *servinfo, *p;

    terminal_buf_len = 0;

    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    
    char s[INET6_ADDRSTRLEN];

    if(argc != 2)
    {
        fprintf(stderr, "usage: client hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("client: socket");
            continue;
        }
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            perror("client: connect");
            close(sockfd);
            continue;
        }
        break;
    }

    if(p == NULL)
    {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr*)p->ai_addr), s, sizeof(s));
    printf("client: connecting to %s\n", s);

    FD_SET(STDIN_FILENO, &master);
    FD_SET(sockfd, &master);

    fdmax = sockfd;

    login_to_server(sockfd, buf);

    init_chat();

    while(1)
    {
        read_fds = master;
        if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(4);
        }

        for(i = 0; i <= fdmax; ++i)
        {
            if(FD_ISSET(i, &read_fds))
            {
                // Incoming data from server
                if(i == sockfd)
                {
                    if((nbytes = recv(sockfd, buf, MAXDATASIZE, 0)) <= 0)
                    {
                        printf("Lost connection to server");
                        exit(0);
                    }
                    write_chat(buf, nbytes);
                }
                
                // User is typing
                else if(i == STDIN_FILENO)
                {
                    c = read_char();

                    switch (c)
                    {
                        case 3: // Ctrl-c
                            exit(0);

                        case 10: // LF
                            clear_input_line();
                            terminal_buf[terminal_buf_len++] = 10; // LF
                            terminal_buf[terminal_buf_len] = '\0';
                            send_msg(sockfd, terminal_buf, terminal_buf_len+1);
                            terminal_buf_len = 0;
                            break;

                        case 127: //
                            if(terminal_buf_len > 0)
                            {
                                terminal_buf[--terminal_buf_len] = '\0';
                                write_backspace_to_input_line();
                            }
                            break;
                    
                        default:
                            if(terminal_buf_len < 254)
                            {
                                terminal_buf[terminal_buf_len++] = c;
                            }
                            write_char_to_input_line(c);
                            break;
                    }
                }
            }
        }
    }

    freeaddrinfo(servinfo);
    close(sockfd);
    return 0;
}