/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Entry point for probing the unprivileged extensions. The probes themselves
 * live in platform/src/z_ext/, one file per instruction family, and each file
 * exports a single hart_probe_z*() that this calls. See
 * platform/include/riscv/z_ext.h.
 */

#include <platform/riscv/z_ext.h>

void
hart_probe_unpriv_caps(struct rvcaps *caps)
{
	/* Same trick as hart_probe_priv_caps(): hand the probes their rvcaps
	 * through hs, so they only need hs as an argument. caps shares a union
	 * with early_caps, so keep a copy of it and put it back when we're done.
	 * Unlike the privileged side we don't clear caps here: that ran first
	 * and already filled it in. */
	struct hart_state *hs = hart_get_hstate_self();
	uint64_t saved_early_caps = hs->early_caps;

	hs->caps = caps;

	hart_probe_zb(hs);

	hs->early_caps = saved_early_caps;
}