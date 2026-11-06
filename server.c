#include "chat.h"

/*
 * ============================================================================
 * MODULE: Multi-Threaded TCP Chat Coordination Server
 * ============================================================================
 *
 * Core Linux Systems Programming Concepts:
 * 1. Process vs. Thread (Kernel Perspective):
 *    - In the Linux kernel, both processes and threads are represented by the
 *      same structure: `struct task_struct` (Light Weight Process - LWP).
 *    - The Server process has one Process ID (PID / TGID - Thread Group ID).
 *    - The Main Listener thread accepts connections on port 8888.
 *    - For every accepted client connection, the server spawns a dedicated
 *      POSIX Worker Thread via `pthread_create()`.
 *    - Each worker thread has its own private execution stack and unique
 *      kernel Thread ID (TID) retrieved via `syscall(SYS_gettid)`.
 *
 * 2. Shared Memory & File Descriptor Table:
 *    - Threads created via `pthread_create()` share the same virtual memory
 *      address space (Heap, Data, BSS) and the same File Descriptor Table.
 *    - This allows all worker threads to easily access the shared `clients[]`
 *      registry and write directly to each other's client socket descriptors.
 *
 * 3. Race Conditions & Synchronization (POSIX Mutex):
 *    - Because multiple worker threads execute concurrently on multi-core CPUs,
 *      concurrently reading/writing to `clients[]` or `client_count` without
 *      protection would cause memory corruption and race conditions.
 *    - We use `pthread_mutex_t clients_mutex` to enforce Mutual Exclusion
 *      around all critical sections (queue_add, queue_remove, broadcast, list).
 *
 * 4. Linux Kernel Inspection Commands:
 *    While the server is running with active clients:
 *      $ ps -eLf -q <PID>             # Displays all LWPs (threads) under the PID
 *      $ top -H -p <PID>              # Real-time thread activity monitor
 *      $ ls -l /proc/<PID>/task/      # Lists directory for each kernel TID
 *      $ cat /proc/<PID>/status | grep Threads
 * ============================================================================
 */

/* Global thread-shared state */
static client_t *clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static int client_count = 0;
static int uid_counter = 1000;
static int server_socket_fd = -1;

/* Add client to thread-safe active client array */
static int queue_add(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i]) {
            clients[i] = cl;
            client_count++;
            pthread_mutex_unlock(&clients_mutex);
            return 0; /* Success */
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1; /* Queue full */
}

/* Remove client from thread-safe active client array by UID */
static void queue_remove(int uid) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->uid == uid) {
            clients[i] = NULL;
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Check if a username is already taken by another active user */
static int is_username_taken(const char *name) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strlen(clients[i]->name) > 0) {
            if (strcasecmp(clients[i]->name, name) == 0) {
                pthread_mutex_unlock(&clients_mutex);
                return 1;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

/* Broadcast message to all connected clients except sender_uid */
static void broadcast_message(const char *msg, int sender_uid) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->uid != sender_uid) {
            if (send(clients[i]->sockfd, msg, strlen(msg), 0) < 0) {
                perror("[SERVER ERROR] send() failed in broadcast");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Send a private direct message from one client to another */
static void send_private_message(const char *msg, const char *target_name, client_t *sender) {
    int found = 0;
    char formatted_msg[BUFFER_SZ + 64];

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strcasecmp(clients[i]->name, target_name) == 0) {
            found = 1;
            /* Send to recipient */
            snprintf(formatted_msg, sizeof(formatted_msg),
                     COLOR_MAGENTA "[Private from %s]: %s\r\n" COLOR_RESET,
                     sender->name, msg);
            send(clients[i]->sockfd, formatted_msg, strlen(formatted_msg), 0);

            /* Send confirmation to sender */
            snprintf(formatted_msg, sizeof(formatted_msg),
                     COLOR_MAGENTA "[Private to %s]: %s\r\n" COLOR_RESET,
                     target_name, msg);
            send(sender->sockfd, formatted_msg, strlen(formatted_msg), 0);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (!found) {
        snprintf(formatted_msg, sizeof(formatted_msg),
                 COLOR_RED "[ERROR] User '%s' is not online.\r\n" COLOR_RESET,
                 target_name);
        send(sender->sockfd, formatted_msg, strlen(formatted_msg), 0);
    }
}

/* Send list of online users to the requesting client */
static void send_user_list(client_t *sender) {
    char list_buf[BUFFER_SZ];
    int offset = 0;

    pthread_mutex_lock(&clients_mutex);
    offset += snprintf(list_buf + offset, sizeof(list_buf) - offset,
                       COLOR_CYAN "=== Online Users (%d/%d) ===\r\n" COLOR_RESET,
                       client_count, MAX_CLIENTS);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strlen(clients[i]->name) > 0) {
            if (clients[i]->uid == sender->uid) {
                offset += snprintf(list_buf + offset, sizeof(list_buf) - offset,
                                   "  - %s (You) [Worker TID: %d]\r\n",
                                   clients[i]->name, clients[i]->tid);
            } else {
                offset += snprintf(list_buf + offset, sizeof(list_buf) - offset,
                                   "  - %s [Worker TID: %d]\r\n",
                                   clients[i]->name, clients[i]->tid);
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    offset += snprintf(list_buf + offset, sizeof(list_buf) - offset,
                       COLOR_CYAN "============================\r\n" COLOR_RESET);
    send(sender->sockfd, list_buf, strlen(list_buf), 0);
}

/* Send Linux kernel thread details to the requesting client */
static void send_threads_info(client_t *sender) {
    char info_buf[BUFFER_SZ];
    pthread_mutex_lock(&clients_mutex);
    snprintf(info_buf, sizeof(info_buf),
             COLOR_YELLOW "=== Linux Kernel Thread Info ===\r\n"
             "  Server Process PID (TGID)  : %d\r\n"
             "  Your Handler Thread (TID)  : %d (LWP)\r\n"
             "  Active Worker Threads      : %d (Max: %d)\r\n"
             "  Total Threads in Server    : %d (1 Listener + %d Workers)\r\n"
             "  Shell Inspection Command   : ps -eLf -q %d\r\n"
             "================================\r\n" COLOR_RESET,
             getpid(), sender->tid, client_count, MAX_CLIENTS,
             client_count + 1, client_count, getpid());
    pthread_mutex_unlock(&clients_mutex);
    send(sender->sockfd, info_buf, strlen(info_buf), 0);
}

/* Send command help instructions to the requesting client */
static void send_help_message(int sockfd) {
    const char *help_msg =
        COLOR_YELLOW "Available Commands:\r\n"
        "  /list               - List all active users online\r\n"
        "  /msg <user> <text>  - Send a private direct message\r\n"
        "  /threads (or /info) - View Linux kernel thread diagnostics\r\n"
        "  /help               - Display this help information\r\n"
        "  /quit               - Exit the chat room\r\n" COLOR_RESET;
    send(sockfd, help_msg, strlen(help_msg), 0);
}

/* Worker thread function: handles communication with one client */
static void *handle_client(void *arg) {
    client_t *cli = (client_t *)arg;
    char buffer[BUFFER_SZ];
    char name_buffer[NAME_LEN];
    int leave_flag = 0;

    /* Record Kernel Thread ID (LWP) */
    cli->tid = (pid_t)syscall(SYS_gettid);
    printf("[THREAD SPAWNED] Worker Thread TID = %d (LWP) for socket FD %d | Server TGID = %d\n",
           cli->tid, cli->sockfd, getpid());

    /* 1. Receive client's username handshake */
    int r = recv(cli->sockfd, name_buffer, sizeof(name_buffer) - 1, 0);
    if (r <= 0) {
        printf("[SERVER] Failed to receive username from %s:%d\n",
               inet_ntoa(cli->address.sin_addr), ntohs(cli->address.sin_port));
        leave_flag = 1;
    } else {
        name_buffer[r] = '\0';
        str_trim_lf(name_buffer, strlen(name_buffer));

        if (strlen(name_buffer) < 2 || strlen(name_buffer) >= (NAME_LEN - 1)) {
            char *err = COLOR_RED "[ERROR] Name must be between 2 and 30 characters. Disconnecting.\r\n" COLOR_RESET;
            send(cli->sockfd, err, strlen(err), 0);
            leave_flag = 1;
        } else if (is_username_taken(name_buffer)) {
            char *err = COLOR_RED "[ERROR] Username already in use. Please choose another name. Disconnecting.\r\n" COLOR_RESET;
            send(cli->sockfd, err, strlen(err), 0);
            leave_flag = 1;
        } else {
            strncpy(cli->name, name_buffer, NAME_LEN - 1);
            cli->name[NAME_LEN - 1] = '\0';

            /* Send welcome header to new client */
            char welcome[BUFFER_SZ];
            snprintf(welcome, sizeof(welcome),
                     COLOR_GREEN "=== Welcome to TCP Chat Application, %s! ===\r\n"
                     "Commands: /list, /msg <user> <msg>, /threads, /help, /quit\r\n"
                     "Connected users: %d/%d (Worker Thread TID: %d)\r\n"
                     "===============================================\r\n" COLOR_RESET,
                     cli->name, client_count, MAX_CLIENTS, cli->tid);
            send(cli->sockfd, welcome, strlen(welcome), 0);

            /* Announce new user to everyone else */
            char join_announcement[BUFFER_SZ];
            snprintf(join_announcement, sizeof(join_announcement),
                     COLOR_GREEN "[SERVER] %s has joined the chat room. (Online: %d/%d)\r\n" COLOR_RESET,
                     cli->name, client_count, MAX_CLIENTS);
            printf("[JOIN] %s (UID %d, Worker TID %d) connected from %s:%d. Total online: %d/%d\n",
                   cli->name, cli->uid, cli->tid, inet_ntoa(cli->address.sin_addr),
                   ntohs(cli->address.sin_port), client_count, MAX_CLIENTS);
            broadcast_message(join_announcement, cli->uid);
        }
    }

    /* 2. Main message listening loop */
    while (!leave_flag) {
        int receive = recv(cli->sockfd, buffer, sizeof(buffer) - 1, 0);
        if (receive > 0) {
            buffer[receive] = '\0';
            str_trim_lf(buffer, strlen(buffer));

            /* Skip empty lines */
            if (strlen(buffer) == 0) {
                continue;
            }

            /* Check for commands */
            if (strcasecmp(buffer, "/quit") == 0 || strcasecmp(buffer, "/exit") == 0) {
                char bye[] = COLOR_YELLOW "[SERVER] Goodbye!\r\n" COLOR_RESET;
                send(cli->sockfd, bye, strlen(bye), 0);
                leave_flag = 1;
            } else if (strcasecmp(buffer, "/list") == 0) {
                send_user_list(cli);
            } else if (strcasecmp(buffer, "/threads") == 0 || strcasecmp(buffer, "/info") == 0) {
                send_threads_info(cli);
            } else if (strcasecmp(buffer, "/help") == 0) {
                send_help_message(cli->sockfd);
            } else if (strncasecmp(buffer, "/msg ", 5) == 0) {
                /* Format: /msg <target> <message> */
                char *target = buffer + 5;
                while (*target == ' ') target++; /* Skip spaces */
                char *msg_body = strchr(target, ' ');
                if (msg_body) {
                    *msg_body = '\0';
                    msg_body++;
                    while (*msg_body == ' ') msg_body++;
                    if (strlen(msg_body) > 0) {
                        send_private_message(msg_body, target, cli);
                    } else {
                        char err[] = COLOR_RED "Usage: /msg <username> <message>\r\n" COLOR_RESET;
                        send(cli->sockfd, err, strlen(err), 0);
                    }
                } else {
                    char err[] = COLOR_RED "Usage: /msg <username> <message>\r\n" COLOR_RESET;
                    send(cli->sockfd, err, strlen(err), 0);
                }
            } else {
                /* Public broadcast message */
                char broadcast_buf[BUFFER_SZ + 64];
                snprintf(broadcast_buf, sizeof(broadcast_buf),
                         COLOR_CYAN "[%s]" COLOR_RESET ": %s\r\n",
                         cli->name, buffer);
                printf("[MSG from %s | TID %d]: %s\n", cli->name, cli->tid, buffer);
                broadcast_message(broadcast_buf, cli->uid);
            }
        } else if (receive == 0 || errno == ECONNRESET) {
            /* Client disconnected cleanly or abruptly */
            leave_flag = 1;
        } else {
            perror("[SERVER ERROR] recv() error");
            leave_flag = 1;
        }
    }

    /* 3. Cleanup on client disconnection */
    printf("[THREAD EXITING] Worker Thread TID = %d exiting for %s (UID %d)\n",
           cli->tid, cli->name, cli->uid);

    close(cli->sockfd);
    queue_remove(cli->uid);

    if (strlen(cli->name) > 0) {
        char leave_announcement[BUFFER_SZ];
        snprintf(leave_announcement, sizeof(leave_announcement),
                 COLOR_YELLOW "[SERVER] %s has left the chat room. (Online: %d/%d)\r\n" COLOR_RESET,
                 cli->name, client_count, MAX_CLIENTS);
        printf("[LEAVE] %s (UID %d) disconnected. Total online: %d/%d\n",
               cli->name, cli->uid, client_count, MAX_CLIENTS);
        broadcast_message(leave_announcement, cli->uid);
    }

    free(cli);
    return NULL;
}

/* Graceful shutdown handler on SIGINT (Ctrl+C) */
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n" COLOR_RED "[SERVER] Shutting down... Closing all connections." COLOR_RESET "\n");

    pthread_mutex_lock(&clients_mutex);
    const char *shutdown_msg = COLOR_RED "\r\n[SERVER NOTICE] Server is shutting down. Goodbye!\r\n" COLOR_RESET;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            send(clients[i]->sockfd, shutdown_msg, strlen(shutdown_msg), 0);
            close(clients[i]->sockfd);
            free(clients[i]);
            clients[i] = NULL;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    if (server_socket_fd >= 0) {
        close(server_socket_fd);
    }

    printf("[SERVER] All sockets closed. Server stopped.\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s. Using default %d.\n", argv[1], DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }

    /* Set up signal handlers */
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN); /* Prevent crash if writing to closed socket */

    /* Create TCP socket */
    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0) {
        perror("[FATAL] socket() creation failed");
        return EXIT_FAILURE;
    }

    /* Set SO_REUSEADDR to avoid 'Address already in use' error on fast restarts */
    int optval = 1;
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("[WARNING] setsockopt(SO_REUSEADDR) failed");
    }

    /* Bind address to socket */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[FATAL] bind() failed");
        close(server_socket_fd);
        return EXIT_FAILURE;
    }

    /* Listen for incoming connections */
    if (listen(server_socket_fd, 10) < 0) {
        perror("[FATAL] listen() failed");
        close(server_socket_fd);
        return EXIT_FAILURE;
    }

    pid_t server_pid = getpid();
    pid_t listener_tid = (pid_t)syscall(SYS_gettid);

    printf(COLOR_CYAN "========================================================\n");
    printf("  🚀 MULTI-THREADED LINUX TCP CHAT SERVER STARTED\n");
    printf("  - Server Process PID (TGID) : %d\n", server_pid);
    printf("  - Main Listener Thread (TID): %d (LWP)\n", listener_tid);
    printf("  - Listening Port            : %d\n", port);
    printf("  - Max Clients Limit         : %d concurrent users\n", MAX_CLIENTS);
    printf("  - Threading Architecture    : 1 Listener Thread + N Worker Threads\n");
    printf("  - Live Thread Inspection    : ps -eLf -q %d\n", server_pid);
    printf("  - Press Ctrl+C to gracefully stop the server\n");
    printf("========================================================" COLOR_RESET "\n");

    /* Accept incoming connections in an infinite loop */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue; /* Interrupted by signal handler */
            perror("[SERVER ERROR] accept() failed");
            continue;
        }

        /* Check if server has reached maximum capacity of 10 clients */
        pthread_mutex_lock(&clients_mutex);
        if (client_count >= MAX_CLIENTS) {
            pthread_mutex_unlock(&clients_mutex);
            printf(COLOR_RED "[REJECTED] Connection from %s:%d rejected (Maximum %d clients reached)." COLOR_RESET "\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), MAX_CLIENTS);

            char *reject_msg = COLOR_RED "[SERVER ERROR] Room is full (Maximum 10 clients reached). Connection rejected.\r\n" COLOR_RESET;
            send(client_fd, reject_msg, strlen(reject_msg), 0);
            close(client_fd);
            continue;
        }
        pthread_mutex_unlock(&clients_mutex);

        /* Client accepted, allocate tracking structure */
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        if (!cli) {
            perror("[SERVER ERROR] malloc() failed for client_t");
            close(client_fd);
            continue;
        }

        cli->address = client_addr;
        cli->sockfd = client_fd;
        cli->uid = ++uid_counter;
        cli->tid = 0;
        memset(cli->name, 0, sizeof(cli->name));

        /* Add to active queue */
        if (queue_add(cli) < 0) {
            printf("[REJECTED] Queue full for %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            close(client_fd);
            free(cli);
            continue;
        }

        /* Spawn dedicated POSIX worker thread for client */
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void *)cli) != 0) {
            perror("[SERVER ERROR] pthread_create() failed");
            queue_remove(cli->uid);
            close(client_fd);
            free(cli);
            continue;
        }

        cli->thread_id = tid;

        /* Detach thread to automatically reclaim resources upon exit */
        pthread_detach(tid);
    }

    close(server_socket_fd);
    return EXIT_SUCCESS;
}
