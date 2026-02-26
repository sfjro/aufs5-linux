// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * inode private data
 */

#include "aufs.h"

struct inode *au_h_iptr(struct inode *inode, aufs_bindex_t bindex)
{
	struct inode *h_inode;
	struct au_hinode *hinode;

	IiMustAnyLock(inode);

	hinode = au_hinode(au_ii(inode), bindex);
	h_inode = hinode->hi_inode;
	AuDebugOn(h_inode && atomic_read(&h_inode->i_count) <= 0);
	return h_inode;
}

/* todo: hard/soft set? */
void au_hiput(struct au_hinode *hinode)
{
	iput(hinode->hi_inode);
}

unsigned int au_hi_flags(struct inode *inode, int isdir)
{
	unsigned int flags;
	const unsigned int mnt_flags = au_mntflags(inode->i_sb);

	flags = 0;
	if (au_opt_test(mnt_flags, XINO))
		au_fset_hi(flags, XINO);
	return flags;
}

void au_set_h_iptr(struct inode *inode, aufs_bindex_t bindex,
		   struct inode *h_inode, unsigned int flags)
{
	struct au_hinode *hinode;
	struct inode *hi;
	struct au_iinfo *iinfo = au_ii(inode);

	IiMustWriteLock(inode);

	hinode = au_hinode(iinfo, bindex);
	hi = hinode->hi_inode;
	AuDebugOn(h_inode && atomic_read(&h_inode->i_count) <= 0);

	if (hi)
		au_hiput(hinode);
	hinode->hi_inode = h_inode;
	if (h_inode) {
		int err;
		struct super_block *sb = inode->i_sb;
		struct au_branch *br;

		AuDebugOn(inode->i_mode
			  && (h_inode->i_mode & S_IFMT)
			  != (inode->i_mode & S_IFMT));
		if (bindex == iinfo->ii_btop)
			au_cpup_igen(inode, h_inode);
		br = au_sbr(sb, bindex);
		hinode->hi_id = br->br_id;
		if (au_ftest_hi(flags, XINO)) {
			err = au_xino_write(sb, bindex, h_inode->i_ino,
					    inode->i_ino);
			if (unlikely(err))
				AuIOErr1("failed au_xino_write() %d\n", err);
		}
	}
}

void au_update_iigen(struct inode *inode, int half)
{
	struct au_iinfo *iinfo;
	struct au_iigen *iigen;
	unsigned int sigen;

	sigen = au_sigen(inode->i_sb);
	iinfo = au_ii(inode);
	iigen = &iinfo->ii_generation;
	spin_lock(&iigen->ig_spin);
	iigen->ig_generation = sigen;
	spin_unlock(&iigen->ig_spin);
}

/* ---------------------------------------------------------------------- */

void au_icntnr_init_once(void *_c)
{
	struct au_icntnr *c = _c;
	struct au_iinfo *iinfo = &c->iinfo;

	spin_lock_init(&iinfo->ii_generation.ig_spin);
	au_rw_init(&iinfo->ii_rwsem);
	inode_init_once(&c->vfs_inode);
	spin_lock_init(&c->nlink_spin);
}

void au_hinode_init(struct au_hinode *hinode)
{
	hinode->hi_inode = NULL;
	hinode->hi_id = -1;
}

int au_iinfo_init(struct inode *inode)
{
	struct au_iinfo *iinfo;
	struct super_block *sb;
	struct au_hinode *hi;
	int nbr, i;

	sb = inode->i_sb;
	iinfo = &(container_of(inode, struct au_icntnr, vfs_inode)->iinfo);
	nbr = au_sbbot(sb) + 1;
	if (unlikely(nbr <= 0))
		nbr = 1;
	hi = kmalloc_array(nbr, sizeof(*iinfo->ii_hinode), GFP_NOFS);
	if (hi) {
		iinfo->ii_hinode = hi;
		for (i = 0; i < nbr; i++, hi++)
			au_hinode_init(hi);

		iinfo->ii_generation.ig_generation = au_sigen(sb);
		iinfo->ii_btop = -1;
		iinfo->ii_bbot = -1;
		return 0;
	}
	return -ENOMEM;
}

int au_hinode_realloc(struct au_iinfo *iinfo, int nbr, int may_shrink)
{
	int err, i;
	struct au_hinode *hip;

	AuRwMustWriteLock(&iinfo->ii_rwsem);

	err = -ENOMEM;
	hip = au_krealloc(iinfo->ii_hinode, sizeof(*hip) * nbr, GFP_NOFS,
			  may_shrink);
	if (hip) {
		iinfo->ii_hinode = hip;
		i = iinfo->ii_bbot + 1;
		hip += i;
		for (; i < nbr; i++, hip++)
			au_hinode_init(hip);
		err = 0;
	}

	return err;
}

void au_iinfo_fin(struct inode *inode)
{
	struct au_iinfo *iinfo;
	struct au_hinode *hi;
	aufs_bindex_t bindex, bbot;
	const unsigned char unlinked = !vfsub_inode_nlink(inode, AU_I_AUFS);

	AuDebugOn(au_is_bad_inode(inode));

	au_xino_delete_inode(inode, unlinked);

	iinfo = au_ii(inode);
	bindex = iinfo->ii_btop;
	if (bindex >= 0) {
		hi = au_hinode(iinfo, bindex);
		bbot = iinfo->ii_bbot;
		while (bindex++ <= bbot) {
			if (hi->hi_inode)
				au_hiput(hi);
			hi++;
		}
	}
	au_kfree_rcu(iinfo->ii_hinode);
	AuRwDestroy(&iinfo->ii_rwsem);
}
