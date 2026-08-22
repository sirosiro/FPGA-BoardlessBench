/*
 * 【解説: UIO 割込対応 FPGA ハードウェアタイマー回路】
 * タイマー有効化(CTRL[0] = 1)の時、一定周期(10クロック)ごとに
 * STATUS[0] (IRQ アサートフラグ) を 1 にし、irq_out 信号を立ち上げます。
 * FW 側が INT_ACK レジスタに 1 を書き込むと STATUS[0] および irq_out が 0 にクリアされます。
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

    reg [31:0] CTRL;
    reg [31:0] STATUS;
    reg [31:0] CNT;

    // Write Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            CTRL   <= 32'h0;
            STATUS <= 32'h0;
        end else if (w_en) begin
            case (addr)
                32'h40000000: CTRL <= w_data;
                32'h40000008: begin
                    // INT_ACK クリア処理
                    if (w_data[0]) begin
                        STATUS[0] <= 1'b0;
                    end
                end
                default: ;
            endcase
        end
    end

    // Read Logic
    always @(*) begin
        case (addr)
            32'h40000000: r_data = CTRL;
            32'h40000004: r_data = STATUS;
            32'h4000000C: r_data = CNT;
            default: r_data = 32'hdeadbeef;
        endcase
    end

    // Timer & IRQ Logic
    reg [15:0] timer_divider;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            timer_divider <= 16'd0;
            CNT           <= 32'd0;
            STATUS[0]     <= 1'b0;
        end else if (CTRL[0]) begin
            if (timer_divider >= 16'd2000) begin
                timer_divider <= 16'd0;
                CNT           <= CNT + 1;
                STATUS[0]     <= 1'b1; // Trigger IRQ
            end else begin
                timer_divider <= timer_divider + 1;
            end
        end
    end

    always @(*) begin
        irq_out = STATUS[0];
    end

endmodule
