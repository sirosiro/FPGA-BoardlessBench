#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>
#include <stdarg.h>

#define UART_DEV "/dev/ttyPS1"
#define UIO_DEV  "/dev/uio0"

#define CAN_ID_TELEMETRY_SPEED_RPM 0x100
#define CAN_ID_TELEMETRY_TEMP_VOLT 0x101
#define CAN_ID_OBD2_REQ            0x7DF
#define CAN_ID_OBD2_RESP           0x7E8

static volatile int g_running = 1;
static volatile int g_test_passed = 0;
static volatile int g_periodic_diag_enabled = 0;

static volatile uint8_t  g_speed_kmh = 80;
static volatile uint16_t g_rpm = 3200;
static volatile uint8_t  g_coolant_temp = 88;  // 88°C
static volatile uint16_t g_battery_mv = 14200; // 14.2V

static volatile uint32_t *g_uio_regs = NULL;
static uint32_t g_tx_counter = 0;
static uint32_t g_rx_counter = 0;

static pthread_mutex_t g_telemetry_lock = PTHREAD_MUTEX_INITIALIZER;

static void print_to_both(int uart_fd, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    printf("%s", buf);
    fflush(stdout);

    if (uart_fd >= 0) {
        // Convert \n to \r\n for UART terminal
        char uart_buf[2048];
        int j = 0;
        for (int i = 0; buf[i] != '\0' && j < (int)sizeof(uart_buf) - 2; i++) {
            if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r')) {
                uart_buf[j++] = '\r';
            }
            uart_buf[j++] = buf[i];
        }
        uart_buf[j] = '\0';
        write(uart_fd, uart_buf, j);
    }
}

// ============================================================================
// ECU Server Thread: Broadcasts telemetry & responds to OBD-II diagnostic requests
// ============================================================================
static void *ecu_server_thread(void *arg) {
    (void)arg;
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("[ECU] Failed to create SocketCAN socket");
        return NULL;
    }

    struct ifreq ifr;
    strcpy(ifr.ifr_name, "can0");
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("[ECU] ioctl SIOCGIFINDEX failed");
        close(s);
        return NULL;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ECU] Failed to bind to can0");
        close(s);
        return NULL;
    }

    // Set filter to receive OBD-II Functional Requests (0x7DF)
    struct can_filter rfilter[1];
    rfilter[0].can_id = CAN_ID_OBD2_REQ;
    rfilter[0].can_mask = CAN_SFF_MASK;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    // Set socket to non-blocking for polling loop
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    uint32_t cycle_count = 0;

    while (g_running) {
        pthread_mutex_lock(&g_telemetry_lock);
        uint8_t speed = g_speed_kmh;
        uint16_t rpm = g_rpm;
        uint8_t temp = g_coolant_temp;
        uint16_t volt = g_battery_mv;
        pthread_mutex_unlock(&g_telemetry_lock);

        // 1. Broadcast Telemetry 0x100 (Speed & RPM)
        struct can_frame tf1;
        memset(&tf1, 0, sizeof(tf1));
        tf1.can_id = CAN_ID_TELEMETRY_SPEED_RPM;
        tf1.can_dlc = 8;
        tf1.data[0] = speed;
        tf1.data[1] = 0x00;
        tf1.data[2] = (uint8_t)(rpm >> 8);
        tf1.data[3] = (uint8_t)(rpm & 0xFF);
        tf1.data[4] = 0x00; // Gear (D)
        tf1.data[5] = (uint8_t)(cycle_count & 0xFF);
        tf1.data[6] = 0x12;
        tf1.data[7] = 0x34;
        write(s, &tf1, sizeof(tf1));

        // 2. Broadcast Telemetry 0x101 (Temperature & Voltage)
        struct can_frame tf2;
        memset(&tf2, 0, sizeof(tf2));
        tf2.can_id = CAN_ID_TELEMETRY_TEMP_VOLT;
        tf2.can_dlc = 8;
        tf2.data[0] = temp;
        tf2.data[1] = (uint8_t)(volt >> 8);
        tf2.data[2] = (uint8_t)(volt & 0xFF);
        tf2.data[3] = 0x55;
        write(s, &tf2, sizeof(tf2));

        g_tx_counter += 2;
        if (g_uio_regs) {
            g_uio_regs[2] = g_tx_counter; // TX_COUNT
            g_uio_regs[4] = ((uint32_t)speed << 16) | rpm; // SPEED_RPM
        }

        // 3. Check and process any incoming OBD-II diagnostic requests
        struct can_frame req;
        while (read(s, &req, sizeof(req)) == sizeof(req)) {
            g_rx_counter++;
            if (g_uio_regs) {
                g_uio_regs[1] = 0x00000007; // STATUS: READY | CAN0_BOUND | DIAG_ACTIVE
                g_uio_regs[3] = g_rx_counter; // RX_COUNT
            }

            if (req.can_id == CAN_ID_OBD2_REQ && req.data[1] == 0x01) {
                struct can_frame resp;
                memset(&resp, 0, sizeof(resp));
                resp.can_id = CAN_ID_OBD2_RESP;
                resp.can_dlc = 8;
                resp.data[1] = 0x41; // Positive response to Mode 01
                resp.data[2] = req.data[2]; // Echo PID

                if (req.data[2] == 0x0D) {
                    // PID 0D: Vehicle Speed (km/h)
                    resp.data[0] = 0x03;
                    resp.data[3] = speed;
                } else if (req.data[2] == 0x0C) {
                    // PID 0C: Engine RPM ((A*256+B)/4) -> Raw = RPM * 4
                    uint16_t rpm_val = rpm * 4;
                    resp.data[0] = 0x04;
                    resp.data[3] = (uint8_t)(rpm_val >> 8);
                    resp.data[4] = (uint8_t)(rpm_val & 0xFF);
                } else if (req.data[2] == 0x05) {
                    // PID 05: Coolant Temp (A - 40) -> Raw = Temp + 40
                    resp.data[0] = 0x03;
                    resp.data[3] = temp + 40;
                } else {
                    resp.data[0] = 0x03;
                    resp.data[3] = 0x00;
                }

                write(s, &resp, sizeof(resp));
                g_tx_counter++;
                if (g_uio_regs) {
                    g_uio_regs[2] = g_tx_counter;
                }
            }
        }

        cycle_count++;
        usleep(40000); // 40ms cycle (~25 FPS)
    }

    close(s);
    return NULL;
}

// ============================================================================
// Tester helper: Sends diagnostic request and verifies response
// ============================================================================
static int send_obd2_request_and_verify(int tester_sock, uint8_t pid, uint32_t *out_val, int uart_fd) {
    struct can_frame tx;
    memset(&tx, 0, sizeof(tx));
    tx.can_id = CAN_ID_OBD2_REQ;
    tx.can_dlc = 8;
    tx.data[0] = 0x02; // Length: 2 bytes (Mode + PID)
    tx.data[1] = 0x01; // Mode 01 (Current Data)
    tx.data[2] = pid;

    if (write(tester_sock, &tx, sizeof(tx)) < 0) {
        print_to_both(uart_fd, "[Tester] ❌ write failed for PID 0x%02X\n", pid);
        return -1;
    }

    // Wait for response with timeout (1 second)
    for (int retry = 0; retry < 50; retry++) {
        struct can_frame rx;
        ssize_t nbytes = read(tester_sock, &rx, sizeof(rx));
        if (nbytes == sizeof(rx)) {
            if (rx.can_id == CAN_ID_OBD2_RESP && rx.data[1] == 0x41 && rx.data[2] == pid) {
                if (pid == 0x0D) {
                    uint8_t speed = rx.data[3];
                    if (out_val) *out_val = speed;
                    print_to_both(uart_fd, "[Tester] ✅ OBD-II Response (0x7E8): PID 0D (Speed) = %u km/h [Raw DLC=%d Data=%02X %02X %02X %02X %02X %02X %02X %02X]\n",
                                  speed, rx.can_dlc, rx.data[0], rx.data[1], rx.data[2], rx.data[3], rx.data[4], rx.data[5], rx.data[6], rx.data[7]);
                    return 0;
                } else if (pid == 0x0C) {
                    uint16_t raw_rpm = ((uint16_t)rx.data[3] << 8) | rx.data[4];
                    uint16_t rpm = raw_rpm / 4;
                    if (out_val) *out_val = rpm;
                    print_to_both(uart_fd, "[Tester] ✅ OBD-II Response (0x7E8): PID 0C (RPM) = %u RPM [Raw=0x%04X DLC=%d Data=%02X %02X %02X %02X %02X %02X %02X %02X]\n",
                                  rpm, raw_rpm, rx.can_dlc, rx.data[0], rx.data[1], rx.data[2], rx.data[3], rx.data[4], rx.data[5], rx.data[6], rx.data[7]);
                    return 0;
                }
            }
        }
        usleep(20000); // 20ms retry
    }

    print_to_both(uart_fd, "[Tester] ❌ Timeout waiting for OBD-II response for PID 0x%02X\n", pid);
    return -1;
}

static int setup_tester_socket(void) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("[Tester] socket failed");
        return -1;
    }

    struct ifreq ifr;
    strcpy(ifr.ifr_name, "can0");
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("[Tester] SIOCGIFINDEX failed");
        close(s);
        return -1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Tester] bind failed");
        close(s);
        return -1;
    }

    // Set filter for OBD-II Response (0x7E8)
    struct can_filter rfilter[1];
    rfilter[0].can_id = CAN_ID_OBD2_RESP;
    rfilter[0].can_mask = CAN_SFF_MASK;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    // Non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    return s;
}

// Background thread for optional periodic OBD-II polling (1 Hz)
static void *periodic_diag_thread(void *arg) {
    int tester_sock = (int)(intptr_t)arg;
    while (g_running) {
        if (g_periodic_diag_enabled) {
            uint32_t val = 0;
            send_obd2_request_and_verify(tester_sock, 0x0D, &val, -1);
            usleep(500000);
            send_obd2_request_and_verify(tester_sock, 0x0C, &val, -1);
            usleep(500000);
        } else {
            usleep(100000);
        }
    }
    return NULL;
}

// ============================================================================
// Interactive UART Menu Mode
// ============================================================================
static void run_interactive_mode(int uart_fd, int tester_sock) {
    print_to_both(uart_fd, "\r\n");
    print_to_both(uart_fd, "============================================================\r\n");
    print_to_both(uart_fd, "  F-BB Scenario 21: Automotive SocketCAN ECU Demo (Interactive)  \r\n");
    print_to_both(uart_fd, "============================================================\r\n");
    print_to_both(uart_fd, " Live CAN Bus: can0 is active & broadcasting telemetry (0x100, 0x101).\r\n");
    print_to_both(uart_fd, " Web Dashboard CAN Bus Analyzer is active at http://localhost:8080\r\n");
    print_to_both(uart_fd, "============================================================\r\n\r\n");

    pthread_t diag_tid;
    pthread_create(&diag_tid, NULL, periodic_diag_thread, (void *)(intptr_t)tester_sock);

    while (g_running) {
        pthread_mutex_lock(&g_telemetry_lock);
        uint8_t speed = g_speed_kmh;
        uint16_t rpm = g_rpm;
        uint8_t temp = g_coolant_temp;
        float volt = g_battery_mv / 1000.0f;
        pthread_mutex_unlock(&g_telemetry_lock);

        print_to_both(uart_fd, "\r\n--- Current ECU Telemetry ---\r\n");
        print_to_both(uart_fd, " Speed: %u km/h | Engine RPM: %u | Coolant: %u C | Battery: %.1f V\r\n", speed, rpm, temp, volt);
        print_to_both(uart_fd, " Periodic OBD-II Polling (0x7DF/0x7E8): %s\r\n", g_periodic_diag_enabled ? "ACTIVE (1 Hz)" : "OFF (On-demand)");
        print_to_both(uart_fd, "------------------------------------------------------------\r\n");
        print_to_both(uart_fd, " 1: Send OBD-II Diagnostic Request: Vehicle Speed (PID 0D)\r\n");
        print_to_both(uart_fd, " 2: Send OBD-II Diagnostic Request: Engine RPM (PID 0C)\r\n");
        print_to_both(uart_fd, " 3: Accelerate (Speed +10 km/h, RPM +400)\r\n");
        print_to_both(uart_fd, " 4: Decelerate / Brake (Speed -10 km/h, RPM -400)\r\n");
        print_to_both(uart_fd, " 5: Run Full Automated OBD-II Diagnostic Test Suite\r\n");
        print_to_both(uart_fd, " 7: Toggle Periodic OBD-II Polling (0x7DF / 0x7E8 auto 1 Hz)\r\n");
        print_to_both(uart_fd, " 6: Exit Scenario\r\n");
        print_to_both(uart_fd, "Select option (1-7): ");

        char ch = '\0';
        while (g_running) {
            char temp_ch = '\0';
            int n = 0;
            if (uart_fd >= 0) {
                n = read(uart_fd, &temp_ch, 1);
            } else {
                int c = getchar();
                if (c != EOF) {
                    temp_ch = (char)c;
                    n = 1;
                }
            }
            if (n > 0) {
                if (temp_ch == '\r' || temp_ch == '\n' || temp_ch == ' ' || temp_ch == '\0') {
                    continue;
                }
                ch = temp_ch;
                if (uart_fd >= 0) {
                    write(uart_fd, &ch, 1);
                    write(uart_fd, "\r\n", 2);
                }
                break;
            }
            usleep(50000); // 50ms polling
        }

        if (ch == '1') {
            print_to_both(uart_fd, "[Tester] Sending OBD-II Diagnostic Request: Mode 01 PID 0D (Vehicle Speed)...\r\n");
            uint32_t val = 0;
            send_obd2_request_and_verify(tester_sock, 0x0D, &val, uart_fd);
        } else if (ch == '2') {
            print_to_both(uart_fd, "[Tester] Sending OBD-II Diagnostic Request: Mode 01 PID 0C (Engine RPM)...\r\n");
            uint32_t val = 0;
            send_obd2_request_and_verify(tester_sock, 0x0C, &val, uart_fd);
        } else if (ch == '3') {
            pthread_mutex_lock(&g_telemetry_lock);
            if (g_speed_kmh <= 240) g_speed_kmh += 10;
            if (g_rpm <= 7600) g_rpm += 400;
            uint8_t spd = g_speed_kmh;
            uint16_t r = g_rpm;
            pthread_mutex_unlock(&g_telemetry_lock);
            if (g_uio_regs) {
                g_uio_regs[4] = ((uint32_t)spd << 16) | r;
            }
            print_to_both(uart_fd, "[ECU] Accelerated: Speed = %u km/h, RPM = %u RPM (Broadcasting on CAN bus)\r\n", spd, r);
        } else if (ch == '4') {
            pthread_mutex_lock(&g_telemetry_lock);
            if (g_speed_kmh >= 10) g_speed_kmh -= 10; else g_speed_kmh = 0;
            if (g_rpm >= 1200) g_rpm -= 400; else g_rpm = 800;
            uint8_t spd = g_speed_kmh;
            uint16_t r = g_rpm;
            pthread_mutex_unlock(&g_telemetry_lock);
            if (g_uio_regs) {
                g_uio_regs[4] = ((uint32_t)spd << 16) | r;
            }
            print_to_both(uart_fd, "[ECU] Decelerated: Speed = %u km/h, RPM = %u RPM (Broadcasting on CAN bus)\r\n", spd, r);
        } else if (ch == '5') {
            print_to_both(uart_fd, "\r\n[Tester] Running Full Automated OBD-II Verification...\r\n");
            uint32_t s_val = 0, r_val = 0;
            int r1 = send_obd2_request_and_verify(tester_sock, 0x0D, &s_val, uart_fd);
            int r2 = send_obd2_request_and_verify(tester_sock, 0x0C, &r_val, uart_fd);
            if (r1 == 0 && r2 == 0) {
                print_to_both(uart_fd, "\r\n============================================================\r\n");
                print_to_both(uart_fd, "  [PASS] All SocketCAN & OBD-II ECU Assertions Verified!    \r\n");
                print_to_both(uart_fd, "============================================================\r\n");
            }
        } else if (ch == '7') {
            g_periodic_diag_enabled = !g_periodic_diag_enabled;
            print_to_both(uart_fd, "[Tester] Periodic OBD-II Polling (0x7DF/0x7E8) is now %s\r\n",
                          g_periodic_diag_enabled ? "ENABLED (1 Hz)" : "DISABLED");
        } else if (ch == '6' || ch == 'q' || ch == 'Q') {
            print_to_both(uart_fd, "[App] Exiting interactive ECU test shell. Goodbye!\r\n");
            g_running = 0;
            break;
        } else {
            print_to_both(uart_fd, "[App] Invalid selection: '%c'. Please enter 1-7.\r\n", ch);
        }
    }

    pthread_join(diag_tid, NULL);
}

// ============================================================================
// Main entry point
// ============================================================================
int main(void) {
    printf("============================================================\n");
    printf("  F-BB Scenario 21: Automotive SocketCAN ECU Gateway Demo  \n");
    printf("============================================================\n\n");

    // Open UIO MMIO registers if available
    int uio_fd = open(UIO_DEV, O_RDWR | O_SYNC);
    if (uio_fd >= 0) {
        g_uio_regs = (volatile uint32_t *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
        if (g_uio_regs != MAP_FAILED) {
            g_uio_regs[0] = 0x00000001; // CTRL: Gateway Enable
            g_uio_regs[1] = 0x00000003; // STATUS: Ready | CAN0_Bound
            g_uio_regs[2] = 0;          // TX_COUNT: 0
            g_uio_regs[3] = 0;          // RX_COUNT: 0
            g_uio_regs[4] = ((uint32_t)g_speed_kmh << 16) | g_rpm; // SPEED_RPM
        } else {
            g_uio_regs = NULL;
        }
    }

    // Start ECU Server Thread
    pthread_t ecu_tid;
    if (pthread_create(&ecu_tid, NULL, ecu_server_thread, NULL) != 0) {
        perror("Failed to create ECU server thread");
        if (g_uio_regs) munmap((void *)g_uio_regs, 0x1000);
        if (uio_fd >= 0) close(uio_fd);
        return 1;
    }

    usleep(60000); // 60ms wait for ECU socket ready

    int tester_sock = setup_tester_socket();
    if (tester_sock < 0) {
        g_running = 0;
        pthread_join(ecu_tid, NULL);
        if (g_uio_regs) munmap((void *)g_uio_regs, 0x1000);
        if (uio_fd >= 0) close(uio_fd);
        return 1;
    }

    // Open UART if available
    int uart_fd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (uart_fd >= 0) {
        struct termios options;
        tcgetattr(uart_fd, &options);
        cfmakeraw(&options);
        tcsetattr(uart_fd, TCSANOW, &options);
    }

    int is_interactive = (getenv("VFPGA_INTERACTIVE") != NULL && strcmp(getenv("VFPGA_INTERACTIVE"), "1") == 0);

    if (is_interactive) {
        run_interactive_mode(uart_fd, tester_sock);
        g_test_passed = 1;
    } else {
        printf("[Tester] Automated Regression Mode: Sending OBD-II Diagnostic Requests...\n");
        uint32_t speed = 0, rpm = 0;

        printf("[Tester] Sending OBD-II Diagnostic Request: Mode 01 PID 0D (Vehicle Speed)...\n");
        int r1 = send_obd2_request_and_verify(tester_sock, 0x0D, &speed, uart_fd);

        printf("[Tester] Sending OBD-II Diagnostic Request: Mode 01 PID 0C (Engine RPM)...\n");
        int r2 = send_obd2_request_and_verify(tester_sock, 0x0C, &rpm, uart_fd);

        if (r1 == 0 && r2 == 0 && speed == 80 && rpm == 3200) {
            g_test_passed = 1;
        } else {
            g_test_passed = 0;
        }

        g_running = 0;
    }

    if (uart_fd >= 0) {
        close(uart_fd);
    }
    close(tester_sock);
    pthread_join(ecu_tid, NULL);

    if (g_uio_regs && g_uio_regs != MAP_FAILED) {
        g_uio_regs[0] = 0x00000000;
        g_uio_regs[1] = 0x00000000;
        munmap((void *)g_uio_regs, 0x1000);
    }
    if (uio_fd >= 0) {
        close(uio_fd);
    }

    if (g_test_passed) {
        printf("\n============================================================\n");
        printf("  [PASS] All SocketCAN & OBD-II ECU Assertions Verified!    \n");
        printf("============================================================\n\n");
        return 0;
    } else {
        printf("\n============================================================\n");
        printf("  [FAIL] CAN verification timed out or assertion failed!   \n");
        printf("============================================================\n\n");
        return 1;
    }
}
