#include "../obj/common.h"
#include "../obj/packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>

#define MAX_RETRANSMISSIONS 5
#define INITIAL_RTO 3
#define MAX_RTO 240
#define ALPHA 0.125
#define BETA 0.25
#define BUF_SIZE 1024

int sockfd;
struct sockaddr_in server_addr;
socklen_t addr_len = sizeof(server_addr);
char buffer[BUF_SIZE];
struct timeval start, end;
double estimated_rtt = 0.0, dev_rtt = 0.0;
double rto = INITIAL_RTO;
int retransmissions = 0;

void calculate_rto(double sample_rtt) {
    if (estimated_rtt == 0.0) {
        // First measurement
        estimated_rtt = sample_rtt;
        dev_rtt = sample_rtt / 2.0;
    } else {
        estimated_rtt = (1 - ALPHA) * estimated_rtt + ALPHA * sample_rtt;
        dev_rtt = (1 - BETA) * dev_rtt + BETA * fabs(sample_rtt - estimated_rtt);
    }
    rto = estimated_rtt + 4 * dev_rtt;

    // Ensure RTO is within bounds
    if (rto < 1) rto = 1;
    if (rto > MAX_RTO) rto = MAX_RTO;
}

int send_packet(char *data, int len) {
    // Start timer
    gettimeofday(&start, NULL);

    // Send packet
    sendto(sockfd, data, len, 0, (struct sockaddr *)&server_addr, addr_len);

    // Receive ACK
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = (int)rto;
    timeout.tv_usec = (int)((rto - (int)rto) * 1000000);

    int retval = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
    if (retval == -1) {
        perror("select()");
        return -1;
    } else if (retval) {
        // Packet ACKed
        recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &addr_len);

        // Calculate RTT
        gettimeofday(&end, NULL);
        double sample_rtt = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

        // Update RTO using RTT estimator (Karn's Algorithm)
        if (retransmissions == 0) {
            calculate_rto(sample_rtt);
        }

        retransmissions = 0;
        return 0;  // Successfully sent and acknowledged
    } else {
        // Timeout occurred, retransmit with exponential backoff
        retransmissions++;
        if (retransmissions >= MAX_RETRANSMISSIONS) {
            fprintf(stderr, "Maximum retransmissions reached, giving up.\n");
            exit(1);
        }

        // Exponential backoff
        rto *= 2;
        if (rto > MAX_RTO) rto = MAX_RTO;

        // Retry sending the packet
        fprintf(stderr, "Timeout, retransmitting...\n");
        return send_packet(data, len);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <Server IP> <Server Port> <File to Send>\n", argv[0]);
        exit(1);
    }

    // Parse arguments
    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    const char *file_path = argv[3];

    // Setup socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket()");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Open file to send
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("fopen()");
        close(sockfd);
        exit(1);
    }

    // Read and send the file in chunks
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
		printf("sending: %s\n", buffer);
        if (send_packet(buffer, bytes_read) < 0) {
            fprintf(stderr, "Error sending data\n");
            fclose(file);
            close(sockfd);
            exit(1);
        }
    }
	printf("outside while loop\n");
    // Send an empty packet to signal the end of transmission
    send_packet("", 0);

    fclose(file);
    close(sockfd);
    printf("File sent successfully. Exiting sender.\n");
    return 0;
}
