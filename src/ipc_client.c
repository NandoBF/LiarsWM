#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int liarsctl_main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: liarsctl <command>\n");
        return 1;
    }

    const char* path = getenv("LIARSWM_SOCKET");
    if (!path) {
        fprintf(stderr, "Liarswm socket not found\n");
        return 1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    char cmd[512] = {0};
    for (int i = 0; i < argc; i++) {
        if (i > 1) strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }

    write(fd, cmd, strlen(cmd));

    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return 0;

}
