// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * virtual or vertical directory
 */

#include "aufs.h"

void au_vdir_free(struct au_vdir *vdir)
{
	unsigned char **deblk;

	deblk = vdir->vd_deblk;
	while (vdir->vd_nblk--)
		au_kfree_try_rcu(*deblk++);
	au_kfree_try_rcu(vdir->vd_deblk);
	au_cache_free_vdir(vdir);
}
