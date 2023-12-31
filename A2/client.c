/*
** client.c -- a stream socket client demo
*/
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>  
#include <signal.h>

#define MAXDATASIZE 4096 // max number of bytes we can get at once
#define TOTAL_LINES 1000
#define SERVER_RATE 100 // lines per second
#define SELFPORT "9801"

char *lines[TOTAL_LINES] = {0};
int* peer_send_socks; 
char** peer_ips;
char** peer_ports; 
int lines_received = 0;
int byes_received = 0;
int* bye_from_peer;
pthread_t* run_server_thread;
pthread_mutex_t lock_lines, lock_peer_conn, vayu_disconnect_lock, bye_lock;
int server_sockfd;
int* peer_connected;
long long vayu_disconnect_time = 0;
int clientnum;


struct peer_thread_args {
    int peer_number;
    char* peer_port;
    char* peer_ip;
};

struct peer_conn_args {
    int peer_number;
    int peer_sockfd;
};

struct server_thread_args {
    char* server_ip;//may be hostname
    char* server_port;
    int clientnum;
};


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int connect_to_server(char* server_ip, char* server_port, int* sock, int retries) {
    int time = 5;
    while(1) {
        int sockfd, numbytes;
        struct addrinfo hints, *servinfo, *p;
        int rv;
        char s[INET6_ADDRSTRLEN];
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if ((rv = getaddrinfo(server_ip, server_port, &hints, &servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
            exit(1);
        }
        // loop through all the results and connect to the first we can
        for (p = servinfo; p != NULL; p = p->ai_next) { 
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
                perror("client: socket");
                continue;
            }
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) { 
                close(sockfd);
                perror("client: connect");
                continue;
            }
            break;
        }
        if (p == NULL) { 
            fprintf(stderr, "client: failed to connect\n");
            retries--;
            freeaddrinfo(servinfo); // all done with this structure
            if (retries < 0)
                break;
            sleep(time);
            continue;
        }
        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s,
                sizeof s);
        freeaddrinfo(servinfo); // all done with this structure
        *sock = sockfd;
        return 1;
    }
    return -1;
}

// could not connect to vayu within timeout
void handle_connect_to_server_fail(int clientnum) {
    fprintf(stderr, "client: failed to connect to server\n");
    pthread_mutex_lock(&vayu_disconnect_lock);
        vayu_disconnect_time = time(NULL)-30;
    pthread_mutex_unlock(&vayu_disconnect_lock); 
        pthread_mutex_lock(&lock_peer_conn);
        for (int i =0; i<clientnum; i++) {
           if (send(peer_send_socks[i], "FAIL\n\n", 6, 0) == -1)
                perror("send fail");
        }
        pthread_mutex_unlock(&lock_peer_conn);
}

void* get_lines_from_server(void* arg) {
    struct server_thread_args* args = (struct server_thread_args*)arg;
    char* server_ip = args->server_ip;
    char* server_port = args->server_port;
    int clientnum = args->clientnum;
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    if (connect_to_server(server_ip, server_port, &sockfd,5) == -1) {
        handle_connect_to_server_fail(clientnum);
        return NULL;
    }
    // connected to vayu
    while(1) {
        pthread_mutex_lock(&lock_lines);
        if (lines_received == TOTAL_LINES) {
            pthread_mutex_unlock(&lock_lines);
            break;
        }
        pthread_mutex_unlock(&lock_lines);

        pthread_mutex_lock(&bye_lock);
        if (byes_received > 0) {
            int bye_client_index= 0;
            for (int i = 0; i < clientnum; i++) {
                if (bye_from_peer[i] == 1) {
                    bye_client_index = i;
                    break;
                }
            }
            pthread_mutex_unlock(&bye_lock);
            pthread_mutex_lock(&lock_lines);
            for (int i = 0; i< TOTAL_LINES; i++) {
                if (lines[i]==NULL) {
                    char send_message[100];
                    sprintf(send_message, "SEND\n%d\n", i);
                    pthread_mutex_lock(&lock_peer_conn);
                    if (send(peer_send_socks[bye_client_index], send_message, strlen(send_message), 0) == -1) {
                        perror("send");
                        break;
                    }
                    //printf("client: sent SEND %d\n", i);
                    char* line_buffer = NULL;
                    size_t line_buffer_size = 0;
                    size_t data_received = 0;
                    while(1) {
                        ssize_t bytes_received = recv(peer_send_socks[bye_client_index], buf, MAXDATASIZE - 1, 0);
                        if (bytes_received == -1) {
                            perror("recv");
                            break;
                        }
                        if (bytes_received == 0) {
                            break;
                        }
                        if (data_received + bytes_received >= line_buffer_size) {
                            line_buffer_size = data_received + MAXDATASIZE;
                            line_buffer = realloc(line_buffer, line_buffer_size);
                            if (line_buffer == NULL) {
                                perror("realloc");
                                exit(1);
                            }
                        }
                        memcpy(line_buffer + data_received, buf, bytes_received);
                        data_received += bytes_received;
                        if (line_buffer[data_received - 1] == '\n') {
                            line_buffer[data_received] = '\0';
                            break;
                        }
                    }
                    pthread_mutex_unlock(&lock_peer_conn);
                        printf("client: received new line %d form peer\n", i);
                        //second line is the line itself. extract line
                        //store line in lines array
                        lines[i] = line_buffer;
                        lines_received++;
                        //send line to all clients

                }
            }
            pthread_mutex_unlock(&lock_lines);

        }
        else {
            pthread_mutex_unlock(&bye_lock);
        }
        //send message "SENDLINE\n" to server
        if (send(sockfd, "SENDLINE\n", 9, 0) == -1) {
            if (connect_to_server(server_ip, server_port, &sockfd,5) == -1) {
                handle_connect_to_server_fail(clientnum);
                return NULL;
            }
            continue;
        }
        //receive line from server
        char* line_buffer = NULL;
        size_t line_buffer_size = 0;
        size_t data_received = 0;
        while(1) {
            ssize_t bytes_received = recv(sockfd, buf, MAXDATASIZE - 1, 0);
            if (bytes_received == -1) {
                perror("recv");
                if (connect_to_server(server_ip, server_port, &sockfd,5) == -1) {
                    handle_connect_to_server_fail(clientnum);
                    return NULL;
                }
                continue;
            }
            if (bytes_received == 0) {
                if (connect_to_server(server_ip, server_port, &sockfd,5) == -1) {
                    handle_connect_to_server_fail(clientnum);
                    return NULL;
                }
                continue;
            }
            if (data_received + bytes_received >= line_buffer_size) {
                line_buffer_size = data_received + MAXDATASIZE;
                line_buffer = realloc(line_buffer, line_buffer_size);
                if (line_buffer == NULL) {
                    perror("realloc");
                    exit(1);
                }
            }
            memcpy(line_buffer + data_received, buf, bytes_received);
            data_received += bytes_received;
            if (line_buffer[data_received - 1] == '\n') {
                line_buffer[data_received] = '\0';
                break;
            }
        }
        //two lines are received. first denoting line number. extract line number
        char line_number_str[10];
        for (int i = 0; i < 10; i++) {
            if (line_buffer[i] == '\n') {
                line_number_str[i] = '\0';
                break;
            }
            if (i==9) {
                fprintf(stderr, "line number too long\n");
                exit(1);
            }
            line_number_str[i] = line_buffer[i];
        }
        int line_number = atoi(line_number_str);
        if (line_number < 0 || line_number >= TOTAL_LINES) {
            free(line_buffer);
            if (line_number == -1)
                continue;
            fprintf(stderr, "client: received invalid line number %d\n", line_number);
            exit(1);
        }
        pthread_mutex_lock(&lock_lines);
        if (lines[line_number] == NULL) {
            printf("client: received new line %d form vayu\n", line_number);
            //second line is the line itself. extract line
            //store line in lines array
            lines[line_number] = line_buffer;
            lines_received++;
            //send line to all clients
            pthread_mutex_lock(&lock_peer_conn);
            for (int i = 0; i < clientnum; i++) {
                if (send(peer_send_socks[i], line_buffer, strlen(line_buffer), 0) == -1)
                    perror("send");
                printf("client: sent line %d to peer %d\n", line_number, i);
            }
            pthread_mutex_unlock(&lock_peer_conn);
        }
        else {
            free(line_buffer);
        }
        pthread_mutex_unlock(&lock_lines);
    }
    pthread_mutex_lock(&vayu_disconnect_lock);
        vayu_disconnect_time = time(NULL);  
    pthread_mutex_unlock(&vayu_disconnect_lock);
    //send message "SUBMIT\n2021CS10559@firewall\nTOTAL_LINES\n" to server
    char submit_message[100];
    sprintf(submit_message, "SUBMIT\n2021CS10559@firewall\n%d\n", TOTAL_LINES);
    //printf("client: sending SUBMIT\n");
    if (send(sockfd, submit_message, strlen(submit_message), 0) == -1)
        perror("send");
    //printf("client: sent SUBMIT\n");
    //send each line to server preceded by its line number
    pthread_mutex_lock(&lock_lines);
    for (int i = 0; i < TOTAL_LINES; i++) {
        if (lines[i]==NULL) {
            //printf("client: line %d is NULL\n", i);
            continue;
        }
        // //printf("%s", lines[i]);
        // fflush(stdout);
        if (send(sockfd, lines[i], strlen(lines[i]), 0) == -1)
            perror("send");
        // //printf("client: sent line %d\n", i);
        // fflush(stdout);
    }
    pthread_mutex_unlock(&lock_lines);
    //printf("client: sent all lines\n");
    // fflush(stdout);
    ssize_t bytes_received = 0;
    if ((bytes_received =recv(sockfd, buf, MAXDATASIZE - 1, 0)) == -1) {
        perror("recv");
        exit(1);
    }
    buf[bytes_received] = '\0';
    printf("client: received\n%s\n", buf);
    close(sockfd);
    //send message "BYE\n\n" to peers
    pthread_mutex_lock(&lock_peer_conn);
    for (int i = 0; i < clientnum; i++) {
        if (send(peer_send_socks[i], "BYE\n\n", 5, 0) == -1)
            perror("send");
        close(peer_send_socks[i]);
        printf("client: sent BYE to client %d\n", i);
        // fflush(stdout);
    }
    pthread_mutex_unlock(&lock_peer_conn);
    // response example from server:
    /*SUBMIT SUCCESS: 2021CS10559@firewall - 10.194.12.47 - 10, 0, 10, 10834, 10, 1000 - 1693325307276, 1693330672784, 1693330672959*/
    // extract last three numbers
    const char *last_part = strrchr(buf, '-');
    if (!last_part) {
        fprintf(stderr, "Failed to find the last part of the response\n");
        exit(1);
    }

    // Tokenize the last part using ","
    char last_part_copy[strlen(last_part) -1];
    strcpy(last_part_copy, last_part+2);

    char *token;
    char *delimiters = ", ";
    token = strtok(last_part_copy, delimiters);

    // Extract the last three numbers
    unsigned long long numbers[3];
    int count = 0;
    while (token != NULL && count < 3) {
        numbers[count] = strtoull(token, NULL, 10);
        token = strtok(NULL, delimiters);
        count++;
    }

    if (count != 3) {
        fprintf(stderr, "Failed to extract three numbers\n");
        exit(1);
    }

    // //printf("Extracted numbers: %llu, %llu, %llu\n", numbers[0], numbers[1], numbers[2]);
    // printf("time taken: %d\n", (int)(numbers[2] - numbers[1]));
    
    return NULL;
}



// send new received lines from server in server function to clients or receive lines from clients in client function
void* run_server(void* arg) {
    struct peer_conn_args* args = (struct peer_conn_args*)arg;
    int client_sockfd = args->peer_sockfd;
    int client_index = args->peer_number;
    free(args);
    char* unordered_lines[TOTAL_LINES]= {0};
    int unordered_lines_index[TOTAL_LINES] = {0};
    int unordered_lines_top = 0;
    char buf[MAXDATASIZE];
    int bytes_received;
    int bye_received = 0;
    while(!bye_received) {
        //receive line from client
        char* line_buffer = NULL;
        size_t line_buffer_size = 0;
        size_t data_received = 0;
        int newlines_recv = 0;
        while(1) {
            ssize_t bytes_received = recv(client_sockfd, buf, MAXDATASIZE - 1, 0);
            if (bytes_received <= 0) {
                if (bytes_received == -1)
                printf("-1 received\n");
                perror("recv");
                pthread_mutex_lock(&lock_peer_conn);
                peer_connected[client_index] = 0;
                pthread_mutex_unlock(&lock_peer_conn);
                return NULL;
            }
            if (data_received + bytes_received >= line_buffer_size) {
                line_buffer_size = data_received + MAXDATASIZE;
                line_buffer = realloc(line_buffer, line_buffer_size);
                if (line_buffer == NULL) {
                    perror("realloc");
                    pthread_mutex_lock(&lock_peer_conn);
                    peer_connected[client_index] = 0;
                    pthread_mutex_unlock(&lock_peer_conn);
                    return NULL;
                }
            }
            memcpy(line_buffer + data_received, buf, bytes_received);
            data_received += bytes_received;
            for (int i = 0; i < bytes_received; i++) {
                if (buf[i] == '\n') {
                    newlines_recv^=1;
                }
            }
            if (line_buffer[data_received - 1] == '\n' && newlines_recv == 0) {
                line_buffer[data_received] = '\0';
                break;
            }
        }

        //check if last 5 bytes are BYE\n\n
        if (data_received >= 5 && strcmp(line_buffer+data_received-5, "BYE\n\n") == 0) {
            bye_received = 1;
            printf("server: received BYE from sock%d\n", client_sockfd);
            pthread_mutex_lock(&bye_lock);
            byes_received++;
            bye_from_peer[client_index] = 1;
            pthread_mutex_unlock(&bye_lock);
            data_received -= 5;
            printf("server: received BYE from sock%d\n", client_sockfd);
        }
        //check if last 6 bytes are FAIL\n\n
        if (data_received >= 6 && strcmp(line_buffer+data_received-6, "FAIL\n\n") == 0) {
            bye_received = 1;
            data_received -= 6;
            printf("server: received FAIL from sock%d\n", client_sockfd);
        }
        //split line_buffer into pairs of line number and line after every second newline
        int start = 0;
        int newline_count = 0;
        for (int i =0 ; i < data_received; i++) {
            // //printf("server: i = %d\n", i);
            if (line_buffer[i] == '\n') {
                // //printf("server: newline\n");
                newline_count++;
            }
            if (newline_count < 2) {
                continue;
            }
            int request = 0;
            // check if peer is requesting a line
            if (strncmp(line_buffer+start, "SEND\n",5) == 0) {
                request = 1;
                start += 5;
                printf("server: received SEND from sock%d\n", client_sockfd);
            }
            //two lines are received. first denoting line number. extract line number
            char line_number_str[10];
            for (int j = 0; j < 10; j++) {
                if (line_buffer[start+j] == '\n') {
                    line_number_str[j] = '\0';
                    break;
                }
                if (j==9) {
                    fprintf(stderr, "line number too long\n");
                    exit(1);
                }
                line_number_str[j] = line_buffer[start+j];
            }
            int line_number = atoi(line_number_str);
            if (request) {
                printf("server: received request for line %d\n", line_number);
                if (line_number < 0 || line_number >= TOTAL_LINES) {
                    //printf("invalid line number %d\n", line_number);
                    free(line_buffer);
                    fprintf(stderr, "server: received invalid line number %d\n", line_number);
                    exit(1);
                }
                pthread_mutex_lock(&lock_lines);
                if (lines[line_number] == NULL) {
                    free(line_buffer);
                    fprintf(stderr, "server: received request for line %d which is NULL\n", line_number);
                    return NULL;
                }
                //send line to client
                if (send(client_sockfd, lines[line_number], strlen(lines[line_number]), 0) == -1)
                    perror("send");
                pthread_mutex_unlock(&lock_lines);
                printf("server: sent line %d to sock%d\n", line_number, client_sockfd);
                start += strlen(line_number_str)+1;
                i = start-1;
                newline_count = 0;
                continue;
            }
            if (line_number < 0 || line_number >= TOTAL_LINES) {
                free(line_buffer);
                fprintf(stderr, "server: received invalid line number %d\n", line_number);
                return NULL;
            }
            //store line number and line in unordered_lines
            unordered_lines_index[unordered_lines_top] = line_number;
            unordered_lines[unordered_lines_top] = malloc(sizeof(char)*(i-start+2));
            memcpy(unordered_lines[unordered_lines_top], line_buffer+start, i-start+1);
            unordered_lines[unordered_lines_top][i-start+1] = '\0';
            start = i+1;
            newline_count = 0;
            unordered_lines_top++;
        }
        free(line_buffer);
        pthread_mutex_lock(&lock_lines);
        for (int i = 0; i < unordered_lines_top; i++) {
            if (lines[unordered_lines_index[i]] == NULL) {
                printf("server: received new line %d from client with sock%d\n", unordered_lines_index[i], client_sockfd);
                lines[unordered_lines_index[i]] = unordered_lines[i];
                lines_received++;
            }
            else {
                printf("server: received duplicate line %d from client with sock%d\n", unordered_lines_index[i], client_sockfd);
                free(unordered_lines[i]);
            }
        }
        pthread_mutex_unlock(&lock_lines);
        unordered_lines_top = 0;
    }
    return NULL;
}

void* connect_to_peer(void* arg) {
    while(1) {
        struct peer_thread_args* args = (struct peer_thread_args*)arg;
        char* peer_ip = args->peer_ip;
        int peer_number = args->peer_number;
        char* peer_port = args->peer_port;
        int sockfd;
        struct addrinfo hints, *servinfo, *p;
        int rv;
        char s[INET6_ADDRSTRLEN];
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if ((rv = getaddrinfo(peer_ip, peer_port, &hints, &servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
            exit(1);
        }
        // loop through all the results and connect to the first we can
        for (p = servinfo; p != NULL; p = p->ai_next) { 
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
                perror("client: socket");
                continue;
            }
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) { 
                close(sockfd);
                perror("client: connect");
                continue;
            }
            break;
        }
        if (p == NULL) { 
            fprintf(stderr, "client: failed to connect\n");
            continue;
        }
        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s,
                sizeof s);
        // //printf("client: connecting to %s\n", s);
        freeaddrinfo(servinfo); // all done with this structure
        pthread_mutex_lock(&lock_peer_conn);
            peer_send_socks[peer_number] = sockfd;
        pthread_mutex_unlock(&lock_peer_conn);
        return NULL;
    }
}

void* accept_connections(void* arg) {
    int* sockfd = (int*)arg;
    
    while(1) {
        // keep accepting new connections after checking ip
        // use select to check if new connection is available and timeout of 5 seconds
        // if new connection is available, accept it and check ip
        // if ip is not in peer_ips, close connection and continue
        // if ip is in peer_ips, store socket in peer_send_socks and continue
        // if timeout, continue
        pthread_mutex_lock(&vayu_disconnect_lock);
        if ((vayu_disconnect_time != 0 && time(NULL) - vayu_disconnect_time >= 30) || (byes_received == clientnum)) {
            pthread_mutex_unlock(&vayu_disconnect_lock);
            break;
        }
        pthread_mutex_unlock(&vayu_disconnect_lock);
        struct sockaddr_storage their_addr; // connector's address information
        socklen_t sin_size;
        sin_size = sizeof their_addr;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(*sockfd, &readfds);
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int retval = select(*sockfd+1, &readfds, NULL, NULL, &tv);
        if (retval > 0) {
            int new_fd = accept(*sockfd, (struct sockaddr *)&their_addr, &sin_size);
            if (new_fd == -1) { 
                perror("accept");
                continue;
            }
            char s[INET_ADDRSTRLEN];
            inet_ntop(their_addr.ss_family, get_in_addr(((struct sockaddr *)&their_addr)),
                s, sizeof s);
            
            printf("server: received connection from %s\n", s);
            //check if ip is in peer_ips and not connected to already
            pthread_mutex_lock(&lock_peer_conn);
            int peer_number = -1;
            for (int i = 0; i < clientnum; i++) {
                if (strcmp(peer_ips[i], s) == 0 && peer_connected[i] == 0) {
                    peer_number = i;
                    break;
                }
            }
            if (peer_number == -1) {
                printf("server: received connection from unknown client or duplicate connection %s\n", s);
                close(new_fd);
                pthread_mutex_unlock(&lock_peer_conn);
                continue;
            }   
            peer_connected[peer_number] = 1;
            int peer_send_sock;
            connect_to_server(peer_ips[peer_number], peer_ports[peer_number], &peer_send_sock, 0);
            peer_send_socks[peer_number] = peer_send_sock;
            pthread_mutex_unlock(&lock_peer_conn);
            struct peer_conn_args* args = malloc(sizeof(struct peer_conn_args));
            args->peer_sockfd = new_fd;
            args->peer_number = peer_number;
            
            pthread_create(&run_server_thread[peer_number], NULL, run_server, args);
        }
        else {
            continue;
        }
    }

}

void parse_ip_port_input(char* inp, char* ip, char* port) {
    int i = 0;
    while(inp[i] != ':') {
        if (i>=30) {
            fprintf(stderr, "ip too long\n");
            exit(1);
        }
        if (inp[i] == '\0') {
            fprintf(stderr, "invalid ip:port format\n");
            exit(1);
        }
        ip[i] = inp[i];
        i++;
    }
    ip[i] = '\0';
    printf("ip = %s\n", ip);
    i++;
    int j = 0;
    while(inp[i] != '\0') {
        if (j>=10) {
            fprintf(stderr, "port too long\n");
            exit(1);
        }
        port[j] = inp[i];
        i++;
        j++;
    }
    port[j] = '\0';
}



int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    clientnum = argc-2;
    //craete a socket file descriptor for listening to incoming connections
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int yes = 1;
    int sockfd;
    //argv format <ip_address>:<port>
    struct peer_thread_args* peer_args = malloc(sizeof(struct peer_thread_args)*clientnum);
    peer_send_socks = malloc(sizeof(int)*clientnum);
    peer_connected = malloc(sizeof(int)*clientnum);
    bye_from_peer = malloc(sizeof(int)*clientnum);
    for (int i = 0; i < clientnum; i++) {
        peer_connected[i] = 0;
        bye_from_peer[i] = 0;
    }
    struct server_thread_args* server_args = malloc(sizeof(struct server_thread_args));
    //argv[1] is server ip
    char* server_ip = malloc(sizeof(char)*30);
    char* server_port = malloc(sizeof(char)*10);
    parse_ip_port_input(argv[1], server_ip, server_port);
    server_args->server_ip = server_ip;
    server_args->server_port = server_port;
    server_args->clientnum = clientnum;
    // //printf("client: server ip = %s\n", server_ip);
    // //printf("client: server port = %s\n", server_port);
    peer_ips = malloc(sizeof(char*)*clientnum);
    peer_ports = malloc(sizeof(char*)*clientnum);
    for (int i = 2; i < argc; i++) {
        char* peer_ip = malloc(sizeof(char)*30);
        char* peer_port = malloc(sizeof(char)*10);
        parse_ip_port_input(argv[i], peer_ip, peer_port);
        // //printf("client: peer ip = %s\n", peer_ip);
        // //printf("client: peer port = %s\n", peer_port);
        peer_ips[i-2] = peer_ip;
        peer_args[i-2].peer_ip = peer_ip;
        peer_args[i-2].peer_number = i-2;
        peer_args[i-2].peer_port = peer_port;
        peer_ports[i-2] = peer_port;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE; // fill in my IP for me
    if ((rv = getaddrinfo(NULL, SELFPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
        exit(1);
    }
    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) { 
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) { 
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) { 
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo); // all done with this structure
    if (p == NULL) { 
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }
    if (listen(sockfd, 2*clientnum) == -1) { 
        perror("listen");
        exit(1);
    }
    // connect to peers

    pthread_mutex_init(&lock_lines, NULL);
    pthread_mutex_init(&lock_peer_conn, NULL);

    pthread_t* connect_to_peer_thread = malloc(sizeof(pthread_t)*clientnum);

    for (int i = 0; i < clientnum; i++) {
        pthread_create(&connect_to_peer_thread[i], NULL, connect_to_peer, &peer_args[i]);
    }

    for (int i = 0; i < clientnum; i++) {
        pthread_join(connect_to_peer_thread[i], NULL);
    }
    run_server_thread = malloc(sizeof(pthread_t)*clientnum);
    for (int i = 0; i < clientnum; i++) {
        struct sockaddr_storage their_addr; // connector's address information
        socklen_t sin_size;
        sin_size = sizeof their_addr;
        int new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) { 
            perror("accept");
            continue;
        }
        char s[INET_ADDRSTRLEN];
        inet_ntop(their_addr.ss_family, get_in_addr(((struct sockaddr *)&their_addr)),
                s, sizeof s);
        
        printf("server: received connection from %s\n",s);
        int peer_number = -1;
        pthread_mutex_lock(&lock_peer_conn);
        for (int j = 0; j < clientnum; j++) {
            if (strcmp(peer_ips[j], s) == 0 && peer_connected[j] == 0) {
                peer_number = j;
                break;
            }
        }   
        if (peer_number == -1) {
            printf("server: received connection from unknown client or duplicate connection %s\n", s);
            close(new_fd);
            i--;
            continue;
        }
        printf("server: received connection from peer %d\n", peer_number);
        peer_connected[peer_number] = 1;
        pthread_mutex_unlock(&lock_peer_conn);
        //printf("server: got connection from client %d at socket %d\n", i, *new_fd);
        struct peer_conn_args* args = malloc(sizeof(struct peer_conn_args));
        args->peer_sockfd = new_fd;
        args->peer_number = peer_number;
        pthread_create(&run_server_thread[i], NULL, run_server, args);
    }
    //printf("server: accepted connections from clients...\n");

    // accept connections from clients
    
    pthread_t main_client_thread;
    pthread_create(&main_client_thread, NULL, get_lines_from_server, server_args);
    pthread_t accept_connections_thread;
    pthread_create(&accept_connections_thread, NULL, accept_connections, &sockfd);
    pthread_join(main_client_thread, NULL);
    pthread_join(accept_connections_thread, NULL);
    for (int i = 0; i < clientnum; i++) {
        pthread_join(run_server_thread[i], NULL);
    }
    close(sockfd);
    free(server_ip);
    free(server_port);
    free(server_args);
    for(int i=0; i<clientnum; i++) {
        free(peer_args[i].peer_ip);
        free(peer_args[i].peer_port);
    }
    free(peer_ips);
    free(peer_args);
    free(peer_send_socks);
    free(run_server_thread);
    free(connect_to_peer_thread);
    //printf("server: received all lines and closed successfully\n");
    //free lines
    for (int i = 0; i < TOTAL_LINES; i++) {
        if (lines[i] != NULL)
            free(lines[i]);
        else {
            fprintf(stderr, "line %d is NULL\n", i);
        }
    }
}