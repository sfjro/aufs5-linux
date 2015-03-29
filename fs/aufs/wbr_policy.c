// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * policies for selecting one among multiple writable branches
 */

#include "aufs.h"

/* subset of cpup_attr() */
static noinline_for_stack
int au_cpdown_attr(struct path *h_path, struct dentry *h_src)
{
	int err, sbits;
	struct iattr ia;
	struct inode *h_isrc;

	err = vfsub_mnt_want_write(h_path->mnt);
	if (unlikely(err))
		goto out;

	h_isrc = d_inode(h_src);
	ia.ia_valid = ATTR_FORCE | ATTR_MODE | ATTR_UID | ATTR_GID;
	ia.ia_mode = h_isrc->i_mode;
	ia.ia_uid = h_isrc->i_uid;
	ia.ia_gid = h_isrc->i_gid;
	sbits = !!(ia.ia_mode & (S_ISUID | S_ISGID));
	au_cpup_attr_flags(d_inode(h_path->dentry), h_isrc->i_flags);
	err = vfsub_sio_notify_change(h_path, &ia);

	/* is this nfs only? */
	if (!err && sbits && au_test_nfs(h_path->dentry->d_sb)) {
		ia.ia_valid = ATTR_FORCE | ATTR_MODE;
		ia.ia_mode = h_isrc->i_mode;
		err = vfsub_sio_notify_change(h_path, &ia);
	}

	vfsub_mnt_drop_write(h_path->mnt);

out:
	return err;
}

#define AuCpdown_PARENT_OPQ	BIT(0)
#define AuCpdown_WHED		BIT(1)
#define AuCpdown_MADE_DIR	BIT(2)
#define AuCpdown_DIROPQ		BIT(3)
#define au_ftest_cpdown(flags, name)	((flags) & AuCpdown_##name)
#define au_fset_cpdown(flags, name) \
	do { (flags) |= AuCpdown_##name; } while (0)
#define au_fclr_cpdown(flags, name) \
	do { (flags) &= ~AuCpdown_##name; } while (0)

static int au_cpdown_dir_opq(struct dentry *dentry, aufs_bindex_t bdst,
			     unsigned int *flags)
{
	int err;
	struct dentry *opq_dentry;

	opq_dentry = au_diropq_create(dentry, bdst);
	err = PTR_ERR(opq_dentry);
	if (IS_ERR(opq_dentry))
		goto out;
	dput(opq_dentry);
	au_fset_cpdown(*flags, DIROPQ);

out:
	return err;
}

static int au_cpdown_dir_wh(struct dentry *dentry, struct dentry *h_parent,
			    struct inode *dir, aufs_bindex_t bdst)
{
	int err;
	struct path h_path;
	struct au_branch *br;

	br = au_sbr(dentry->d_sb, bdst);
	h_path.dentry = au_wh_lkup(h_parent, &dentry->d_name, br);
	err = PTR_ERR(h_path.dentry);
	if (IS_ERR(h_path.dentry))
		goto out;

	err = 0;
	if (d_is_positive(h_path.dentry)) {
		h_path.mnt = au_br_mnt(br);
		err = au_wh_unlink_dentry(au_h_iptr(dir, bdst), &h_path,
					  dentry);
	}
	dput(h_path.dentry);

out:
	return err;
}

static int au_cpdown_dir(struct dentry *dentry, aufs_bindex_t bdst,
			 struct au_pin *pin,
			 struct dentry *h_parent, void *arg)
{
	int err, rerr;
	aufs_bindex_t bopq, btop;
	struct path h_path;
	struct dentry *parent, *ret;
	struct inode *h_dir, *h_inode, *inode, *dir;
	unsigned int *flags = arg;

	btop = au_dbtop(dentry);
	/* dentry is di-locked */
	parent = dget_parent(dentry);
	dir = d_inode(parent);
	h_dir = d_inode(h_parent);
	AuDebugOn(h_dir != au_h_iptr(dir, bdst));
	IMustLock(h_dir);

	err = au_lkup_neg(dentry, bdst, /*wh*/0);
	if (unlikely(err < 0))
		goto out;
	h_path.dentry = au_h_dptr(dentry, bdst);
	h_path.mnt = au_sbr_mnt(dentry->d_sb, bdst);
	ret = vfsub_sio_mkdir(au_h_iptr(dir, bdst), &h_path, 0755);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		goto out_put;
	} else if (ret && ret != h_path.dentry) {
		h_path.dentry = dget(ret);
		au_set_h_dptr(dentry, bdst, ret);
	}
	au_fset_cpdown(*flags, MADE_DIR);

	bopq = au_dbdiropq(dentry);
	au_fclr_cpdown(*flags, WHED);
	au_fclr_cpdown(*flags, DIROPQ);
	if (au_dbwh(dentry) == bdst)
		au_fset_cpdown(*flags, WHED);
	if (!au_ftest_cpdown(*flags, PARENT_OPQ) && bopq <= bdst)
		au_fset_cpdown(*flags, PARENT_OPQ);
	h_inode = d_inode(h_path.dentry);
	inode_lock_nested(h_inode, AuLsc_I_CHILD);
	if (au_ftest_cpdown(*flags, WHED)) {
		err = au_cpdown_dir_opq(dentry, bdst, flags);
		if (unlikely(err)) {
			inode_unlock(h_inode);
			goto out_dir;
		}
	}

	err = au_cpdown_attr(&h_path, au_h_dptr(dentry, btop));
	inode_unlock(h_inode);
	if (unlikely(err))
		goto out_opq;

	if (au_ftest_cpdown(*flags, WHED)) {
		err = au_cpdown_dir_wh(dentry, h_parent, dir, bdst);
		if (unlikely(err))
			goto out_opq;
	}

	inode = d_inode(dentry);
	if (au_ibbot(inode) < bdst)
		au_set_ibbot(inode, bdst);
	au_set_h_iptr(inode, bdst, au_igrab(h_inode),
		      au_hi_flags(inode, /*isdir*/1));
	goto out; /* success */

	/* revert */
out_opq:
	if (au_ftest_cpdown(*flags, DIROPQ)) {
		inode_lock_nested(h_inode, AuLsc_I_CHILD);
		rerr = au_diropq_remove(dentry, bdst);
		inode_unlock(h_inode);
		if (unlikely(rerr)) {
			AuIOErr("failed removing diropq for %pd b%d (%d)\n",
				dentry, bdst, rerr);
			err = -EIO;
			goto out;
		}
	}
out_dir:
	if (au_ftest_cpdown(*flags, MADE_DIR)) {
		rerr = vfsub_sio_rmdir(au_h_iptr(dir, bdst), &h_path);
		if (unlikely(rerr)) {
			AuIOErr("failed removing %pd b%d (%d)\n",
				dentry, bdst, rerr);
			err = -EIO;
		}
	}
out_put:
	au_set_h_dptr(dentry, bdst, NULL);
	if (au_dbbot(dentry) == bdst)
		au_update_dbbot(dentry);
out:
	dput(parent);
	return err;
}

int au_cpdown_dirs(struct dentry *dentry, aufs_bindex_t bdst)
{
	int err;
	unsigned int flags;

	flags = 0;
	err = au_cp_dirs(dentry, bdst, au_cpdown_dir, &flags);

	return err;
}

/* ---------------------------------------------------------------------- */

/* policies for create */

int au_wbr_nonopq(struct dentry *dentry, aufs_bindex_t bindex)
{
	int err, i, j, ndentry;
	aufs_bindex_t bopq;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry **dentries, *parent, *d;

	err = au_dpages_init(&dpages, GFP_NOFS);
	if (unlikely(err))
		goto out;
	parent = dget_parent(dentry);
	err = au_dcsub_pages_rev_aufs(&dpages, parent, /*do_include*/0);
	if (unlikely(err))
		goto out_free;

	err = bindex;
	for (i = 0; i < dpages.ndpage; i++) {
		dpage = dpages.dpages + i;
		dentries = dpage->dentries;
		ndentry = dpage->ndentry;
		for (j = 0; j < ndentry; j++) {
			d = dentries[j];
			di_read_lock_parent2(d, !AuLock_IR);
			bopq = au_dbdiropq(d);
			di_read_unlock(d, !AuLock_IR);
			if (bopq >= 0 && bopq < err)
				err = bopq;
		}
	}

out_free:
	dput(parent);
	au_dpages_free(&dpages);
out:
	return err;
}

static int au_wbr_bu(struct super_block *sb, aufs_bindex_t bindex)
{
	for (; bindex >= 0; bindex--)
		if (!au_br_rdonly(au_sbr(sb, bindex)))
			return bindex;
	return -EROFS;
}

/* top down parent */
static int au_wbr_create_tdp(struct dentry *dentry,
			     unsigned int flags __maybe_unused)
{
	int err;
	aufs_bindex_t btop, bindex;
	struct super_block *sb;
	struct dentry *parent, *h_parent;

	sb = dentry->d_sb;
	btop = au_dbtop(dentry);
	err = btop;
	if (!au_br_rdonly(au_sbr(sb, btop)))
		goto out;

	err = -EROFS;
	parent = dget_parent(dentry);
	for (bindex = au_dbtop(parent); bindex < btop; bindex++) {
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent || d_is_negative(h_parent))
			continue;

		if (!au_br_rdonly(au_sbr(sb, bindex))) {
			err = bindex;
			break;
		}
	}
	dput(parent);

	/* bottom up here */
	if (unlikely(err < 0)) {
		err = au_wbr_bu(sb, btop - 1);
		if (err >= 0)
			err = au_wbr_nonopq(dentry, err);
	}

out:
	AuDbg("b%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* policies for copyup */

/* top down parent */
static int au_wbr_copyup_tdp(struct dentry *dentry)
{
	return au_wbr_create_tdp(dentry, /*flags, anything is ok*/0);
}

/* ---------------------------------------------------------------------- */

struct au_wbr_copyup_operations au_wbr_copyup_ops[] = {
	[AuWbrCopyup_TDP] = {
		.copyup	= au_wbr_copyup_tdp
	}
};

struct au_wbr_create_operations au_wbr_create_ops[] = {
	[AuWbrCreate_TDP] = {
		.create	= au_wbr_create_tdp
	}
};
