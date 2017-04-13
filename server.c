/**
 * Chatroom Lab
 * CS 241 - Spring 2017
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

#define MAX_CLIENTS 8

void *process_client(void *p);

static volatile int endSession;
static volatile int sock_fd;
static volatile int clientsCount;
static volatile int clients[MAX_CLIENTS];

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Signal handler for SIGINT.
 * Used to set flag to end server.
 */
void close_server() {
    endSession = 1;
}

/**
 * Cleanup function called in main after `run_server` exits.
 * Server ending clean up (such as shutting down clients) should be handled
 * here.
 */
void cleanup() {
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] != -1)
        {
            shutdown(clients[i], SHUT_RD);
            close(clients[i]);
        }
    }
    close(sock_fd);
}

/**
 * Sets up a server connection.
 * Does not accept more than MAX_CLIENTS connections.  If more than MAX_CLIENTS
 * clients attempts to connects, simply shuts down
 * the new client and continues accepting.
 * Per client, a thread should be created and 'process_client' should handle
 * that client.
 * Makes use of 'endSession', 'clientsCount', 'client', and 'mutex'.
 *
 * port - port server will run on.
 *
 * If any networking call fails, the appropriate error is printed and the
 * function calls exit(1):
 *    - fprtinf to stderr for getaddrinfo
 *    - perror() for any other call
 */
void run_server(char *port) {
    int s;
    int optval1 = 1;
    int optval2 = 1;
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("socket() failed\n");
        exit(1);
    }
    int setsock1 = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval1, sizeof(optval1));
    int setsock2 = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &optval2, sizeof(optval2));
    if (setsock1 != 0 || setsock2 != 0)
    {
        perror("setsockopt() failed\n");
        exit(1);
    }
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    s = getaddrinfo(NULL, port, &hints, &result);

    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        freeaddrinfo(result);
        exit(1);
    }

    if (bind(sock_fd, result->ai_addr, result->ai_addrlen) != 0)
    {
        perror("bind() failed\n");
        freeaddrinfo(result);
        exit(1);
    }

    if (listen(sock_fd, MAX_CLIENTS + 1) != 0)
    {
        perror("bind() failed\n");
        freeaddrinfo(result);
        exit(1);
    }
    freeaddrinfo(result);
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i] = -1;
    while(endSession == 0)
    {
        printf("Waiting for connection...\n");
        struct sockaddr_storage clientaddr;
        socklen_t clientaddrsize = sizeof(clientaddr);
        memset(&clientaddr, 0, sizeof(struct sockaddr_storage));
        int client = accept(sock_fd, (struct sockaddr *)&clientaddr, &clientaddrsize);
        if (client == -1)
        {
            perror("accept() failed\n");
            exit(1);
        }
        pthread_mutex_lock(&mutex);
        clientsCount++;
        intptr_t client_id = -1;
        if (clientsCount > MAX_CLIENTS)
        {
            //shutdown(client, SHUT_RD);
            clientsCount--;
            close(client);
            pthread_mutex_unlock(&mutex);
            continue;
        }
        else
        {
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] == -1)
                {
                    clients[i] = client;
                    client_id = i;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&mutex);
        pthread_t id;
        pthread_create(&id, NULL, process_client, (void *)client_id);
    }
    shutdown(sock_fd, SHUT_RD);
    close(sock_fd);
}

/**
 * Broadcasts the message to all connected clients.
 *
 * message  - the message to send to all clients.
 * size     - length in bytes of message to send.
 */
void write_to_clients(const char *message, size_t size) {
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != -1) {
            ssize_t retval = write_message_size(size, clients[i]);
            if (retval > 0) {
                retval = write_all_to_socket(clients[i], message, size);
            }
            if (retval == -1) {
                perror("write(): ");
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}

/**
 * Handles the reading to and writing from clients.
 *
 * p  - (void*)intptr_t index where clients[(intptr_t)p] is the file descriptor
 * for this client.
 *
 * Return value not used.
 */
void *process_client(void *p) {
    pthread_detach(pthread_self());
    intptr_t clientId = (intptr_t)p;
    ssize_t retval = 1;
    char *buffer = NULL;

    while (retval > 0 && endSession == 0) {
        retval = get_message_size(clients[clientId]);
        if (retval > 0) {
            buffer = calloc(1, retval);
            retval = read_all_from_socket(clients[clientId], buffer, retval);
        }
        if (retval > 0)
            write_to_clients(buffer, retval);

        free(buffer);
        buffer = NULL;
    }

    printf("User %d left\n", (int)clientId);
    close(clients[clientId]);

    pthread_mutex_lock(&mutex);
    clients[clientId] = -1;
    clientsCount--;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stderr, "./server <port>\n");
        return -1;
    }

    struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_handler = close_server;
    if (sigaction(SIGINT, &act, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    // signal(SIGINT, close_server);
    run_server(argv[1]);
    cleanup();
    pthread_exit(NULL);
}
