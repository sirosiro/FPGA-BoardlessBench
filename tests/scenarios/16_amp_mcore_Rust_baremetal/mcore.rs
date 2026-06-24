#![no_std]
#![no_main]

// リンク時にプラットフォームごとに解決される外部インターフェース
extern "C" {
    fn delay_ms(ms: u32);
}

// UIO0 register base addresses mapped by libfpgashim
// These correspond to the registers defined in config.dts
const REG_CMD: *mut u32 = 0x40000010 as *mut u32;
const REG_STATUS: *mut u32 = 0x40000014 as *mut u32;
const REG_DATA_IN: *mut u32 = 0x40000018 as *mut u32;
const REG_DATA_OUT: *mut u32 = 0x4000001c as *mut u32;

#[no_mangle]
pub extern "C" fn main() -> ! {
    loop {
        unsafe {
            // Poll for command from A-Core
            let cmd = core::ptr::read_volatile(REG_CMD);
            if cmd == 0xA1 {
                let data = core::ptr::read_volatile(REG_DATA_IN);
                
                // Simulate computation delay (call link-time resolved function)
                delay_ms(100);
                
                let result = data * 2;
                
                // Write output register
                core::ptr::write_volatile(REG_DATA_OUT, result);
                // Signal READY status to A-Core
                core::ptr::write_volatile(REG_STATUS, 0x01);
                
                // Clear the command to acknowledge receipt
                core::ptr::write_volatile(REG_CMD, 0);
            }
            // Delay loop to prevent 100% CPU usage
            delay_ms(10);
        }
    }
}
