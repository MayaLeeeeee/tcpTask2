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
#include <stdbool.h>

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

// Congestion control variables
double cwnd = 1.0;
int ssthresh = 64;
int dup_acks = 0;
int last_ack = -1;
bool slow_start = true;

// CSV logging variables and functions
#include <stdbool.h>
FILE *csv_file;

bool initialize_csv_logging(const char *csv_path) {
    csv_file = fopen(csv_path, "w");
    if (!csv_file) {
        perror("fopen()");
        return false;
    }
    fprintf(csv_file, "cwnd,ssthresh,rto,retransmissions\n");
    fflush(csv_file);
    return true;
}

void record_csv_logging(double cwnd, int ssthresh, double rto, int retransmissions) {
    fprintf(csv_file, "%d,%d,%d,%d\n", (int)cwnd, ssthresh, (int)rto, retransmissions);
    fflush(csv_file);
}

void close_csv_logging() {
    if (csv_file) {
        fclose(csv_file);
    }
}

// Function to calculate the retransmission timeout (RTO)
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

// Function to adjust the congestion window
void adjust_cwnd(int ack_num) {
    record_csv_logging(cwnd, ssthresh, rto, retransmissions);

    if (ack_num == last_ack) {
        dup_acks++;
        if (dup_acks == 3) {
            // Fast Retransmit
            ssthresh = fmax(cwnd / 2, 2);
            cwnd = 1;
            dup_acks = 0;
            slow_start = true;
            fprintf(stderr, "Fast Retransmit triggered: ssthresh = %d, cwnd = %f\n", ssthresh, cwnd);
        }
    } else {
        dup_acks = 0;
        last_ack = ack_num;

        if (slow_start) {
            cwnd += 1.0;
            if (cwnd >= ssthresh) {
                slow_start = false;
                fprintf(stderr, "Switching to Congestion Avoidance\n");
            }
        } else {
            cwnd += 1.0 / cwnd;
        }
    }
}

// Function to send a packet and manage retransmissions
int send_packet(const char *data, size_t len) {
    int sent_bytes = sendto(sockfd, data, len, 0, (struct sockaddr *)&server_addr, addr_len);
    if (sent_bytes < 0) {
        perror("sendto()");
        return -1;
    }

    gettimeofday(&start, NULL);

    // Wait for acknowledgment
    char ack_buf[BUF_SIZE];
    struct timeval timeout;
    timeout.tv_sec = (int)rto;
    timeout.tv_usec = (int)((rto - timeout.tv_sec) * 1000000);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    int result = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
    if (result > 0 && FD_ISSET(sockfd, &readfds)) {
        int received_bytes = recvfrom(sockfd, ack_buf, BUF_SIZE, 0, (struct sockaddr *)&server_addr, &addr_len);
        if (received_bytes > 0) {
            int ack_num = atoi(ack_buf);
            gettimeofday(&end, NULL);
            double sample_rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
            calculate_rto(sample_rtt);
            adjust_cwnd(ack_num);
            retransmissions = 0;
            return sent_bytes;
        }
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
    return -1;
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

    // Initialize CSV logging
    const char *csv_path = "CWND.csv";
    if (!initialize_csv_logging(csv_path)) {
        fclose(file);
        close(sockfd);
        exit(1);
    }

    // Read and send the file in chunks
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
        int packets_to_send = (int)floor(cwnd);
        for (int i = 0; i < packets_to_send; ++i) {
            if (send_packet(buffer, bytes_read) < 0) {
                fprintf(stderr, "Error sending data\n");
                fclose(file);
                close(sockfd);
                close_csv_logging();
                exit(1);
            }
        }
    }

    // Send an empty packet to signal the end of transmission
    send_packet("", 0);

    fclose(file);
    close(sockfd);
    close_csv_logging();
    printf("File sent successfully. Exiting sender.\n");
    return 0;
}
