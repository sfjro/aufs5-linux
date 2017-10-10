/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * branch filesystems and xino for them
 */

#ifndef __AUFS_BRANCH_H__
#define __AUFS_BRANCH_H__

#ifdef __KERNEL__

#include <linux/mount.h>
#include "lcnt.h"
#include "super.h"

/* ---------------------------------------------------------------------- */

/* protected by superblock rwsem */
struct au_branch {
	aufs_bindex_t		br_id;

	int			br_perm;
	struct path		br_path;
	au_lcnt_t		br_count;	/* in-use for other */
};

/* ---------------------------------------------------------------------- */

static inline struct vfsmount *au_br_mnt(struct au_branch *br)
{
	return br->br_path.mnt;
}

static inline struct dentry *au_br_dentry(struct au_branch *br)
{
	return br->br_path.dentry;
}

static inline struct super_block *au_br_sb(struct au_branch *br)
{
	return au_br_mnt(br)->mnt_sb;
}

/* ---------------------------------------------------------------------- */

/* branch.c */
struct au_sbinfo;
void au_br_free(struct au_sbinfo *sinfo);
int au_br_index(struct super_block *sb, aufs_bindex_t br_id);
struct au_opt_add;
int au_br_add(struct super_block *sb, struct au_opt_add *add);

/* ---------------------------------------------------------------------- */

/* Superblock to branch */
static inline
aufs_bindex_t au_sbr_id(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_sbr(sb, bindex)->br_id;
}

static inline
struct super_block *au_sbr_sb(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_br_sb(au_sbr(sb, bindex));
}

static inline int au_sbr_perm(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_sbr(sb, bindex)->br_perm;
}

#endif /* __KERNEL__ */
#endif /* __AUFS_BRANCH_H__ */
