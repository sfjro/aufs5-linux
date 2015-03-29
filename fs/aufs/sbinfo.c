// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * superblock private data
 */

#include <linux/iversion.h>
#include "aufs.h"

/*
 * they are necessary regardless sysfs is disabled.
 */
void au_si_free(struct kobject *kobj)
{
	int i;
	struct au_sbinfo *sbinfo;

	sbinfo = container_of(kobj, struct au_sbinfo, si_kobj);
	for (i = 0; i < AuPlink_NHASH; i++)
		AuDebugOn(!hlist_bl_empty(sbinfo->si_plink + i));
	AuDebugOn(atomic_read(&sbinfo->si_nowait.nw_len));

	au_rw_write_lock(&sbinfo->si_rwsem);
	au_br_free(sbinfo);
	au_rw_write_unlock(&sbinfo->si_rwsem);

	au_kfree_try_rcu(sbinfo->si_branch);
	mutex_destroy(&sbinfo->si_xib_mtx);
	AuRwDestroy(&sbinfo->si_rwsem);

	au_kfree_rcu(sbinfo);
}

struct au_sbinfo *au_si_alloc(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;
	int err, i;

	err = -ENOMEM;
	sbinfo = kzalloc(sizeof(*sbinfo), GFP_NOFS);
	if (unlikely(!sbinfo))
		goto out;

	/* will be reallocated separately */
	sbinfo->si_branch = kzalloc(sizeof(*sbinfo->si_branch), GFP_NOFS);
	if (unlikely(!sbinfo->si_branch))
		goto out_sbinfo;

	err = sysaufs_si_init(sbinfo);
	if (unlikely(err))
		goto out_br;

	au_nwt_init(&sbinfo->si_nowait);
	au_rw_init_wlock(&sbinfo->si_rwsem);

	sbinfo->si_bbot = -1;
	sbinfo->si_last_br_id = AUFS_BRANCH_MAX / 2;

	sbinfo->si_wbr_copyup = AuWbrCopyup_Def;
	sbinfo->si_wbr_create = AuWbrCreate_Def;
	sbinfo->si_wbr_copyup_ops = au_wbr_copyup_ops + sbinfo->si_wbr_copyup;
	sbinfo->si_wbr_create_ops = au_wbr_create_ops + sbinfo->si_wbr_create;

	sbinfo->si_mntflags = au_opts_plink(AuOpt_Def);

	sbinfo->si_xino_jiffy = jiffies;
	sbinfo->si_xino_expire
		= msecs_to_jiffies(AUFS_XINO_DEF_SEC * MSEC_PER_SEC);
	mutex_init(&sbinfo->si_xib_mtx);
	/* leave si_xib_last_pindex and si_xib_next_bit */

	for (i = 0; i < AuPlink_NHASH; i++)
		INIT_HLIST_BL_HEAD(sbinfo->si_plink + i);
	init_waitqueue_head(&sbinfo->si_plink_wq);
	spin_lock_init(&sbinfo->si_plink_maint_lock);

	/* leave other members for sysaufs and si_mnt. */
	sbinfo->si_sb = sb;
	if (sb) {
		sb->s_fs_info = sbinfo;
		si_pid_set(sb);
	}
	return sbinfo; /* success */

out_br:
	au_kfree_try_rcu(sbinfo->si_branch);
out_sbinfo:
	au_kfree_rcu(sbinfo);
out:
	return ERR_PTR(err);
}

int au_sbr_realloc(struct au_sbinfo *sbinfo, int nbr, int may_shrink)
{
	int err, sz;
	struct au_branch **brp;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	err = -ENOMEM;
	sz = sizeof(*brp) * (sbinfo->si_bbot + 1);
	if (unlikely(!sz))
		sz = sizeof(*brp);
	brp = au_kzrealloc(sbinfo->si_branch, sz, sizeof(*brp) * nbr, GFP_NOFS,
			   may_shrink);
	if (brp) {
		sbinfo->si_branch = brp;
		err = 0;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

unsigned int au_sigen_inc(struct super_block *sb)
{
	unsigned int gen;
	struct inode *inode;

	SiMustWriteLock(sb);

	gen = ++au_sbi(sb)->si_generation;
	au_update_digen(sb->s_root);
	inode = d_inode(sb->s_root);
	au_update_iigen(inode, /*half*/0);
	inode_inc_iversion(inode);
	return gen;
}

aufs_bindex_t au_new_br_id(struct super_block *sb)
{
	aufs_bindex_t br_id;
	int i;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	for (i = 0; i <= AUFS_BRANCH_MAX; i++) {
		br_id = ++sbinfo->si_last_br_id;
		AuDebugOn(br_id < 0);
		if (br_id && au_br_index(sb, br_id) < 0)
			return br_id;
	}

	return -1;
}

/* ---------------------------------------------------------------------- */

/* it is ok that new 'nwt' tasks are appended while we are sleeping */
int si_read_lock(struct super_block *sb, int flags)
{
	int err;

	err = 0;
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);

	si_noflush_read_lock(sb);
	err = au_plink_maint(sb, flags);
	if (unlikely(err))
		si_read_unlock(sb);

	return err;
}

int si_write_lock(struct super_block *sb, int flags)
{
	int err;

	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);

	si_noflush_write_lock(sb);
	err = au_plink_maint(sb, flags);
	if (unlikely(err))
		si_write_unlock(sb);

	return err;
}
