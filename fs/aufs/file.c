// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * handling file/dir, and address_space operation
 */

#include <linux/fsnotify.h>
#include "aufs.h"

/* drop flags for writing */
unsigned int au_file_roflags(unsigned int flags)
{
	flags &= ~(O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC);
	flags |= O_RDONLY | O_NOATIME;
	return flags;
}

/* common functions to regular file and dir */
struct file *au_h_open(struct dentry *dentry, aufs_bindex_t bindex, int flags,
		       struct file *file)
{
	struct file *h_file;
	struct dentry *h_dentry;
	struct inode *h_inode;
	struct super_block *sb;
	struct au_branch *br;
	struct path h_path;
	int err;

	/* a race condition can happen between open and unlink/rmdir */
	h_file = ERR_PTR(-ENOENT);
	h_dentry = au_h_dptr(dentry, bindex);
	if (!h_dentry || d_is_negative(h_dentry))
		goto out;
	h_inode = d_inode(h_dentry);
	spin_lock(&h_dentry->d_lock);
	err = (!d_unhashed(dentry) && d_unlinked(h_dentry))
		/* || !d_inode(dentry)->i_nlink */
		;
	spin_unlock(&h_dentry->d_lock);
	if (unlikely(err))
		goto out;

	sb = dentry->d_sb;
	br = au_sbr(sb, bindex);
	err = au_br_test_oflag(flags, br);
	h_file = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	/* drop flags for writing */
	if (au_test_ro(sb, bindex, d_inode(dentry)))
		flags = au_file_roflags(flags);
	flags &= ~O_CREAT;
	au_lcnt_inc(&br->br_nfiles);
	h_path.dentry = h_dentry;
	h_path.mnt = au_br_mnt(br);
	/*
	 * vfs::backing_file_open() looks promising since it can get rid of
	 * mm::vm_prfile approach from my mind.
	 * but I keep current approach for a while.
	 */
	h_file = vfsub_dentry_open(&h_path, flags);
	if (IS_ERR(h_file))
		goto out_br;

	fsnotify_open(h_file);
	goto out; /* success */

out_br:
	au_lcnt_dec(&br->br_nfiles);
out:
	return h_file;
}
