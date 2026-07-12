module vfpga_top(
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    /* verilator lint_off UNUSED */
    input wire [31:0] w_data,
    /* verilator lint_on UNUSED */
    input wire w_en,
    output reg [31:0] r_data,
    
    // PL-side SPI Pins
    output reg pl_spi_sclk = 1'b0,
    output reg pl_spi_mosi = 1'b0,
    output reg pl_spi_cs_n = 1'b1,
    input wire pl_spi_miso,

    // 118-pin standard interface for Dashboard mapping
    output wire [117:0] l_pins_o,
    /* verilator lint_off UNUSED */
    input wire [117:0] l_pins_i,
    /* verilator lint_on UNUSED */
    output wire [117:0] l_pins_t
);

    // Register maps
    // 0x40000000: REG_SPI_TX (RW)
    // 0x40000004: REG_SPI_RX (RO)
    // 0x40000008: REG_SPI_CTRL (RW) - Bit 0 starts transfer
    // 0x4000000c: REG_SPI_STATUS (RO) - Bit 0 is busy flag
    reg [7:0] r_spi_tx = 8'd0;
    reg [7:0] r_spi_rx = 8'd0;
    reg r_spi_ctrl = 1'b0;
    reg r_spi_status = 1'b0;

    // SPI controller state machine
    reg [3:0] state = 4'd0; // STATE_IDLE
    reg [3:0] bit_cnt = 4'd0;
    reg [2:0] sclk_cnt = 3'd0; // clock divider
    reg [7:0] shift_tx = 8'd0;
    reg [7:0] shift_rx = 8'd0;

    localparam STATE_IDLE      = 4'd0;
    localparam STATE_CS_LOW    = 4'd1;
    localparam STATE_SCLK_LOW  = 4'd2;
    localparam STATE_SCLK_HIGH = 4'd3;
    localparam STATE_CS_HIGH   = 4'd4;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r_spi_tx <= 8'd0;
            r_spi_rx <= 8'd0;
            r_spi_ctrl <= 1'b0;
            r_spi_status <= 1'b0;
            
            pl_spi_sclk <= 1'b0;
            pl_spi_mosi <= 1'b0;
            pl_spi_cs_n <= 1'b1;
            
            state <= STATE_IDLE;
            bit_cnt <= 4'd0;
            sclk_cnt <= 3'd0;
            shift_tx <= 8'd0;
            shift_rx <= 8'd0;
        end else begin
            // Write Register Logic (from AXI / Host)
            if (w_en) begin
                case (addr)
                    32'h40000000: r_spi_tx <= w_data[7:0];
                    32'h40000008: begin
                        if (state == STATE_IDLE) begin
                            r_spi_ctrl <= w_data[0];
                        end
                    end
                endcase
            end else begin
                // auto clear control bit when active
                if (state == STATE_CS_LOW) begin
                    r_spi_ctrl <= 1'b0;
                end
            end

            // SPI Controller State Machine logic
            case (state)
                STATE_IDLE: begin
                    pl_spi_cs_n <= 1'b1;
                    pl_spi_sclk <= 1'b0;
                    r_spi_status <= 1'b0;
                    if (r_spi_ctrl) begin
                        r_spi_status <= 1'b1;
                        shift_tx <= r_spi_tx;
                        bit_cnt <= 4'd0;
                        state <= STATE_CS_LOW;
                    end
                end

                STATE_CS_LOW: begin
                    pl_spi_cs_n <= 1'b0;
                    sclk_cnt <= 3'd0;
                    // output first MSB bit
                    pl_spi_mosi <= shift_tx[7];
                    shift_tx <= {shift_tx[6:0], 1'b0};
                    state <= STATE_SCLK_LOW;
                end

                STATE_SCLK_LOW: begin
                    pl_spi_sclk <= 1'b0;
                    if (sclk_cnt == 3'd3) begin
                        sclk_cnt <= 3'd0;
                        state <= STATE_SCLK_HIGH;
                    end else begin
                        sclk_cnt <= sclk_cnt + 1'b1;
                    end
                end

                STATE_SCLK_HIGH: begin
                    pl_spi_sclk <= 1'b1;
                    if (sclk_cnt == 3'd3) begin
                        sclk_cnt <= 3'd0;
                        // sample MISO on rising edge (or before falling)
                        shift_rx <= {shift_rx[6:0], pl_spi_miso};
                        bit_cnt <= bit_cnt + 1'b1;
                        if (bit_cnt == 4'd7) begin
                            state <= STATE_CS_HIGH;
                        end else begin
                            // setup next MOSI bit
                            pl_spi_mosi <= shift_tx[7];
                            shift_tx <= {shift_tx[6:0], 1'b0};
                            state <= STATE_SCLK_LOW;
                        end
                    end else begin
                        sclk_cnt <= sclk_cnt + 1'b1;
                    end
                end

                STATE_CS_HIGH: begin
                    pl_spi_sclk <= 1'b0;
                    pl_spi_cs_n <= 1'b1;
                    r_spi_rx <= shift_rx;
                    state <= STATE_IDLE;
                end
                
                default: begin
                    state <= STATE_IDLE;
                end
            endcase
        end
    end

    // Read Register Logic
    always @(*) begin
        case (addr)
            32'h40000000: r_data = {24'd0, r_spi_tx};
            32'h40000004: r_data = {24'd0, r_spi_rx};
            32'h40000008: r_data = {31'd0, r_spi_ctrl};
            32'h4000000c: r_data = {31'd0, r_spi_status};
            32'h40002000: r_data = {24'd0, l_pins_o[7:0]}; // Dummy GPIO DATA register output
            32'h40002004: r_data = 32'h0;                  // Dummy GPIO TRI register output (all outputs)
            default:      r_data = 32'd0;
        endcase
    end

    // Map SPI pins & controller state to Dashboard GPIO LEDs
    assign l_pins_o[0]      = pl_spi_sclk;
    assign l_pins_o[1]      = pl_spi_mosi;
    assign l_pins_o[2]      = pl_spi_cs_n;
    assign l_pins_o[3]      = pl_spi_miso;
    assign l_pins_o[7:4]    = state;
    assign l_pins_o[117:8]  = 110'h0;

    assign l_pins_t = 118'h0;

endmodule
