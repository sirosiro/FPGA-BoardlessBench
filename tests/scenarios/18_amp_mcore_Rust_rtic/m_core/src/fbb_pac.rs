// Auto-generated PAC (Peripheral Access Crate)
#![allow(unused)]

pub struct Register<T> {
    ptr: *mut T,
}

impl<T> Register<T> {
    pub const fn new(address: usize) -> Self {
        Self { ptr: address as *mut T }
    }
    
    pub fn read(&self) -> T where T: Copy {
        unsafe { core::ptr::read_volatile(self.ptr) }
    }
    
    pub fn write(&self, value: T) where T: Copy {
        unsafe { core::ptr::write_volatile(self.ptr, value) }
    }
}

pub struct Vfpga {
    pub cmd: Register<u32>,
    pub status: Register<u32>,
    pub data_in: Register<u32>,
    pub data_out: Register<u32>,
    pub timer_target: Register<u32>,
    pub timer_current: Register<u32>,
    pub timer_irq: Register<u32>,
}

impl Vfpga {
    pub const fn new() -> Self {
        Self {
            cmd: Register::new(0x40000010),
            status: Register::new(0x40000014),
            data_in: Register::new(0x40000018),
            data_out: Register::new(0x4000001c),
            timer_target: Register::new(0x40000020),
            timer_current: Register::new(0x40000024),
            timer_irq: Register::new(0x40000028),
        }
    }
}

pub struct Peripherals {
    pub vfpga: Vfpga,
}

static mut TAKEN: bool = false;

impl Peripherals {
    pub fn take() -> Option<Self> {
        unsafe {
            if TAKEN {
                None
            } else {
                TAKEN = true;
                Some(Self {
                    vfpga: Vfpga::new(),
                })
            }
        }
    }
}
