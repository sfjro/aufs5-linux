// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * whiteout for logical deletion and opaque directory
 */

#include "aufs.h"

#define WH_MASK			0444

/*
 * If a directory contains this file, then it is opaque.  We start with the
 * .wh. flag so that it is blocked by lookup.
 */
static struct qstr diropq_name = QSTR_INIT(AUFS_WH_DIROPQ,
					   sizeof(AUFS_WH_DIROPQ) - 1);

/*
 * generate whiteout name, which is NOT terminated by NULL.
 * @name: original d_name.name
 * @len: original d_name.len
 * @wh: whiteout qstr
 * returns zero when succeeds, otherwise error.
 * succeeded value as wh->name should be freed by kfree().
 */
int au_wh_name_alloc(struct qstr *wh, const struct qstr *name)
{
	char *p;

	if (unlikely(name->len > PATH_MAX - AUFS_WH_PFX_LEN))
		return -ENAMETOOLONG;

	wh->len = name->len + AUFS_WH_PFX_LEN;
	p = kmalloc(wh->len, GFP_NOFS);
	wh->name = p;
	if (p) {
		memcpy(p, AUFS_WH_PFX, AUFS_WH_PFX_LEN);
		memcpy(p + AUFS_WH_PFX_LEN, name->name, name->len);
		/* smp_mb(); */
		return 0;
	}
	return -ENOMEM;
}

/* ---------------------------------------------------------------------- */

/*
 * test if the @wh_name exists under @h_ppath.
 * @try_sio specifies the necessary of super-io.
 */
int au_wh_test(struct mnt_idmap *h_idmap, struct path *h_ppath,
	       const struct qstr *wh_name, int try_sio)
{
	int err;
	struct dentry *wh_dentry;

#if 0 /* re-commit later */
	if (!try_sio)
		wh_dentry = vfsub_lkup_one(wh_name, h_ppath);
	else
		wh_dentry = au_sio_lkup_one(h_idmap, wh_name, h_ppath);
#else
	wh_dentry = ERR_PTR(-ENOENT);
#endif
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry)) {
		if (err == -ENAMETOOLONG)
			err = 0;
		goto out;
	}

	err = 0;
	if (d_is_negative(wh_dentry))
		goto out_wh; /* success */

	err = 1;
	if (d_is_reg(wh_dentry))
		goto out_wh; /* success */

	err = -EIO;
	AuIOErr("%pd Invalid whiteout entry type 0%o.\n",
		wh_dentry, d_inode(wh_dentry)->i_mode);

out_wh:
	dput(wh_dentry);
out:
	return err;
}

/*
 * test if the @h_path->dentry sets opaque or not.
 */
int au_diropq_test(struct mnt_idmap *h_idmap, struct path *h_path)
{
	int err;
	struct inode *h_dir;

	h_dir = d_inode(h_path->dentry);
	err = au_wh_test(h_idmap, h_path, &diropq_name,
			 au_test_h_perm_sio(h_idmap, h_dir, MAY_EXEC));
	return err;
}

/* ---------------------------------------------------------------------- */
/*
 * functions for removing a whiteout
 */

static int do_unlink_wh(struct inode *h_dir, struct path *h_path)
{
	int err, force;

	/*
	 * forces superio when the dir has a sticky bit.
	 * this may be a violation of unix fs semantics.
	 */
	force = (h_dir->i_mode & S_ISVTX)
		&& !uid_eq(current_fsuid(), d_inode(h_path->dentry)->i_uid);
	err = vfsub_unlink(h_dir, h_path, force);
	return err;
}

int au_wh_unlink_dentry(struct inode *h_dir, struct path *h_path,
			struct dentry *dentry)
{
	int err;

	err = do_unlink_wh(h_dir, h_path);
	if (!err && dentry)
		au_set_dbwh(dentry, -1);

	return err;
}

/* ---------------------------------------------------------------------- */
/*
 * initialize/clean whiteout for a branch
 */

static void au_wh_clean(struct inode *h_dir, struct path *whpath,
			const int isdir)
{
	int err;

	if (d_is_negative(whpath->dentry))
		return;

	if (isdir)
		err = vfsub_rmdir(h_dir, whpath);
	else
		err = vfsub_unlink(h_dir, whpath, /*force*/0);
	if (unlikely(err))
		pr_warn("failed removing %pd (%d), ignored.\n",
			whpath->dentry, err);
}

static int test_linkable(struct dentry *h_root)
{
	struct inode *h_dir = d_inode(h_root);

	if (h_dir->i_op->link)
		return 0;

	pr_err("%pd (%s) doesn't support link(2), use noplink and rw+nolwh\n",
	       h_root, au_sbtype(h_root->d_sb));
	return -ENOSYS;	/* the branch doesn't have its ->link() */
}

/* todo: should this mkdir be done in /sbin/mount.aufs helper? */
static int au_whdir(struct inode *h_dir, struct path *path)
{
	int err;
	struct dentry *ret;

	err = -EEXIST;
	if (d_is_negative(path->dentry)) {
		int mode = 0700;

		if (au_test_nfs(path->dentry->d_sb))
			mode |= 0111;
		ret = vfsub_mkdir(h_dir, path, mode);
		if (IS_ERR(ret))
			err = PTR_ERR(ret);
		else {
			err = 0;
			if (ret && ret != path->dentry)
				path->dentry = ret;
		}
	} else if (d_is_dir(path->dentry))
		err = 0;
	else
		pr_err("unknown %pd exists\n", path->dentry);

	return err;
}

struct au_wh_base {
	const struct qstr *name;
	struct dentry *dentry;
};

static void au_wh_init_ro(struct inode *h_dir, struct au_wh_base base[],
			  struct path *h_path)
{
	h_path->dentry = base[AuBrWh_BASE].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/0);
	h_path->dentry = base[AuBrWh_PLINK].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/1);
	h_path->dentry = base[AuBrWh_ORPH].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/1);
}

/*
 * returns tri-state,
 * minus: error, caller should print the message
 * zero: success
 * plus: error, caller should NOT print the message
 */
static int au_wh_init_rw_nolink(struct dentry *h_root, struct au_wbr *wbr,
				int do_plink, struct au_wh_base base[],
				struct path *h_path)
{
	int err;
	struct inode *h_dir;

	h_dir = d_inode(h_root);
	h_path->dentry = base[AuBrWh_BASE].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/0);
	h_path->dentry = base[AuBrWh_PLINK].dentry;
	if (do_plink) {
		err = test_linkable(h_root);
		if (unlikely(err)) {
			err = 1;
			goto out;
		}

		err = au_whdir(h_dir, h_path);
		if (unlikely(err))
			goto out;
		else if (h_path->dentry != base[AuBrWh_PLINK].dentry) {
			dput(base[AuBrWh_PLINK].dentry);
			base[AuBrWh_PLINK].dentry = dget(h_path->dentry);
		}
		wbr->wbr_plink = dget(base[AuBrWh_PLINK].dentry);
	} else
		au_wh_clean(h_dir, h_path, /*isdir*/1);
	h_path->dentry = base[AuBrWh_ORPH].dentry;
	err = au_whdir(h_dir, h_path);
	if (unlikely(err))
		goto out;
	else if (h_path->dentry != base[AuBrWh_ORPH].dentry) {
		dput(base[AuBrWh_ORPH].dentry);
		base[AuBrWh_ORPH].dentry = dget(h_path->dentry);
	}
	wbr->wbr_orph = dget(base[AuBrWh_ORPH].dentry);

out:
	return err;
}

/*
 * for the moment, aufs supports the branch filesystem which does not support
 * link(2). testing on FAT which does not support i_op->setattr() fully either,
 * copyup failed. finally, such filesystem will not be used as the writable
 * branch.
 *
 * returns tri-state, see above.
 */
static int au_wh_init_rw(struct dentry *h_root, struct au_wbr *wbr,
			 int do_plink, struct au_wh_base base[],
			 struct path *h_path)
{
	int err;
	struct inode *h_dir;

	WbrWhMustWriteLock(wbr);

	err = test_linkable(h_root);
	if (unlikely(err)) {
		err = 1;
		goto out;
	}

	/*
	 * todo: should this create be done in /sbin/mount.aufs helper?
	 */
	err = -EEXIST;
	h_dir = d_inode(h_root);
	if (d_is_negative(base[AuBrWh_BASE].dentry)) {
		h_path->dentry = base[AuBrWh_BASE].dentry;
		err = vfsub_create(h_dir, h_path, WH_MASK, /*want_excl*/true);
	} else if (d_is_reg(base[AuBrWh_BASE].dentry))
		err = 0;
	else
		pr_err("unknown %pd2 exists\n", base[AuBrWh_BASE].dentry);
	if (unlikely(err))
		goto out;

	h_path->dentry = base[AuBrWh_PLINK].dentry;
	if (do_plink) {
		err = au_whdir(h_dir, h_path);
		if (unlikely(err))
			goto out;
		else if (h_path->dentry != base[AuBrWh_PLINK].dentry) {
			dput(base[AuBrWh_PLINK].dentry);
			base[AuBrWh_PLINK].dentry = dget(h_path->dentry);
		}
		wbr->wbr_plink = dget(base[AuBrWh_PLINK].dentry);
	} else
		au_wh_clean(h_dir, h_path, /*isdir*/1);
	wbr->wbr_whbase = dget(base[AuBrWh_BASE].dentry);

	h_path->dentry = base[AuBrWh_ORPH].dentry;
	err = au_whdir(h_dir, h_path);
	if (unlikely(err))
		goto out;
	else if (h_path->dentry != base[AuBrWh_ORPH].dentry) {
		dput(base[AuBrWh_ORPH].dentry);
		base[AuBrWh_ORPH].dentry = dget(h_path->dentry);
	}
	wbr->wbr_orph = dget(base[AuBrWh_ORPH].dentry);

out:
	return err;
}

/*
 * initialize the whiteout base file/dir for @br.
 */
int au_wh_init(struct au_branch *br, struct super_block *sb)
{
	int err, i, need_drop;
	const unsigned char do_plink
		= !!au_opt_test(au_mntflags(sb), PLINK);
	struct inode *h_dir;
	struct path path = br->br_path;
	struct dentry *h_root = path.dentry;
	struct au_wbr *wbr = br->br_wbr;
	static const struct qstr base_name[] = {
		[AuBrWh_BASE] = QSTR_INIT(AUFS_BASE_NAME,
					  sizeof(AUFS_BASE_NAME) - 1),
		[AuBrWh_PLINK] = QSTR_INIT(AUFS_PLINKDIR_NAME,
					   sizeof(AUFS_PLINKDIR_NAME) - 1),
		[AuBrWh_ORPH] = QSTR_INIT(AUFS_ORPHDIR_NAME,
					  sizeof(AUFS_ORPHDIR_NAME) - 1)
	};
	struct au_wh_base base[] = {
		[AuBrWh_BASE] = {
			.name	= base_name + AuBrWh_BASE,
			.dentry	= NULL
		},
		[AuBrWh_PLINK] = {
			.name	= base_name + AuBrWh_PLINK,
			.dentry	= NULL
		},
		[AuBrWh_ORPH] = {
			.name	= base_name + AuBrWh_ORPH,
			.dentry	= NULL
		}
	};

	if (wbr)
		WbrWhMustWriteLock(wbr);

	for (i = 0; i < AuBrWh_Last; i++) {
		/* doubly whiteouted */
		struct dentry *d;

		d = au_wh_lkup(h_root, (void *)base[i].name, br);
		err = PTR_ERR(d);
		if (IS_ERR(d))
			goto out;

		base[i].dentry = d;
		AuDebugOn(wbr
			  && wbr->wbr_wh[i]
			  && wbr->wbr_wh[i] != base[i].dentry);
	}

	if (wbr)
		for (i = 0; i < AuBrWh_Last; i++) {
			dput(wbr->wbr_wh[i]);
			wbr->wbr_wh[i] = NULL;
		}

	need_drop = 0;
	err = vfsub_mnt_want_write(path.mnt);
	if (unlikely(err))
		goto out_err;
	need_drop = 1;

	if (!au_br_writable(br->br_perm)) {
		h_dir = d_inode(h_root);
		au_wh_init_ro(h_dir, base, &path);
	} else if (!au_br_wh_linkable(br->br_perm)) {
		err = au_wh_init_rw_nolink(h_root, wbr, do_plink, base, &path);
		if (err > 0)
			goto out;
		else if (err)
			goto out_err;
	} else {
		err = au_wh_init_rw(h_root, wbr, do_plink, base, &path);
		if (err > 0)
			goto out;
		else if (err)
			goto out_err;
	}
	goto out; /* success */

out_err:
	pr_err("an error(%d) on the writable branch %pd(%s)\n",
	       err, h_root, au_sbtype(h_root->d_sb));
out:
	if (need_drop)
		vfsub_mnt_drop_write(path.mnt);
	for (i = 0; i < AuBrWh_Last; i++)
		dput(base[i].dentry);
	return err;
}

/* ---------------------------------------------------------------------- */
/*
 * whiteouts are all hard-linked usually.
 * when its link count reaches a ceiling, we create a new whiteout base
 * asynchronously.
 */

struct reinit_br_wh {
	struct super_block *sb;
	struct au_branch *br;
};

static void reinit_br_wh(void *arg)
{
	int err;
	aufs_bindex_t bindex;
	struct path h_path;
	struct reinit_br_wh *a = arg;
	struct au_wbr *wbr;
	struct inode *dir;
	struct dentry *h_root;
	struct au_hinode *hdir;

	err = 0;
	wbr = a->br->br_wbr;
	/* big aufs lock */
	si_noflush_write_lock(a->sb);
	if (!au_br_writable(a->br->br_perm))
		goto out;
	bindex = au_br_index(a->sb, a->br->br_id);
	if (unlikely(bindex < 0))
		goto out;

	di_read_lock_parent(a->sb->s_root, AuLock_IR);
	dir = d_inode(a->sb->s_root);
	hdir = au_hi(dir, bindex);
	h_root = au_h_dptr(a->sb->s_root, bindex);
	AuDebugOn(h_root != au_br_dentry(a->br));

	inode_lock_nested(hdir->hi_inode, AuLsc_I_PARENT);
	wbr_wh_write_lock(wbr);
	err = au_h_verify(wbr->wbr_whbase, hdir->hi_inode, h_root, a->br);
	if (!err) {
		h_path.dentry = wbr->wbr_whbase;
		h_path.mnt = au_br_mnt(a->br);
		err = vfsub_mnt_want_write(h_path.mnt);
		if (!err) {
			err = vfsub_unlink(hdir->hi_inode, &h_path, /*force*/0);
			vfsub_mnt_drop_write(h_path.mnt);
		}
	} else {
		pr_warn("%pd is moved, ignored\n", wbr->wbr_whbase);
		err = 0;
	}
	dput(wbr->wbr_whbase);
	wbr->wbr_whbase = NULL;
	if (!err)
		err = au_wh_init(a->br, a->sb);
	wbr_wh_write_unlock(wbr);
	inode_unlock(hdir->hi_inode);
	di_read_unlock(a->sb->s_root, AuLock_IR);

out:
	if (wbr)
		atomic_dec(&wbr->wbr_wh_running);
	au_lcnt_dec(&a->br->br_count);
	si_write_unlock(a->sb);
	au_nwt_done(&au_sbi(a->sb)->si_nowait);
	au_kfree_rcu(a);
	if (unlikely(err))
		AuIOErr("err %d\n", err);
}

static void kick_reinit_br_wh(struct super_block *sb, struct au_branch *br)
{
	int do_dec, wkq_err;
	struct reinit_br_wh *arg;

	do_dec = 1;
	if (atomic_inc_return(&br->br_wbr->wbr_wh_running) != 1)
		goto out;

	/* ignore ENOMEM */
	arg = kmalloc(sizeof(*arg), GFP_NOFS);
	if (arg) {
		/*
		 * dec(wh_running), kfree(arg) and dec(br_count)
		 * in reinit function
		 */
		arg->sb = sb;
		arg->br = br;
		au_lcnt_inc(&br->br_count);
		wkq_err = au_wkq_nowait(reinit_br_wh, arg, sb, /*flags*/0);
		if (unlikely(wkq_err)) {
			atomic_dec(&br->br_wbr->wbr_wh_running);
			au_lcnt_dec(&br->br_count);
			au_kfree_rcu(arg);
		}
		do_dec = 0;
	}

out:
	if (do_dec)
		atomic_dec(&br->br_wbr->wbr_wh_running);
}

/* ---------------------------------------------------------------------- */

/*
 * create the whiteout @wh.
 */
static int link_or_create_wh(struct super_block *sb, aufs_bindex_t bindex,
			     struct dentry *wh)
{
	int err;
	struct path h_path = {
		.dentry = wh
	};
	struct au_branch *br;
	struct au_wbr *wbr;
	struct dentry *h_parent;
	struct inode *h_dir;

	h_parent = wh->d_parent; /* dir inode is locked */
	h_dir = d_inode(h_parent);
	IMustLock(h_dir);

	br = au_sbr(sb, bindex);
	h_path.mnt = au_br_mnt(br);
	wbr = br->br_wbr;
	wbr_wh_read_lock(wbr);
	if (wbr->wbr_whbase) {
		err = vfsub_link(wbr->wbr_whbase, h_dir, &h_path);
		AuTraceErr(err);
		if (!err || err != -EMLINK)
			goto out;

		/* link count full. re-initialize br_whbase. */
		kick_reinit_br_wh(sb, br);
	}

	/* return this error in this context */
	err = vfsub_create(h_dir, &h_path, WH_MASK, /*want_excl*/true);

out:
	wbr_wh_read_unlock(wbr);
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * create or remove the diropq.
 */
static struct dentry *do_diropq(struct dentry *dentry, aufs_bindex_t bindex,
				unsigned int flags)
{
	struct dentry *opq_dentry;
	struct super_block *sb;
	struct au_branch *br;
	struct path h_path;
	int err;

	sb = dentry->d_sb;
	br = au_sbr(sb, bindex);
	h_path.dentry = au_h_dptr(dentry, bindex);
	h_path.mnt = au_br_mnt(br);
	opq_dentry = vfsub_lkup_one(&diropq_name, &h_path);
	if (IS_ERR(opq_dentry))
		goto out;

	if (au_ftest_diropq(flags, CREATE)) {
		err = link_or_create_wh(sb, bindex, opq_dentry);
		if (!err) {
			au_set_dbdiropq(dentry, bindex);
			goto out; /* success */
		}
	} else {
		h_path.dentry = opq_dentry;
		err = do_unlink_wh(au_h_iptr(d_inode(dentry), bindex), &h_path);
		if (!err)
			au_set_dbdiropq(dentry, -1);
	}
	dput(opq_dentry);
	opq_dentry = ERR_PTR(err);

out:
	return opq_dentry;
}

struct do_diropq_args {
	struct dentry **errp;
	struct dentry *dentry;
	aufs_bindex_t bindex;
	unsigned int flags;
};

static void call_do_diropq(void *args)
{
	struct do_diropq_args *a = args;
	*a->errp = do_diropq(a->dentry, a->bindex, a->flags);
}

struct dentry *au_diropq_sio(struct dentry *dentry, aufs_bindex_t bindex,
			     unsigned int flags)
{
	struct dentry *diropq, *h_dentry;
	struct mnt_idmap *h_idmap;

	h_idmap = au_sbr_idmap(dentry->d_sb, bindex);
	h_dentry = au_h_dptr(dentry, bindex);
	if (!au_test_h_perm_sio(h_idmap, d_inode(h_dentry),
				MAY_EXEC | MAY_WRITE))
		diropq = do_diropq(dentry, bindex, flags);
	else {
		int wkq_err;
		struct do_diropq_args args = {
			.errp		= &diropq,
			.dentry		= dentry,
			.bindex		= bindex,
			.flags		= flags
		};

		wkq_err = au_wkq_wait(call_do_diropq, &args);
		if (unlikely(wkq_err))
			diropq = ERR_PTR(wkq_err);
	}

	return diropq;
}

/* ---------------------------------------------------------------------- */

/*
 * lookup whiteout dentry.
 * @h_parent: lower parent dentry which must exist and be locked
 * @base_name: name of dentry which will be whiteouted
 * returns dentry for whiteout.
 */
struct dentry *au_wh_lkup(struct dentry *h_parent, const struct qstr *base_name,
			  struct au_branch *br)
{
	int err;
	struct qstr wh_name;
	struct dentry *wh_dentry;
	struct path h_path;

	err = au_wh_name_alloc(&wh_name, base_name);
	wh_dentry = ERR_PTR(err);
	if (!err) {
		h_path.dentry = h_parent;
		h_path.mnt = au_br_mnt(br);
		wh_dentry = vfsub_lkup_one(&wh_name, &h_path);
		au_kfree_try_rcu(wh_name.name);
	}
	return wh_dentry;
}

/*
 * link/create a whiteout for @dentry on @bindex.
 */
struct dentry *au_wh_create(struct dentry *dentry, aufs_bindex_t bindex,
			    struct dentry *h_parent)
{
	struct dentry *wh_dentry;
	struct super_block *sb;
	int err;

	sb = dentry->d_sb;
	wh_dentry = au_wh_lkup(h_parent, &dentry->d_name, au_sbr(sb, bindex));
	if (!IS_ERR(wh_dentry) && d_is_negative(wh_dentry)) {
		err = link_or_create_wh(sb, bindex, wh_dentry);
		if (!err)
			au_set_dbwh(dentry, bindex);
		else {
			dput(wh_dentry);
			wh_dentry = ERR_PTR(err);
		}
	}

	return wh_dentry;
}
