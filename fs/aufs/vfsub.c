// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * sub-routines for VFS
 */

#include <linux/namei.h>
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

/* ---------------------------------------------------------------------- */

int vfsub_kern_path(const char *name, unsigned int flags, struct path *path)
{
	int err;

	err = kern_path(name, flags, path);
	/* add more later */
	return err;
}

/* ---------------------------------------------------------------------- */

/* todo: support mmap_sem? */
ssize_t vfsub_read_u(struct file *file, char __user *ubuf, size_t count,
		     loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = vfs_read(file, ubuf, count, ppos);
	lockdep_on();
	/* re-commit later */
	AuTraceErr(err);
	return err;
}

ssize_t vfsub_read_k(struct file *file, void *kbuf, size_t count,
		     loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = kernel_read(file, kbuf, count, ppos);
	lockdep_on();
	/* re-commit later */
	AuTraceErr(err);
	return err;
}

ssize_t vfsub_write_u(struct file *file, const char __user *ubuf, size_t count,
		      loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = vfs_write(file, ubuf, count, ppos);
	lockdep_on();
	/* re-commit later */
	return err;
}

ssize_t vfsub_write_k(struct file *file, void *kbuf, size_t count, loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = kernel_write(file, kbuf, count, ppos);
	lockdep_on();
	/* re-commit later */
	return err;
}
