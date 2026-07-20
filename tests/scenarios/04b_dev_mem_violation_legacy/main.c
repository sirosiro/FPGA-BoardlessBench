#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <termios.h>

#if defined(__SANITIZE_ADDRESS__)
#define HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAS_ASAN 1
#endif
#endif

#ifndef HAS_ASAN
#define HAS_ASAN 0
#endif

#define MEM_DEVICE "/dev/mem"
#define FPGA_BASE_ADDR 0x40000000
#define REG_SIZE 4096

int main() {
    printf("--- /dev/mem Out-Of-Bounds Access Test Start ---\n");

    printf("[App] Opening %s...\n", MEM_DEVICE);
    int fd = open(MEM_DEVICE, O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("open /dev/mem failed");
        return 1;
    }

    printf("[App] Mapping physical address 0x%08X with size %d...\n", FPGA_BASE_ADDR, REG_SIZE);
    void *virt_base = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_BASE_ADDR);
    if (virt_base == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // Access normal registers first to show they work
    volatile uint32_t *regs = (volatile uint32_t *)virt_base;
    printf("[App] Writing 1 to EN (offset 0x14)...\n");
    regs[5] = 1; // 0x14 / 4 = 5
    
    char *interactive = getenv("VFPGA_INTERACTIVE");
    if (interactive && strcmp(interactive, "1") == 0) {
        printf("[App] Opening /dev/ttyPS1...\n");
        int uart_fd = open("/dev/ttyPS1", O_RDWR | O_NOCTTY);
        if (uart_fd < 0) {
            perror("open /dev/ttyPS1 failed");
            munmap(virt_base, REG_SIZE);
            close(fd);
            return 1;
        }

        // Configure UART in raw mode
        struct termios options;
        tcgetattr(uart_fd, &options);
        cfmakeraw(&options);
        tcsetattr(uart_fd, TCSANOW, &options);

        const char *menu_str = 
            "\r\n==================================================\r\n"
            " F-BB Memory Violation Interactive Test Menu\r\n"
            "==================================================\r\n";
        write(uart_fd, menu_str, strlen(menu_str));

        while (1) {
            char status_str[256];
#if HAS_ASAN
            snprintf(status_str, sizeof(status_str), "   [ASan Status: ENABLED]\r\n\r\n");
#else
            snprintf(status_str, sizeof(status_str), "   [ASan Status: DISABLED (Compile with -DFBB_ENABLE_ASAN=ON to enable!)]\r\n\r\n");
#endif
            write(uart_fd, status_str, strlen(status_str));

            const char *options_str = 
                " 1: Trigger Memory Guard Page Violation (SIGSEGV)\r\n"
                " 2: Trigger AddressSanitizer (ASan) Heap Buffer Overflow\r\n"
                " 3: Exit normally\r\n"
                "Select option (1-3): ";
            write(uart_fd, options_str, strlen(options_str));

            char choice_buf[16];
            int choice_len = 0;
            while (choice_len < (int)sizeof(choice_buf) - 1) {
                char ch;
                if (read(uart_fd, &ch, 1) <= 0) {
                    break;
                }
                // Echo back
                write(uart_fd, &ch, 1);
                if (ch == '\r' || ch == '\n') {
                    write(uart_fd, "\n", 1);
                    break;
                }
                choice_buf[choice_len++] = ch;
            }
            choice_buf[choice_len] = '\0';

            if (strcmp(choice_buf, "1") == 0) {
                const char *msg = "Triggering Memory Guard Page Violation at offset 0x1000 (4096 bytes)...\r\n";
                write(uart_fd, msg, strlen(msg));
                sleep(1);
                *(volatile uint32_t *)((uintptr_t)virt_base + 4096) = 0xDEADBEEF;
                const char *warn = "WARNING: Guard page did not trigger! (Error)\r\n";
                write(uart_fd, warn, strlen(warn));
            } else if (strcmp(choice_buf, "2") == 0) {
                const char *msg = "Triggering ASan Heap Buffer Overflow...\r\n";
                write(uart_fd, msg, strlen(msg));
                sleep(1);
                volatile char *ptr = (volatile char *)malloc(16);
                ptr[24] = 'X';
                char read_msg[64];
                snprintf(read_msg, sizeof(read_msg), "Read back out-of-bounds value: %c\r\n", ptr[24]);
                write(uart_fd, read_msg, strlen(read_msg));
                const char *warn = "WARNING: ASan did not catch the overflow (compile without -DFBB_ENABLE_ASAN=ON?)\r\n";
                write(uart_fd, warn, strlen(warn));
                free((void *)ptr);
            } else if (strcmp(choice_buf, "3") == 0) {
                const char *msg = "Exiting normally...\r\n";
                write(uart_fd, msg, strlen(msg));
                break;
            } else {
                const char *msg = "Invalid choice. Please select 1, 2, or 3.\r\n";
                write(uart_fd, msg, strlen(msg));
            }
        }
        close(uart_fd);
    } else {
        // Non-interactive: run Memory Guard Page violation immediately for automatic testing
        printf("[App] Non-interactive mode. Simulating out-of-bounds access on Guard Page immediately...\n");
        sleep(1); // Give the dashboard a second to render normal state
        *(volatile uint32_t *)((uintptr_t)virt_base + 4096) = 0xDEADBEEF;
        printf("[App] WARNING: If you see this, the guard page did not trigger! (Error)\n");
    }

    munmap(virt_base, REG_SIZE);
    close(fd);
    printf("--- /dev/mem Out-Of-Bounds Access Test End ---\n");
    return 0;
}
