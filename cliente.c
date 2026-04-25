//Cliente pipe - testado no WSL
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCK_PATH "/tmp/pipeso"

int main()
{
    int sockfd, len;
    struct sockaddr_un remote;
    char buffer[1024];

    // Create socket
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Falha em criar o socket");
        return 1;
    }

    // Connect to server
    memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    strncpy(remote.sun_path, SOCK_PATH, sizeof(remote.sun_path) - 1);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(sockfd, (struct sockaddr *)&remote, len) < 0)
    {
        perror("Falha em conectar no servidor");
        close(sockfd);
        return 1;
    }

    printf("Conectado ao servidor!\n\n");

    while (1) {
        // Send data to server
        printf("Entre com o comando a ser enviado: ");
        fgets(buffer, sizeof(buffer), stdin);
        if (write(sockfd, buffer, strlen(buffer) + 1) < 0)
        {
            perror("Falha em escrever no socket");
            close(sockfd);
            return 1;
        }

        printf("\n");
        printf("Comando enviado ao servidor.\n");
        printf("Esperando resposta...\n\n");

        // Read data from server
        if (read(sockfd, buffer, sizeof(buffer)) < 0)
        {
            perror("Falha em ler do socket");
            close(sockfd);
            return 1;
        }

        printf("%s\n\n", buffer);
    }
    
    // Close socket and exit
    close(sockfd);
    return 0;
}