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

    // Connect DR/GDIR to external 118-pin interface
    // GDIR=1 (output) maps to l_pins_t=0 (output)
    assign l_pins_o = { {86{1'b0}}, DR };
    assign l_pins_t = { {86{1'b1}}, ~GDIR };

    // Write Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            DR   <= 32'h0;
            GDIR <= 32'h0; // All inputs by default on reset (0 = input)
        end else if (w_en) begin
            case (addr)
                // i.MX 8M Plus GPIO1 (0x30200000) & i.MX95 GPIO1 (0x47400000)
                32'h30200000, 32'h47400000: DR   <= w_data;
                32'h30200004, 32'h47400004: GDIR <= w_data;
                default: ;
            endcase
        end
    end

    // Read Logic
    always @(*) begin
        case (addr)
            // DATA register (offset 0x00): reads DR directly to reflect both output writes and dashboard input injections
            32'h30200000, 32'h47400000: r_data = DR;
            // GDIR register (offset 0x04): reads original NXP direction values
            32'h30200004, 32'h47400004: r_data = GDIR;
            // TRI register (offset 0x08): virtual register for F-BB dashboard, returns inverted GDIR
            32'h30200008, 32'h47400008: r_data = ~GDIR;
            default:                    r_data = 32'hDEADBEEF;
        endcase
    end

endmodule
