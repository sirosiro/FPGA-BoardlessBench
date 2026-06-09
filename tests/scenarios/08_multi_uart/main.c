#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>

void configure_raw_mode(int fd) {
    struct termios options;
    tcgetattr(fd, &options);
    cfmakeraw(&options);
    tcsetattr(fd, TCSANOW, &options);
}

int main() {
    printf("--- Multi-UART Simultaneous Open Test Start ---\n");

    printf("[App] Opening /dev/ttyPS0...\n");
    int fd0 = open("/dev/ttyPS0", O_RDWR | O_NOCTTY);
    if (fd0 < 0) {
        perror("open /dev/ttyPS0 failed");
        return 1;
    }
    configure_raw_mode(fd0);

    printf("[App] Opening /dev/ttyPS1...\n");
    int fd1 = open("/dev/ttyPS1", O_RDWR | O_NOCTTY);
    if (fd1 < 0) {
        perror("open /dev/ttyPS1 failed");
        close(fd0);
        return 1;
    }
    configure_raw_mode(fd1);

    printf("[App] Both UART ports opened successfully (fd0=%d, fd1=%d).\n", fd0, fd1);

    const char *msg0 = "\r\n[UART 1] Welcome to the FIRST UART port!\r\n";
    const char *msg1 = "\r\n[UART 2] Welcome to the SECOND UART port!\r\n";

    write(fd0, msg0, strlen(msg0));
    write(fd1, msg1, strlen(msg1));
    printf("[App] Sent greetings to both UART ports.\n");

    if (getenv("VFPGA_INTERACTIVE")) {
        printf("[App] Entering Interactive Mode. Send messages via Dashboard terminals.\n");
        printf("[App] Type 'exit' in UART 1 to end the test.\n");
        char buf0[128];
        char buf1[128];
        
        while (1) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(fd0, &readfds);
            FD_SET(fd1, &readfds);
            
            int max_fd = (fd0 > fd1) ? fd0 : fd1;
            struct timeval tv = {1, 0}; // 1s timeout
            
            int ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);
            if (ret > 0) {
                if (FD_ISSET(fd0, &readfds)) {
                    ssize_t n = read(fd0, buf0, sizeof(buf0) - 1);
                    if (n > 0) {
                        buf0[n] = '\0';
                        if (strstr(buf0, "exit")) {
                            write(fd0, "Exiting...\r\n", 12);
                            break;
                        }
                        // Echo to UART 1
                        write(fd0, "[UART 1 Echo]: ", 15);
                        write(fd0, buf0, n);
                        write(fd0, "\r\n", 2);
                    }
                }
                if (FD_ISSET(fd1, &readfds)) {
                    ssize_t n = read(fd1, buf1, sizeof(buf1) - 1);
                    if (n > 0) {
                        buf1[n] = '\0';
                        // Echo to UART 2
                        write(fd1, "[UART 2 Echo]: ", 15);
                        write(fd1, buf1, n);
                        write(fd1, "\r\n", 2);
                    }
                }
            }
        }
    } else {
        printf("[App] Running in Automated Mode. Sleep for 2 seconds to allow dashboard syncing...\n");
        sleep(2);
    }

    close(fd0);
    close(fd1);
    printf("--- Multi-UART Test End (SUCCESS) ---\n");
    return 0;
}
