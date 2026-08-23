#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <sys/select.h>

#define UIO_DEV "/dev/uio0"
#define GPIO_DEV "/dev/uio1"
#define UART_DEV "/dev/ttyUL0"
#define MAP_SIZE 0x1000

extern void fbb_write_reg(void *base_addr, unsigned long offset, uint32_t val);
extern uint32_t fbb_read_reg(void *base_addr, unsigned long offset);

static void fw_write_reg(void *base_addr, unsigned long offset, uint32_t val) {
    fbb_write_reg(base_addr, offset, val);
}

static uint32_t fw_read_reg(void *base_addr, unsigned long offset) {
    return fbb_read_reg(base_addr, offset);
}

static void print_help_menu() {
    printf("\n============================================================\n");
    printf("🎯 F-BB Scenario 01c: Interactive Protocol & Transaction Inspector\n");
    printf("============================================================\n");
    printf("Dashboard Web UI: http://localhost:8080\n");
    printf("Status: Ready. Awaiting user action (GPIO Pin Toggle or UART Key)\n\n");
    printf("[GPIO Input Pin Trigger Map (Dashboard 'GPIO / Pin Array' Pane)]:\n");
    printf("  - Pin 0 (Bit 0): Valid WRITE to CTRL (0x00) -> [OK] WRITE\n");
    printf("  - Pin 1 (Bit 1): Valid READ from STATUS (0x04) -> [OK] READ\n");
    printf("  - Pin 2 (Bit 2): Illegal WRITE to RO STATUS (0x04) -> 🚨 [PROTOCOL_VIOLATION] WRITE_TO_RO\n");
    printf("  - Pin 3 (Bit 3): Illegal READ from WO TRIG (0x08) -> 🚨 [PROTOCOL_VIOLATION] READ_FROM_WO\n\n");
    printf("[Interactive UART Console Commands (Dashboard UART Pane)]:\n");
    printf("  1 : Trigger Valid WRITE to CTRL (0x00)\n");
    printf("  2 : Trigger Valid READ from STATUS (0x04)\n");
    printf("  3 : Trigger Illegal WRITE to RO STATUS (0x04)\n");
    printf("  4 : Trigger Illegal READ from WO TRIG (0x08)\n");
    printf("  h : Display this Help Menu\n");
    printf("  q : Quit Interactive Loop\n");
    printf("============================================================\n\n");
    fflush(stdout);
}

static void set_stdin_nonblocking() {
    struct termios tt;
    if (tcgetattr(STDIN_FILENO, &tt) == 0) {
        tt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &tt);
    }
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    // Clean log file on start
    unlink("/tmp/fbb_protocol_violations.log");

    const char *interactive_env = getenv("FBB_INTERACTIVE");
    if (!interactive_env) interactive_env = getenv("VFPGA_INTERACTIVE");
    int is_interactive = (interactive_env != NULL && interactive_env[0] == '1');

    int fd = open(UIO_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " UIO_DEV);
        return 1;
    }

    uint32_t *vfpga = (uint32_t *)mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (vfpga == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    int gpio_fd = open(GPIO_DEV, O_RDWR);
    volatile uint32_t *gpio_regs = NULL;
    if (gpio_fd >= 0) {
        gpio_regs = (uint32_t *)mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, gpio_fd, 0);
        if (gpio_regs != MAP_FAILED) {
            gpio_regs[1] = 0xFFFFFFFF; // Set Channel 1 TRI to Input Mode
            gpio_regs[3] = 0xFFFFFFFF; // Set Channel 2 TRI to Input Mode
        }
    }

    // --- Mode A: Automated Regression Test Mode (fbb test / regression_test.py) ---
    if (!is_interactive) {
        printf("=== F-BB Scenario: 01c_protocol_assertion (Automated Mode) ===\n");
        // 1. Valid RW access to CTRL (0x00)
        fw_write_reg(vfpga, 0x00, 0x12345678);

        // 2. Valid RO read from STATUS (0x04)
        fw_read_reg(vfpga, 0x04);

        // 3. Intentionally attempt illegal Write to Read-Only STATUS register (0x04)
        fw_write_reg(vfpga, 0x04, 0xDEADBEEF);

        FILE *f = fopen("/tmp/fbb_protocol_violations.log", "r");
        if (!f) {
            printf("❌ FAILED: Violation log not found!\n");
            munmap(vfpga, MAP_SIZE);
            close(fd);
            if (gpio_regs && gpio_regs != MAP_FAILED) {
                munmap((void *)gpio_regs, MAP_SIZE);
                close(gpio_fd);
            }
            return 1;
        }
        char line[256];
        int found_violation = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "WRITE_TO_RO") && strstr(line, "reg=STATUS")) {
                found_violation = 1;
            }
        }
        fclose(f);

        if (found_violation) {
            printf("=== Scenario 01c_protocol_assertion Test Result: SUCCESS ===\n");
            munmap(vfpga, MAP_SIZE);
            close(fd);
            if (gpio_regs && gpio_regs != MAP_FAILED) {
                munmap((void *)gpio_regs, MAP_SIZE);
                close(gpio_fd);
            }
            return 0;
        } else {
            printf("❌ FAILED: Protocol assertion did not capture WRITE_TO_RO for STATUS!\n");
            munmap(vfpga, MAP_SIZE);
            close(fd);
            if (gpio_regs && gpio_regs != MAP_FAILED) {
                munmap((void *)gpio_regs, MAP_SIZE);
                close(gpio_fd);
            }
            return 1;
        }
    }

    // --- Mode B: Interactive Lab Mode (start_lab.sh / VFPGA_INTERACTIVE=1) ---
    // Open UART device for UART Console redirection
    int uart_fd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (uart_fd >= 0) {
        dup2(uart_fd, STDIN_FILENO);
        dup2(uart_fd, STDOUT_FILENO);
        dup2(uart_fd, STDERR_FILENO);
    }

    print_help_menu();
    set_stdin_nonblocking();

    uint32_t prev_gpio = 0;
    if (gpio_regs && gpio_regs != MAP_FAILED) {
        prev_gpio = gpio_regs[2] | gpio_regs[0]; // Initial GPIO_DATA / GPIO2_DATA state
    }

    int keep_running = 1;
    static uint32_t write_counter = 0x1000;

    while (keep_running) {
        // 1. Poll GPIO Input Pins (GPIO_DATA / GPIO2_DATA) for Dashboard Pin Array Toggles
        if (gpio_regs && gpio_regs != MAP_FAILED) {
            uint32_t current_gpio = gpio_regs[2] | gpio_regs[0];
            uint32_t changed_high = (current_gpio ^ prev_gpio) & current_gpio; // Rising edge

            if (changed_high & (1 << 0)) { // Pin 0 ON
                uint32_t val = 0xA0000000 | (++write_counter);
                printf("\n⚡ [GPIO Pin 0 Triggered] Executing Valid WRITE to CTRL (0x00) with 0x%08X...\n", val);
                fw_write_reg(vfpga, 0x00, val);
                fflush(stdout);
            }
            if (changed_high & (1 << 1)) { // Pin 1 ON
                printf("\n⚡ [GPIO Pin 1 Triggered] Executing Valid READ from STATUS (0x04)...\n");
                uint32_t rval = fw_read_reg(vfpga, 0x04);
                printf("   [Result] STATUS = 0x%08X\n", rval);
                fflush(stdout);
            }
            if (changed_high & (1 << 2)) { // Pin 2 ON
                printf("\n⚡ [GPIO Pin 2 Triggered] Executing Illegal WRITE to RO STATUS (0x04)...\n");
                fw_write_reg(vfpga, 0x04, 0xBAD00000 | (++write_counter));
                fflush(stdout);
            }
            if (changed_high & (1 << 3)) { // Pin 3 ON
                printf("\n⚡ [GPIO Pin 3 Triggered] Executing Illegal READ from WO TRIG (0x08)...\n");
                uint32_t rval = fw_read_reg(vfpga, 0x08);
                printf("   [Result] Read from Write-Only TRIG returned 0x%08X (Protocol Violation Recorded)\n", rval);
                fflush(stdout);
            }

            prev_gpio = current_gpio;
        }

        // 2. Poll UART Console Inputs
        char ch;
        if (read(STDIN_FILENO, &ch, 1) == 1) {
            switch (ch) {
                case '1': {
                    uint32_t val = 0xB0000000 | (++write_counter);
                    printf("\n⌨️ [UART Key '1'] Executing Valid WRITE to CTRL (0x00) with 0x%08X...\n", val);
                    fw_write_reg(vfpga, 0x00, val);
                    fflush(stdout);
                    break;
                }
                case '2': {
                    printf("\n⌨️ [UART Key '2'] Executing Valid READ from STATUS (0x04)...\n");
                    uint32_t rval = fw_read_reg(vfpga, 0x04);
                    printf("   [Result] STATUS = 0x%08X\n", rval);
                    fflush(stdout);
                    break;
                }
                case '3': {
                    printf("\n⌨️ [UART Key '3'] Executing Illegal WRITE to RO STATUS (0x04)...\n");
                    fw_write_reg(vfpga, 0x04, 0xBAD00000 | (++write_counter));
                    fflush(stdout);
                    break;
                }
                case '4': {
                    printf("\n⌨️ [UART Key '4'] Executing Illegal READ from WO TRIG (0x08)...\n");
                    uint32_t rval = fw_read_reg(vfpga, 0x08);
                    printf("   [Result] Read from Write-Only TRIG returned 0x%08X\n", rval);
                    fflush(stdout);
                    break;
                }
                case 'h':
                case 'H':
                    print_help_menu();
                    break;
                case 'q':
                case 'Q':
                    printf("\n[FW] Exiting Interactive Loop...\n");
                    keep_running = 0;
                    break;
                default:
                    break;
            }
        }

        usleep(50000); // 50ms polling interval
    }

    munmap(vfpga, MAP_SIZE);
    close(fd);
    if (gpio_regs && gpio_regs != MAP_FAILED) {
        munmap((void *)gpio_regs, MAP_SIZE);
        close(gpio_fd);
    }
    return 0;
}
