`timescale 1ns / 1ps

/* verilator lint_off UNUSED */
module vfpga_top (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [31:0] addr,
    input  wire [31:0] w_data,
    input  wire        w_en,
    output reg  [31:0] r_data,
    output wire        irq_out,
    input  wire [117:0] l_pins_i,
    output wire [117:0] l_pins_o,
    output wire [117:0] l_pins_t
);

    assign l_pins_o = 118'b0;
    assign l_pins_t = 118'hFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF;
    wire [117:0] _unused = l_pins_i;

    reg [31:0] reg_ctrl;
    reg [31:0] reg_status;

    assign irq_out = reg_status[0];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_ctrl   <= 32'h0;
            reg_status <= 32'h0;
            r_data     <= 32'h0;
        end else begin
            if (w_en) begin
                if (addr == 32'h40000000) reg_ctrl <= w_data;
                if (addr == 32'h40000008 && w_data == 32'h1) reg_status[0] <= 1'b0; // ACK
            end
            
            // Read handling
            case (addr)
                32'h40000000: r_data <= reg_ctrl;
                32'h40000004: r_data <= reg_status;
                default:     r_data <= 32'h0;
            endcase
        end
    end

endmodule
/* verilator lint_on UNUSED */
