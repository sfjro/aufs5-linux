/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * sub-routines for VFS
 */

#ifndef __AUFS_VFSUB_H__
#define __AUFS_VFSUB_H__

#ifdef __KERNEL__

#include <linux/fs.h>
#include "debug.h"
#include "fstype.h"

/* to debug easier, do not make them inlined functions */
#define MtxMustLock(mtx)	AuDebugOn(!mutex_is_locked(mtx))
#define IMustLock(i)		AuDebugOn(!inode_is_locked(i))

/* ---------------------------------------------------------------------- */

unsigned int vfsub_inode_nlink_aufs(struct inode *inode);

enum au_inode_type {
	AU_I_AUFS,
	AU_I_BRANCH,
	AU_I_UNKNOWN
};

static inline unsigned int vfsub_inode_nlink(struct inode *inode,
					     enum au_inode_type type)
{
	unsigned int nlink;

	switch (type) {
	case AU_I_AUFS:
		nlink = vfsub_inode_nlink_aufs(inode);
		break;
	case AU_I_BRANCH: /* aufs cannot be a branch of another aufs mount */
		AuDebugOn(au_test_aufs(inode->i_sb));
		nlink = inode->i_nlink;
		break;
	case AU_I_UNKNOWN:
		if (au_test_aufs(inode->i_sb))
			nlink = vfsub_inode_nlink_aufs(inode);
		else
			nlink = inode->i_nlink;
		break;
	};

	return nlink;
}

void vfsub_inc_nlink(struct inode *inode);
void vfsub_drop_nlink(struct inode *inode);
void vfsub_clear_nlink(struct inode *inode);
void vfsub_set_nlink(struct inode *inode, unsigned int nlink);

static inline void vfsub_inode_nlink_init(struct inode *inode,
					  unsigned int nlink)
{
	/* to ignore sb->s_remove_count, do not use set_nlink() */
	inode->__i_nlink = nlink;
}

/* ---------------------------------------------------------------------- */

int vfsub_kern_path(const char *name, unsigned int flags, struct path *path);

#endif /* __KERNEL__ */
#endif /* __AUFS_VFSUB_H__ */
