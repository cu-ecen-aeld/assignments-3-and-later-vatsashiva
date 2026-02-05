#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUF_SIZE 1024

static int server_fd = -1;
static volatile sig_atomic_t exit_requested = 0;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

struct Thread {
    pthread_t thread_id;
    int client_fd;
    bool completed;
    struct Thread *next;
};

static struct Thread *thread_list = NULL;
static pthread_t timestamp_tid;

/* Signal handler */
static void handle_signal(int sig)
{
    (void)sig;
    exit_requested = 1;
    if (server_fd != -1)
        close(server_fd);
}

/* Daemonize */
static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

/* Write timestamp to file */
static void write_timestamp(void)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char timebuf[128];
    strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S", &tm_info);

    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(FILE_PATH, "a");
    if (fp) {
        fprintf(fp, "timestamp:%s\n", timebuf);
        fclose(fp);
    }
    pthread_mutex_unlock(&file_mutex);
}

/* Timestamp thread */
static void *timestamp_thread(void *arg)
{
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_REALTIME, &next);

    
    write_timestamp();

    // next 10-second 
    next.tv_sec = ((next.tv_sec / 10) + 1) * 10;
    next.tv_nsec = 0;

    while (!exit_requested) {
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next, NULL);
        } while (rc == EINTR && !exit_requested);

        if (exit_requested) break;
        write_timestamp();
        next.tv_sec += 10;
    }
    return NULL;
}

/* Client thread */
static void *client_thread(void *arg)
{
    struct Thread *node = arg;
    char buf[BUF_SIZE];
    int newline_found = 0;

    FILE *fp = fopen(FILE_PATH, "a+");
    if (!fp) {
        close(node->client_fd);
        node->completed = true;
        return NULL;
    }

    /* Receive data and write */
    while (!newline_found && !exit_requested) {
        ssize_t bytes = recv(node->client_fd, buf, sizeof(buf), 0);
        if (bytes <= 0) break;

        for (ssize_t i = 0; i < bytes; i++)
            if (buf[i] == '\n') { newline_found = 1; break; }

        pthread_mutex_lock(&file_mutex);
        fwrite(buf, 1, bytes, fp);
        fflush(fp);
        pthread_mutex_unlock(&file_mutex);
    }

    /* Read file and send to client */
    pthread_mutex_lock(&file_mutex);
    fseek(fp, 0, SEEK_SET);
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0)
        send(node->client_fd, buf, r, 0);
    pthread_mutex_unlock(&file_mutex);

    fclose(fp);
    close(node->client_fd);
    node->completed = true;
    return NULL;
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    struct sockaddr_in addr;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-d") == 0) daemon_mode = 1;

    if (daemon_mode) openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    unlink(FILE_PATH);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return 1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return 1;
    if (listen(server_fd, 10) < 0) return 1;

    if (daemon_mode) daemonize();

    pthread_create(&timestamp_tid, NULL, timestamp_thread, NULL);

    while (!exit_requested) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd < 0) { if (errno == EINTR && exit_requested) break; else continue; }

        struct Thread *node = calloc(1, sizeof(*node));
        node->client_fd = fd;
        node->completed = false;
        pthread_create(&node->thread_id, NULL, client_thread, node);

        node->next = thread_list;
        thread_list = node;

        /* Clean completed threads */
        struct Thread **curr = &thread_list;
        while (*curr) {
            if ((*curr)->completed) {
                pthread_join((*curr)->thread_id, NULL);
                struct Thread *tmp = *curr;
                *curr = tmp->next;
                free(tmp);
            } else {
                curr = &(*curr)->next;
            }
        }
    }

    if (server_fd != -1) close(server_fd);
    pthread_join(timestamp_tid, NULL);

    while (thread_list) {
        pthread_join(thread_list->thread_id, NULL);
        struct Thread *tmp = thread_list;
        thread_list = tmp->next;
        free(tmp);
    }

    unlink(FILE_PATH);
    if (daemon_mode) closelog();

    return 0;
}

