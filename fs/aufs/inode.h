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
#include "fstype.h"
#include "rwsem.h"

struct au_hinode {
	struct inode		*hi_inode;
	aufs_bindex_t		hi_id;
};

struct au_iigen {
	spinlock_t	ig_spin;
	__u32		ig_generation;
};

struct au_iinfo {
	struct au_iigen		ii_generation;
	struct super_block	*ii_hsb1;	/* no get/put */

	struct au_rwsem		ii_rwsem;
	aufs_bindex_t		ii_btop, ii_bbot;
	__u32			ii_higen;
	struct au_hinode	*ii_hinode;
};

struct au_icntnr {
	struct au_iinfo		iinfo;
	struct inode		vfs_inode;
	spinlock_t		nlink_spin; /* protects vfs_inode.i_nlink */
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;

/* ---------------------------------------------------------------------- */

static inline struct au_iinfo *au_ii(struct inode *inode)
{
	BUG_ON(is_bad_inode(inode));
	return &(container_of(inode, struct au_icntnr, vfs_inode)->iinfo);
}

static inline void au_nlink_lock(struct inode *inode)
{
	spinlock_t *spin;

	AuDebugOn(!au_test_aufs(inode->i_sb));
	AuDebugOn(is_bad_inode(inode));
	spin = &(container_of(inode, struct au_icntnr, vfs_inode)->nlink_spin);
	spin_lock(spin);
}

static inline void au_nlink_unlock(struct inode *inode)
{
	spinlock_t *spin;

	spin = &(container_of(inode, struct au_icntnr, vfs_inode)->nlink_spin);
	spin_unlock(spin);
}

/* ---------------------------------------------------------------------- */

/* inode.c */
struct inode *au_igrab(struct inode *inode);

/* iinfo.c */
struct inode *au_h_iptr(struct inode *inode, aufs_bindex_t bindex);
void au_hiput(struct au_hinode *hinode);

void au_set_h_iptr(struct inode *inode, aufs_bindex_t bindex,
		   struct inode *h_inode, unsigned int flags);

void au_update_iigen(struct inode *inode, int half);

void au_icntnr_init_once(void *_c);
void au_hinode_init(struct au_hinode *hinode);
int au_iinfo_init(struct inode *inode);
void au_iinfo_fin(struct inode *inode);
int au_hinode_realloc(struct au_iinfo *iinfo, int nbr, int may_shrink);

/* ---------------------------------------------------------------------- */

/* lock subclass for iinfo */
enum {
	AuLsc_II_CHILD,		/* child first */
	AuLsc_II_CHILD2,	/* rename(2), link(2), and cpup at hnotify */
	AuLsc_II_CHILD3,	/* copyup dirs */
	AuLsc_II_PARENT,	/* see AuLsc_I_PARENT in vfsub.h */
	AuLsc_II_PARENT2,
	AuLsc_II_PARENT3,	/* copyup dirs */
	AuLsc_II_NEW_CHILD
};

/*
 * ii_read_lock_child, ii_write_lock_child,
 * ii_read_lock_child2, ii_write_lock_child2,
 * ii_read_lock_child3, ii_write_lock_child3,
 * ii_read_lock_parent, ii_write_lock_parent,
 * ii_read_lock_parent2, ii_write_lock_parent2,
 * ii_read_lock_parent3, ii_write_lock_parent3,
 * ii_read_lock_new_child, ii_write_lock_new_child,
 */
#define AuReadLockFunc(name, lsc) \
static inline void ii_read_lock_##name(struct inode *i) \
{ \
	au_rw_read_lock_nested(&au_ii(i)->ii_rwsem, AuLsc_II_##lsc); \
}

#define AuWriteLockFunc(name, lsc) \
static inline void ii_write_lock_##name(struct inode *i) \
{ \
	au_rw_write_lock_nested(&au_ii(i)->ii_rwsem, AuLsc_II_##lsc); \
}

#define AuRWLockFuncs(name, lsc) \
	AuReadLockFunc(name, lsc) \
	AuWriteLockFunc(name, lsc)

AuRWLockFuncs(child, CHILD);
AuRWLockFuncs(child2, CHILD2);
AuRWLockFuncs(child3, CHILD3);
AuRWLockFuncs(parent, PARENT);
AuRWLockFuncs(parent2, PARENT2);
AuRWLockFuncs(parent3, PARENT3);
AuRWLockFuncs(new_child, NEW_CHILD);

#undef AuReadLockFunc
#undef AuWriteLockFunc
#undef AuRWLockFuncs

#define ii_read_unlock(i)	au_rw_read_unlock(&au_ii(i)->ii_rwsem)
#define ii_write_unlock(i)	au_rw_write_unlock(&au_ii(i)->ii_rwsem)
#define ii_downgrade_lock(i)	au_rw_dgrade_lock(&au_ii(i)->ii_rwsem)

#define IiMustNoWaiters(i)	AuRwMustNoWaiters(&au_ii(i)->ii_rwsem)
#define IiMustAnyLock(i)	AuRwMustAnyLock(&au_ii(i)->ii_rwsem)
#define IiMustWriteLock(i)	AuRwMustWriteLock(&au_ii(i)->ii_rwsem)

/* ---------------------------------------------------------------------- */

static inline void au_icntnr_init(struct au_icntnr *c)
{
#ifdef CONFIG_AUFS_DEBUG
	c->vfs_inode.i_mode = 0;
#endif
}

static inline unsigned int au_iigen(struct inode *inode)
{
	unsigned int gen;
	struct au_iinfo *iinfo;
	struct au_iigen *iigen;

	iinfo = au_ii(inode);
	iigen = &iinfo->ii_generation;
	spin_lock(&iigen->ig_spin);
	gen = iigen->ig_generation;
	spin_unlock(&iigen->ig_spin);

	return gen;
}

/* tiny test for inode number */
/* tmpfs generation is too rough */
static inline int au_test_higen(struct inode *inode, struct inode *h_inode)
{
	struct au_iinfo *iinfo;

	iinfo = au_ii(inode);
	AuRwMustAnyLock(&iinfo->ii_rwsem);
	return !(iinfo->ii_hsb1 == h_inode->i_sb
		 && iinfo->ii_higen == h_inode->i_generation);
}

static inline void au_iigen_dec(struct inode *inode)
{
	struct au_iinfo *iinfo;
	struct au_iigen *iigen;

	iinfo = au_ii(inode);
	iigen = &iinfo->ii_generation;
	spin_lock(&iigen->ig_spin);
	iigen->ig_generation--;
	spin_unlock(&iigen->ig_spin);
}

static inline int au_iigen_test(struct inode *inode, unsigned int sigen)
{
	int err;

	err = 0;
	if (unlikely(inode && au_iigen(inode) != sigen))
		err = -EIO;

	return err;
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

static inline aufs_bindex_t au_ii_br_id(struct inode *inode,
					aufs_bindex_t bindex)
{
	IiMustAnyLock(inode);
	return au_hinode(au_ii(inode), bindex)->hi_id;
}

static inline aufs_bindex_t au_ibtop(struct inode *inode)
{
	IiMustAnyLock(inode);
	return au_ii(inode)->ii_btop;
}

static inline aufs_bindex_t au_ibbot(struct inode *inode)
{
	IiMustAnyLock(inode);
	return au_ii(inode)->ii_bbot;
}

static inline void au_set_ibtop(struct inode *inode, aufs_bindex_t bindex)
{
	IiMustWriteLock(inode);
	au_ii(inode)->ii_btop = bindex;
}

static inline void au_set_ibbot(struct inode *inode, aufs_bindex_t bindex)
{
	IiMustWriteLock(inode);
	au_ii(inode)->ii_bbot = bindex;
}

static inline struct au_hinode *au_hi(struct inode *inode, aufs_bindex_t bindex)
{
	IiMustAnyLock(inode);
	return au_hinode(au_ii(inode), bindex);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_INODE_H__ */
