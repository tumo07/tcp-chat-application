#include "chat.h"

/*
 * ============================================================================
 * MODULE: Multi-Threaded Full-Duplex TCP Chat Client
 * ============================================================================
 *
 * Core Linux Systems Programming Concepts:
 * 1. Why Multi-Threading is Needed in a Network Client:
 *    - In a command-line chat application, the client must simultaneously:
 *        a) Wait for user keyboard input on standard input (blocking fgets).
 *        b) Wait for incoming network packets from the server (blocking recv).
 *    - If single-threaded with blocking I/O, the program would hang on fgets()
 *      and miss incoming messages until the user pressed Enter!
 *    - Solution: Multi-Threading! We divide duties into two concurrent POSIX threads:
 *        1) `send_msg_handler` (Keyboard Reader Thread)
 *        2) `recv_msg_handler` (Network Receiver Thread)
 *
 * 2. Shared Memory & Communication Between Client Threads:
 *    - Both threads share the process memory space and the socket file descriptor `sockfd`.
 *    - A shared volatile signal variable `stop_flag` allows either thread
 *      (or the SIGINT signal handler) to signal program termination to the other.
 *
 * 3. Inspecting Threads in Linux:
 *    While running this client in a terminal:
 *      $ ps -eLf -q <PID>             # Displays both LWP threads under this client PID
 *      $ ls -l /proc/<PID>/task/      # Shows 2 tasks (Send and Recv threads)
 * ============================================================================
 */

/* Global client variables */
static volatile sig_atomic_t stop_flag = 0;
static int sockfd = -1;
static char name[NAME_LEN];

/* Handle Ctrl+C (SIGINT) */
static void catch_ctrl_c_and_exit(int sig) {
    (void)sig;
    stop_flag = 1;
}

/* Thread function 1: Sending user input to server */
static void *send_msg_handler(void *arg) {
    (void)arg;
    char buffer[BUFFER_SZ];
    pid_t send_tid = (pid_t)syscall(SYS_gettid);
    (void)send_tid;

    while (!stop_flag) {
        printf("> ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin)) {
            /* EOF reached (e.g. Ctrl+D) */
            stop_flag = 1;
            break;
        }

        str_trim_lf(buffer, strlen(buffer));

        /* If user typed nothing, re-prompt */
        if (strlen(buffer) == 0) {
            continue;
        }

        /* Check if user wants to quit */
        if (strcasecmp(buffer, "/quit") == 0 || strcasecmp(buffer, "/exit") == 0) {
            send(sockfd, buffer, strlen(buffer), 0);
            stop_flag = 1;
            break;
        }

        /* Send message/command to server */
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            if (!stop_flag) {
                perror("[ERROR] send() failed");
            }
            stop_flag = 1;
            break;
        }
    }
    return NULL;
}

/* Thread function 2: Receiving messages from server */
static void *recv_msg_handler(void *arg) {
    (void)arg;
    char message[BUFFER_SZ];
    pid_t recv_tid = (pid_t)syscall(SYS_gettid);
    (void)recv_tid;

    while (!stop_flag) {
        int r = recv(sockfd, message, sizeof(message) - 1, 0);
        if (r > 0) {
            message[r] = '\0';
            /* Erase current prompt line and print incoming message */
            printf("\r\33[2K%s> ", message);
            fflush(stdout);
        } else if (r == 0) {
            printf("\r\33[2K" COLOR_YELLOW "[NOTICE] Disconnected from server." COLOR_RESET "\n");
            stop_flag = 1;
            break;
        } else {
            if (stop_flag) break;
            if (errno == EINTR) continue;
            perror("\r\33[2K[ERROR] recv() error");
            stop_flag = 1;
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    const char *server_ip = DEFAULT_IP;
    int port = DEFAULT_PORT;

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s. Using default %d.\n", argv[2], DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }

    signal(SIGINT, catch_ctrl_c_and_exit);
    signal(SIGPIPE, SIG_IGN);

    pid_t client_pid = getpid();

    /* Get username from user */
    printf(COLOR_CYAN "========================================================\n");
    printf("         💬 MULTI-THREADED TCP CHAT CLIENT\n");
    printf("  - Client PID (TGID)       : %d\n", client_pid);
    printf("  - Target Server           : %s:%d\n", server_ip, port);
    printf("  - Threading Architecture  : 2 POSIX Threads (Send + Recv)\n");
    printf("========================================================" COLOR_RESET "\n");

    while (1) {
        printf("Enter your username (2-30 characters): ");
        if (!fgets(name, sizeof(name), stdin)) {
            return EXIT_SUCCESS;
        }
        str_trim_lf(name, strlen(name));

        if (strlen(name) < 2) {
            printf(COLOR_RED "Username too short! Please enter at least 2 characters.\n" COLOR_RESET);
            continue;
        }
        if (strlen(name) >= (NAME_LEN - 1)) {
            printf(COLOR_RED "Username too long! Maximum 30 characters.\n" COLOR_RESET);
            continue;
        }
        break;
    }

    /* Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[FATAL] socket() failed");
        return EXIT_FAILURE;
    }

    /* Configure server address */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("[FATAL] Invalid server IP address format");
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Connect to server */
    printf("[CONNECTING] Connecting to server %s:%d...\n", server_ip, port);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[FATAL] connect() to server failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Send username as handshake */
    if (send(sockfd, name, strlen(name), 0) < 0) {
        perror("[FATAL] Failed to send username to server");
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Spawn send and receive POSIX threads */
    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, send_msg_handler, NULL) != 0) {
        perror("[FATAL] pthread_create() for send handler failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, recv_msg_handler, NULL) != 0) {
        perror("[FATAL] pthread_create() for receive handler failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Wait for exit flag */
    while (!stop_flag) {
        usleep(100000); /* 100ms sleep */
    }

    /* Cleanup */
    if (sockfd >= 0) {
        /* Send quit notification */
        send(sockfd, "/quit", 5, 0);
        close(sockfd);
    }

    pthread_cancel(send_msg_thread);
    pthread_cancel(recv_msg_thread);
    pthread_join(send_msg_thread, NULL);
    pthread_join(recv_msg_thread, NULL);

    printf("\n" COLOR_GREEN "Exited chat application. Goodbye, %s!" COLOR_RESET "\n", name);
    return EXIT_SUCCESS;
}
