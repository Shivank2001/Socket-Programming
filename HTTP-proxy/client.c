#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 4096

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <proxy address> <proxy port> <URL to retrieve>\n", argv[0]);
        exit(1);
    }

    char *proxy_address = argv[1];
    int proxy_port = atoi(argv[2]);
    char *url = argv[3];
    char *if_modified_since_date = getenv("IF_MODIFIED_SINCE");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    struct sockaddr_in proxy_addr;
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_port = htons(proxy_port);

    if (inet_pton(AF_INET, proxy_address, &proxy_addr.sin_addr) <= 0)
        error("Invalid address/ Address not supported");

    if (connect(sockfd, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0)
        error("ERROR connecting to proxy");

    printf("Connected to proxy server at %s:%d\n", proxy_address, proxy_port);

    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\n", url, url);
    if (if_modified_since_date) {
        strcat(request, "If-Modified-Since: ");
        strcat(request, if_modified_since_date);
        strcat(request, "\r\n");
    }
    strcat(request, "\r\n");

    if (send(sockfd, request, strlen(request), 0) < 0)
        error("ERROR sending request to proxy");

    printf("Sent request: %s\n", request);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    FILE *fp = fopen("downloaded_file", "wb");
    if (fp == NULL)
        error("ERROR opening file for writing");

    while ((bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, fp);
    }

    if (bytes_received < 0)
        error("ERROR receiving response from proxy");

    fclose(fp);
    close(sockfd);

    printf("File downloaded successfully.\n");

    // 파일을 읽어서 HTML 내용을 출력
    fp = fopen("downloaded_file", "rb");
    if (fp == NULL)
        error("ERROR opening file for reading");

    printf("HTML Content:\n");
    while ((bytes_received = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        fwrite(buffer, 1, bytes_received, stdout);
    }

    fclose(fp);

    return 0;
}