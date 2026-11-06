/*
 * Supplementary linker script for the test/probe menu framework.
 * This extends the base linker script without modifying it.
 *
 * Creates read-only menu-entry array sections that are grouped with .rodata
 * in the same loadable segment (PT_LOAD with :rodata program header). Each group
 * (yalibc, platform, hart_priv, hart_unpriv) holds a mix of probe and test
 * entries (see test_framework.h), hence the neutral "__menu_*" naming.
 *
 * Implementation notes:
 * - Input sections use names (__menu_yalibc, __menu_platform, __menu_hart_priv,
 *   __menu_hart_unpriv) that won't match the base script's .rodata.* wildcard,
 *   so they aren't consumed by the base .rodata output section
 * - Each entry lives in its own "<sect>.<func>" input subsection, so
 *   SORT_BY_NAME() orders the array by symbol name -- within a group that lists
 *   probe_* entries before test_* entries, and alphabetically within each
 * - Output sections are placed in rom region after .data's LMA
 * - KEEP() prevents garbage collection during linking
 * - __start/__stop bounds symbols are defined explicitly below
 * - No runtime copying needed - accessed directly from ROM like .rodata
 */

SECTIONS
{
	.menu_yalibc : {
		__start_rodata_menu_yalibc = .;
		KEEP(*(SORT_BY_NAME(__menu_yalibc.*)))
		. = ALIGN(16);
		__stop_rodata_menu_yalibc = .;
	} > rom :rodata

	.menu_platform : {
		__start_rodata_menu_platform = .;
		KEEP(*(SORT_BY_NAME(__menu_platform.*)))
		. = ALIGN(16);
		__stop_rodata_menu_platform = .;
	} > rom :rodata

	.menu_hart_priv : {
		__start_rodata_menu_hart_priv = .;
		KEEP(*(SORT_BY_NAME(__menu_hart_priv.*)))
		. = ALIGN(16);
		__stop_rodata_menu_hart_priv = .;
	} > rom :rodata

	.menu_hart_unpriv : {
		__start_rodata_menu_hart_unpriv = .;
		KEEP(*(SORT_BY_NAME(__menu_hart_unpriv.*)))
		. = ALIGN(16);
		__stop_rodata_menu_hart_unpriv = .;
	} > rom :rodata
}
