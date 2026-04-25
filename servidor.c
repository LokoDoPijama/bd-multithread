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
#include "sgbd.h"

#define SOCK_PATH "/tmp/pipeso"

static const size_t num_threads = 4;
tpool_t *tm;

/* ------------ LISTA ENCADEADA DE SOCKETS ------------ */

typedef struct socket_list
{
    struct socket_node *first;
    struct socket_node *last;
    size_t socket_count;
    pthread_mutex_t socket_mutex; // Mutex para modificar a lista de sockets
    
} socket_list;

typedef struct socket_node
{
    int sockfd;
    struct socket_node *next;
    
} socket_node;


socket_list *sock_list; // Lista global de sockets

// Função para debug
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

    pthread_mutex_init(&(new_list->socket_mutex), NULL);

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

    pthread_mutex_lock(&(list->socket_mutex));
    
    if (list->first == NULL) {
        list->first = new_socket;
        list->last = new_socket;
    } else {
        list->last->next = new_socket;
        list->last = new_socket;
    }

    list->socket_count++;

    pthread_mutex_unlock(&(list->socket_mutex));

    return new_socket;
}

void removeSocket(socket_list *list, int sockfd) {
    pthread_mutex_lock(&(list->socket_mutex));

    socket_node *temp = list->first;
    socket_node *found = NULL;
    
    if (temp == NULL) {
        return;
    }

    if (temp->sockfd != sockfd) { // Se o item a ser removido não é o primeiro da lista
        while (temp->next != NULL) {
            if (temp->next->sockfd == sockfd) {
                found = temp->next;
                temp->next = temp->next->next;
                if (found->next == NULL) { // Se item a ser removido é o último da lista
                    list->last = temp;
                }
                break;
            }
            temp = temp->next;
        }

    } else {
        // Item a ser removido é o primeiro da lista
        found = temp;
        list->first = temp->next;
        if (temp->next == NULL) {
            list->last = NULL;
        }

    }

    pthread_mutex_unlock(&(list->socket_mutex));

    if (found != NULL) {
        close(found->sockfd);
        free(found);
    }

}

/* ------------ FIM LISTA ENCADEADA DE SOCKETS ------------ */


// Variáveis globais para a função 'dataProcessing()' (inicializadas no main)
pthread_mutex_t write_mutex;      // Mutex de escrita para controlar quais operações podem executar simultaneamente
pthread_mutex_t wr_control_mutex; // Mutex para proteger operações envolvendo 'read_cont'
pthread_cond_t done_cond;         // Ajuda a sinalizar que uma operação de banco terminou
size_t read_cont = 0;             // Número de leituras sendo realizadas

// Função que é executada pelo worker de cada thread
char *dataProcessing(void *args) {
    char *data = (char *) args;

    db_parsed_command cmd;

    if (!parse_command(data, &cmd)) { // Salva o comando estruturado em 'cmd'
        char *message = "Comando inválido!";
        return message;
    }


    pthread_mutex_lock(&(write_mutex)); // Tranca 'write_mutex' (todos os comandos são barrados aqui se ele estiver trancado)

    if (cmd.type == CMD_SELECT) {
        // Leitura
        pthread_mutex_unlock(&write_mutex); // Libera 'write_mutex', caso o comando for de leitura
        pthread_mutex_lock(&wr_control_mutex);
        read_cont++;
        pthread_mutex_unlock(&wr_control_mutex);
        
    } else {
        // Escrita
        // 'write_mutex' não é liberado aqui
        pthread_mutex_lock(&wr_control_mutex);
        while (read_cont > 0)
            pthread_cond_wait(&done_cond, &wr_control_mutex); // Libera 'wr_control_mutex' e fica esperando 'done_cond' ser sinalizado por 'pthread_cond_broadcast()'
        pthread_mutex_unlock(&wr_control_mutex);

    }

    sleep(2); // sleep opcional para simular operações demoradas

    db_response *response = db_execute_statement(data);

    if (cmd.type == CMD_SELECT) {
        // Leitura
        pthread_mutex_lock(&wr_control_mutex);
        read_cont--;
        pthread_mutex_unlock(&wr_control_mutex);
        
    } else {
        // Escrita
        pthread_mutex_unlock(&write_mutex); // Libera 'write_mutex'

    }

    pthread_cond_broadcast(&done_cond); // Sinaliza que uma operação de banco terminou (serve para operações de escrita que estão esperando 'done_cond')


    char *message = response->message;
    
    if (response->entry != NULL) {
        free(response->entry);
    }
    free(response);

    return (void *) message; // Retorna conteúdo textual
}

void *readSocket(void *args) {
    socket_node *socket = (socket_node *) args;
    char buffer[1024];

    while (1) {
        memset(buffer, '\0', strlen(buffer));

        printf("Esperando comandos do cliente no socket %d...\n\n", socket->sockfd);

        // Read data from client
        if (read(socket->sockfd, buffer, sizeof(buffer)) < 0)
        {
            perror("Falha em ler do socket");
            removeSocket(sock_list, socket->sockfd);
            return 1;
        }

        if (write(socket->sockfd, NULL, 0) < 0) {
            printf("Socket %d fechado\n\n", socket->sockfd);
            removeSocket(sock_list, socket->sockfd);
            return 1;            
        }

        printf("Comando recebido: %s\n", buffer);

        char *output = (char *) tpool_add_work(tm, dataProcessing, buffer);

        // Write socket->sockfd data back to client
        if (write(socket->sockfd, output, strlen(output) + 1) < 0)
        {
            free(output);
            perror("Falha em escrever no socket");
            removeSocket(sock_list, socket->sockfd);
            return 1;
        }

        printf("Resposta enviada para o cliente no socket %d.\n\n", socket->sockfd);

    }
}

// Função que fica esperando conexões de clientes e cria uma thread para cada cliente novo
void *listenToConnections(void *args) {
    int sockfd, newsockfd, len;
    struct sockaddr_un local, remote;

    // Cria o socket para a conexão
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Falha em criar o pipe");
        return 1;
    }

    // Faz o 'bind' do socket com o endereço (SOCK_PATH)
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

    // Prepara para escutar o socket
    if (listen(sockfd, 5) < 0)
    {
        perror("Falha em escutar o socket");
        close(sockfd);
        return 1;
    }

    printf("Servidor Named Pipe ouvindo em %s...\n\n\n", SOCK_PATH);

    while (1) {
        memset(&remote, 0, sizeof(remote));
        len = sizeof(remote);

        // Fica esperando conexões de clientes
        newsockfd = accept(sockfd, (struct sockaddr *)&remote, &len);

        if (newsockfd < 0)
        {
            perror("Falha em aceitar coneccao");
            close(sockfd);
            return 1;
        }
        
        // Cria um socket para a nova conexão com o cliente
        socket_node *new_socket = insertSocket(sock_list, newsockfd);

        // Cria uma thread nova para o cliente
        pthread_t clientThread;
        pthread_create(&clientThread, NULL, readSocket, new_socket);
        pthread_detach(clientThread);

        printf("Cliente conectado em socket %d!\n\n", newsockfd);

    }
}


void sigpipe_handler()
{
    // SIGPIPE caught
}

int main()
{
    // Capturando sinais SIGPIPE, que indicam 'broken pipe' (conexão com o cliente terminada) e executando 'sigpipe_handler()'.
    // Na prática, isso faz com que uma terminação da conexão do cliente não pare o servidor.
    struct sigaction act = {
        .sa_handler = sigpipe_handler,
        .sa_flags = 0,
    };
    sigaction(SIGPIPE, &act, NULL);


    // Inicializa variáveis globais

    pthread_mutex_init(&write_mutex, NULL);
    pthread_mutex_init(&wr_control_mutex, NULL);
    pthread_cond_init(&done_cond, NULL);

    tm = tpool_create(num_threads);
    sock_list = createSocketList();


    // Inicia thread principal que espera conexões de clientes
    
    pthread_t listenerThread;
    pthread_create(&listenerThread, NULL, listenToConnections, NULL);
    pthread_join(listenerThread, NULL);

    return 0;
}