module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output wire [31:0] r_data
);
    /* verilator lint_off UNUSED */
    wire _dummy = clk | rst_n | (|addr) | (|w_data) | w_en;
    /* verilator lint_on UNUSED */
    assign r_data = 32'hdeadbeef;
endmodule
