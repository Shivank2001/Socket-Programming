#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h> // For getting current time for Access-Date

#define MAX_CACHE_ENTRY 10
#define MAX_LEN 100000

// Cache structure
struct Cache {
    char URL[256];
    char Last_Modified[50];
    char Access_Date[50]; 
    char Expires[50];
    char *body;
};

static const struct Cache Clear_Entry;
int num_cache_entries = 0;
struct Cache Proxy_Cache[MAX_CACHE_ENTRY];

// Function to handle errors
void err_sys(const char *msg) {
    perror(msg);
    exit(1);
}

// Parse the headers for a specific field
int parseHDR(const char *hdr, const char *buf, char *op) {
    char *st = strstr(buf, hdr);
    if (!st) return 0;

    char *end = strstr(st, "\r\n");
    st += strlen(hdr);
    while (*st == ' ') ++st;
    while (*(end - 1) == ' ') --end;

    strncpy(op, st, end - st);
    op[end - st] = '\0';
    return 1;
}

int parse_URL(char *URL, char *hostname, int *port, char *path) {
    char *tmp1 = URL;
    char *tmp2;

    // Check if the URL has the "http://" prefix
    if (strstr(URL, "http://") != NULL) {
        tmp1 += 7; // Skip "http://"
    }

    // Extract the hostname (up to the first colon or slash)
    tmp2 = strtok(tmp1, ":/"); // strtok breaks at ':' or '/'
    if (tmp2 != NULL) {
        strcpy(hostname, tmp2);
    } else {
        printf("Error: Invalid hostname format.\n");
        return -1;
    }

    // Check if there's a port specified (colon ":" in URL)
    char *port_pos = strchr(tmp1, ':');
    if (port_pos != NULL) {
        // Extract the port number after the colon
        *port = atoi(port_pos + 1);  // Convert the string after ":" to an integer
    } else {
        // Default port is 80 (HTTP)
        *port = 80;
    }

    // Extract the path (after the hostname and port, if present)
    char *path_start = strchr(tmp1, '/');
    if (path_start != NULL) {
        strcpy(path, path_start);  // Copy the remaining part as the path
    } else {
        // Default path is "/"
        strcpy(path, "/");
    }

    return 0;
}

// Extract data from the server response
int Extract_Read(int fd, char *msg) {
    int total = 0;
    char buffer[MAX_LEN] = {0};
    int cnt = 1;
    while (cnt > 0) {
        memset(buffer, 0, sizeof(buffer));
        cnt = read(fd, buffer, MAX_LEN);
        if (cnt == 0) break;

        // Ensure we don't overflow msg by checking the available space
        if (total + cnt < MAX_LEN) {
            memcpy(msg + total, buffer, cnt);
            total += cnt;
        } else {
            // If we exceed the buffer size, truncate the data
            memcpy(msg + total, buffer, MAX_LEN - total - 1);
            total = MAX_LEN - 1;
            break;
        }

        if (buffer[cnt - 1] == EOF) {
            // Ensure no trailing newline is left
            msg[total - 1] = '\0';
            total--;
            break;
        }
    }
    return total;
}

// Open a client socket to a server with a given hostname and port using getaddrinfo
int open_clientfd(char *hostname_port) {
    int clientfd;
    struct addrinfo hints, *res, *p;
    char hostname[128];
    int port;

    // Parse the hostname:port format
    if (sscanf(hostname_port, "%127[^:]:%d", hostname, &port) != 2) {
        fprintf(stderr, "Error: Invalid hostname and port format: %s\n", hostname_port);
        return -1;  // Return failure (-1) if parsing fails
    }

    // Set up hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;  // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP socket

    // Get address information for the hostname and port
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    int res_status = getaddrinfo(hostname, port_str, &hints, &res);
    if (res_status != 0) {
        fprintf(stderr, "Error: Could not resolve hostname: %s. Reason: %s\n", hostname, gai_strerror(res_status));
        return -1;  // Return failure (-1) if resolving fails
    }

    // Loop through the results and try to create a socket and connect
    for (p = res; p != NULL; p = p->ai_next) {
        clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (clientfd < 0) {
            continue;  // Try the next address if socket creation fails
        }

        if (connect(clientfd, p->ai_addr, p->ai_addrlen) == 0) {
            freeaddrinfo(res);  // Free the linked list of addresses
            return clientfd;  // Successfully connected
        }

        close(clientfd);  // Close socket if connection fails
    }

    // If no connection could be established
    freeaddrinfo(res);
    return -1;  // Return failure (-1) if connection fails
}


// Function to evict the oldest cached entry (LRU)
void evict_cache_entry() {
    if (num_cache_entries >= MAX_CACHE_ENTRY) {
        // Free the memory of the oldest (last) cache entry
        free(Proxy_Cache[MAX_CACHE_ENTRY - 1].body);
        
        // Shift all entries towards the tail (right shift)
        for (int i = MAX_CACHE_ENTRY - 1; i > 0; i--) {
            Proxy_Cache[i] = Proxy_Cache[i - 1];  // Shift each entry to the right
        }

        num_cache_entries--;  // Decrease the count after eviction
    }
}

// Function to store a response in the cache, evicting if necessary
void store_in_cache(char *URL, char *resp) {
    // If the cache is full, evict the oldest entry
    if (num_cache_entries == MAX_CACHE_ENTRY) {
        evict_cache_entry();
    }

    // Shift existing entries one step down to make room for the new one at the front
    for (int i = num_cache_entries; i > 0; i--) {
        Proxy_Cache[i] = Proxy_Cache[i - 1];  // Shift each entry to the right
    }

    // Store the new cache entry at the front (index 0)
    strncpy(Proxy_Cache[0].URL, URL, sizeof(Proxy_Cache[0].URL) - 1);
    Proxy_Cache[0].body = malloc(strlen(resp) + 1);
    if (Proxy_Cache[0].body == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for cache body\n");
        exit(1);
    }
    strcpy(Proxy_Cache[0].body, resp);

    // Set Access-Date to the current time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(Proxy_Cache[0].Access_Date, sizeof(Proxy_Cache[0].Access_Date), "%a, %d %b %Y %H:%M:%S CDT", t);

    // Set default values for Last-Modified and Expires if not set
    if (parseHDR("Last-Modified:", resp, Proxy_Cache[0].Last_Modified) == 0) {
        strcpy(Proxy_Cache[0].Last_Modified, "-");
    }
    if (parseHDR("Expires:", resp, Proxy_Cache[0].Expires) == 0) {
        strcpy(Proxy_Cache[0].Expires, "-");
    }

    num_cache_entries++;
}

// Display cache contents
void Cache_Display() {
    printf("\nCache Contents:\n");
    // Adjusted column widths for consistent formatting
    printf("%-40s %-30s %-30s %-30s\n", "URL", "Last-Modified", "Access-Date", "Expires");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < num_cache_entries; i++) {
        // Print each entry with consistent padding for missing fields
        printf("%-40s %-30s %-30s %-30s\n",
               Proxy_Cache[i].URL,
               (strlen(Proxy_Cache[i].Last_Modified) > 0) ? Proxy_Cache[i].Last_Modified : "-",
               (strlen(Proxy_Cache[i].Access_Date) > 0) ? Proxy_Cache[i].Access_Date : "-",
               (strlen(Proxy_Cache[i].Expires) > 0) ? Proxy_Cache[i].Expires : "-");
    }
    printf("------------------------------------------------------------\n");
}


// Function to handle proxy server logic
// Function to handle proxy server logic
void Proxy_Server(int client_fd) {
    char to_client[MAX_LEN], forward_client_msg[MAX_LEN];
    char path[256], hostname[64];
    int port = 80;
    char URL[256] = {0};
    char Method[8] = {0};
    char Protocol[16] = {0};
    int ret;

    char msg[MAX_LEN];
    ret = read(client_fd, msg, MAX_LEN);
    if (ret < 0) {
        fprintf(stderr, "Error: Failed to read request from client. Skipping this request.\n");
        close(client_fd);
        return;  // Skip this request and move on
    }

    sscanf(msg, "%s %s %s", Method, URL, Protocol);
    printf("Request method: %s, URL: %s, Protocol: %s\n", Method, URL, Protocol);

    // Check if the URL is already cached
    int cache_el = -1;
    for (int i = 0; i < num_cache_entries; i++) {
        if (strcmp(Proxy_Cache[i].URL, URL) == 0) {
            cache_el = i;
            break;
        }
    }

    if (cache_el != -1) {
        // Cache hit: Serve from cache
        printf("Cache hit: Serving from cache\n");

        // If the accessed URL is not already at the front, shift other entries
        if (cache_el != 0) {
            // First, we save the accessed entry (so it doesn't get overwritten)
            struct Cache temp = Proxy_Cache[cache_el];

            // Shift all entries above the accessed URL down by one position
            for (int i = cache_el; i > 0; i--) {
                Proxy_Cache[i] = Proxy_Cache[i - 1];  // Shift each entry to the right
            }

            // Now, move the accessed URL to the top (index 0)
            Proxy_Cache[0] = temp;  // Put the saved entry at the top
        }

        // Prepare the response
        snprintf(to_client, MAX_LEN,
                 "HTTP/1.1 200 OK\r\nLast-Modified: %s\r\nAccess-Date: %s\r\nExpires: %s\r\n\r\n%s",
                 Proxy_Cache[0].Last_Modified,
                 Proxy_Cache[0].Access_Date,
                 Proxy_Cache[0].Expires,
                 Proxy_Cache[0].body);
    } else {
        // URL is not cached, fetch from web server
        parse_URL(URL, hostname, &port, path);

        // Build hostname:port string for open_clientfd
        char hostname_port[128];
        snprintf(hostname_port, sizeof(hostname_port), "%s:%d", hostname, port);

        int webs_sockfd = open_clientfd(hostname_port);
        if (webs_sockfd < 0) {
            fprintf(stderr, "Error: Could not connect to server %s. Skipping this request.\n", hostname_port);
            snprintf(to_client, MAX_LEN, "HTTP/1.1 502 Bad Gateway\r\n\r\nUnable to fetch the requested content.\r\n");
            write(client_fd, to_client, strlen(to_client));
            close(client_fd);
            return;  // Skip this request
        }

        snprintf(forward_client_msg, MAX_LEN, "%s %s %s\r\nHost: %s\r\nUser-Agent: HTTPTool/1.0\r\n\r\n", Method, path, Protocol, hostname);
        write(webs_sockfd, forward_client_msg, strlen(forward_client_msg));

        char *resp = (char *)malloc(100000);
        if (resp == NULL) {
            fprintf(stderr, "Error: Memory allocation failed for response. Skipping this request.\n");
            close(webs_sockfd);
            snprintf(to_client, MAX_LEN, "HTTP/1.1 500 Internal Server Error\r\n\r\nUnable to allocate memory.\r\n");
            write(client_fd, to_client, strlen(to_client));
            close(client_fd);
            return;  // Skip this request
        }

        int bytes_received = Extract_Read(webs_sockfd, resp);
        if (bytes_received <= 0) {
            fprintf(stderr, "Error: Failed to read response from server. Skipping this request.\n");
            free(resp);
            close(webs_sockfd);
            snprintf(to_client, MAX_LEN, "HTTP/1.1 502 Bad Gateway\r\n\r\nFailed to fetch content.\r\n");
            write(client_fd, to_client, strlen(to_client));
            close(client_fd);
            return;  // Skip this request
        }

        // Extract headers from the response
        parseHDR("Last-Modified:", resp, Proxy_Cache[num_cache_entries].Last_Modified);
        parseHDR("Expires:", resp, Proxy_Cache[num_cache_entries].Expires);
        parseHDR("Date:", resp, Proxy_Cache[num_cache_entries].Access_Date);

        if (strlen(resp) < MAX_LEN) {
            memcpy(to_client, resp, strlen(resp));
        } else {
            fprintf(stderr, "Error: Response too large to fit in buffer. Skipping this request.\n");
            free(resp);
            close(webs_sockfd);
            snprintf(to_client, MAX_LEN, "HTTP/1.1 500 Internal Server Error\r\n\r\nResponse too large.\r\n");
            write(client_fd, to_client, strlen(to_client));
            close(client_fd);
            return;  // Skip this request
        }

        close(webs_sockfd);

        // Store the response in the cache
        store_in_cache(URL, resp);

        free(resp);
    }

    // Send the response to the client
    write(client_fd, to_client, strlen(to_client));

    // Display the cache table
    Cache_Display();

    close(client_fd);
}


int main(int argc, char *argv[]) {
    int listen_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if (argc != 3) {  // Ensure we have 3 arguments (IP address, port)
        fprintf(stderr, "Usage: %s <IP address> <Port>\n", argv[0]);
        exit(1);
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) err_sys("Error in socket");

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);  // Use the provided IP address
    server_addr.sin_port = htons(atoi(argv[2]));  // Use the provided port

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        err_sys("Error in bind");
    }

    listen(listen_fd, 5);
    printf("Proxy server running on %s:%s\n", argv[1], argv[2]);

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            err_sys("Error in accept");
        }

        Proxy_Server(client_fd);
    }

    close(listen_fd);
    return 0;
}
