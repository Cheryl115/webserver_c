#include "hw1.h"

/* Get "Content-Type" of response headers */
const char *get_content_type(const char* path)
{
    const char *last_dot = strrchr(path, '.');
    if (last_dot) {
        if (strcmp(last_dot, ".css") == 0) return "text/css";
        if (strcmp(last_dot, ".csv") == 0) return "text/csv";
        if (strcmp(last_dot, ".gif") == 0) return "image/gif";
        if (strcmp(last_dot, ".htm") == 0) return "text/html";
        if (strcmp(last_dot, ".html") == 0) return "text/html";
        if (strcmp(last_dot, ".ico") == 0) return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0) return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0) return "application/javascript";
        if (strcmp(last_dot, ".json") == 0) return "application/json";
        if (strcmp(last_dot, ".png") == 0) return "image/png";
        if (strcmp(last_dot, ".pdf") == 0) return "application/pdf";
        if (strcmp(last_dot, ".svg") == 0) return "image/svg+xml";
        if (strcmp(last_dot, ".txt") == 0) return "text/plain";
    }

    return "application/octet-stream";
}

/* Create a socket */
SOCKET create_socket(const char* host, const char *port)
{
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *bind_address;
    getaddrinfo(host, port, &hints, &bind_address);

    printf("Creating socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family,
            bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Binding socket to local address...\n");
    if (bind(socket_listen,
                bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    printf("Listening...\n");
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return socket_listen;
}

/* Maximum file upload size is 10 KB */
#define MAX_REQUEST_SIZE 10000

/* A structure to store the clients' information */
struct client_info {
    socklen_t address_length;
    struct sockaddr_storage address;
    char address_buffer[128];
    SOCKET socket;
    char request[MAX_REQUEST_SIZE + 1];
    int received;   /* total length of a request */
    struct client_info *next;
};

/* Create a new client */
struct client_info *get_client()
{
    struct client_info *n =
        (struct client_info*) calloc(1, sizeof(struct client_info));

    if (!n) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    n->address_length = sizeof(n->address);
    return n;
}

/* Let a client drop */
void drop_client(struct client_info *client)
{
    CLOSESOCKET(client->socket);
    fprintf(stderr, "drop client not found.\n");
    exit(1);
}

/* Get the address of a client */
const char *get_client_address(struct client_info *ci)
{
    getnameinfo((struct sockaddr*) &ci->address,
            ci->address_length,
            ci->address_buffer, sizeof(ci->address_buffer), 0, 0,
            NI_NUMERICHOST);
    return ci->address_buffer;
}

/* Send "400 Bad Request" status code */
void send_400(struct client_info *client)
{
    const char *c400 = "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Length: 11\r\n\r\nBad Request";
    send(client->socket, c400, strlen(c400), 0);
    drop_client(client);
}

/* Send "404 Not Found" status code */
void send_404(struct client_info *client)
{
    const char *c404 = "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Length: 9\r\n\r\nNot Found";
    send(client->socket, c404, strlen(c404), 0);
    drop_client(client);
}

/* Create http response header */
void serve_resource(struct client_info *client, const char *path)
{
    printf("serve_resource %s %s\n", get_client_address(client), path);

    if (strcmp(path, "/") == 0)
        path = "/index.html";
    if (strlen(path) > 100) {
        send_400(client);
        return;
    }
    if (strstr(path, "..")) {
        send_404(client);
        return;
    }

    char full_path[128];
    sprintf(full_path, "public%s", path);

#if defined(_WIN32)
    char *p = full_path;
    while (*p) {
        if (*p == '/') *p = '\\';
        ++p;
    }
#endif

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        send_404(client);
        return;
    }
    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);  /* cl: content length */
    rewind(fp);

    const char *ct = get_content_type(full_path);   /* ct: content type */

#define BSIZE 1024
    char buffer[BSIZE];

    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);
    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Length: %u\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    int r = fread(buffer, 1, BSIZE, fp);
    while (r) {
        send(client->socket, buffer, r, 0);
        r = fread(buffer, 1, BSIZE, fp);
    }

    fclose(fp);
    drop_client(client);
}

/* Get "Content-Length" of a request */
int get_content_len(struct client_info *client)
{
    char *cl = strstr(client->request, "Content-Length: ");
    if (cl)
        return atoi(cl + 15);
    else
        return 0;
}

/* Get the entity body of a request */
void get_body(char *buf, struct client_info *client, int cl)
{
    char *header_end = strstr(client->request, "\r\n\r\n");
    int buflen = client->received - (header_end + 4 - client->request); /* length of body */
    memcpy(buf, header_end + 4, client->received - (header_end + 4 - client->request));
    
    while (buflen < cl) {
        if (MAX_REQUEST_SIZE == client->received) { /* if the file is too large */
            send_400(client);
            continue;
        }

        int r = recv(client->socket, buf + buflen, MAX_REQUEST_SIZE - buflen, 0);
        if (r < 1) {
            printf("Unexpected disconnect from %s.\n", get_client_address(client));
            drop_client(client);
        }
        else {
            buflen += r;
            buf[buflen] = 0;
        }
    }
    return;
}

/* To understand which method does the request message use */
void handle_request(struct client_info *client, char *request)
{
    /* if GET method is used */
    if (!strncmp("GET /", request, 5)) {
        char *path = request + 4;
        char *end_path = strstr(path, " ");
        if (!end_path) {
            send_400(client);
        }
        else {
            *end_path = 0;
            serve_resource(client, path);
        }
    }

    /* if POST method is used */
    else if (!strncmp("POST /", request, 6)) {
        char *boundary = (char *) malloc(sizeof(char) * 70);
        char *data_start, *data_end;
        char filename[300] = {0};
        char buffer[MAX_REQUEST_SIZE + 1] = {0};
        int content_len = get_content_len(client);

        get_body(buffer, client, content_len);

        char *body_info_end = strstr(buffer, "\r\n\r\n");
        char *boundary_end = strstr(buffer, "\r\n");

        strcpy(boundary, "\r\n");
        strncat(boundary, buffer, boundary_end - buffer);
        strcat(boundary, "--\r\n");

        data_start = body_info_end + 4;
        char *fn_ptr = strstr(buffer, "filename=\"") + 10;
        char *fn_end = strstr(fn_ptr, "\"");
        *fn_end = 0;
        printf("Uploaded %s\n", fn_ptr);

        strcpy(filename, "uploaded_files/");
        strncat(filename, fn_ptr, fn_end - fn_ptr);
        FILE* fp = fopen(filename, "w");
        fwrite(data_start, sizeof(char), content_len - (data_start - buffer) - strlen(boundary), fp);
        fclose(fp);
        
        serve_resource(client, "/success.html");    /* After uploading, open "success.html" */
        drop_client(client);
        
        free(boundary);
    }
    
    /* if error */
    else {
        send_400(client);
    }
    return;
}

int main()
{

#if defined(_WIN32)
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }
#endif

    SOCKET server = create_socket(0, "8080");
    struct client_info *client = get_client();

    pid_t pid;
    signal(SIGCHLD, SIG_IGN);

    while (1) {
        struct client_info *client = get_client();
        client->socket = accept(server, (struct sockaddr*) &(client->address), &(client->address_length));
        if (!ISVALIDSOCKET(client->socket)) {
            fprintf(stderr, "accept() failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }
        printf("New connection from %s\n", get_client_address(client));
        
        if ((pid = fork()) < 0) {   /* create child process */
            fprintf(stderr, "fork() failed.\n");
            return 1;
        }
        else {
            if (pid == 0) { /* child process */
                CLOSESOCKET(server);
                while (client) {
                    if (MAX_REQUEST_SIZE == client->received) {
                        send_400(client);
                        continue;
                    }

                    int r = recv(client->socket, client->request + client->received, MAX_REQUEST_SIZE - client->received, 0);
                    if (r < 1) {
                        printf("Unexpected disconnect from %s\n", get_client_address(client));
                        drop_client(client);
                    }
                    else {
                        client->received += r;
                        client->request[client->received] = 0;

                        char *q = strstr(client->request, "\r\n\r\n");
                        if (q) {
                            handle_request(client, client->request);
                        }
                    }
                }
                return 0;
            }
            else {  /* parent process */
                CLOSESOCKET(client->socket);
            }
        }
    } /* while(1) */

    printf("\nClosing socket...\n");
    CLOSESOCKET(server);

#if defined(_WIN32)
    WSACleanup();
#endif

    printf("Finished.\n");
    return 0;
}
