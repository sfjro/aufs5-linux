// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * dentry private data
 */

#include "aufs.h"

void au_di_init_once(void *_dinfo)
{
	struct au_dinfo *dinfo = _dinfo;

	au_rw_init(&dinfo->di_rwsem);
}

struct au_dinfo *au_di_alloc(struct super_block *sb, unsigned int lsc)
{
	struct au_dinfo *dinfo;
	int nbr, i;

	dinfo = au_cache_alloc_dinfo();
	if (unlikely(!dinfo))
		goto out;

	nbr = au_sbbot(sb) + 1;
	if (nbr <= 0)
		nbr = 1;
	dinfo->di_hdentry = kcalloc(nbr, sizeof(*dinfo->di_hdentry), GFP_NOFS);
	if (dinfo->di_hdentry) {
		au_rw_write_lock_nested(&dinfo->di_rwsem, lsc);
		dinfo->di_btop = -1;
		dinfo->di_bbot = -1;
		dinfo->di_bwh = -1;
		dinfo->di_bdiropq = -1;
		for (i = 0; i < nbr; i++)
			dinfo->di_hdentry[i].hd_id = -1;
		goto out;
	}

	au_cache_free_dinfo(dinfo);
	dinfo = NULL;

out:
	return dinfo;
}

void au_di_free(struct au_dinfo *dinfo)
{
	struct au_hdentry *p;
	aufs_bindex_t bbot, bindex;

	/* dentry may not be revalidated */
	bindex = dinfo->di_btop;
	if (bindex >= 0) {
		bbot = dinfo->di_bbot;
		p = au_hdentry(dinfo, bindex);
		while (bindex++ <= bbot)
			au_hdput(p++);
	}
	au_kfree_try_rcu(dinfo->di_hdentry);
	au_cache_free_dinfo(dinfo);
}

int au_di_init(struct dentry *dentry)
{
	int err;
	struct super_block *sb;
	struct au_dinfo *dinfo;

	err = 0;
	sb = dentry->d_sb;
	dinfo = au_di_alloc(sb, AuLsc_DI_CHILD);
	if (dinfo) {
		atomic_set(&dinfo->di_generation, au_sigen(sb));
		/* smp_mb(); */ /* atomic_set */
		dentry->d_fsdata = dinfo;
	} else
		err = -ENOMEM;

	return err;
}

void au_di_fin(struct dentry *dentry)
{
	struct au_dinfo *dinfo;

	dinfo = au_di(dentry);
	AuRwDestroy(&dinfo->di_rwsem);
	au_di_free(dinfo);
}

int au_di_realloc(struct au_dinfo *dinfo, int nbr, int may_shrink)
{
	int err, sz;
	struct au_hdentry *hdp;

	AuRwMustWriteLock(&dinfo->di_rwsem);

	err = -ENOMEM;
	sz = sizeof(*hdp) * (dinfo->di_bbot + 1);
	if (!sz)
		sz = sizeof(*hdp);
	hdp = au_kzrealloc(dinfo->di_hdentry, sz, sizeof(*hdp) * nbr, GFP_NOFS,
			   may_shrink);
	if (hdp) {
		dinfo->di_hdentry = hdp;
		err = 0;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

static void do_ii_write_lock(struct inode *inode, unsigned int lsc)
{
	switch (lsc) {
	case AuLsc_DI_CHILD:
		ii_write_lock_child(inode);
		break;
	case AuLsc_DI_CHILD2:
		ii_write_lock_child2(inode);
		break;
	case AuLsc_DI_CHILD3:
		ii_write_lock_child3(inode);
		break;
	case AuLsc_DI_PARENT:
		ii_write_lock_parent(inode);
		break;
	case AuLsc_DI_PARENT2:
		ii_write_lock_parent2(inode);
		break;
	case AuLsc_DI_PARENT3:
		ii_write_lock_parent3(inode);
		break;
	default:
		BUG();
	}
}

static void do_ii_read_lock(struct inode *inode, unsigned int lsc)
{
	switch (lsc) {
	case AuLsc_DI_CHILD:
		ii_read_lock_child(inode);
		break;
	case AuLsc_DI_CHILD2:
		ii_read_lock_child2(inode);
		break;
	case AuLsc_DI_CHILD3:
		ii_read_lock_child3(inode);
		break;
	case AuLsc_DI_PARENT:
		ii_read_lock_parent(inode);
		break;
	case AuLsc_DI_PARENT2:
		ii_read_lock_parent2(inode);
		break;
	case AuLsc_DI_PARENT3:
		ii_read_lock_parent3(inode);
		break;
	default:
		BUG();
	}
}

void di_read_lock(struct dentry *d, int flags, unsigned int lsc)
{
	struct inode *inode;

	au_rw_read_lock_nested(&au_di(d)->di_rwsem, lsc);
	if (d_really_is_positive(d)) {
		inode = d_inode(d);
		if (au_ftest_lock(flags, IW))
			do_ii_write_lock(inode, lsc);
		else if (au_ftest_lock(flags, IR))
			do_ii_read_lock(inode, lsc);
	}
}

void di_read_unlock(struct dentry *d, int flags)
{
	struct inode *inode;

	if (d_really_is_positive(d)) {
		inode = d_inode(d);
		if (au_ftest_lock(flags, IW)) {
			au_dbg_verify_dinode(d);
			ii_write_unlock(inode);
		} else if (au_ftest_lock(flags, IR)) {
			au_dbg_verify_dinode(d);
			ii_read_unlock(inode);
		}
	}
	au_rw_read_unlock(&au_di(d)->di_rwsem);
}

void di_downgrade_lock(struct dentry *d, int flags)
{
	if (d_really_is_positive(d) && au_ftest_lock(flags, IR))
		ii_downgrade_lock(d_inode(d));
	au_rw_dgrade_lock(&au_di(d)->di_rwsem);
}

void di_write_lock(struct dentry *d, unsigned int lsc)
{
	au_rw_write_lock_nested(&au_di(d)->di_rwsem, lsc);
	if (d_really_is_positive(d))
		do_ii_write_lock(d_inode(d), lsc);
}

void di_write_unlock(struct dentry *d)
{
	au_dbg_verify_dinode(d);
	if (d_really_is_positive(d))
		ii_write_unlock(d_inode(d));
	au_rw_write_unlock(&au_di(d)->di_rwsem);
}

/* ---------------------------------------------------------------------- */

struct dentry *au_h_dptr(struct dentry *dentry, aufs_bindex_t bindex)
{
	struct dentry *d;

	DiMustAnyLock(dentry);

	if (au_dbtop(dentry) < 0 || bindex < au_dbtop(dentry))
		return NULL;
	AuDebugOn(bindex < 0);
	d = au_hdentry(au_di(dentry), bindex)->hd_dentry;
	AuDebugOn(d && au_dcount(d) <= 0);
	return d;
}

aufs_bindex_t au_dbtail(struct dentry *dentry)
{
	aufs_bindex_t bbot, bwh;

	bbot = au_dbbot(dentry);
	if (0 <= bbot) {
		bwh = au_dbwh(dentry);
		if (!bwh)
			return bwh;
		if (0 < bwh && bwh < bbot)
			return bwh - 1;
	}
	return bbot;
}

aufs_bindex_t au_dbtaildir(struct dentry *dentry)
{
	aufs_bindex_t bbot, bopq;

	bbot = au_dbtail(dentry);
	if (0 <= bbot) {
		bopq = au_dbdiropq(dentry);
		if (0 <= bopq && bopq < bbot)
			bbot = bopq;
	}
	return bbot;
}

/* ---------------------------------------------------------------------- */

void au_set_h_dptr(struct dentry *dentry, aufs_bindex_t bindex,
		   struct dentry *h_dentry)
{
	struct au_dinfo *dinfo;
	struct au_hdentry *hd;
	struct au_branch *br;

	DiMustWriteLock(dentry);

	dinfo = au_di(dentry);
	hd = au_hdentry(dinfo, bindex);
	au_hdput(hd);
	hd->hd_dentry = h_dentry;
	if (h_dentry) {
		br = au_sbr(dentry->d_sb, bindex);
		hd->hd_id = br->br_id;
	}
}

int au_digen_test(struct dentry *dentry, unsigned int sigen)
{
	int err;

	err = 0;
	if (unlikely(au_digen(dentry) != sigen
		     || au_iigen_test(d_inode(dentry), sigen)))
		err = -EIO;

	return err;
}

void au_update_digen(struct dentry *dentry)
{
	atomic_set(&au_di(dentry)->di_generation, au_sigen(dentry->d_sb));
	/* smp_mb(); */ /* atomic_set */
}

void au_update_dbtop(struct dentry *dentry)
{
	aufs_bindex_t bindex, bbot;
	struct dentry *h_dentry;

	bbot = au_dbbot(dentry);
	for (bindex = au_dbtop(dentry); bindex <= bbot; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		if (d_is_positive(h_dentry)) {
			au_set_dbtop(dentry, bindex);
			return;
		}
		au_set_h_dptr(dentry, bindex, NULL);
	}
}

void au_update_dbbot(struct dentry *dentry)
{
	aufs_bindex_t bindex, btop;
	struct dentry *h_dentry;

	btop = au_dbtop(dentry);
	for (bindex = au_dbbot(dentry); bindex >= btop; bindex--) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		if (d_is_positive(h_dentry)) {
			au_set_dbbot(dentry, bindex);
			return;
		}
		au_set_h_dptr(dentry, bindex, NULL);
	}
}

int au_find_dbindex(struct dentry *dentry, struct dentry *h_dentry)
{
	aufs_bindex_t bindex, bbot;

	bbot = au_dbbot(dentry);
	for (bindex = au_dbtop(dentry); bindex <= bbot; bindex++)
		if (au_h_dptr(dentry, bindex) == h_dentry)
			return bindex;
	return -1;
}
