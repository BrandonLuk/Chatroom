#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <pthread.h>
#include "terminal.h"
#include "notices.h"

#define PORT "54060"
#define BACKLOG 10
#define MAXDATASIZE 512
#define MAXCONNECTIONS 10
#define MAX_USERNAME_LENGTH 20

#define SERVER_TERMINAL_COLOR terminal_colors[1] // Color of the server's name when sending messages

struct user
{
    int sockfd;
    char username[MAX_USERNAME_LENGTH];
    int text_color;
};

struct thread_info
{
    int client_fd;
    int wakeup_pipe_fd;
    fd_set *master;
};

char terminal_buf[MAXDATASIZE];
int terminal_buf_len;

static int pipefd[2]; // This pipe will be used as a self-pipe to wake up select()

struct user *userlist[MAXCONNECTIONS];
int num_users;

pthread_mutex_t userlist_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t self_terminal_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t master_fd_set_mutex = PTHREAD_MUTEX_INITIALIZER;

// Convert all characters in string to lower-case for normalization
void strToLower(char *buf)
{
    int len = strlen(buf);
    for(int i = 0; i < len; ++i)
    {
        buf[i] = tolower(buf[i]);
    }
}

void initialize_userlist()
{
    for(int i = 0; i < MAXCONNECTIONS; ++i)
    {
        userlist[i] = 0;
    }
}

void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
    Thread synchronized.
    Output msg to server terminal.
*/
void send_msg_to_self(char *msg, int nbytes)
{
    pthread_mutex_lock(&self_terminal_mutex);
    write_chat(msg, nbytes);
    pthread_mutex_unlock(&self_terminal_mutex);
}

/*
    Thread synchronized.
    Send msg to all clients.
*/
void send_msg_to_clients(char* msg, int nbytes)
{
    pthread_mutex_lock(&userlist_mutex);
    for(int i = 0; i < MAXCONNECTIONS; ++i)
    {
        if(userlist[i] != 0)
        {
            send(userlist[i]->sockfd, msg, nbytes, 0);
        }
    }
    pthread_mutex_unlock(&userlist_mutex);
}

int open_server_socket()
{
    int sockfd, rv, yes = 1;
    struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
        {
            perror("server: socket");
            continue;
        }

        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
        {
            perror("setsockopt");
            exit(1);
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if(p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if(listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    return sockfd;
}

int find_index_of_user_in_userlist_from_fd(int fd)
{
    int i = 0;
    struct user *cur = userlist[i];

    while(cur == 0 || cur->sockfd != fd)
    {
        cur = userlist[++i];

        // Should never be true, hopefully
        if(i > sizeof(userlist))
        {
            return -1;
        }
    }

    return i;
}

int find_empty_userlist_index()
{
    for(int i = 0; i < MAXCONNECTIONS; ++i)
    {
        if(userlist[i] == 0)
        {
            return i;
        }
    }
    return -1;
}

// Check if a message is valid before sending
// A message is valid if it does not contain only spaces or is not of length 0
int is_valid_message(char *msg)
{
    char *cur = msg;
    
    while(*cur != '\0')
    {
        if(*cur != ' ')
        {
            return 1;
        }
        cur++;
    }
    return 0;
}

// Check to see if a clients color request is valid, and if so return the index of terminal_colors[] that corresponds to the choice
int parse_client_color_selection(char* buf)
{
    strToLower(buf);

    if(strcmp("green", buf) == 0)
    {
        return 2;
    }
    else if(strcmp("yellow", buf) == 0)
    {
        return 3;
    }
    else if(strcmp("blue", buf) == 0)
    {
        return 4;
    }
    else if(strcmp("magenta", buf) == 0)
    {
        return 5;
    }
    else if(strcmp("cyan", buf) == 0)
    {
        return 6;
    }
    else if(strcmp("white", buf) == 0)
    {
        return 7;
    }
    else
    {
        return -1;
    }
}

// While adding a client, check to see if there is enough space for them (i.e. num_users < MAXCONNECTIONS)
int add_client_check_if_space(int clientfd)
{
    char confirmation_buf[2];
    if(num_users >= MAXCONNECTIONS)
    {
        send(clientfd, server_is_full_notice, server_is_full_notice_nbytes, 0);
        return 1;
    }
    else
    {
        send(clientfd, "0", 2, 0);
    }

    // Get confirmation from the client
    if(recv(clientfd, confirmation_buf, 2, 0) == 0)
    {
        return 1;
    }
    return 0;
}

// While adding a client, query them for their desired username
int add_client_query_username(struct user *u, char *buf, int clientfd)
{
    send(clientfd, name_request_msg, name_request_msg_len, 0);
    if(recv(clientfd, buf, 256, 0) == 0)
    {
        return 1;
    }
    strcpy(u->username, buf);

    return 0;
}

// While adding a client, query them for their desired color, prompting them again if their input is not recognized
int add_client_query_color(struct user *u, char *buf, int clientfd)
{
    int client_color_response;
    char confirmation_buf[2];
    char formatted_color_request_msg[256];

    sprintf(formatted_color_request_msg, color_request_msg, buf);
    send(clientfd, formatted_color_request_msg, strlen(formatted_color_request_msg)+1, 0);
    if(recv(clientfd, buf, 256, 0) == 0)
    {
        return 1;
    }

    client_color_response = parse_client_color_selection(buf);

    if(client_color_response == -1)
    {

        while((client_color_response = parse_client_color_selection(buf)) == -1)
        {
            send(clientfd, "0", 2, 0);
            send(clientfd, retry_color_dialog, strlen(retry_color_dialog)+1, 0);
            if(recv(clientfd, buf, 256, 0) == 0)
            {
                return 1;
            }

            client_color_response = parse_client_color_selection(buf);
        }
    }
    send(clientfd, "1", 2, 0);
    u->text_color = client_color_response;

    if(recv(clientfd, confirmation_buf, 2, 0) == 0)
    {
        return 1;
    }

    return 0;
}

// Add a client to the server, querying them for their username and desired color
void *add_client(void *thread_info_ptr)
{
    struct user *user = malloc(sizeof(struct user));
    struct thread_info *thread_info = (struct thread_info*)thread_info_ptr;

    int joining_client_fd = thread_info->client_fd;
    int wakeup_pipe_fd = thread_info->wakeup_pipe_fd;
    fd_set *master = thread_info->master;

    int joining_nbytes;
    char input_buf[MAXDATASIZE];

    int empty_userlist_index;

    user->text_color = -1;

    // Check if there is space for the new client
    if( add_client_check_if_space(joining_client_fd)                    != 0 ||
        add_client_query_username(user, input_buf, joining_client_fd)   != 0 ||
        add_client_query_color(user, input_buf, joining_client_fd)      != 0)
    {
        goto FAILURE;
    }

    // Joining confirmation
    send(joining_client_fd, server_join_msg, server_join_msg_len, 0);

    user->sockfd = joining_client_fd;

    empty_userlist_index = find_empty_userlist_index();

    free(thread_info);

    pthread_mutex_lock(&userlist_mutex);
    num_users++;
    userlist[empty_userlist_index] = user;
    FD_SET(joining_client_fd, master);
    write(wakeup_pipe_fd, "0", 1); // Write arbitrary byte to write end of the pipe to wakeup select() in main thread
    pthread_mutex_unlock(&userlist_mutex);

    joining_nbytes = sprintf(input_buf, user_join_notice, terminal_colors[user->text_color], user->username, terminal_colors[0]);

    send_msg_to_self(input_buf, joining_nbytes);
    send_msg_to_clients(input_buf, joining_nbytes);

    goto SUCCESS;

FAILURE:
    free(user);

SUCCESS:
    pthread_exit(NULL);
}

// A client has disconnected, so remove them from the server
void remove_client(int clientfd, fd_set *master)
{
    char buf[256];
    int i = find_index_of_user_in_userlist_from_fd(clientfd);
    int nbytes;
    struct user *u = userlist[i];

    nbytes = sprintf(buf, user_leave_notice, terminal_colors[u->text_color], u->username, terminal_colors[0]);

    // Remove user from the userlist
    pthread_mutex_lock(&userlist_mutex);
    free(userlist[i]);
    userlist[i] = 0;
    pthread_mutex_unlock(&userlist_mutex);
    num_users--;

    // Remove user from master fd_set
    pthread_mutex_lock(&master_fd_set_mutex);
    FD_CLR(clientfd, master);
    pthread_mutex_unlock(&master_fd_set_mutex);

    close(clientfd);

    send_msg_to_self(buf, nbytes);
    send_msg_to_clients(buf, nbytes);
}

// Prepare a message from the server by prefixing it with server designation and color
char *prep_server_msg(char* buf)
{
    char *msg = (char*)malloc(MAXDATASIZE);
    sprintf(msg, "%sSERVER:%s %s", terminal_colors[1], terminal_colors[0], buf);
    return msg;
}

// Prepare a message from a client by prefixing it with that client's username and color
char *prep_client_msg(int clientfd, char *buf)
{
    char *msg = malloc(MAXDATASIZE);
    struct user *client = userlist[find_index_of_user_in_userlist_from_fd(clientfd)];

    sprintf(msg, "%s%s%s: %s", terminal_colors[client->text_color], client->username, terminal_colors[0], buf);

    write_chat(client->username, strlen(client->username)+1);

    return msg;
}

// Send out a message originating from the server
void send_server_to_clients_msg(char *buf)
{
    char *msg = prep_server_msg(buf);
    int msg_nbytes = strlen(msg);

    write_chat(msg, msg_nbytes);
    send_msg_to_clients(msg, msg_nbytes);
    free(msg);
}

// Send out a message originating from a client
void send_client_to_clients_msg(int clientfd, char *buf)
{
    char *msg = prep_client_msg(clientfd, buf);

    write_chat(msg, strlen(msg)+1);
    send_msg_to_clients(msg, strlen(msg)+1);
    free(msg);
}

int main()
{
    fd_set master;
    fd_set read_fds;

    int fdmax;

    int sockfd;
    int newfd;
    struct sockaddr_storage remoteaddr;
    socklen_t addrlen;

    terminal_buf_len = 0;
    terminal_buf[terminal_buf_len] = '\0';

    char buf[256];
    int nbytes;

    num_users = 0;

    char remoteIP[INET6_ADDRSTRLEN];

    int i;
    char c;

    sockfd = open_server_socket();

    if(pipe2(pipefd, O_NONBLOCK) == -1)
    {
        perror("Error: pipe2");
        exit(EXIT_FAILURE);
    }
    
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(STDIN_FILENO, &master);
    FD_SET(sockfd, &master);
    FD_SET(pipefd[0], &master);

    fdmax = pipefd[1];

    printf("%sStarting server...%s\n", terminal_colors[1], terminal_colors[0]);
    init_chat();
    initialize_userlist();
    
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
                // New client connection
                if(i == sockfd) 
                {
                    addrlen = sizeof(remoteaddr);
                    newfd = accept(sockfd, (struct sockaddr*)&remoteaddr, &addrlen);

                    if(newfd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {
                        if(newfd > fdmax)
                        {
                            fdmax = newfd;
                        }
                        nbytes = sprintf(buf, "New connection from %s\n", inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN));
                        write_chat(buf, nbytes);

                        struct thread_info *ti = malloc(sizeof *ti);
                        ti->client_fd = newfd;
                        ti->master = &master;
                        ti->wakeup_pipe_fd = pipefd[1];

                        pthread_t thread;
                        pthread_create(&thread, NULL, add_client, (void*)ti);
                    }
                }
                // Another thread has finished logging a new user in and has added their socket_fd to the fdset, so reset select()
                else if(i == pipefd[0])
                {
                    for(;;)
                    {
                        if(read(pipefd[0], &c, 1) == -1)
                        {
                            if(errno == EAGAIN)
                            {
                                break;
                            }
                        }
                    }
                }
                // Server user is typing
                else if(i == STDIN_FILENO)
                {
                    c = read_char();

                    switch (c)
                    {
                        case 3: // Ctrl-c
                            exit(0);

                        case 10: // LF
                            if(is_valid_message(terminal_buf))
                            {
                                //clear_input_line();
                                terminal_buf[terminal_buf_len++] = 10; // LF
                                terminal_buf[terminal_buf_len] = '\0';
                                send_server_to_clients_msg(terminal_buf);
                                terminal_buf_len = 0;
                                terminal_buf[terminal_buf_len] = '\0';
                            }
                            break;

                        case 127: // Backspace
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
                // Data from a client
                else
                {
                    if((nbytes = recv(i, buf, MAXDATASIZE, 0)) <= 0)
                    {
                        if(nbytes == 0)
                        {
                            remove_client(i, &master);
                        }
                        else
                        {
                            perror("recv");
                        }
                    }
                    else
                    {
                        send_client_to_clients_msg(i, buf);
                    }
                }
            }
        }
    }
    
    return 0;
}