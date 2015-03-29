// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * superblock private data
 */

#include "aufs.h"

/*
 * they are necessary regardless sysfs is disabled.
 */
void au_si_free(struct kobject *kobj)
{
	struct au_sbinfo *sbinfo;

	sbinfo = container_of(kobj, struct au_sbinfo, si_kobj);
	au_kfree_try_rcu(sbinfo->si_branch);
	AuRwDestroy(&sbinfo->si_rwsem);

	au_kfree_rcu(sbinfo);
}

struct au_sbinfo *au_si_alloc(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;
	int err;

	err = -ENOMEM;
	sbinfo = kzalloc(sizeof(*sbinfo), GFP_NOFS);
	if (unlikely(!sbinfo))
		goto out;

	/* will be reallocated separately */
	sbinfo->si_branch = kzalloc(sizeof(*sbinfo->si_branch), GFP_NOFS);
	if (unlikely(!sbinfo->si_branch))
		goto out_sbinfo;

	au_rw_init_wlock(&sbinfo->si_rwsem);

	sbinfo->si_bbot = -1;

	/* leave other members for sysaufs and si_mnt. */
	if (sb)
		sb->s_fs_info = sbinfo;
	return sbinfo; /* success */

	au_kfree_try_rcu(sbinfo->si_branch);
out_sbinfo:
	au_kfree_rcu(sbinfo);
out:
	return ERR_PTR(err);
}
