/*
 * Supplementary linker script for the test/probe framework.
 * This extends the base linker script without modifying it.
 *
 * Creates read-only per-group arrays of `struct unit_probe` (see test_framework.h),
 * grouped with .rodata in the same loadable segment (PT_LOAD with :rodata header).
 * One output section per group: the privileged sub-groups (M/S/U/Z), unprivileged,
 * platform and yalibc.
 *
 * Implementation notes:
 * - Input sections use names (__units_*) that won't match the base script's
 *   .rodata.* wildcard, so they aren't consumed by the base .rodata output section.
 * - Each unit lives in its own "<sect>.<func>" input subsection, so SORT_BY_NAME()
 *   orders the group's array alphabetically by symbol (= unit function) name.
 * - Output sections are placed in the rom region after .data's LMA.
 * - KEEP() prevents garbage collection; __start/__stop bounds are defined below.
 * - No runtime copying needed - accessed directly from ROM like .rodata.
 */

SECTIONS
{
	.units_hart_priv_m : {
		__start_units_hart_priv_m = .;
		KEEP(*(SORT_BY_NAME(__units_hart_priv_m.*)))
		. = ALIGN(16);
		__stop_units_hart_priv_m = .;
	} > rom :rodata

	.units_hart_priv_s : {
		__start_units_hart_priv_s = .;
		KEEP(*(SORT_BY_NAME(__units_hart_priv_s.*)))
		. = ALIGN(16);
		__stop_units_hart_priv_s = .;
	} > rom :rodata

	.units_hart_priv_z : {
		__start_units_hart_priv_z = .;
		KEEP(*(SORT_BY_NAME(__units_hart_priv_z.*)))
		. = ALIGN(16);
		__stop_units_hart_priv_z = .;
	} > rom :rodata

	.units_hart_unpriv : {
		__start_units_hart_unpriv = .;
		KEEP(*(SORT_BY_NAME(__units_hart_unpriv.*)))
		. = ALIGN(16);
		__stop_units_hart_unpriv = .;
	} > rom :rodata

	.units_platform : {
		__start_units_platform = .;
		KEEP(*(SORT_BY_NAME(__units_platform.*)))
		. = ALIGN(16);
		__stop_units_platform = .;
	} > rom :rodata

	.units_yalibc : {
		__start_units_yalibc = .;
		KEEP(*(SORT_BY_NAME(__units_yalibc.*)))
		. = ALIGN(16);
		__stop_units_yalibc = .;
	} > rom :rodata
}
