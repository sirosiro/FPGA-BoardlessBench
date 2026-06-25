#![no_std]
#![no_main]

mod fbb_pac;

// リンク時にプラットフォームごとに解決される外部インターフェース
extern "C" {
    fn delay_ms(ms: u32);
}

#[no_mangle]
pub extern "C" fn main() -> ! {
    let dp = fbb_pac::Peripherals::take().unwrap();
    let vfpga = dp.vfpga;

    loop {
        // Poll for command from A-Core
        let cmd = vfpga.cmd.read();
        if cmd == 0xA1 {
            let data = vfpga.data_in.read();
            
            // Simulate computation delay (call link-time resolved function)
            unsafe {
                delay_ms(100);
            }
            
            let result = data * 2;
            
            // Write output register
            vfpga.data_out.write(result);
            // Signal READY status to A-Core
            vfpga.status.write(0x01);
            
            // Clear the command to acknowledge receipt
            vfpga.cmd.write(0);
        }
        // Delay loop to prevent 100% CPU usage
        unsafe {
            delay_ms(10);
        }
    }
}
