#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>

#define MAX_PAYLOAD_SIZE 512
#define MAX_WINDOW_SIZE 10
#define MAX_RETRIES 5
#define TIMEOUT_SEC 3
#define MAX_SERVERS 10

typedef struct {
    int seq_num;
    int payload_len;
    char payload[MAX_PAYLOAD_SIZE];
} Packet;

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    char *in_file;
    char *out_file;
    int mss;
    int win_size;
} ThreadArgs;

void log_event(const char *event, int seq_num, int base_sn, int next_sn, int win_size, struct sockaddr_in *server_addr) {
    char timestamp[30];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    fprintf(stdout, "%s,%d,%s,%d,%s,%d,%d,%d,%d\n",
            timestamp, ntohs(server_addr->sin_port), inet_ntoa(server_addr->sin_addr),
            ntohs(server_addr->sin_port), event, seq_num, base_sn, next_sn, base_sn + win_size);
    fflush(stdout);
}

void *send_file_to_server(void *args) {
    ThreadArgs *targs = (ThreadArgs *)args;
    FILE *file = fopen(targs->in_file, "rb");
    if (!file) {
        perror("Error opening input file");
        exit(1);
    }

    Packet window[MAX_WINDOW_SIZE];
    bool acked[MAX_WINDOW_SIZE] = {false};
    int retries[MAX_WINDOW_SIZE] = {0};
    int base_sn = 1, next_sn = 1;
    socklen_t addr_len = sizeof(targs->server_addr);
    
    struct timeval timeouts[MAX_WINDOW_SIZE];
    Packet filename_packet;
    memset(&filename_packet, 0, sizeof(Packet));
    filename_packet.seq_num = 0;
    filename_packet.payload_len = snprintf(filename_packet.payload, MAX_PAYLOAD_SIZE, "%s", targs->out_file);

    int filename_retries = 0;
    int ack_sn = -1;
    time_t start_time = time(NULL);

    while (filename_retries < MAX_RETRIES) {
        sendto(targs->sockfd, &filename_packet, sizeof(Packet), 0,
               (struct sockaddr *)&targs->server_addr, sizeof(targs->server_addr));
        fprintf(stdout, "Sent filename packet (attempt %d)\n", filename_retries + 1);

        struct timeval timeout = {TIMEOUT_SEC, 0};
        setsockopt(targs->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        if (recvfrom(targs->sockfd, &ack_sn, sizeof(int), 0, (struct sockaddr *)&targs->server_addr, &addr_len) > 0) {
            if (ack_sn == -20) {
                fprintf(stderr, "File is in progress by another client.\n");
                exit(20);
            }
            if (ack_sn == 0) {
                fprintf(stdout, "Received ACK for filename packet\n");
                break;
            }
        }
        filename_retries++;
        
        if (time(NULL) - start_time >= 30) {
            fprintf(stderr, "Cannot detect server IP %s port %d\n",
                    inet_ntoa(targs->server_addr.sin_addr), ntohs(targs->server_addr.sin_port));
            exit(3);
        }
    }

    if (filename_retries == MAX_RETRIES) {
        fprintf(stderr, "Max retries reached for filename packet. Aborting.\n");
        exit(4);
    }

    while (!feof(file) || base_sn < next_sn) {
        while (!feof(file) && next_sn < base_sn + targs->win_size) {
            int index = next_sn % targs->win_size;
            memset(&window[index], 0, sizeof(Packet));
            window[index].seq_num = next_sn;
            window[index].payload_len = fread(window[index].payload, 1, targs->mss, file);

            sendto(targs->sockfd, &window[index], sizeof(Packet), 0,
                   (struct sockaddr *)&targs->server_addr, addr_len);
            log_event("DATA", next_sn, base_sn, next_sn, targs->win_size, &targs->server_addr);
            
            gettimeofday(&timeouts[index], NULL);
            next_sn++;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(targs->sockfd, &readfds);
        struct timeval timeout = {TIMEOUT_SEC, 0};

        int select_ret = select(targs->sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (select_ret > 0) {
            int ack_sn;
            recvfrom(targs->sockfd, &ack_sn, sizeof(int), 0, NULL, NULL);
            if (ack_sn >= base_sn && ack_sn < next_sn) {
                int index = ack_sn % targs->win_size;
                if (!acked[index]) {
                    log_event("ACK", ack_sn, base_sn, next_sn, targs->win_size, &targs->server_addr);
                    acked[index] = true;
                    retries[index] = 0;
                }
            }
        } else {
            fprintf(stderr, "⚠Timeout! Retransmitting lost packets\n");
            for (int i = base_sn; i < next_sn; i++) {
                int index = i % targs->win_size;
                if (!acked[index] && retries[index] < MAX_RETRIES) {
                    fprintf(stderr, "Packet loss detected. Retransmitting seq_num %d, attempt %d\n", i, retries[index] + 1);
                    sendto(targs->sockfd, &window[index], sizeof(Packet), 0,
                           (struct sockaddr *)&targs->server_addr, addr_len);
                    retries[index]++;
                }
            }
        }

        while (base_sn < next_sn && acked[base_sn % targs->win_size]) {
            acked[base_sn % targs->win_size] = false;
            retries[base_sn % targs->win_size] = 0;
            base_sn++;
        }
    }

    Packet eof_packet;
    memset(&eof_packet, 0, sizeof(Packet));
    eof_packet.seq_num = -1;
    sendto(targs->sockfd, &eof_packet, sizeof(Packet), 0, 
           (struct sockaddr *)&targs->server_addr, addr_len);
    fprintf(stdout, "Sent EOF packet\n");

    fclose(file);
    close(targs->sockfd);
    exit(0);
}




int main(int argc, char *argv[]) {
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <servn> <server_config> <mss> <winsz> <input_file> <output_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int servn = atoi(argv[1]);
    int mss = atoi(argv[3]);
    int winsz = atoi(argv[4]);
    char *input_file = argv[5];
    char *output_file = argv[6];

    int min_mss = 512;
    if (mss < min_mss) {
        fprintf(stderr, "Required minimum MSS is %d\n", min_mss);
        exit(1);
    }

    struct sockaddr_in servers[MAX_SERVERS];
    pthread_t threads[MAX_SERVERS];
    ThreadArgs args[MAX_SERVERS];

    FILE *fp = fopen(argv[2], "r");
    if (!fp) {
        perror("Failed to open server config file");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < servn; i++) {
        char ip[16];
        int port;
        fscanf(fp, "%15s %d", ip, &port);
        servers[i].sin_family = AF_INET;
        servers[i].sin_port = htons(port);
        inet_pton(AF_INET, ip, &servers[i].sin_addr);
    }
    fclose(fp);

    for (int i = 0; i < servn; i++) {
        args[i].sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (args[i].sockfd < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
        args[i].server_addr = servers[i];
        args[i].in_file = input_file;
        args[i].out_file = output_file;
        args[i].mss = mss;
        args[i].win_size = winsz;

        pthread_create(&threads[i], NULL, send_file_to_server, &args[i]);
    }

    for (int i = 0; i < servn; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}

