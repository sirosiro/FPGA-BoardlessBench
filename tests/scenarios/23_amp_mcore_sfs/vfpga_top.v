/*
 * Scenario 23: Programmable Hardware Timer IP for M-Core SFS (vfpga_timer@40000000)
 * Pure hardware IP: generates mcore_irq_out when timer_div reaches TIMER_PERIOD.
 * AMP shared memory (0x40001000+) is intentionally unmapped in RTL (returns 32'hdeadbeef)
 * so sim_main.cpp treats amp_shared_mem as pure software-shared RAM (OCM / AXI BRAM).
 */
module vfpga_top (
    input  wire        clk,
    input  wire        rst_n,
    /* verilator lint_off UNUSED */
    input  wire [31:0] addr,
    input  wire [31:0] w_data,
    /* verilator lint_on UNUSED */
    input  wire        w_en,
    output reg  [31:0] r_data,
    output wire        mcore_irq_out
);

    reg [31:0] TIMER_CTRL;
    reg [31:0] TIMER_PERIOD;
    reg [31:0] TIMER_STATUS;
    reg [31:0] TIMER_HW_CNT;
    reg [31:0] timer_div;

    assign mcore_irq_out = TIMER_STATUS[0];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            TIMER_CTRL   <= 32'h0;
            TIMER_PERIOD <= 32'h0;
            TIMER_STATUS <= 32'h0;
            TIMER_HW_CNT <= 32'h0;
            timer_div    <= 32'h0;
        end else begin
            if (w_en) begin
                case (addr)
                    32'h40000000: begin
                        TIMER_CTRL <= w_data;
                        if (!w_data[0]) begin
                            timer_div       <= 32'h0;
                            TIMER_STATUS[0] <= 1'b0;
                        end
                    end
                    32'h40000004: begin
                        TIMER_PERIOD <= w_data;
                        timer_div    <= 32'h0;
                    end
                    32'h4000000C: begin
                        if (w_data[0]) begin
                            TIMER_STATUS[0] <= 1'b0;
                        end
                    end
                    default: ;
                endcase
            end

            if (TIMER_CTRL[0] && (TIMER_PERIOD != 32'd0)) begin
                if (timer_div + 32'd1 >= TIMER_PERIOD) begin
                    timer_div       <= 32'd0;
                    TIMER_HW_CNT    <= TIMER_HW_CNT + 32'd1;
                    TIMER_STATUS[0] <= 1'b1;
                end else begin
                    timer_div <= timer_div + 32'd1;
                end
            end
        end
    end

    always @(*) begin
        case (addr)
            32'h40000000: r_data = TIMER_CTRL;
            32'h40000004: r_data = TIMER_PERIOD;
            32'h40000008: r_data = TIMER_STATUS;
            32'h4000000C: r_data = 32'h0; /* WO register: always reads 0 to auto-clear SHM */
            32'h40000010: r_data = TIMER_HW_CNT;
            default:      r_data = 32'hdeadbeef;
        endcase
    end

endmodule
