#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>


//goal:
//performa GET and HEAD

//create_socket()
int create_socket() {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("Socket creation failed");
		exit(1);
	}
	return sock;
}

void connect_to_server(int sock, const char *ip, int port) {
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);

	if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
		perror("Invalid IP address");
		close(sock);
		exit(1);
	}

	int flags = fcntl(sock, F_GETFL, 0);
	if (flags == -1) {
		perror("Failed to get socket flags");
		close(sock);
		exit(1);
	}
	if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
		perror("Failed to set socket to non_blocking mode");
		close(sock);
		exit(1);
	}

	int result = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (result < 0 && errno != EINPROGRESS) {
		perror("Connection failed");
		close(sock);
		exit(1);
	}

	if (result < 0 && errno == EINPROGRESS) {
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;

		fd_set write_fds;
		FD_ZERO(&write_fds);
		FD_SET(sock, &write_fds);

		result = select(sock + 1, NULL, &write_fds, NULL, &timeout);
		if (result == 0) {
			fprintf(stderr, "Connection timed out\n");
			close(sock);
			exit(1);
		} else if (result < 0) {
			perror("Connection error");
			close(sock);
			exit(1);
		}

		int so_error;
		socklen_t len = sizeof(so_error);
		if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
			perror("getsockopt failed");
			close(sock);
			exit(1);
		}
		if (so_error != 0) {
			fprintf(stderr, "Connection failed; %s\n", strerror(so_error));
			close(sock);
			exit(1);
		}
	}

	if (fcntl(sock, F_SETFL, flags) == -1) {
		perror("Failed to restore socket to blocking mode");
		close(sock);
		exit(1);
	}
}

void parse_url(const char *url, char *ip, char *path, int *port) {
	if (strchr(url, ':')) {
		if (sscanf(url, "%[^:]:%d/%s", ip, port, path) != 3) {
			fprintf(stderr, "Invalid URL format. Expected format: IP:port/path\n");
			exit(1);
		}
	} else {
		*port = 80;
		if (sscanf(url, "%[^/]/%s", ip, path) != 2) {
			fprintf(stderr, "Invalid URL format. Expected format: IP/path\n");
			exit(1);
		}
	}
	
}

void construct_request(char *buffer, size_t buffer_size, const char *method, const char *path, const char *hostname) {
	snprintf(buffer, buffer_size, "%s /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", method, path, hostname);
}

void send_request(int sock, const char *request) {
	if (send(sock, request, strlen(request), 0) < 0) {
		perror("Failed to send request");
		close(sock);
		exit(1);
	}
}

void receive_response(int sock, int is_head) {
	char buffer[4096];
	FILE *output_file = is_head ? NULL : fopen("output.dat", "w");

	if (!is_head && !output_file) {
		perror("Failed to open output.dat");
		close(sock);
		exit(1);
	}

	int bytes_received;
	while((bytes_received = recv(sock, buffer, sizeof(buffer) -1, 0)) > 0) {
		buffer[bytes_received] = '\0';
		
		if (is_head) {
			printf("%s", buffer);
		} else {
			fprintf(output_file, "%s", buffer);
		}
	}

	if (!is_head && output_file) {
		fclose(output_file);
	}
	if (bytes_received < 0) {
		perror("Failed to receive response");
	}
}

int main(int argc, char const *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "Usage:%s <hostname> <IP:port/path> [-h]\n\n", argv[0]);
		return 1;
	}

	char *hostname = strdup(argv[1]);
	char *url = strdup(argv[2]);
	int head = (argc == 4 && strcmp(argv[3], "-h") == 0);

	char ip[100], path[100];
	int port;

	parse_url(url, ip, path, &port);
	int sock = create_socket();

	connect_to_server(sock, ip, port);

	char request[1024];
	construct_request(request, sizeof(request), head ? "HEAD" : "GET", path, hostname);

	send_request(sock, request);

	receive_response(sock, head);

	close(sock);
	return 0;
}




