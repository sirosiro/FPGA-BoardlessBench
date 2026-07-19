#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <stdarg.h>
#include "vfpga_device_config.h"

#define MOUNT_SOURCE "/dev/mmcblk0p1"
#define MOUNT_TARGET "/mnt/sd"
#define WRITE_FILE "/mnt/sd/test_write.txt"
#define UART_DEV FBB_DEV_PATH_SERIAL

void print_to_both(int uart_fd, const char *fmt, ...) {
    va_list args;
    char buf[512];

    // Format to temp buffer
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Print to stdout
    printf("%s", buf);

    // Write to UART with carriage return if UART is open
    if (uart_fd >= 0) {
        char uart_buf[1024];
        int j = 0;
        for (int i = 0; buf[i] != '\0' && j < (int)sizeof(uart_buf) - 2; i++) {
            if (buf[i] == '\n' && (i == 0 || buf[i-1] != '\r')) {
                uart_buf[j++] = '\r';
            }
            uart_buf[j++] = buf[i];
        }
        write(uart_fd, uart_buf, j);
    }
}

void print_menu(int uart_fd) {
    const char *menu = "\r\n"
                       "========================================\r\n"
                       "   F-BB SD Card Step-by-Step Test Menu\r\n"
                       "========================================\r\n"
                       "1. Mount Virtual SD Card\r\n"
                       "2. Write test_write.txt\r\n"
                       "3. Verify test_write.txt\r\n"
                       "4. Unmount Virtual SD Card\r\n"
                       "5. Exit / Finish Test\r\n"
                       "----------------------------------------\r\n"
                       "Enter choice (1-5): ";
    if (uart_fd >= 0) {
        write(uart_fd, menu, strlen(menu));
    }
}

int do_mount(int uart_fd) {
    print_to_both(uart_fd, "[App] Mounting %s to %s...\n", MOUNT_SOURCE, MOUNT_TARGET);
    if (mount(MOUNT_SOURCE, MOUNT_TARGET, "vfat", 0, NULL) != 0) {
        print_to_both(uart_fd, "[App] ERROR: mount failed (errno: %d, %s)\n", errno, strerror(errno));
        return -1;
    }
    print_to_both(uart_fd, "[App] Mount successful!\n");
    return 0;
}

int do_write(int uart_fd) {
    print_to_both(uart_fd, "[App] Writing to %s...\n", WRITE_FILE);
    FILE *wf = fopen(WRITE_FILE, "w");
    if (!wf) {
        print_to_both(uart_fd, "[App] ERROR: failed to open %s for writing (errno: %d, %s)\n", WRITE_FILE, errno, strerror(errno));
        return -1;
    }

    const char *write_data = "F-BB SD Test: SUCCESS\n";
    if (fputs(write_data, wf) == EOF) {
        print_to_both(uart_fd, "[App] ERROR: failed to write to %s\n", WRITE_FILE);
        fclose(wf);
        return -1;
    }
    fclose(wf);
    print_to_both(uart_fd, "[App] Write completed successfully!\n");
    return 0;
}

int do_verify(int uart_fd) {
    print_to_both(uart_fd, "[App] Verifying written file content...\n");
    FILE *vf = fopen(WRITE_FILE, "r");
    if (!vf) {
        print_to_both(uart_fd, "[App] ERROR: failed to open %s for verification (errno: %d, %s)\n", WRITE_FILE, errno, strerror(errno));
        return -1;
    }

    char verify_buf[256];
    if (fgets(verify_buf, sizeof(verify_buf), vf) == NULL) {
        print_to_both(uart_fd, "[App] ERROR: failed to read verification data from %s\n", WRITE_FILE);
        fclose(vf);
        return -1;
    }
    fclose(vf);

    const char *write_data = "F-BB SD Test: SUCCESS\n";
    if (strcmp(verify_buf, write_data) != 0) {
        print_to_both(uart_fd, "[App] ERROR: verification failed! Got: '%s', Expected: '%s'\n", verify_buf, write_data);
        return -1;
    }
    print_to_both(uart_fd, "[App] Write content verification PASSED!\n");
    return 0;
}

int do_unmount(int uart_fd) {
    print_to_both(uart_fd, "[App] Unmounting %s...\n", MOUNT_TARGET);
    if (umount(MOUNT_TARGET) != 0) {
        print_to_both(uart_fd, "[App] ERROR: umount failed (errno: %d, %s)\n", errno, strerror(errno));
        return -1;
    }
    print_to_both(uart_fd, "[App] Unmount successful!\n");

    // Verify cleanup
    struct stat st;
    if (lstat(MOUNT_TARGET, &st) == 0) {
        print_to_both(uart_fd, "[App] ERROR: mount point %s still exists after umount!\n", MOUNT_TARGET);
        return -1;
    }
    print_to_both(uart_fd, "[App] Cleanup verification PASSED!\n");
    return 0;
}

int main() {
    printf("[App] Starting A-core SD Mount Scenario Verification\n");

    // Open UART if interactive mode or if device is present
    int uart_fd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (uart_fd >= 0) {
        struct termios options;
        tcgetattr(uart_fd, &options);
        cfmakeraw(&options);
        tcsetattr(uart_fd, TCSANOW, &options);
    }

    // Determine execution mode
    if (getenv("VFPGA_INTERACTIVE")) {
        print_to_both(uart_fd, "[App] Entering Interactive Mode (VFPGA_INTERACTIVE is set)\n");
        while (1) {
            print_menu(uart_fd);
            char ch = '\0';
            while (1) {
                if (read(uart_fd, &ch, 1) > 0) {
                    // echo back character to terminal
                    write(uart_fd, &ch, 1);
                    write(uart_fd, "\r\n", 2);
                    break;
                }
                usleep(50000); // 50ms polling
            }

            if (ch == '1') {
                do_mount(uart_fd);
            } else if (ch == '2') {
                do_write(uart_fd);
            } else if (ch == '3') {
                do_verify(uart_fd);
            } else if (ch == '4') {
                do_unmount(uart_fd);
            } else if (ch == '5') {
                print_to_both(uart_fd, "[App] Exiting interactive test shell. Goodbye!\n");
                break;
            } else {
                print_to_both(uart_fd, "[App] Invalid selection: '%c'. Enter 1-5.\n", ch);
            }
        }
    } else {
        print_to_both(uart_fd, "[App] Running in Automated Mode.\n");
        if (do_mount(uart_fd) != 0) {
            if (uart_fd >= 0) close(uart_fd);
            return EXIT_FAILURE;
        }
        if (do_write(uart_fd) != 0) {
            umount(MOUNT_TARGET);
            if (uart_fd >= 0) close(uart_fd);
            return EXIT_FAILURE;
        }
        if (do_verify(uart_fd) != 0) {
            umount(MOUNT_TARGET);
            if (uart_fd >= 0) close(uart_fd);
            return EXIT_FAILURE;
        }
        if (do_unmount(uart_fd) != 0) {
            if (uart_fd >= 0) close(uart_fd);
            return EXIT_FAILURE;
        }
        print_to_both(uart_fd, "[App] SUCCESS: SD Mount & File IO verified successfully!\n");
    }

    if (uart_fd >= 0) {
        close(uart_fd);
    }
    return EXIT_SUCCESS;
}
