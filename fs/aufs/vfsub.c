// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * sub-routines for VFS
 */

#include "aufs.h"

unsigned int vfsub_inode_nlink_aufs(struct inode *inode)
{
	unsigned int nlink;

	au_nlink_lock(inode);
	nlink = inode->i_nlink;
	au_nlink_unlock(inode);

	return nlink;
}

void vfsub_inc_nlink(struct inode *inode)
{
	au_nlink_lock(inode);
	inc_nlink(inode);
	au_nlink_unlock(inode);
}

void vfsub_drop_nlink(struct inode *inode)
{
	au_nlink_lock(inode);
	AuDebugOn(!inode->i_nlink);
	drop_nlink(inode);
	au_nlink_unlock(inode);
}

void vfsub_clear_nlink(struct inode *inode)
{
	au_nlink_lock(inode);
	/* it can happen */
	/* AuDebugOn(!inode->i_nlink); */
	clear_nlink(inode);
	au_nlink_unlock(inode);
}

void vfsub_set_nlink(struct inode *inode, unsigned int nlink)
{
	/*
	 * stop setting the value equal to the current one, in order to stop
	 * a useless warning from vfs:destroy_inode() about sb->s_remove_count.
	 */
	au_nlink_lock(inode);
	if (nlink != inode->i_nlink)
		set_nlink(inode, nlink);
	au_nlink_unlock(inode);
}
