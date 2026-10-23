/* link.ld - a .um2 program: one RAM image at 0x20000000 (the UVMC2 loads it there and starts it from its vector table) */
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K }
ENTRY (reset_handler)
SECTIONS
{
	.text : { KEEP (*(.vectors)) *(.text*) *(.rodata*) } > RAM
	.data : { *(.data*) } > RAM
	.bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN (4); __bss_end__ = .; } > RAM
	__stack_top__ = ORIGIN (RAM) + LENGTH (RAM);
}
