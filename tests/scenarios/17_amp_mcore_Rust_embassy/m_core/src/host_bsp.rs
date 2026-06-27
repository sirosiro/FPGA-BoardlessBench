use core::future::Future;
use core::pin::Pin;
use core::task::{Context, Poll};
use crate::fbb_pac::Vfpga;

pub struct DelayMs {
    target_ticks: u32,
    initialized: bool,
}

impl DelayMs {
    pub fn new(ms: u32) -> Self {
        Self {
            target_ticks: ms * 10, // 1ms delay maps to 10 timer clock cycles in F-BB RTL
            initialized: false,
        }
    }
}

impl Future for DelayMs {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let vfpga = Vfpga::new();
        if !self.initialized {
            vfpga.timer_target.write(self.target_ticks);
            self.initialized = true;
        }

        if vfpga.timer_irq.read() != 0 {
            Poll::Ready(())
        } else {
            // Wake up context again to poll in the next executor tick
            cx.waker().wake_by_ref();
            // Sleep slightly in the host thread to prevent 100% CPU usage
            std::thread::sleep(std::time::Duration::from_micros(100));
            Poll::Pending
        }
    }
}
