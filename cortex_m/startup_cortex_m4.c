/* Minimal Cortex-M4 reset/vector-table startup for the cross-compilation
 * validation smoke target. This is intentionally not a vendor CMSIS/HAL
 * startup file: it exists only so the smoke test links into a runnable
 * bare-metal image for footprint measurement, not to run on real hardware.
 *
 * ARMv7-M loads the initial main stack pointer from vector-table word 0 and
 * the initial program counter (Reset_Handler) from word 1 directly in
 * hardware before executing any instruction, so no hand-written assembly
 * reset preamble is needed here -- this file is plain C.
 */

#include <stdint.h>

extern uint32_t _sidata; /* Start of .data initializer values in flash. */
extern uint32_t _sdata;  /* Start of .data in RAM. */
extern uint32_t _edata;  /* End of .data in RAM. */
extern uint32_t _sbss;   /* Start of .bss in RAM. */
extern uint32_t _ebss;   /* End of .bss in RAM. */
extern uint32_t _estack; /* Initial main stack pointer (top of RAM). */

int main(void);

void Reset_Handler(void);
static void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

/* ISO C has no portable way to express "either an initial stack-pointer
 * value or an exception-handler function pointer" as a single scalar type:
 * casting the &_estack object pointer to a function-pointer type is a
 * constraint violation under -Wpedantic. A union of the two pointer kinds
 * lets each vector-table entry be initialized as whichever kind it
 * actually is, with no invalid cast in either case. */
typedef union {
    void (*handler)(void);
    void *initial_sp;
} quarry_isr_entry_t;

__attribute__((section(".isr_vector"),
                used)) const quarry_isr_entry_t g_quarry_isr_vector[] = {
    {.initial_sp = &_estack},
    {.handler = Reset_Handler},
    {.handler = NMI_Handler},
    {.handler = HardFault_Handler},
    {.handler = MemManage_Handler},
    {.handler = BusFault_Handler},
    {.handler = UsageFault_Handler},
    {.handler = 0},
    {.handler = 0},
    {.handler = 0},
    {.handler = 0},
    {.handler = SVC_Handler},
    {.handler = DebugMon_Handler},
    {.handler = 0},
    {.handler = PendSV_Handler},
    {.handler = SysTick_Handler},
};

static void Default_Handler(void) {
    for (;;) {
        /* Trap: no hosted facility (no _exit) is appropriate here. */
    }
}

void Reset_Handler(void) {
    uint32_t *source = &_sidata;
    uint32_t *destination = &_sdata;
    while (destination < &_edata) {
        *destination++ = *source++;
    }

    destination = &_sbss;
    while (destination < &_ebss) {
        *destination++ = 0U;
    }

    (void)main();

    for (;;) {
        /* main() should never return on bare metal; trap if it does. */
    }
}
