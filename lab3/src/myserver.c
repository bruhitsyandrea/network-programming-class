/* myserver.c redo version 3 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h> // For time() and srand()
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>

#define MAX_PAYLOAD_SIZE 512
#define MAX_PACKETS 10000
#define MAX_WINDOW_SIZE 100 // Define a maximum window size
#define MAX_CLIENTS 10 


typedef struct {
    int seq_num;
    int payload_len;
    char payload[MAX_PAYLOAD_SIZE];
} Packet;

typedef struct {
    struct sockaddr_in addr; 
    FILE *file;  
    char outfile[256];  
    int expected_seq;  
    Packet packet_buffer[MAX_PACKETS];  
    bool received[MAX_PACKETS];  
} ClientData;

ClientData clients[MAX_CLIENTS];
int client_count = 0; 

ClientData* get_client(struct sockaddr_in *client_addr) {
    for (int i = 0; i < client_count; i++) {
        if (memcmp(&clients[i].addr, client_addr, sizeof(struct sockaddr_in)) == 0) {
            return &clients[i];
        }
    }

    if (client_count >= MAX_CLIENTS) {
        printf("Max clients reached, dropping client.\n");
        return NULL;
    }

    ClientData *new_client = &clients[client_count++];
    new_client->addr = *client_addr;
    new_client->file = NULL;  
    new_client->expected_seq = 1; 
    memset(new_client->received, 0, sizeof(new_client->received));

    return new_client;
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

int create_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    return sock;
}
int start_server(int port_number) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_number);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        close(sockfd);
        exit(1);
    }

    printf("Server started on port %d\n", port_number);
    return sockfd;
}

void create_output_directory(const char *filepath) {
    char path[256];
    strncpy(path, filepath, sizeof(path) - 1);
    path[sizeof(path) -1] = '\0';

    char *dir = dirname(path);

    if (strcmp(dir, ".") != 0) {
        struct stat st;
        if (stat(dir, &st) == -1) {
            if (mkdir(dir, 0777) == -1) {
                perror("Error creating output directory");
                exit(1);
            }
        }
    }
}

void send_ack(int sockfd, struct sockaddr_in *client_addr, int ack_num, int drop_rate) {
    char timestamp[30];
    get_rfc_time(timestamp, sizeof(timestamp));

    if (rand() % 100 < drop_rate) {
        fprintf(stdout, "%s, DROP ACK, %d\n", timestamp, ack_num);
        fflush(stdout);
        return;
    }

    fprintf(stdout, "%s, ACK, %d\n", timestamp, ack_num);
    fflush(stdout);

    sendto(sockfd, &ack_num, sizeof(int), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
}

void receive_packets(int sockfd, int drop_rate) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    Packet packet;
    ClientData *client;

    while (1) {
        ssize_t received_bytes = recvfrom(sockfd, &packet, sizeof(Packet), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (received_bytes < (ssize_t)(sizeof(int))) {
            continue;
        }

        char timestamp[30];
        get_rfc_time(timestamp, sizeof(timestamp));

        // Simulate packet drop
        if (rand() % 100 < drop_rate) {
            fprintf(stdout, "%s, DROP DATA, %d\n", timestamp, packet.seq_num);
            fflush(stdout);
            continue;
        }

        // Get client object
        client = get_client(&client_addr);
        if (!client) continue;

        if (packet.seq_num == 0) { 
            strncpy(client->outfile, packet.payload, sizeof(client->outfile) -1);
            client->outfile[sizeof(client->outfile) -1] = '\0';

            create_output_directory(client->outfile);

            client->file = fopen(client->outfile, "wb");
            if (!client->file) {
                perror("Error creating file");
                exit(1);
            }
            printf("Saving file as: %s\n", client->outfile);
            send_ack(sockfd, &client_addr, 0, drop_rate);
            continue;
        }

        if (packet.payload_len == 0) {
            fclose(client->file);
            printf("File transfer complete for client.\n");
            send_ack(sockfd, &client_addr, packet.seq_num, drop_rate);
            continue;
        }

        if (!client->received[packet.seq_num]) {  
            client->received[packet.seq_num] = true;
            client->packet_buffer[packet.seq_num] = packet;

            fprintf(stdout, "%s, DATA, %d\n", timestamp, packet.seq_num);
            fflush(stdout);

            send_ack(sockfd, &client_addr, packet.seq_num, drop_rate);
        }

        while (client->received[client->expected_seq]) {
            fwrite(client->packet_buffer[client->expected_seq].payload, 1, 
                   client->packet_buffer[client->expected_seq].payload_len, client->file);
            printf("Wrote packet %d to file, length %d\n",
                   client->expected_seq, client->packet_buffer[client->expected_seq].payload_len);

            send_ack(sockfd, &client_addr, client->expected_seq, drop_rate);
            client->expected_seq++;
        }
    }
}



int main (int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <drop_rate>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    int drop_rate = atoi(argv[2]);

    int sockfd = start_server(port);
    receive_packets(sockfd, drop_rate);
    close(sockfd);
    return 0;
}