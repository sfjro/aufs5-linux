// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * inode functions
 */

#include "aufs.h"

struct inode *au_igrab(struct inode *inode)
{
	if (inode) {
		AuDebugOn(!atomic_read(&inode->i_count));
		ihold(inode);
	}
	return inode;
}

/* ---------------------------------------------------------------------- */

int au_test_ro(struct super_block *sb, aufs_bindex_t bindex,
	       struct inode *inode)
{
	int err;
	struct inode *hi;

	err = au_br_rdonly(au_sbr(sb, bindex));

	/* pseudo-link after flushed may happen out of bounds */
	if (!err
	    && inode
	    && au_ibtop(inode) <= bindex
	    && bindex <= au_ibbot(inode)) {
		/*
		 * permission check is unnecessary since vfsub routine
		 * will be called later
		 */
		hi = au_h_iptr(inode, bindex);
		if (hi)
			err = IS_IMMUTABLE(hi) ? -EROFS : 0;
	}

	return err;
}

int au_test_h_perm(struct mnt_idmap *h_idmap, struct inode *h_inode,
		   int mask)
{
	if (uid_eq(current_fsuid(), GLOBAL_ROOT_UID))
		return 0;
	return inode_permission(h_idmap, h_inode, mask);
}

int au_test_h_perm_sio(struct mnt_idmap *h_idmap, struct inode *h_inode,
		       int mask)
{
	if (au_test_nfs(h_inode->i_sb)
	    && (mask & MAY_WRITE)
	    && S_ISDIR(h_inode->i_mode))
		mask |= MAY_READ; /* force permission check */
	return au_test_h_perm(h_idmap, h_inode, mask);
}
