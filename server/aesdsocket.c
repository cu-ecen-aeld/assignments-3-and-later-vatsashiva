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
#include <sys/ioctl.h>
#include "aesd_ioctl.h"


#define PORT 9000

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
    #define FILE_PATH   "/dev/aesdchar"
#else
    #define FILE_PATH   "/var/tmp/aesdsocketdata"
#endif

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

#if !USE_AESD_CHAR_DEVICE
static pthread_t timestamp_tid;
#endif

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
    if (setsid() < 0) exit(1);
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

#if !USE_AESD_CHAR_DEVICE

/* Write timestamp to file */
static void write_timestamp(void)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char timebuf[128];
    strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S", &tm_info);

    pthread_mutex_lock(&file_mutex);
    int fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char line[160];
        int  n = snprintf(line, sizeof(line), "timestamp:%s\n", timebuf);
        write(fd, line, n);
        close(fd);
    }
    pthread_mutex_unlock(&file_mutex);
}

/* Timestamp thread */
static void *timestamp_thread(void *arg)
{
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_REALTIME, &next);

    /* next 10-second boundary */
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

#endif

/* Client thread */
static void *client_thread(void *arg)
{
    struct Thread *node     = arg;
    int            cfd      = node->client_fd;
    char          *accum    = NULL;
    size_t         accum_sz = 0;
    char           io_buf[BUF_SIZE];
    int            file_fd  = -1;
    char          *nl;

    /* Receive bytes until '\n'  */
 while (!exit_requested) {
    ssize_t bytes = recv(cfd, io_buf, sizeof(io_buf), 0);
    syslog(LOG_INFO, "RECV %ld bytes", bytes);
    
 if (bytes <= 0)
        break;

    char *tmp = realloc(accum, accum_sz + bytes);
    if (!tmp)
        goto cleanup;

    accum = tmp;
    memcpy(accum + accum_sz, io_buf, bytes);
    accum_sz += bytes;

    if (memchr(accum, '\n', accum_sz))
        break;
}

    if (!accum || accum_sz == 0) goto cleanup;

    /* Find newline and  write length */
    nl = memchr(accum, '\n', accum_sz);
    size_t write_len;

    if (nl) {
        write_len = (size_t)(nl - accum) + 1;
    } else {
        char *tmp = realloc(accum, accum_sz + 1);
        if (!tmp)
            goto cleanup;
        accum = tmp;
        accum[accum_sz] = '\n';
        accum_sz += 1;
        write_len = accum_sz;
    }

    /* Write and read all contents --------- */
    pthread_mutex_lock(&file_mutex);

#if USE_AESD_CHAR_DEVICE
    file_fd = open(FILE_PATH, O_RDWR);
#else
    file_fd = open(FILE_PATH, O_RDWR | O_CREAT | O_APPEND, 0644);
#endif

    if (file_fd < 0) {
        syslog(LOG_ERR, "open(%s): %s", FILE_PATH, strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        goto cleanup;
    }

    /* Write */

#if USE_AESD_CHAR_DEVICE
if (strncmp(accum, IOCTL_PREFIX, strlen(IOCTL_PREFIX)) == 0) {

    struct aesd_seekto seekto;

    if (sscanf(accum + strlen(IOCTL_PREFIX), "%u,%u",
               &seekto.write_cmd,
               &seekto.write_cmd_offset) != 2) {
        syslog(LOG_ERR, "Invalid IOCTL format");
        close(file_fd);
        pthread_mutex_unlock(&file_mutex);
        goto cleanup;
    }

    if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) < 0) {
        syslog(LOG_ERR, "ioctl failed: %s", strerror(errno));
        close(file_fd);
        pthread_mutex_unlock(&file_mutex);
        goto cleanup;
    }

} else
#endif
{
    /* Normal write path */
    size_t written = 0;
    while (written < write_len) {
        ssize_t n = write(file_fd, accum + written, write_len - written);
        if (n <= 0) {
            syslog(LOG_ERR, "write failed");
            close(file_fd);
            pthread_mutex_unlock(&file_mutex);
            goto cleanup;
        }
        written += n;
    }

#if !USE_AESD_CHAR_DEVICE
    /* Only for file mode */
    if (lseek(file_fd, 0, SEEK_SET) < 0) {
        syslog(LOG_ERR, "lseek failed");
    }
#endif
}

/* Read + send */
ssize_t r;
while ((r = read(file_fd, io_buf, sizeof(io_buf))) > 0) {

    syslog(LOG_INFO, "READ %ld bytes", r);

    size_t sent = 0;
    while (sent < r) {
        ssize_t s = send(cfd, io_buf + sent, r - sent, 0);
        if (s <= 0) {
            close(file_fd);
            pthread_mutex_unlock(&file_mutex);
            goto cleanup;
        }
        sent += s;
    }
}

    close(file_fd);
    file_fd = -1;
    pthread_mutex_unlock(&file_mutex);

cleanup:
    free(accum);

    if (file_fd >= 0) {
        close(file_fd);
    }

    shutdown(cfd, SHUT_WR);  
    close(cfd);

    node->completed = true;
    return NULL;
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-d") == 0) daemon_mode = 1;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

#if !USE_AESD_CHAR_DEVICE
    unlink(FILE_PATH);
#endif

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        return 1;
    }

    if (daemon_mode) daemonize();

#if !USE_AESD_CHAR_DEVICE
    pthread_create(&timestamp_tid, NULL, timestamp_thread, NULL);
#endif

    syslog(LOG_INFO, "aesdsocket listening on port %d → %s", PORT, FILE_PATH);

    /* Accept */
    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);

        int fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (fd < 0) {
            if (errno == EINTR && exit_requested) break;
            continue;
        }

        /* Log client IP */
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        struct Thread *node = calloc(1, sizeof(*node));
        if (!node) {
            close(fd);
            continue;
        }
        node->client_fd  = fd;
        node->completed  = false;

        if (pthread_create(&node->thread_id, NULL, client_thread, node) != 0) {
            syslog(LOG_ERR, "pthread_create: %s", strerror(errno));
            free(node);
            close(fd);
            continue;
        }

        node->next  = thread_list;
        thread_list = node;

       
        struct Thread **curr = &thread_list;
        while (*curr) {
            if ((*curr)->completed) {
                struct Thread *tmp = *curr;
                pthread_join(tmp->thread_id, NULL);
                *curr = tmp->next;
                free(tmp);
            } else {
                curr = &(*curr)->next;
            }
        }
    }

  
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }

    while (thread_list) {
        struct Thread *tmp = thread_list;
        pthread_join(tmp->thread_id, NULL);
        thread_list = tmp->next;
        free(tmp);
    }

#if !USE_AESD_CHAR_DEVICE
    pthread_join(timestamp_tid, NULL);
    unlink(FILE_PATH);
#endif

    pthread_mutex_destroy(&file_mutex);
    syslog(LOG_INFO, "aesdsocket exiting");
    closelog();

    return 0;
}
