// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * sub-routines for VFS
 */

#include <linux/filelock.h>
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

struct file *vfsub_dentry_open(struct path *path, int flags)
{
	return dentry_open(path, flags /* | __FMODE_NONOTIFY */,
			   current_cred());
}

struct file *vfsub_filp_open(const char *path, int oflags, int mode)
{
	struct file *file;

	lockdep_off();
	file = filp_open(path,
			 oflags /* | __FMODE_NONOTIFY */,
			 mode);
	lockdep_on();

	return file;
}

int vfsub_kern_path(const char *name, unsigned int flags, struct path *path)
{
	int err;

	err = kern_path(name, flags, path);
	/* add more later */
	return err;
}

struct dentry *vfsub_lookup_one_len(const char *name, struct path *ppath,
				    int len)
{
	struct path path;

	/* VFS checks it too, but by WARN_ON_ONCE() */
	IMustLock(d_inode(ppath->dentry));

	path.dentry = lookup_noperm(&QSTR_LEN(name, len), ppath->dentry);
	if (IS_ERR(path.dentry))
		goto out;

out:
	AuTraceErrPtr(path.dentry);
	return path.dentry;
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

/* ---------------------------------------------------------------------- */

struct unlink_args {
	int *errp;
	struct inode *dir;
	const struct path *path;
};

static void call_unlink(void *args)
{
	struct unlink_args *a = args;
	struct dentry *d = a->path->dentry;
	struct inode *h_inode;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};
	const int stop_sillyrename = (au_test_nfs(d->d_sb)
				      && au_dcount(d) == 1);
	struct path tmp = {
		.dentry = d->d_parent,
		.mnt	= a->path->mnt
	};

	IMustLock(a->dir);

	*a->errp = security_path_unlink(&tmp, d);
	if (unlikely(*a->errp))
		return;

	if (!stop_sillyrename)
		dget(d);
	h_inode = NULL;
	if (d_is_positive(d)) {
		h_inode = d_inode(d);
		ihold(h_inode);
	}

	idmap = mnt_idmap(a->path->mnt);
	do {
		lockdep_off();
		*a->errp = vfs_unlink(idmap, a->dir, d, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			int e;

			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

	if (!stop_sillyrename)
		dput(d);
	if (h_inode)
		iput(h_inode);

	AuTraceErr(*a->errp);
}

/*
 * @dir: must be locked.
 * @dentry: target dentry.
 */
int vfsub_unlink(struct inode *dir, const struct path *path, int force)
{
	int err;
	struct unlink_args args = {
		.errp	= &err,
		.dir	= dir,
		.path	= path
	};

	if (!force)
		call_unlink(&args);
	else {
		int wkq_err;

		wkq_err = au_wkq_wait(call_unlink, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}
