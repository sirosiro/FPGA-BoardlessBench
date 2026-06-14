CC ?= gcc
CFLAGS += -Wall -Wextra -I../../../src/include

MCORE_SRCS = mcore_rtos.c \
             FreeRTOS-Kernel/tasks.c \
             FreeRTOS-Kernel/queue.c \
             FreeRTOS-Kernel/list.c \
             FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/port.c \
             FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
             FreeRTOS-Kernel/portable/MemMang/heap_3.c

all: FreeRTOS-Kernel test_bin mcore_rtos.elf

FreeRTOS-Kernel:
	@if [ ! -f "FreeRTOS-Kernel/tasks.c" ]; then \
		echo "[Makefile] Cloning FreeRTOS-Kernel (sparse, depth 1)..."; \
		rm -rf FreeRTOS-Kernel; \
		git clone --depth 1 --filter=blob:none --sparse https://github.com/FreeRTOS/FreeRTOS-Kernel.git FreeRTOS-Kernel; \
		cd FreeRTOS-Kernel && git sparse-checkout set include portable/ThirdParty/GCC/Posix portable/MemMang tasks.c queue.c list.c; \
	fi

FreeRTOS-Kernel/%: FreeRTOS-Kernel
	@:

test_bin: main.c
	$(CC) $(CFLAGS) -o $@ $<

mcore_rtos.elf: FreeRTOS-Kernel $(MCORE_SRCS)
	$(CC) $(CFLAGS) -IFreeRTOS-Kernel/include -IFreeRTOS-Kernel/portable/ThirdParty/GCC/Posix -I. $(MCORE_SRCS) -o $@ -lpthread -lrt

clean:
	rm -f test_bin mcore_rtos.elf *.o

distclean cleanall: clean
	rm -rf FreeRTOS-Kernel

.PHONY: all clean distclean cleanall FreeRTOS-Kernel
