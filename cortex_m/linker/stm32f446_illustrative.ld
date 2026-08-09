/* Minimal, illustrative linker script for the Cortex-M cross-compilation
 * validation smoke target.
 *
 * The FLASH/RAM sizes and origins below are representative of an
 * STM32F446-class MCU (512 KiB flash at 0x08000000, 128 KiB SRAM at
 * 0x20000000) but this is NOT a vendor-supplied or board-specific linker
 * script, and this project is not a board support package. It exists only
 * to produce a linkable bare-metal ELF for build/footprint validation, not
 * for flashing or execution on real hardware.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Illustrative reserved stack; not measured or enforced by this build. */
_min_stack_size = 4K;
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        . = . + _min_stack_size;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.eh_frame)
    }
}
