#ifndef CMSIS_COMPILER_H
#define CMSIS_COMPILER_H

#define __ASM            __asm
#define __VOLATILE       volatile

#ifndef __STATIC_INLINE
#define __STATIC_INLINE  static inline
#endif

#ifndef __WEAK
#define __WEAK           __attribute__((weak))
#endif

#ifdef __get_IPSR
#undef __get_IPSR
#endif
#define __get_IPSR()     (0U)

#ifdef __get_CONTROL
#undef __get_CONTROL
#endif
#define __get_CONTROL()  (0U)

#ifndef __get_PRIMASK
#define __get_PRIMASK()  (0U)
#endif

#ifndef __disable_irq
#define __disable_irq()  do {} while(0)
#endif

#ifndef __enable_irq
#define __enable_irq()   do {} while(0)
#endif

#endif /* CMSIS_COMPILER_H */
