mod fbb_pac;
mod host_bsp;

use fbb_pac::Vfpga;
use host_bsp::DelayMs;

#[embassy_executor::task]
async fn command_processor_task() {
    let vfpga = Vfpga::new();
    println!("[M-Core Embassy] Command processor task started.");
    
    loop {
        // Poll for command from A-Core
        let cmd = vfpga.cmd.read();
        if cmd == 0xA1 {
            let data = vfpga.data_in.read();
            println!("[M-Core Embassy] Received command 0xA1, data: {}", data);
            
            // Simulate computation delay via F-BB RTL timer (async sleep)
            DelayMs::new(100).await;
            
            let result = data * 3; // Use multiplier 3 to distinguish from baremetal scenario (*2)
            
            // Write output register
            vfpga.data_out.write(result);
            // Signal READY status to A-Core
            vfpga.status.write(0x01);
            // Clear the command to acknowledge receipt
            vfpga.cmd.write(0);
            println!("[M-Core Embassy] Processed. Result: {}", result);
        }
        
        // Low overhead sleep using RTL timer
        DelayMs::new(10).await;
    }
}

fn main() {
    println!("[M-Core Embassy] Starting Embassy Executor on Host PC...");
    let executor = Box::leak(Box::new(embassy_executor::Executor::new()));
    executor.run(|spawner| {
        spawner.spawn(command_processor_task().unwrap());
    });
}
