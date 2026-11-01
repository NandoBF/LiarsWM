#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: liarsctl <command> [args...]\n");
        return 1;
    }

    const char* path = getenv("LIARSWM_SOCKET");
    if (!path) {
        fprintf(stderr, "LIARSWM_SOCKET not set. Is liars running?\n");
        return 1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    char cmd[512] = {0};
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }

    write(fd, cmd, strlen(cmd));

    char buf[4096]; // too much?
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return 0;
}
