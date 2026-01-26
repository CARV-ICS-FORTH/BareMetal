/*
 * Supplementary linker script for test registration framework.
 * This extends the base linker script without modifying it.
 *
 * Creates read-only test array sections that are grouped with .rodata
 * in the same loadable segment (PT_LOAD with :rodata program header).
 *
 * Implementation notes:
 * - Input sections use names (__tests_yalibc, __tests_platform) that
 *   won't match the base script's .rodata.* wildcard, preventing them
 *   from being consumed by the base .rodata output section
 * - Output sections are placed in rom region after .data's LMA
 * - KEEP() prevents garbage collection during linking
 * - Linker auto-generates __start/__stop symbols for array bounds
 * - No runtime copying needed - accessed directly from ROM like .rodata
 */

SECTIONS
{
	.tests_yalibc : {
		__start_rodata_tests_yalibc = .;
		KEEP(*(__tests_yalibc))
		. = ALIGN(16);
		__stop_rodata_tests_yalibc = .;
	} > rom :rodata

	.tests_platform : {
		__start_rodata_tests_platform = .;
		KEEP(*(__tests_platform))
		. = ALIGN(16);
		__stop_rodata_tests_platform = .;
	} > rom :rodata
}
