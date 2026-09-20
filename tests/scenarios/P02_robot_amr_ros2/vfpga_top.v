/*
 * ==============================================================================
 * Scenario P02: ros2_control Autonomous Mobile Robot (AMR) Core (vfpga_top.v)
 * ==============================================================================
 * Implements:
 * 1. 1kHz Periodic Timer with UIO IRQ assertion (status bit 2 & irq_out).
 * 2. INT_ACK clear mechanism.
 * 3. E-STOP Fail-safe: CONTROL[1] immediately clamps PWM to 0 and asserts FAULT.
 * 4. Dual-channel DC Motor + Quadrature Encoder pulse emulation loop.
 * 5. Free-running 1kHz cycle tick counter (CYCLE_CNT).
 * ==============================================================================
 */

module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data,
    output reg irq_out
);

    // Register Declarations
    reg [31:0] STATUS;                // 0x00: RO (bit0: FAULT/ESTOP, bit1: ENABLED, bit2: IRQ_PENDING)
    reg signed [31:0] LEFT_ENCODER;   // 0x04: RO (signed 32-bit accumulator)
    reg signed [31:0] RIGHT_ENCODER;  // 0x08: RO (signed 32-bit accumulator)
    reg [31:0] CONTROL;               // 0x10: RW (bit0: RUN, bit1: ESTOP, bit2: RESET, bit3: TEST_LOAD)
    reg signed [31:0] LEFT_PWM;       // 0x14: RW (signed -1000 ~ +1000)
    reg signed [31:0] RIGHT_PWM;      // 0x18: RW (signed -1000 ~ +1000)
    reg [31:0] CYCLE_CNT;             // 0x1C: RO

    // 1kHz Timer Divider (10 cycles per ms in simulation)
    reg [15:0] timer_divider;

    // Bus Write Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            CONTROL       <= 32'h0;
            LEFT_PWM      <= 32'sd0;
            RIGHT_PWM     <= 32'sd0;
            STATUS        <= 32'h0;
            LEFT_ENCODER  <= 32'sd0;
            RIGHT_ENCODER <= 32'sd0;
            CYCLE_CNT     <= 32'd0;
            timer_divider <= 16'd0;
        end else begin
            // Handle Register Writes
            if (w_en) begin
                case (addr)
                    32'h4000000C: begin
                        // INT_ACK: Writing 1 to bit 0 clears the pending IRQ
                        if (w_data[0]) begin
                            STATUS[2] <= 1'b0;
                        end
                    end
                    32'h40000010: begin
                        CONTROL <= w_data;
                        // Synchronous reset of encoder accumulators
                        if (w_data[2]) begin
                            LEFT_ENCODER  <= 32'sd0;
                            RIGHT_ENCODER <= 32'sd0;
                        end
                    end
                    32'h40000014: begin
                        // Clamp and assign Left PWM
                        if (!CONTROL[1]) begin
                            LEFT_PWM <= w_data;
                        end
                    end
                    32'h40000018: begin
                        // Clamp and assign Right PWM
                        if (!CONTROL[1]) begin
                            RIGHT_PWM <= w_data;
                        end
                    end
                    // TEST_LOAD feature: If CONTROL[3] is high, allow preloading encoders for wrap-around testing
                    32'h40000004: begin
                        if (CONTROL[3]) LEFT_ENCODER <= w_data;
                    end
                    32'h40000008: begin
                        if (CONTROL[3]) RIGHT_ENCODER <= w_data;
                    end
                    default: ;
                endcase
            end

            // Hardware E-STOP immediate override
            if (CONTROL[1]) begin
                LEFT_PWM  <= 32'sd0;
                RIGHT_PWM <= 32'sd0;
                STATUS[0] <= 1'b1; // FAULT asserted
                STATUS[1] <= 1'b0; // ENABLED dropped
            end else begin
                STATUS[0] <= 1'b0; // FAULT cleared
                STATUS[1] <= CONTROL[0]; // ENABLED tracks RUN
            end

            // 1kHz Periodic Timer & Motor Simulation Logic
            if (CONTROL[0]) begin
                if (timer_divider >= 16'd10) begin
                    timer_divider <= 16'd0;
                    CYCLE_CNT     <= CYCLE_CNT + 1;
                    STATUS[2]     <= 1'b1; // Trigger IRQ

                    // Simulate motor dynamics when not in E-STOP
                    if (!CONTROL[1]) begin
                        LEFT_ENCODER  <= LEFT_ENCODER + {{16{LEFT_PWM[15]}}, LEFT_PWM[15:0]};
                        RIGHT_ENCODER <= RIGHT_ENCODER + {{16{RIGHT_PWM[15]}}, RIGHT_PWM[15:0]};
                    end
                end else begin
                    timer_divider <= timer_divider + 1;
                end
            end else begin
                timer_divider <= 16'd0;
            end

            // Hardware E-STOP immediate cutoff
            if (CONTROL[1]) begin
                LEFT_PWM  <= 32'd0;
                RIGHT_PWM <= 32'd0;
            end
        end
    end

    // Bus Read Logic
    always @(*) begin
        case (addr)
            32'h40000000: r_data = STATUS;
            32'h40000004: r_data = LEFT_ENCODER;
            32'h40000008: r_data = RIGHT_ENCODER;
            32'h4000000C: r_data = 32'h0;
            32'h40000010: r_data = CONTROL;
            32'h40000014: r_data = LEFT_PWM;
            32'h40000018: r_data = RIGHT_PWM;
            32'h4000001C: r_data = CYCLE_CNT;
            default:      r_data = 32'hdeadbeef;
        endcase
    end

    // IRQ Output line
    always @(*) begin
        irq_out = STATUS[2];
    end

endmodule
