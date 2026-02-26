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

struct dentry *vfsub_lookup_one_len_unlocked(const char *name,
					     struct path *ppath, int len)
{
	struct path path;

	path.dentry = lookup_noperm_unlocked(&QSTR_LEN(name, len),
					     ppath->dentry);
	AuTraceErrPtr(path.dentry);
	return path.dentry;
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

void vfsub_call_lkup_one(void *args)
{
	struct vfsub_lkup_one_args *a = args;
	*a->errp = vfsub_lkup_one(a->name, a->ppath);
}

/* ---------------------------------------------------------------------- */

int vfsub_create(struct inode *dir, struct path *path, int mode, bool want_excl)
{
	int err, e;
	struct dentry *d;
	struct inode *inode;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	inode = d_inode(path->dentry);
	err = security_path_mknod(path, d, mode_strip_umask(inode, mode), 0);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	idmap = mnt_idmap(path->mnt);
	do {
		lockdep_off();
		err = vfs_create(idmap, d, mode, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);
	if (!err)
		security_path_post_mknod(idmap, d);

out:
	return err;
}

int vfsub_symlink(struct inode *dir, struct path *path, const char *symname)
{
	int err, e;
	struct dentry *d;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_symlink(path, d, symname);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	idmap = mnt_idmap(path->mnt);
	do {
		lockdep_off();
		err = vfs_symlink(idmap, dir, d, symname, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

out:
	return err;
}

int vfsub_mknod(struct inode *dir, struct path *path, int mode, dev_t dev)
{
	int err, e;
	struct dentry *d;
	struct inode *inode;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	inode = d_inode(path->dentry);
	err = security_path_mknod(path, d, mode_strip_umask(inode, mode),
				  new_encode_dev(dev));
	path->dentry = d;
	if (unlikely(err))
		goto out;

	idmap = mnt_idmap(path->mnt);
	do {
		lockdep_off();
		err = vfs_mknod(idmap, dir, path->dentry, mode, dev, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

out:
	return err;
}

static int au_test_nlink(struct inode *inode)
{
	const unsigned int link_max = UINT_MAX >> 1; /* rough margin */

	if (!au_test_fs_no_limit_nlink(inode->i_sb)
	    || vfsub_inode_nlink(inode, AU_I_BRANCH) < link_max)
		return 0;
	return -EMLINK;
}

int vfsub_link(struct dentry *src_dentry, struct inode *dir, struct path *path)
{
	int err, e;
	struct dentry *d;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	IMustLock(dir);

	err = au_test_nlink(d_inode(src_dentry));
	if (unlikely(err))
		return err;

	/* we don't call may_linkat() */
	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_link(src_dentry, path, d);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	idmap = mnt_idmap(path->mnt);
	do {
		lockdep_off();
		err = vfs_link(src_dentry, idmap, dir, path->dentry, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

out:
	return err;
}

int vfsub_rename(struct inode *src_dir, struct dentry *src_dentry,
		 struct inode *dir, struct path *path, unsigned int flags)
{
	int err, e;
	struct renamedata rd;
	struct delegated_inode deleg = {};
	struct path tmp = {
		.mnt	= path->mnt
	};
	struct dentry *d;

	IMustLock(dir);
	IMustLock(src_dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	tmp.dentry = src_dentry->d_parent;
	err = security_path_rename(&tmp, src_dentry, path, d, /*flags*/0);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	rd.mnt_idmap = mnt_idmap(path->mnt);
	rd.old_dentry = src_dentry;
	rd.old_parent = rd.old_dentry->d_parent;
	rd.new_dentry = path->dentry;
	rd.new_parent = rd.new_dentry->d_parent;
	rd.delegated_inode = &deleg;
	rd.flags = flags;
	do {
		lockdep_off();
		err = vfs_rename(&rd);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

out:
	return err;
}

struct dentry *vfsub_mkdir(struct inode *dir, struct path *path, int mode)
{
	int err, e;
	struct dentry *d, *ret;
	struct inode *inode;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	inode = d_inode(path->dentry);
	err = security_path_mkdir(path, d, mode_strip_umask(inode, mode));
	path->dentry = d;
	ret = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	idmap = mnt_idmap(path->mnt);
	do {
		/* on error, vfs_mkdir() calls dput() */
		/* and unlocks the parent dir. Ouch! */
		dget(d);
		lockdep_off();
		ret = vfs_mkdir(idmap, dir, d, mode, &deleg);
		if (IS_ERR(ret))
			inode_lock(dir);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);
	if (IS_ERR(ret))
		goto out;
	dput(d);

out:
	return ret;
}

int vfsub_rmdir(struct inode *dir, struct path *path)
{
	int err, e;
	struct dentry *d;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	IMustLock(dir);

	d = path->dentry;
	path->dentry = d->d_parent;
	err = security_path_rmdir(path, d);
	path->dentry = d;
	if (unlikely(err))
		goto out;

	idmap = mnt_idmap(path->mnt);
	do {
		lockdep_off();
		err = vfs_rmdir(idmap, dir, d, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

out:
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

int vfsub_iterate_dir(struct file *file, struct dir_context *ctx)
{
	int err;

	AuDbg("%pD, ctx{%ps, %llu}\n", file, ctx->actor, ctx->pos);

	lockdep_off();
	err = iterate_dir(file, ctx);
	lockdep_on();

	return err;
}

/* ---------------------------------------------------------------------- */

struct au_vfsub_mkdir_args {
	struct dentry **errp;
	struct inode *dir;
	struct path *path;
	int mode;
};

static void au_call_vfsub_mkdir(void *args)
{
	struct au_vfsub_mkdir_args *a = args;
	*a->errp = vfsub_mkdir(a->dir, a->path, a->mode);
}

struct dentry *vfsub_sio_mkdir(struct inode *dir, struct path *path, int mode)
{
	int err, do_sio;
	struct mnt_idmap *idmap;
	struct dentry *ret;

	idmap = mnt_idmap(path->mnt);
	do_sio = au_test_h_perm_sio(idmap, dir, MAY_EXEC | MAY_WRITE);
	if (!do_sio)
		ret = vfsub_mkdir(dir, path, mode);
	else {
		struct au_vfsub_mkdir_args args = {
			.errp	= &ret,
			.dir	= dir,
			.path	= path,
			.mode	= mode
		};
		err = au_wkq_wait(au_call_vfsub_mkdir, &args);
		if (unlikely(err))
			ret = ERR_PTR(err);
	}

	return ret;
}

struct au_vfsub_rmdir_args {
	int *errp;
	struct inode *dir;
	struct path *path;
};

static void au_call_vfsub_rmdir(void *args)
{
	struct au_vfsub_rmdir_args *a = args;
	*a->errp = vfsub_rmdir(a->dir, a->path);
}

int vfsub_sio_rmdir(struct inode *dir, struct path *path)
{
	int err, do_sio, wkq_err;
	struct mnt_idmap *idmap;

	idmap = mnt_idmap(path->mnt);
	do_sio = au_test_h_perm_sio(idmap, dir, MAY_EXEC | MAY_WRITE);
	if (!do_sio) {
		lockdep_off();
		err = vfsub_rmdir(dir, path);
		lockdep_on();
	} else {
		struct au_vfsub_rmdir_args args = {
			.errp	= &err,
			.dir	= dir,
			.path	= path
		};
		wkq_err = au_wkq_wait(au_call_vfsub_rmdir, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

struct notify_change_args {
	int *errp;
	const struct path *path;
	struct iattr *ia;
};

static void call_notify_change(void *args)
{
	struct notify_change_args *a = args;
	struct inode *h_inode;
	struct mnt_idmap *idmap;
	struct delegated_inode deleg = {};

	h_inode = d_inode(a->path->dentry);
	IMustLock(h_inode);

	*a->errp = -EPERM;
	if (IS_IMMUTABLE(h_inode) || IS_APPEND(h_inode))
		goto out;

	idmap = mnt_idmap(a->path->mnt);
	do {
		lockdep_off();
		*a->errp = notify_change(idmap, a->path->dentry, a->ia, &deleg);
		lockdep_on();
		if (is_delegated(&deleg)) {
			int e;

			e = break_deleg_wait(&deleg);
			if (!e)
				continue;
		}
		break;
	} while (1);

out:
	AuTraceErr(*a->errp);
}

int vfsub_notify_change(const struct path *path, struct iattr *ia)
{
	int err;
	struct notify_change_args args = {
		.errp	= &err,
		.path	= path,
		.ia	= ia
	};

	call_notify_change(&args);

	return err;
}

int vfsub_sio_notify_change(struct path *path, struct iattr *ia)
{
	int err, wkq_err;
	struct notify_change_args args = {
		.errp	= &err,
		.path	= path,
		.ia	= ia
	};

	wkq_err = au_wkq_wait(call_notify_change, &args);
	if (unlikely(wkq_err))
		err = wkq_err;

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
