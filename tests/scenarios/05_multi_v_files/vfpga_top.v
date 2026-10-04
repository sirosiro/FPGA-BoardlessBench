/* 
 * 【解説: トップモジュール vfpga_top】
 * AXI-Lite 相当のバスインターフェースを持ち、内部でサブモジュールを制御します。
 */
module vfpga_top (
    input wire clk,          // クロック
    input wire rst_n,        // リセット（負論理）
    input wire [31:0] addr,  // アドレス
    input wire [31:0] w_data, // 書き込みデータ
    input wire w_en,         // 書き込み有効信号
    output wire [31:0] r_data // 読み出しデータ
);
    reg [31:0] reg0;
    wire [31:0] sub_out;

    /* 
     * 【解説: サブモジュールのインスタンス化】
     * 別のファイル (sub_logic.v) で定義されたモジュールを呼び出します。
     * REG0 の値と、安定した固定値を入力として渡します。
     */
    sub_logic u_sub (
        .in_a(reg0),
        /* 
         * 【解説: バス設計原則と固定値 0 の使用理由】
         * 実機FPGAの標準的なバス（AXI-Lite等）において、書き込みデータ線 w_data (WDATA) は
         * 書き込み有効時 (w_en=1) のみ値が保証される信号です。
         * レジスタ（FF）を介さずに生のバス線 w_data を組み合わせ回路に入力し、
         * その結果を読み出しデータ (r_data) に直結すると、実機ハードウェアであっても
         * 読み出しサイクル中に w_data が不定値となり演算結果が化けてしまいます
         * （本シミュレータ環境でも同様に不安定になります）。
         * 正統な設計ではレジスタで保持して渡しますが、本シナリオでは「複数ファイルの結合」
         * の学習に焦点を絞るため、余計なレジスタを増やさず安定した固定値 0 を使用しています。
         */
        .in_b(32'h0),
        .out_y(sub_out)
    );

    /* 【解説: レジスタ書き込みロジック】 */
    always @(posedge clk) begin
        if (!rst_n) begin
            reg0 <= 32'h0;
        end else if (w_en && addr == 32'h40000000) begin
            reg0 <= w_data;
        end
    end

    /* 
     * 【解説: 読み出しロジック】
     * アドレス 0x4 (REG1) を読み出した際に、サブモジュールの結果を返します。
     */
    assign r_data = (addr == 32'h40000004) ? sub_out : reg0;

endmodule
