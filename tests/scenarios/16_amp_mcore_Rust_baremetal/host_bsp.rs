#![no_std]
#![no_main]

// Declarations to import the Linux C library's sleep function
extern "C" {
    fn usleep(usecs: u32) -> i32;
}

// Host simulation implementation of delay_ms using usleep
#[no_mangle]
pub unsafe extern "C" fn delay_ms(ms: u32) {
    usleep(ms * 1000);
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
