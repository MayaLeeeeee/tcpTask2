
#include "../obj/common.h"
#include "../obj/packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Port> <Output File>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    const char *output_file = argv[2];

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUF_SIZE];
    socklen_t addr_len = sizeof(client_addr);
    int ack_num = 0;

    // Setup socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket()");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind()");
        close(sockfd);
        exit(1);
    }

    while (1) {
        // Open the output file for each sender
        FILE *file = fopen(output_file, "wb");
        if (!file) {
            perror("fopen()");
            close(sockfd);
            exit(1);
        }

        int receiving = 1;
        while (receiving) {
            // Receive data
            int len = recvfrom(sockfd, buffer, BUF_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
            if (len < 0) {
                perror("recvfrom()");
                continue;
            }

            // If an empty packet is received, it indicates the end of the transmission
            if (len == 0) {
                receiving = 0;
				sendto(sockfd, &ack_num, sizeof(ack_num), 0, (struct sockaddr *)&client_addr, addr_len);
				//ack_num++;
            } else {
                // Write data to file
				printf("received: %s\n", buffer);
                fwrite(buffer, 1, len, file);
                fflush(file);

                // Send ACK
                sendto(sockfd, &ack_num, sizeof(ack_num), 0, (struct sockaddr *)&client_addr, addr_len);
                ack_num++;
            }
        }

        fclose(file);
    }

    close(sockfd);
    return 0;
}
