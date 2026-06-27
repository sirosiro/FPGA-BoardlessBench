mod fbb_pac;
mod host_bsp;

use fbb_pac::Vfpga;

// Task bound to virtual interrupt (SIGUSR1) in RTIC model
#[no_mangle]
pub extern "C" fn on_sigusr1_interrupt() {
    let vfpga = Vfpga::new();
    let cmd = vfpga.cmd.read();
    
    if cmd == 0xA1 {
        let data = vfpga.data_in.read();
        println!("[M-Core RTIC] Interrupt (SIGUSR1) handler triggered. Input: {}", data);
        
        // Process data (multiplier 4 to verify RTIC scenario)
        let result = data * 4;
        
        // Write results to virtual registers
        vfpga.data_out.write(result);
        vfpga.status.write(0x01); // READY
        vfpga.cmd.write(0);      // Clear command
        
        println!("[M-Core RTIC] Processed. Result: {}", result);
    }
}

fn main() {
    println!("[M-Core RTIC] Starting RTIC Task Manager on Host PC...");
    
    // Hook SIGUSR1 as virtual interrupt (IRQ)
    host_bsp::init_interrupts();
    
    // Idle loop
    loop {
        // Idle task running
        host_bsp::idle_delay(5);
    }
}
