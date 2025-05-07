#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>

#define MAX_FILENAME 256
#define MAX_PAYLOAD_SIZE 512
#define MAX_PACKET_SIZE 32768
#define MAX_CLIENTS 10
#define MAX_SEQ_NUM 10000

typedef struct {
    int seq_num;
    int payload_len;
    char payload[MAX_PAYLOAD_SIZE];
} Packet;

typedef struct {
    struct sockaddr_in addr;
    FILE *file;
    char outfile[MAX_FILENAME];
    bool received[MAX_SEQ_NUM];
    Packet buffer[MAX_SEQ_NUM];
    int expected_seq;
    bool active;
} ClientData;

ClientData clients[MAX_CLIENTS];
int client_count = 0;

void log_event(const char *event, int seq_num, struct sockaddr_in *client_addr) {
    char timestamp[30];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    // Print log to stdout as required in CSV format
    printf("%s,%d,%s,%d,%s,%d\n",
           timestamp, ntohs(client_addr->sin_port), inet_ntoa(client_addr->sin_addr),
           ntohs(client_addr->sin_port), event, seq_num);
    fflush(stdout);
}

ClientData *get_client(struct sockaddr_in *client_addr) {
    for (int i = 0; i < client_count; i++) {
        if (memcmp(&clients[i].addr, client_addr, sizeof(struct sockaddr_in)) == 0) {
            return &clients[i];
        }
    }
    if (client_count < MAX_CLIENTS) {
        clients[client_count].addr = *client_addr;
        clients[client_count].file = NULL;
        clients[client_count].expected_seq = 1;
        clients[client_count].active = true;
        memset(clients[client_count].received, 0, sizeof(clients[client_count].received));
        return &clients[client_count++];
    }
    return NULL;
}

void create_directory_recursive(char *path) {
    char *p = path;
    for (p = path; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0777);
            *p = '/';
        }
    }
    mkdir(path, 0777); // Ensure final directory exists
}

void receive_packets(int sockfd, int droppc, const char *root_folder) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    Packet packet;
    int ack;

    while (1) {
        if (recvfrom(sockfd, &packet, sizeof(Packet), 0, (struct sockaddr *)&client_addr, &addr_len) < 0) {
            perror("recvfrom failed");
            continue;
        }

        // **Simulate packet loss (Dropping randomly)**
        if ((rand() % 100) < droppc) {
            log_event("DROP DATA", packet.seq_num, &client_addr);
            continue;
        }

        // **Find or create a client entry**
        ClientData *client = get_client(&client_addr);
        if (!client) {
            fprintf(stderr, "Server full: Too many clients.\n");
            continue;
        }
        // Handle EOF packet properly
        if (packet.seq_num == -1) {
            printf("Received EOF packet. Closing file for client %s\n",
                   inet_ntoa(client_addr.sin_addr));
            if (client->file) {
                fclose(client->file);
                client->file = NULL;
            }
            client->active = false; // Mark client as inactive
            continue; // Keep server running for other clients
        }

        // **Ensure sequence number is valid**
        if (packet.seq_num < 0 || packet.seq_num >= MAX_SEQ_NUM) {
            fprintf(stderr, "ERROR: Packet %d is out of range. Dropping.\n", packet.seq_num);
            continue;
        }

        // **Handle Filename Packet (seq_num = 0)**
        if (packet.seq_num == 0) {
            printf("Received filename packet: %s\n", packet.payload);
            // no buffer overflow
            snprintf(client->outfile, sizeof(client->outfile) - 1, "%s/%.250s", root_folder, packet.payload);
            client->outfile[sizeof(client->outfile) - 1] = '\0'; // Null-terminate just in case


            // **Ensure directory exists before writing the file**
            char dir_path[MAX_FILENAME];
            strncpy(dir_path, client->outfile, sizeof(dir_path));
            char *last_slash = strrchr(dir_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                create_directory_recursive(dir_path);
            }

            client->file = fopen(client->outfile, "wb");
            if (!client->file) {
                perror("Error opening file");
                continue;
            }

            client->expected_seq = 1;
            ack = 0;
            sendto(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)&client_addr, addr_len);
            log_event("ACK", ack, &client_addr);
        }

        // **Buffer packets & store them**
        if (!client->received[packet.seq_num]) {
            client->received[packet.seq_num] = true;
            client->buffer[packet.seq_num] = packet;
            log_event("DATA", packet.seq_num, &client_addr);
            printf("Buffered packet %d\n", packet.seq_num);
        }

        // **Write packets in order if possible**
        while (client->expected_seq < MAX_SEQ_NUM && client->received[client->expected_seq]) {
            Packet *p = &client->buffer[client->expected_seq];

            // **Ensure file is valid before writing**
            if (!client->file) {
                fprintf(stderr, "ERROR: File pointer is NULL. Skipping write.\n");
                continue;
            }

            printf("Writing packet %d to file\n", client->expected_seq);
            fwrite(p->payload, 1, p->payload_len, client->file);
            fflush(client->file);

            client->received[client->expected_seq] = false;
            client->expected_seq++;
        }

        // **Send ACK for the received packet**
        ack = packet.seq_num;
        sendto(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)&client_addr, addr_len);
        log_event("ACK", ack, &client_addr);
    }
}



int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <droppc> <root_folder>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int droppc = atoi(argv[2]);
    const char *root_folder = argv[3];
    //srand(time(NULL) + getpid());

    mkdir(root_folder, 0777);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d with droppc=%d, saving files in: %s\n", port, droppc, root_folder);
    srand(time(NULL)); // Seed for random packet drops
    receive_packets(sockfd, droppc, root_folder);

    close(sockfd);
    return 0;
}

