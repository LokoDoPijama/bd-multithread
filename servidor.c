// Compilar com:
// gcc -o servidor servidor.c tpool.c

//Servidor pipe (testado usando WSL)
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include "tpool.h"

#define SOCK_PATH "/tmp/pipeso"

static const size_t num_threads = 4;
tpool_t *tm;


/* ------------ LISTA ENCADEADA DE SOCKETS ------------ */

typedef struct socket_list
{
    struct socket_node *first;
    struct socket_node *last;
    size_t socket_count;
    
} socket_list;

typedef struct socket_node
{
    int sockfd;
    struct socket_node *next;
    
} socket_node;


socket_list *sock_list; // Lista global de sockets

void printList(socket_list *list) {
    socket_node *temp = list->first;

    while (temp != NULL) {
        printf("[DEBUG] sockfd: %d\n", temp->sockfd);
        temp = temp->next;
    }
}

socket_list *createSocketList() {
    socket_list *new_list = (socket_list *) malloc(sizeof(socket_list));

    new_list->first = NULL;
    new_list->last = NULL;
    new_list->socket_count = 0;

    return new_list;
}

socket_node *createSocketNode(int sockfd) {
    socket_node *new_socket = (socket_node *) malloc(sizeof(socket_node));
    new_socket->next = NULL;
    new_socket->sockfd = sockfd;

    return new_socket;
}

socket_node *insertSocket(socket_list *list, int sockfd) {
    socket_node *new_socket = createSocketNode(sockfd);

    if (list->first == NULL) {
        list->first = new_socket;
        list->last = new_socket;
    } else {
        list->last->next = new_socket;
        list->last = new_socket;
    }

    list->socket_count++;

    return new_socket;
}

// TODO: atualizar last
void removeSocket(socket_list *list, int sockfd) {
    socket_node *temp = list->first;
    socket_node *found = NULL;
    
    if (temp == NULL) {
        return;
    }

    if (temp->sockfd != sockfd) {
        while (temp->next != NULL) {
            if (temp->next->sockfd == sockfd) {
                found = temp->next;
                temp->next = temp->next->next;
                break;
            }
            temp = temp->next;
        }
    } else {
        found = temp;
        list->first = temp->next;
    }

    if (found != NULL) {
        close(found->sockfd);
        free(found);
    }

}

char *dataProcessing(void *args) {
    char *data = (char *) args;

    for (int i = 0; i < strlen(data); i++) {
        data[i] = toupper(data[i]);
    }

    sleep(2);

    return (void *) data;
}

void *readSocket(void *args) {
    socket_node *socket = (socket_node *) args;
    char buffer[1024];

    while (1) {
        memset(buffer, '\0', strlen(buffer));

        printf("Esperando dados do cliente...\n");

        // Read data from client
        if (read(socket->sockfd, buffer, sizeof(buffer)) < 0)
        {
            perror("Falha em ler do socket");
            removeSocket(sock_list, socket->sockfd); // TODO: adicionar mutex específico para quando modificar lista de sockets (com insertSocket ou removeSocket)
            return 1;
        }

        if (write(socket->sockfd, NULL, 0) < 0) {
            printf("Socket %d fechado\n", socket->sockfd);
            removeSocket(sock_list, socket->sockfd);
            return 1;            
        }

        printf("Dado recebido: %s\n", buffer);

        char *output = (char *) tpool_add_work(tm, dataProcessing, buffer);

        // Write socket->sockfd data back to client
        if (write(socket->sockfd, output, strlen(output) + 1) < 0)
        {
            perror("Falha em escrever no socket");
            removeSocket(sock_list, socket->sockfd);
            return 1;
        }

        printf("Dado enviado de volta para o cliente.\n");

    }
}

void *testThread(void *arg)
{
    char *input = (char *) arg;

    printf("\e[33mTrabalhando em thread de teste... vindo de (em pool) tid=%p\n\e[0m", pthread_self(), input);
    
    sleep(strlen(input));
    
    printf("\e[33mIsso eh um teste vindo de (em pool) tid=%p\nCom parâmetro: %s\n\e[0m", pthread_self(), input);

    return NULL;
}

void *listenToConnections(void *args) {
    int sockfd, newsockfd, len;
    struct sockaddr_un local, remote;

    // Create socket
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Falha em criar o pipe");
        return 1;
    }

    // Bind socket to local address
    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    strncpy(local.sun_path, SOCK_PATH, sizeof(local.sun_path) - 1);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(sockfd, (struct sockaddr *)&local, len) < 0)
    {
        perror("Falha em capturar o socket");
        close(sockfd);
        return 1;
    }

    // Listen for connections
    if (listen(sockfd, 5) < 0)
    {
        perror("Falha em escutar o socket");
        close(sockfd);
        return 1;
    }

    printf("Servidor Named pipe ouvindo em %s...\n", SOCK_PATH);

    while (1) {
        // Accept connections
        memset(&remote, 0, sizeof(remote));
        len = sizeof(remote);

        newsockfd = accept(sockfd, (struct sockaddr *)&remote, &len);

        if (newsockfd < 0)
        {
            perror("Falha em aceitar coneccao");
            close(sockfd);
            return 1;
        }
        
        socket_node *new_socket = insertSocket(sock_list, newsockfd);

        pthread_t clientThread;
        pthread_create(&clientThread, NULL, readSocket, new_socket);
        pthread_detach(clientThread);

        // tpool_add_work(tm, testThread, "c");
        // tpool_add_work(tm, testThread, "medi");
        // tpool_add_work(tm, testThread, "Teste looongo");
        // tpool_add_work(tm, testThread, "VAMO ENCHER ESSA FILA AEEEE");
        // tpool_add_work(tm, readSocket, new_socket);

        printf("Cliente conectado!\n");

    }
}

/* ------------ FIM LISTA ENCADEADA DE SOCKETS ------------ */


void sigpipe_handler()
{
    printf("\nSIGPIPE caught\n");
}

int main()
{
    signal(SIGPIPE,sigpipe_handler);

    tm = tpool_create(num_threads);
    sock_list = createSocketList();

    pthread_t listenerThread;
    pthread_create(&listenerThread, NULL, listenToConnections, NULL);
    pthread_join(listenerThread, NULL);










    return 0;
}