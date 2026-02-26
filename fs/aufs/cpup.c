// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * copy-up functions, see wbr_policy.c for copy-down
 */

#include <linux/fs_stack.h>
#include "aufs.h"

void au_cpup_attr_flags(struct inode *dst, unsigned int iflags)
{
	const unsigned int mask = S_DEAD | S_SWAPFILE | S_PRIVATE
		| S_NOATIME | S_NOCMTIME | S_AUTOMOUNT;

	BUILD_BUG_ON(sizeof(iflags) != sizeof(dst->i_flags));

	dst->i_flags |= iflags & ~mask;
	if (au_test_fs_notime(dst->i_sb))
		dst->i_flags |= S_NOATIME | S_NOCMTIME;
}

void au_cpup_attr_timesizes(struct inode *inode)
{
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, au_ibtop(inode));
	fsstack_copy_attr_times(inode, h_inode);
	fsstack_copy_inode_size(inode, h_inode);
}

void au_cpup_attr_nlink(struct inode *inode, int force)
{
	struct inode *h_inode;
	struct super_block *sb;
	aufs_bindex_t bindex, bbot;

	sb = inode->i_sb;
	bindex = au_ibtop(inode);
	h_inode = au_h_iptr(inode, bindex);
	if (!force
	    && !S_ISDIR(h_inode->i_mode)
	    && au_opt_test(au_mntflags(sb), PLINK)
	    && au_plink_test(inode))
		return;

	/*
	 * 0 can happen in revalidating.
	 * h_inode->i_mutex may not be held here, but it is harmless since once
	 * i_nlink reaches 0, it will never become positive except O_TMPFILE
	 * case.
	 * todo: O_TMPFILE+linkat(AT_SYMLINK_FOLLOW) bypassing aufs may cause
	 *	 the incorrect link count.
	 */
	vfsub_set_nlink(inode, vfsub_inode_nlink(h_inode, AU_I_BRANCH));

	/*
	 * fewer nlink makes find(1) noisy, but larger nlink doesn't.
	 * it may includes whplink directory.
	 */
	if (S_ISDIR(h_inode->i_mode)) {
		bbot = au_ibbot(inode);
		for (bindex++; bindex <= bbot; bindex++) {
			h_inode = au_h_iptr(inode, bindex);
			if (h_inode)
				au_add_nlink(inode, h_inode);
		}
	}
}

void au_cpup_attr_changeable(struct inode *inode)
{
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, au_ibtop(inode));
	inode->i_mode = h_inode->i_mode;
	inode->i_uid = h_inode->i_uid;
	inode->i_gid = h_inode->i_gid;
	au_cpup_attr_timesizes(inode);
	au_cpup_attr_flags(inode, h_inode->i_flags);
}

void au_cpup_igen(struct inode *inode, struct inode *h_inode)
{
	struct au_iinfo *iinfo = au_ii(inode);

	IiMustWriteLock(inode);

	iinfo->ii_higen = h_inode->i_generation;
	iinfo->ii_hsb1 = h_inode->i_sb;
}

void au_cpup_attr_all(struct inode *inode, int force)
{
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, au_ibtop(inode));
	au_cpup_attr_changeable(inode);
	au_cpup_attr_nlink(inode, force);
	inode->i_rdev = h_inode->i_rdev;
	inode->i_blkbits = h_inode->i_blkbits;
	au_cpup_igen(inode, h_inode);
}

/* ---------------------------------------------------------------------- */

/* Note: dt_dentry and dt_h_dentry are not dget/dput-ed */

/* keep the timestamps of the parent dir when cpup */
void au_dtime_store(struct au_dtime *dt, struct dentry *dentry,
		    struct path *h_path)
{
	struct inode *h_inode;

	dt->dt_dentry = dentry;
	dt->dt_h_path = *h_path;
	h_inode = d_inode(h_path->dentry);
	dt->dt_atime = inode_get_atime(h_inode);
	dt->dt_mtime = inode_get_mtime(h_inode);
	/* smp_mb(); */
}

void au_dtime_revert(struct au_dtime *dt)
{
	struct iattr attr;
	int err;

	attr.ia_atime = dt->dt_atime;
	attr.ia_mtime = dt->dt_mtime;
	attr.ia_valid = ATTR_FORCE | ATTR_MTIME | ATTR_MTIME_SET
		| ATTR_ATIME | ATTR_ATIME_SET
		| ATTR_CTIME;

	err = vfsub_notify_change(&dt->dt_h_path, &attr);
	if (unlikely(err))
		pr_warn("restoring timestamps failed(%d). ignored\n", err);
}
