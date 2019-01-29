/* Define memory regions. */
MEMORY
{
	/* If a bootloader is being used, the starting address and
	 * the size of the flash memory area should be adjusted here */
	rom (rx) : ORIGIN = 0x08000000 + 0K, LENGTH = 128K - 0K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

/* Include the common ld script. */
INCLUDE cortex-m-generic.ld

