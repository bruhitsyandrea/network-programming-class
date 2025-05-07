/* myclient.c redo version 3 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/select.h> // For select()

#define MAX_PAYLOAD_SIZE 512
#define MAX_PACKETS 10000
#define MAX_WINDOW_SIZE 100
#define TIMEOUT_SEC 1
#define MAX_RETRIES 5

typedef struct {
    int seq_num;
    int payload_len;
    char payload[MAX_PAYLOAD_SIZE];
} Packet;

int create_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    return sock;
}
void connect_to_server(int sock, struct sockaddr_in *server_addr, const char *ip, int port) {
    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr->sin_addr) <= 0) {
        perror("Invalid IP address");
        exit(EXIT_FAILURE);
    }

    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("Failed to set socket timeout");
            //close(sock);
            exit(EXIT_FAILURE);
        }
}
void parse_input(int argc, char *argv[], char *ip, int *port, int *mss, int *win_size, char *in_file, char *out_file) {
    printf("argc: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d]: %s\n", i, argv[i]);
    }

    if (argc < 7) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <mss> <winsz> <input_file> <output_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse server IP
    strncpy(ip, argv[1], 15);
    ip[15] = '\0';  // Ensure null termination

    // Parse port number
    *port = atoi(argv[2]);
    if (*port <= 0 || *port > 65535) {
        fprintf(stderr, "Invalid port number. Must be between 1 and 65535.\n");
        exit(EXIT_FAILURE);
    }

    // Parse MSS (Maximum Segment Size)
    *mss = atoi(argv[3]);
    if (*mss <= (int)sizeof(int)) {  // Ensuring MSS is greater than an int (typically 4 bytes)
        fprintf(stderr, "Invalid MSS size. Must be greater than %lu bytes.\n", sizeof(int));
        exit(EXIT_FAILURE);
    }

    // Parse Window Size
    *win_size = atoi(argv[4]);
    if (*win_size <= 0) {
        fprintf(stderr, "Invalid window size. Must be greater than 0.\n");
        exit(EXIT_FAILURE);
    }

    // Parse file paths
    strncpy(in_file, argv[5], 255);
    in_file[254] = '\0';
    strncpy(out_file, argv[6], 255);
    out_file[254] = '\0';
/*
    printf("Parsed Input:\n");
    printf("  Server IP: %s\n", ip);
    printf("  Port: %d\n", *port);
    printf("  MSS: %d\n", *mss);
    printf("  Window Size: %d\n", *win_size);
    printf("  Input File: %s\n", in_file);
    printf("  Output File: %s\n", out_file);
    */
}

void get_rfc_time(char *buffer, size_t len) {
    struct timespec ts;
    struct tm *tm_info;

    clock_gettime(CLOCK_REALTIME, &ts);
    tm_info = gmtime(&ts.tv_sec);

    snprintf(buffer, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             ts.tv_nsec / 1000000);
}

void send_file(int sockfd, struct sockaddr_in* server_addr, int mss, int win_size, const char* in_file_path, const char* out_file_path) {
    FILE *file = fopen(in_file_path, "rb");
    if (!file) {
        perror("Error opening file");
        exit(1);
    }

    Packet filename;
    memset(&filename, 0, sizeof(Packet));
    filename.seq_num = 0;
    strncpy(filename.payload, out_file_path, sizeof(filename.payload) - 1);
    filename.payload[sizeof(filename.payload) -1] = '\0';
    filename.payload_len = strlen(out_file_path) + 1;

    int ack;
    socklen_t addr_len = sizeof(*server_addr);

    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int retries = 0;
    while (retries < MAX_RETRIES) {
        sendto(sockfd, &filename, sizeof(Packet), 0, (struct sockaddr *)server_addr, addr_len);
        printf("Sent filename packet: %s\n", filename.payload);

        if (recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)server_addr, &addr_len) > 0 && ack == 0) {
            printf("Received ACK for filename. Starting file transfer...\n");
            break;
        }

        printf("Timeout waiting for filename ACK, retrying...\n");
        retries++;
    }
    if (retries == MAX_RETRIES) {
        fprintf(stderr, "Error: No ACK Received for filename. Exiting.\n");
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Sliding window setup
    int base = 1, next_seq_num = 1;
    //Packet packet_buffer[MAX_WINDOW_SIZE];
    bool acked[MAX_WINDOW_SIZE] = {false};
    //int retries_buffer[MAX_WINDOW_SIZE] = {0};
    char timestamp[30];

    while (!feof(file) || base < next_seq_num) {
        while (!feof(file) && next_seq_num < base + win_size) {
            Packet pck;
            memset(&pck, 0, sizeof(Packet));
            pck.seq_num = next_seq_num;
            pck.payload_len = fread(pck.payload, 1, mss, file);

            if (pck.payload_len == 0) break;

            //packet_buffer[next_seq_num % win_size] = pck;
            acked[next_seq_num % win_size] = false;
            //retries_buffer[next_seq_num % win_size] = 0;

            sendto(sockfd, &pck, sizeof(Packet), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
            get_rfc_time(timestamp, sizeof(timestamp));
            fprintf(stdout, "%s, DATA, %d, %d, %d, %d\n", timestamp, pck.seq_num, base, next_seq_num, base + win_size);
            fflush(stdout);

            next_seq_num++;
        }

        // Wait for ACK
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        timeout = (struct timeval){ .tv_sec = 1, .tv_usec = 0 };

        int select_ret = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (select_ret > 0) {
            if (recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)server_addr, &addr_len) > 0) {
                if (ack >= base && ack < next_seq_num) {
                    acked[ack % win_size] = true;
                    get_rfc_time(timestamp, sizeof(timestamp));
                    fprintf(stdout, "%s, ACK, %d, %d, %d, %d\n", timestamp, ack, base, next_seq_num, base + win_size);
                    fflush(stdout);
                    
                    while (acked[base % win_size] && base < next_seq_num) {
                        base++;
                    }
                }
            }
        }
    }

    Packet eof_packet;
    memset(&eof_packet, 0, sizeof(Packet));
    eof_packet.seq_num = next_seq_num;
    eof_packet.payload_len = 0;
    sendto(sockfd, &eof_packet, sizeof(Packet), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Sent EOF packet with seq_num %d\n", next_seq_num);

    bool eof_sent = false;
    while (!eof_sent) {
        if (recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)server_addr, &addr_len) > 0 && ack == next_seq_num) {
            eof_sent = true;
        }
    }

    printf("File transfer complete.\n");
    fclose(file);
    close(sockfd);
    exit(0);
}


int main(int argc, char *argv[]) {
    char server_ip[16];
    int server_port, mss, win_size;
    char input_file[256], output_file[256];

    parse_input(argc, argv, server_ip, &server_port, &mss, &win_size, input_file, output_file);


    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket error");
        exit(1);
    }

    struct sockaddr_in server_addr;
    connect_to_server(sockfd, &server_addr, server_ip, server_port);

    send_file(sockfd, &server_addr, mss, win_size, input_file, output_file);

    close(sockfd);
    return 0;
}
