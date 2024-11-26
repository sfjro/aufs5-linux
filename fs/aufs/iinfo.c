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
		AuDebugOn(inode->i_mode
			  && (h_inode->i_mode & S_IFMT)
			  != (inode->i_mode & S_IFMT));
		/* add more later */
	}
}

/* ---------------------------------------------------------------------- */

void au_icntnr_init_once(void *_c)
{
	struct au_icntnr *c = _c;
	struct au_iinfo *iinfo = &c->iinfo;

	au_rw_init(&iinfo->ii_rwsem);
	inode_init_once(&c->vfs_inode);
	spin_lock_init(&c->nlink_spin);
}

void au_hinode_init(struct au_hinode *hinode)
{
	hinode->hi_inode = NULL;
}

int au_iinfo_init(struct inode *inode)
{
	struct au_iinfo *iinfo;
	struct super_block *sb;
	struct au_hinode *hi;
	int nbr, i;

	sb = inode->i_sb;
	iinfo = &(container_of(inode, struct au_icntnr, vfs_inode)->iinfo);
	nbr = 1; /* re-commit later */
	hi = kmalloc_array(nbr, sizeof(*iinfo->ii_hinode), GFP_NOFS);
	if (hi) {
		iinfo->ii_hinode = hi;
		for (i = 0; i < nbr; i++, hi++)
			au_hinode_init(hi);

		iinfo->ii_btop = -1;
		iinfo->ii_bbot = -1;
		return 0;
	}
	return -ENOMEM;
}

void au_iinfo_fin(struct inode *inode)
{
	struct au_iinfo *iinfo;
	struct au_hinode *hi;
	aufs_bindex_t bindex, bbot;

	AuDebugOn(au_is_bad_inode(inode));

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
	au_kfree_small(iinfo->ii_hinode);
	AuRwDestroy(&iinfo->ii_rwsem);
}
