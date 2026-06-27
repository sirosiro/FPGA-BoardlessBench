use std::sync::atomic::{AtomicBool, Ordering};

extern "C" {
    fn on_sigusr1_interrupt();
}

static INTERRUPT_PENDING: AtomicBool = AtomicBool::new(false);

extern "C" fn handle_sigusr1(_sig: libc::c_int) {
    // Set pending flag safely (asynchronous signal safe)
    INTERRUPT_PENDING.store(true, Ordering::SeqCst);
}

pub fn init_interrupts() {
    unsafe {
        libc::signal(libc::SIGUSR1, handle_sigusr1 as *const () as usize);
    }
}

pub fn idle_delay(ms: u32) {
    // Check and clear pending interrupt flag, then dispatch to task (ISR)
    if INTERRUPT_PENDING.swap(false, Ordering::SeqCst) {
        unsafe {
            on_sigusr1_interrupt();
        }
    }
    // Prevent 100% CPU usage
    std::thread::sleep(std::time::Duration::from_millis(ms as u64));
}
