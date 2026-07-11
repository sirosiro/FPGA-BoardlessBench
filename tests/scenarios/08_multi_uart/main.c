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

    printf("[App] Opening /dev/ttyPS2 (Loopback Target)...\n");
    int fd2 = open("/dev/ttyPS2", O_RDWR | O_NOCTTY);
    if (fd2 < 0) {
        perror("open /dev/ttyPS2 failed");
        close(fd0);
        close(fd1);
        return 1;
    }
    configure_raw_mode(fd2);
    printf("[App] Waiting 1 second for loopback daemon to connect...\n");
    sleep(1);

    printf("[App] All three UART ports opened successfully (fd0=%d, fd1=%d, fd2=%d).\n", fd0, fd1, fd2);

    const char *msg0 = "\r\n[UART 1] Welcome to the FIRST UART port!\r\n";
    const char *msg1 = "\r\n[UART 2] Welcome to the SECOND UART port!\r\n";

    write(fd0, msg0, strlen(msg0));
    write(fd1, msg1, strlen(msg1));
    printf("[App] Sent greetings to UART 1 and UART 2.\n");

    // Test Loopback on UART 3 (Automated Verification)
    printf("[App] Testing UART 3 loopback...\n");
    const char *test_msg = "Hello F-BB UART Loopback!";
    write(fd2, test_msg, strlen(test_msg));
    
    fd_set test_fds;
    FD_ZERO(&test_fds);
    FD_SET(fd2, &test_fds);
    struct timeval tv_test = {2, 0}; // 2s timeout
    int test_ret = select(fd2 + 1, &test_fds, NULL, NULL, &tv_test);
    
    int loopback_ok = 0;
    if (test_ret > 0 && FD_ISSET(fd2, &test_fds)) {
        char rx_buf[128];
        ssize_t rx_len = read(fd2, rx_buf, sizeof(rx_buf) - 1);
        if (rx_len > 0) {
            rx_buf[rx_len] = '\0';
            printf("[App] UART 3 Loopback Received: '%s'\n", rx_buf);
            loopback_ok = 1;
        }
    } else {
        printf("[App] UART 3 Loopback: Timeout/No Response\n");
    }

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
                        
                        // Send data to UART 3 (Loopback Target)
                        write(fd2, buf0, n);
                        
                        // Synchronously wait for loopback response
                        fd_set lp_fds;
                        FD_ZERO(&lp_fds);
                        FD_SET(fd2, &lp_fds);
                        struct timeval tv_lp = {1, 0}; // 1s timeout
                        int lp_ret = select(fd2 + 1, &lp_fds, NULL, NULL, &tv_lp);
                        
                        if (lp_ret > 0 && FD_ISSET(fd2, &lp_fds)) {
                            char lp_buf[128];
                            ssize_t lp_n = read(fd2, lp_buf, sizeof(lp_buf) - 1);
                            if (lp_n > 0) {
                                lp_buf[lp_n] = '\0';
                                // Echo back to UART 1 (fd0)
                                write(fd0, "[UART 1 Loopback Echo]: ", 24);
                                write(fd0, lp_buf, lp_n);
                                write(fd0, "\r\n", 2);
                            }
                        } else {
                            write(fd0, "[UART 1 Error]: Loopback Timeout\r\n", 34);
                        }
                    }
                }
                if (FD_ISSET(fd1, &readfds)) {
                    ssize_t n = read(fd1, buf1, sizeof(buf1) - 1);
                    if (n > 0) {
                        buf1[n] = '\0';
                        
                        // Send data to UART 3 (Loopback Target)
                        write(fd2, buf1, n);
                        
                        // Synchronously wait for loopback response
                        fd_set lp_fds;
                        FD_ZERO(&lp_fds);
                        FD_SET(fd2, &lp_fds);
                        struct timeval tv_lp = {1, 0}; // 1s timeout
                        int lp_ret = select(fd2 + 1, &lp_fds, NULL, NULL, &tv_lp);
                        
                        if (lp_ret > 0 && FD_ISSET(fd2, &lp_fds)) {
                            char lp_buf[128];
                            ssize_t lp_n = read(fd2, lp_buf, sizeof(lp_buf) - 1);
                            if (lp_n > 0) {
                                lp_buf[lp_n] = '\0';
                                // Echo back to UART 2 (fd1)
                                write(fd1, "[UART 2 Loopback Echo]: ", 24);
                                write(fd1, lp_buf, lp_n);
                                write(fd1, "\r\n", 2);
                            }
                        } else {
                            write(fd1, "[UART 2 Error]: Loopback Timeout\r\n", 34);
                        }
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
    close(fd2);
    
    if (loopback_ok) {
        printf("--- Multi-UART Test End (SUCCESS) ---\n");
        return 0;
    } else {
        printf("--- Multi-UART Test End (FAILURE: Loopback failed) ---\n");
        return 1;
    }
}
