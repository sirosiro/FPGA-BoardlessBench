`timescale 1ns / 100ps

module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data,
    /* verilator lint_off UNUSED */
    input  wire [117:0] l_pins_i,
    /* verilator lint_on UNUSED */
    output wire [117:0] l_pins_o,
    output wire [117:0] l_pins_t
);

    // GPIO Registers (NXP standard DR/GDIR)
    reg [31:0] DR;
    reg [31:0] GDIR;
    reg [31:0] PDIR;

    // Connect DR/GDIR to external 118-pin interface
    // GDIR=1 (output) maps to l_pins_t=0 (output)
    assign l_pins_o = { {86{1'b0}}, DR };
    assign l_pins_t = { {86{1'b1}}, ~GDIR };

    // Write Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            DR   <= 32'h0;
            GDIR <= 32'h0; // All inputs by default on reset (0 = input)
            PDIR <= 32'h0;
        end else if (w_en) begin
            case (addr)
                // i.MX 8M Plus GPIO1 (0x30200000) & i.MX95 GPIO1 (0x47400000)
                32'h30200000, 32'h47400000: DR   <= w_data;
                // i.MX95 PDIR (offset 0x04) - writable from dashboard for input simulation
                32'h47400004:               PDIR <= w_data;
                // i.MX 8M Plus GDIR (0x30200004) & i.MX95 PDDR (0x47400008)
                32'h30200004, 32'h47400008: GDIR <= w_data;
                default: ;
            endcase
        end
    end

    // Read Logic
    always @(*) begin
        case (addr)
            // i.MX 8M Plus DR / i.MX95 PDOR (offset 0x00)
            32'h30200000, 32'h47400000: r_data = DR;
            // i.MX 8M Plus GDIR (offset 0x04)
            32'h30200004:               r_data = GDIR;
            // i.MX95 PDIR (offset 0x04): reads internal PDIR register
            32'h47400004:               r_data = PDIR;
            // i.MX 8M Plus TRI (offset 0x08): virtual register returning ~GDIR
            32'h30200008:               r_data = ~GDIR;
            // i.MX95 PDDR (offset 0x08)
            32'h47400008:               r_data = GDIR;
            default:                    r_data = 32'hDEADBEEF;
        endcase
    end

endmodule
