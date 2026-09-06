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
    assign irq_out = 1'b0;
    wire [117:0] _unused = l_pins_i;

    reg [31:0] reg_ctrl;
    reg [31:0] reg_counter;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_ctrl    <= 32'h0;
            reg_counter <= 32'h0;
        end else begin
            if (reg_ctrl[0]) begin
                reg_counter <= reg_counter + 1;
            end
            if (w_en) begin
                case (addr[7:0])
                    8'h00: reg_ctrl <= w_data;
                    8'h08: reg_counter <= w_data;
                    default: ;
                endcase
            end
        end
    end

    always @(*) begin
        case (addr[7:0])
            8'h00: r_data = reg_ctrl;
            8'h04: r_data = 32'hA5A50001; // STATUS
            8'h08: r_data = reg_counter;
            default: r_data = 32'hDEADBEEF;
        endcase
    end

endmodule
