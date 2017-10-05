/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * inode operations
 */

#ifndef __AUFS_INODE_H__
#define __AUFS_INODE_H__

#ifdef __KERNEL__

#include <linux/fs.h>

struct au_hinode {
	struct inode		*hi_inode;
};

struct au_iinfo {
	struct rw_semaphore	ii_rwsem;
	aufs_bindex_t		ii_btop, ii_bbot;
	struct au_hinode	*ii_hinode;
};

struct au_icntnr {
	struct au_iinfo		iinfo;
	struct inode		vfs_inode;
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;

/* ---------------------------------------------------------------------- */

static inline struct au_iinfo *au_ii(struct inode *inode)
{
	BUG_ON(is_bad_inode(inode));
	return &(container_of(inode, struct au_icntnr, vfs_inode)->iinfo);
}

/* ---------------------------------------------------------------------- */

/* iinfo.c */
struct inode *au_h_iptr(struct inode *inode, aufs_bindex_t bindex);
void au_hiput(struct au_hinode *hinode);

void au_set_h_iptr(struct inode *inode, aufs_bindex_t bindex,
		   struct inode *h_inode, unsigned int flags);

void au_icntnr_init_once(void *_c);
void au_hinode_init(struct au_hinode *hinode);
int au_iinfo_init(struct inode *inode);
void au_iinfo_fin(struct inode *inode);

/* ---------------------------------------------------------------------- */

static inline void au_icntnr_init(struct au_icntnr *c)
{
	/* re-commit later */
}

/* ---------------------------------------------------------------------- */

static inline struct au_hinode *au_hinode(struct au_iinfo *iinfo,
					  aufs_bindex_t bindex)
{
	return iinfo->ii_hinode + bindex;
}

static inline int au_is_bad_inode(struct inode *inode)
{
	return !!(is_bad_inode(inode) || !au_hinode(au_ii(inode), 0));
}

static inline aufs_bindex_t au_ibtop(struct inode *inode)
{
	return au_ii(inode)->ii_btop;
}

static inline aufs_bindex_t au_ibbot(struct inode *inode)
{
	return au_ii(inode)->ii_bbot;
}

static inline void au_set_ibtop(struct inode *inode, aufs_bindex_t bindex)
{
	au_ii(inode)->ii_btop = bindex;
}

static inline void au_set_ibbot(struct inode *inode, aufs_bindex_t bindex)
{
	au_ii(inode)->ii_bbot = bindex;
}

static inline struct au_hinode *au_hi(struct inode *inode, aufs_bindex_t bindex)
{
	return au_hinode(au_ii(inode), bindex);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_INODE_H__ */
