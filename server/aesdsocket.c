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

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUF_SIZE 1024

static int server_fd = -1;
static int client_fd = -1;
static volatile sig_atomic_t exit_requested = 0;

/* Signal handler */
static void handle_signal(int sig)
{
    (void)sig;
    exit_requested = 1;
}

/* Daemonize the process (single fork) */
static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", strerror(errno));
        exit(1);
    }

    if (pid > 0) {
        /* Parent exits, child continues */
        exit(0);
    }

    umask(0);

    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Redirect stdin/stdout/stderr to /dev/null */
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    struct sockaddr_in addr;
    char buf[BUF_SIZE];

    /* Check for -d argument (daemon mode) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
            break;
        }
    }

    if (daemon_mode)
        openlog("aesdsocket", LOG_PID, LOG_USER);

    /* Install signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Remove old data file */
    unlink(FILE_PATH);

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        if (daemon_mode)
            syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        else
            perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* Bind before daemonizing (assignment requirement) */
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (daemon_mode)
            syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        else
            perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        if (daemon_mode)
            syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        else
            perror("listen");
        close(server_fd);
        return 1;
    }

    /* Daemonize AFTER bind/listen if -d */
    if (daemon_mode)
        daemonize();

    if (daemon_mode)
        syslog(LOG_INFO, "Server listening on port %d", PORT);
    else
        printf("Server listening on port %d\n", PORT);

    /* Main server loop */
    while (!exit_requested) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR && exit_requested)
                break;
            if (daemon_mode)
                syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            else
                perror("accept");
            continue;
        }

        FILE *fp = fopen(FILE_PATH, "a+");
        if (!fp) {
            if (daemon_mode)
                syslog(LOG_ERR, "fopen failed: %s", strerror(errno));
            else
                perror("fopen");
            close(client_fd);
            client_fd = -1;
            continue;
        }

        int newline_found = 0;

        while (!newline_found && !exit_requested) {
            ssize_t bytes = recv(client_fd, buf, sizeof(buf), 0);
            if (bytes < 0) {
                if (errno == EINTR)
                    continue;
                if (daemon_mode)
                    syslog(LOG_ERR, "recv failed: %s", strerror(errno));
                else
                    perror("recv");
                break;
            }
            if (bytes == 0)
                break;

            for (ssize_t i = 0; i < bytes; i++) {
                if (buf[i] == '\n') {
                    newline_found = 1;
                    break;
                }
            }

            fwrite(buf, 1, bytes, fp);
            fflush(fp);
        }

        /* Send entire file back */
        fseek(fp, 0, SEEK_SET);
        while (!feof(fp) && !exit_requested) {
            size_t r = fread(buf, 1, sizeof(buf), fp);
            if (r > 0)
                send(client_fd, buf, r, 0);
        }

        fclose(fp);
        close(client_fd);
        client_fd = -1;
    }

    /* Cleanup */
    if (client_fd != -1)
        close(client_fd);
    if (server_fd != -1)
        close(server_fd);

    unlink(FILE_PATH);

    if (daemon_mode)
        syslog(LOG_INFO, "Caught signal, exiting");
    else
        printf("Caught signal, exiting\n");

    if (daemon_mode)
        closelog();

    return 0;
}

