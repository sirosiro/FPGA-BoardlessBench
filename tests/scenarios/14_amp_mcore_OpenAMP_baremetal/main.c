#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int main(void) {
    printf("[A-Core App] Opening /dev/rpmsg0...\n");
    int fd = open("/dev/rpmsg0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/rpmsg0");
        return 1;
    }
    printf("[A-Core App] Successfully opened /dev/rpmsg0 (fd: %d)\n", fd);

    const char *msg = "Hello OpenAMP from A-Core!";
    printf("[A-Core App] Sending message: '%s'\n", msg);
    ssize_t written = write(fd, msg, strlen(msg));
    if (written < 0) {
        perror("Failed to write to /dev/rpmsg0");
        close(fd);
        return 1;
    }

    printf("[A-Core App] Reading echo response...\n");
    char buf[256];
    memset(buf, 0, sizeof(buf));
    ssize_t read_bytes = read(fd, buf, sizeof(buf) - 1);
    if (read_bytes < 0) {
        perror("Failed to read from /dev/rpmsg0");
        close(fd);
        return 1;
    }

    printf("[A-Core App] Received response: '%s'\n", buf);

    /* Verify response starts with "[Baremetal Echo]" and contains the original message */
    if (strstr(buf, "[Baremetal Echo]") != NULL && strstr(buf, msg) != NULL) {
        printf("[A-Core App] Verification SUCCESS!\n");
        close(fd);
        return 0;
    } else {
        printf("[A-Core App] Verification FAILURE: Unexpected response content.\n");
        close(fd);
        return 2;
    }
}
