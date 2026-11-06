#ifndef CHAT_H
#define CHAT_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

/* Maximum concurrent clients allowed */
#define MAX_CLIENTS 10

/* Buffer sizes */
#define BUFFER_SZ 2048
#define NAME_LEN 32

/* Default network parameters */
#define DEFAULT_PORT 8888
#define DEFAULT_IP "127.0.0.1"

/* ANSI Terminal Colors */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"

/* Client structure tracked in shared memory across worker threads */
typedef struct {
    struct sockaddr_in address; /* Client socket address (IP, Port) */
    int sockfd;                 /* Client socket file descriptor */
    int uid;                    /* Unique identifier assigned by server */
    char name[NAME_LEN];        /* User's registered display name */
    pthread_t thread_id;        /* POSIX thread identifier */
    pid_t tid;                  /* Linux Kernel Thread ID (LWP) via SYS_gettid */
} client_t;

/* Utility function: Trim trailing newline and carriage return */
static inline void str_trim_lf(char *arr, int length) {
    for (int i = 0; i < length; i++) {
        if (arr[i] == '\n' || arr[i] == '\r') {
            arr[i] = '\0';
            break;
        }
    }
}

#endif /* CHAT_H */
