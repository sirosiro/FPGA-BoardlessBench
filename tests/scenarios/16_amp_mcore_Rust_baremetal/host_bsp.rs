#![no_std]
#![no_main]

mod fbb_pac;

// Host simulation implementation of delay_ms using RTL hardware timer
#[no_mangle]
pub unsafe extern "C" fn delay_ms(ms: u32) {
    let vfpga = fbb_pac::Vfpga::new();
    // Set timer target: 1ms delay maps to 10 timer clock cycles
    vfpga.timer_target.write(ms * 10);

    // Poll TIMER_IRQ until it becomes 1 (timeout)
    while vfpga.timer_irq.read() == 0 {
        core::hint::spin_loop();
    }
}

// Mandatory panic handler for no_std Rust
use core::panic::PanicInfo;
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

// Dummy personality routine for exception handling/unwinding required by libcore
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}
