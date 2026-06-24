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

    // Write Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            CMD      <= 32'h0;
            STATUS   <= 32'h0;
            DATA_IN  <= 32'h0;
            DATA_OUT <= 32'h0;
        end else if (w_en) begin
            case (addr)
                32'h40000010: CMD      <= w_data;
                32'h40000014: STATUS   <= w_data;
                32'h40000018: DATA_IN  <= w_data;
                32'h4000001C: DATA_OUT <= w_data;
                default: ;
            endcase
        end
    end

    // Read Logic
    always @(*) begin
        case (addr)
            32'h40000010: r_data = CMD;
            32'h40000014: r_data = STATUS;
            32'h40000018: r_data = DATA_IN;
            32'h4000001C: r_data = DATA_OUT;
            default: r_data = 32'hdeadbeef;
        endcase
    end

endmodule
