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

/* ---------------------------------------------------------------------- */

/* lock subclass for lower inode */
/* default MAX_LOCKDEP_SUBCLASSES(8) is not enough */
/* reduce? gave up. */
enum {
	AuLsc_I_Begin = I_MUTEX_PARENT2, /* 5 */
	AuLsc_I_PARENT,		/* lower inode, parent first */
	AuLsc_I_PARENT2,	/* copyup dirs */
	AuLsc_I_PARENT3,	/* copyup wh */
	AuLsc_I_CHILD,
	AuLsc_I_CHILD2,
	AuLsc_I_End
};

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

struct file *vfsub_dentry_open(struct path *path, int flags);
struct file *vfsub_filp_open(const char *path, int oflags, int mode);
int vfsub_kern_path(const char *name, unsigned int flags, struct path *path);
struct dentry *vfsub_lookup_one_len(const char *name, struct path *ppath,
				    int len);

/* ---------------------------------------------------------------------- */

static inline int vfsub_mnt_want_write(struct vfsmount *mnt)
{
	int err;

	lockdep_off();
	err = mnt_want_write(mnt);
	lockdep_on();
	return err;
}

static inline void vfsub_mnt_drop_write(struct vfsmount *mnt)
{
	lockdep_off();
	mnt_drop_write(mnt);
	lockdep_on();
}

/* ---------------------------------------------------------------------- */

ssize_t vfsub_read_u(struct file *file, char __user *ubuf, size_t count,
		     loff_t *ppos);
ssize_t vfsub_read_k(struct file *file, void *kbuf, size_t count,
			loff_t *ppos);
ssize_t vfsub_write_u(struct file *file, const char __user *ubuf, size_t count,
		      loff_t *ppos);
ssize_t vfsub_write_k(struct file *file, void *kbuf, size_t count,
		      loff_t *ppos);

static inline loff_t vfsub_f_size_read(struct file *file)
{
	return i_size_read(file_inode(file));
}

/* ---------------------------------------------------------------------- */

int vfsub_unlink(struct inode *dir, const struct path *path, int force);

#endif /* __KERNEL__ */
#endif /* __AUFS_VFSUB_H__ */
