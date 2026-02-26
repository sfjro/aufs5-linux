// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * lookup and dentry operations
 */

#include <linux/iversion.h>
#include "aufs.h"

/* subset of struct inode */
struct au_iattr {
	unsigned long		i_ino;
	/* unsigned int		i_nlink; */
	kuid_t			i_uid;
	kgid_t			i_gid;
	u64			i_version;
/*
	loff_t			i_size;
	blkcnt_t		i_blocks;
*/
	umode_t			i_mode;
};

static void au_iattr_save(struct au_iattr *ia, struct inode *h_inode)
{
	ia->i_ino = h_inode->i_ino;
	/* ia->i_nlink = h_inode->i_nlink; */
	ia->i_uid = h_inode->i_uid;
	ia->i_gid = h_inode->i_gid;
	ia->i_version = inode_query_iversion(h_inode);
/*
	ia->i_size = h_inode->i_size;
	ia->i_blocks = h_inode->i_blocks;
*/
	ia->i_mode = (h_inode->i_mode & S_IFMT);
}

static int au_iattr_test(struct au_iattr *ia, struct inode *h_inode)
{
	return ia->i_ino != h_inode->i_ino
		/* || ia->i_nlink != h_inode->i_nlink */
		|| !uid_eq(ia->i_uid, h_inode->i_uid)
		|| !gid_eq(ia->i_gid, h_inode->i_gid)
		|| !inode_eq_iversion(h_inode, ia->i_version)
/*
		|| ia->i_size != h_inode->i_size
		|| ia->i_blocks != h_inode->i_blocks
*/
		|| ia->i_mode != (h_inode->i_mode & S_IFMT);
}

static int au_h_verify_dentry(struct dentry *h_dentry, struct dentry *h_parent,
			      struct au_branch *br)
{
	int err;
	struct au_iattr ia;
	struct inode *h_inode;
	struct dentry *h_d;
	struct super_block *h_sb;
	struct path h_ppath;

	err = 0;
	memset(&ia, -1, sizeof(ia));
	h_sb = h_dentry->d_sb;
	h_inode = NULL;
	if (d_is_positive(h_dentry)) {
		h_inode = d_inode(h_dentry);
		au_iattr_save(&ia, h_inode);
	} else if (au_test_nfs(h_sb))
		/* nfs d_revalidate may return 0 for negative dentry */
		goto out;

	/* main purpose is namei.c:cached_lookup() and d_revalidate */
	h_ppath.dentry = h_parent;
	h_ppath.mnt = au_br_mnt(br);
	h_d = vfsub_lkup_one(&h_dentry->d_name, &h_ppath);
	err = PTR_ERR(h_d);
	if (IS_ERR(h_d))
		goto out;

	err = 0;
	if (unlikely(h_d != h_dentry
		     || d_inode(h_d) != h_inode
		     || (h_inode && au_iattr_test(&ia, h_inode))))
		err = -EBUSY;
	dput(h_d);

out:
	AuTraceErr(err);
	return err;
}

int au_h_verify(struct dentry *h_dentry, struct inode *h_dir,
		struct dentry *h_parent, struct au_branch *br)
{
	int err;

	err = 0;
	if (!au_test_fs_remote(h_dentry->d_sb)) {
		IMustLock(h_dir);
		err = (d_inode(h_dentry->d_parent) != h_dir);
	} else
		err = au_h_verify_dentry(h_dentry, h_parent, br);

	return err;
}
