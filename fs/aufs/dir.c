// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * directory operations
 */

#include "aufs.h"

void au_add_nlink(struct inode *dir, struct inode *h_dir)
{
	unsigned int nlink, h_nlink;

	AuDebugOn(!S_ISDIR(dir->i_mode) || !S_ISDIR(h_dir->i_mode));

	nlink = vfsub_inode_nlink(dir, AU_I_AUFS);
	h_nlink = vfsub_inode_nlink(h_dir, AU_I_BRANCH);
	nlink += h_nlink - 2;
	if (h_nlink < 2)
		nlink += 2;
	/* 0 can happen in revaliding */
	vfsub_set_nlink(dir, nlink);
}

void au_sub_nlink(struct inode *dir, struct inode *h_dir)
{
	unsigned int nlink, h_nlink;

	AuDebugOn(!S_ISDIR(dir->i_mode) || !S_ISDIR(h_dir->i_mode));

	nlink = vfsub_inode_nlink(dir, AU_I_AUFS);
	h_nlink = vfsub_inode_nlink(h_dir, AU_I_BRANCH);
	nlink -= h_nlink - 2;
	if (h_nlink < 2)
		nlink -= 2;
	/* nlink == 0 means the branch-fs is broken */
	vfsub_set_nlink(dir, nlink);
}

struct au_dir_ts_arg {
	struct dentry *dentry;
	aufs_bindex_t brid;
};

static void au_do_dir_ts(void *arg)
{
	struct au_dir_ts_arg *a = arg;
	struct au_dtime dt;
	struct path h_path;
	struct timespec64 ts;
	struct inode *dir, *h_dir;
	struct super_block *sb;
	struct au_branch *br;
	struct au_hinode *hdir;
	int err;
	aufs_bindex_t btop, bindex;

	sb = a->dentry->d_sb;
	if (d_really_is_negative(a->dentry))
		goto out;
	/* no dir->i_mutex lock */
	si_read_lock(sb, /*flags*/0); /* noflush */
	di_write_lock(a->dentry, AuLsc_DI_CHILD);

	dir = d_inode(a->dentry);
	btop = au_ibtop(dir);
	bindex = au_br_index(sb, a->brid);
	if (bindex < btop)
		goto out_unlock;

	br = au_sbr(sb, bindex);
	h_path.dentry = au_h_dptr(a->dentry, bindex);
	if (!h_path.dentry)
		goto out_unlock;
	h_path.mnt = au_br_mnt(br);
	au_dtime_store(&dt, a->dentry, &h_path);

	br = au_sbr(sb, btop);
	if (!au_br_writable(br->br_perm))
		goto out_unlock;
	h_path.dentry = au_h_dptr(a->dentry, btop);
	h_path.mnt = au_br_mnt(br);
	err = vfsub_mnt_want_write(h_path.mnt);
	if (err)
		goto out_unlock;
	hdir = au_hi(dir, btop);
	inode_lock_nested(hdir->hi_inode, AuLsc_I_PARENT);
	h_dir = au_h_iptr(dir, btop);
	ts = inode_get_mtime(h_dir);
	if (vfsub_inode_nlink(h_dir, AU_I_BRANCH)
	    && timespec64_compare(&ts, &dt.dt_mtime) < 0) {
		dt.dt_h_path = h_path;
		au_dtime_revert(&dt);
	}
	inode_unlock(hdir->hi_inode);
	vfsub_mnt_drop_write(h_path.mnt);
	au_cpup_attr_timesizes(dir);

out_unlock:
	di_write_unlock(a->dentry);
	si_read_unlock(sb);
out:
	dput(a->dentry);
	au_nwt_done(&au_sbi(sb)->si_nowait);
	au_kfree_try_rcu(arg);
}

void au_dir_ts(struct inode *dir, aufs_bindex_t bindex)
{
	int perm, wkq_err;
	aufs_bindex_t btop;
	struct au_dir_ts_arg *arg;
	struct dentry *dentry;
	struct super_block *sb;

	IMustLock(dir);

	dentry = d_find_any_alias(dir);
	AuDebugOn(!dentry);
	sb = dentry->d_sb;
	btop = au_ibtop(dir);
	if (btop == bindex) {
		au_cpup_attr_timesizes(dir);
		goto out;
	}

	perm = au_sbr_perm(sb, btop);
	if (!au_br_writable(perm))
		goto out;

	arg = kmalloc(sizeof(*arg), GFP_NOFS);
	if (!arg)
		goto out;

	arg->dentry = dget(dentry); /* will be dput-ted by au_do_dir_ts() */
	arg->brid = au_sbr_id(sb, bindex);
	wkq_err = au_wkq_nowait(au_do_dir_ts, arg, sb, /*flags*/0);
	if (unlikely(wkq_err)) {
		pr_err("wkq %d\n", wkq_err);
		dput(dentry);
		au_kfree_try_rcu(arg);
	}

out:
	dput(dentry);
}
