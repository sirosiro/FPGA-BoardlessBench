/*
 * Scenario 01e: AXI Bus Contention & Audio DMA Latency Verification
 * 
 * Demonstrates:
 *   - Verilator ideal zero-latency DRAM vs. Real Zynq AXI bus contention
 *   - Tight 4-sample audio FIFO consuming 1 sample every 4 cycles (0.5us deadline)
 *   - Chaos Injection reproducing AXI slave latency stalls and buffer underruns
 */
module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data,

    // AXI4 Master Read Channel (to simulated DRAM via F-BB AxiStallEngine)
    output reg [31:0] m_axi_araddr,
    output reg m_axi_arvalid,
    input wire m_axi_arready,
    input wire [31:0] m_axi_rdata,
    input wire m_axi_rvalid,
    output reg m_axi_rready
);

    // Register file
    reg [31:0] cr_ctrl;        // 0x40000010: bit0=enable, bit1=fifo_reset
    reg [31:0] underrun_cnt;   // 0x40000018
    reg [31:0] samples_played; // 0x4000001C

    // UIO Write Logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cr_ctrl <= 32'h0;
        end else if (w_en) begin
            case (addr)
                32'h40000010: cr_ctrl <= w_data;
                default: ;
            endcase
        end
    end

    // UIO Read Logic
    always @(*) begin
        case (addr)
            32'h40000010: r_data = cr_ctrl;
            32'h40000014: r_data = {30'h0, (underrun_cnt > 0), cr_ctrl[0]};
            32'h40000018: r_data = underrun_cnt;
            32'h4000001C: r_data = samples_played;
            default:      r_data = 32'hdeadbeef;
        endcase
    end

    // Audio FIFO (Depth = 4)
    /* verilator lint_off UNUSED */
    reg [31:0] fifo_mem [0:3];
    reg [31:0] current_sample;
    /* verilator lint_on UNUSED */
    reg [2:0] fifo_count;
    reg [1:0] fifo_rd_ptr;
    reg [1:0] fifo_wr_ptr;

    // Audio Playback Pace Generator (every 4 cycles = 0.5us deadline at 8MHz)
    reg [2:0] pace_cnt;
    reg [31:0] next_dram_addr;
    reg axi_read_in_flight;

    wire sample_received = (m_axi_rvalid && m_axi_rready && axi_read_in_flight);
    wire sample_consumed = (pace_cnt == 3'd3 && fifo_count > 3'd0);
    wire underrun_hit    = (pace_cnt == 3'd3 && fifo_count == 3'd0);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fifo_count         <= 3'd0;
            fifo_rd_ptr        <= 2'd0;
            fifo_wr_ptr        <= 2'd0;
            current_sample     <= 32'd0;
            underrun_cnt       <= 32'd0;
            samples_played     <= 32'd0;
            pace_cnt           <= 3'd0;
            next_dram_addr     <= 32'h40000000;
            m_axi_araddr       <= 32'h0;
            m_axi_arvalid      <= 1'b0;
            m_axi_rready       <= 1'b1;
            axi_read_in_flight <= 1'b0;
        end else if (cr_ctrl[1]) begin
            // FIFO Reset command
            fifo_count         <= 3'd0;
            fifo_rd_ptr        <= 2'd0;
            fifo_wr_ptr        <= 2'd0;
            current_sample     <= 32'd0;
            underrun_cnt       <= 32'd0;
            samples_played     <= 32'd0;
            pace_cnt           <= 3'd0;
            axi_read_in_flight <= 1'b0;
            m_axi_arvalid      <= 1'b0;
        end else if (cr_ctrl[0]) begin
            // 1. Playback Pace Generator
            if (pace_cnt == 3'd3) begin
                pace_cnt <= 3'd0;
            end else begin
                pace_cnt <= pace_cnt + 3'd1;
            end

            // 2. Playback consumption or underrun detection
            if (sample_consumed) begin
                fifo_rd_ptr    <= fifo_rd_ptr + 2'd1;
                current_sample <= fifo_mem[fifo_rd_ptr];
                samples_played <= samples_played + 32'd1;
            end else if (underrun_hit) begin
                underrun_cnt   <= underrun_cnt + 32'd1;
            end

            // 3. FIFO refill request via AXI Master Read Channel
            if (!axi_read_in_flight && (fifo_count < 3'd4) && !m_axi_arvalid) begin
                m_axi_araddr       <= next_dram_addr;
                m_axi_arvalid      <= 1'b1;
                next_dram_addr     <= next_dram_addr + 32'd4;
                axi_read_in_flight <= 1'b1;
            end else if (m_axi_arvalid && m_axi_arready) begin
                m_axi_arvalid      <= 1'b0;
            end

            // 4. Sample receive from AXI Read Data Channel
            if (sample_received) begin
                fifo_mem[fifo_wr_ptr] <= m_axi_rdata;
                fifo_wr_ptr           <= fifo_wr_ptr + 2'd1;
                axi_read_in_flight    <= 1'b0;
            end

            // 5. FIFO occupancy tracking
            case ({sample_received, sample_consumed})
                2'b10: fifo_count <= fifo_count + 3'd1;
                2'b01: fifo_count <= fifo_count - 3'd1;
                default: ; // 2'b00 or 2'b11: unchanged
            endcase
        end
    end

endmodule
