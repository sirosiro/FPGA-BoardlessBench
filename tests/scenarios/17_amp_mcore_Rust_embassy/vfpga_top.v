module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data
);

    reg [31:0] CMD;
    reg [31:0] STATUS;
    reg [31:0] DATA_IN;
    reg [31:0] DATA_OUT;
    reg [31:0] TIMER_TARGET;
    reg [31:0] TIMER_CURRENT;
    reg [31:0] TIMER_IRQ;

    // Write & Count Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            CMD           <= 32'h0;
            STATUS        <= 32'h0;
            DATA_IN       <= 32'h0;
            DATA_OUT      <= 32'h0;
            TIMER_TARGET  <= 32'h0;
            TIMER_CURRENT <= 32'h0;
            TIMER_IRQ     <= 32'h0;
        end else begin
            if (w_en) begin
                case (addr)
                    32'h40000010: CMD           <= w_data;
                    32'h40000014: STATUS        <= w_data;
                    32'h40000018: DATA_IN       <= w_data;
                    32'h4000001C: DATA_OUT      <= w_data;
                    32'h40000020: begin
                        TIMER_TARGET  <= w_data;
                        TIMER_CURRENT <= 32'h0;
                        TIMER_IRQ     <= 32'h0;
                    end
                    32'h40000024: TIMER_CURRENT <= w_data;
                    32'h40000028: TIMER_IRQ     <= w_data;
                    default: ;
                endcase
            end

            // Timer counting logic
            if (!(w_en && addr == 32'h40000020) && !(w_en && addr == 32'h40000024) && !(w_en && addr == 32'h40000028)) begin
                if (TIMER_TARGET != 32'h0 && TIMER_CURRENT < TIMER_TARGET) begin
                    TIMER_CURRENT <= TIMER_CURRENT + 1;
                    if (TIMER_CURRENT + 1 >= TIMER_TARGET) begin
                        TIMER_IRQ <= 32'h1;
                    end
                end
            end
        end
    end

    // Read Logic
    always @(*) begin
        case (addr)
            32'h40000010: r_data = CMD;
            32'h40000014: r_data = STATUS;
            32'h40000018: r_data = DATA_IN;
            32'h4000001C: r_data = DATA_OUT;
            32'h40000020: r_data = TIMER_TARGET;
            32'h40000024: r_data = TIMER_CURRENT;
            32'h40000028: r_data = TIMER_IRQ;
            default: r_data = 32'hdeadbeef;
        endcase
    end

endmodule
