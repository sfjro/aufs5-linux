/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2026 Junjiro R. Okajima
 */

/*
 * renamed dir info
 */

#ifndef __AUFS_DIRREN_H__
#define __AUFS_DIRREN_H__

#ifdef __KERNEL__

#include <linux/statfs.h>
#include <linux/uuid.h>
#include "hbl.h"

#define AuDirren_NHASH 100

#ifdef CONFIG_AUFS_DIRREN
enum au_brid_type {
	AuBrid_Unset,
	AuBrid_UUID,
	AuBrid_FSID,
	AuBrid_DEV
};

struct au_dr_brid {
	enum au_brid_type	type;
	union {
		uuid_t	uuid;	/* unimplemented yet */
		fsid_t	fsid;
		dev_t	dev;
	};
};

/* 20 is the max digits length of ulong 64 */
/* brid-type "_" uuid "_" inum */
#define AUFS_DIRREN_FNAME_SZ	(1 + 1 + UUID_STRING_LEN + 20)
#define AUFS_DIRREN_ENV_VAL_SZ	(AUFS_DIRREN_FNAME_SZ + 1 + 20)

struct au_dr_hino {
	struct hlist_bl_node	dr_hnode;
	ino_t			dr_h_ino;
};

struct au_dr_br {
	struct hlist_bl_head	dr_h_ino[AuDirren_NHASH];
	struct au_dr_brid	dr_brid;
};
#else
struct au_dr_hino;
/* empty */
struct au_dr_br { };
#endif

/* ---------------------------------------------------------------------- */

struct qstr;
struct au_branch;
struct au_hinode;
struct path;
struct super_block;
struct dentry;
#ifdef CONFIG_AUFS_DIRREN
int au_dr_hino_test_add(struct au_dr_br *dr, ino_t h_ino,
			struct au_dr_hino *add_ent);
void au_dr_hino_free(struct au_dr_br *dr);
int au_dr_br_init(struct super_block *sb, struct au_branch *br,
		  const struct path *path);
int au_dr_br_fin(struct super_block *sb, struct au_branch *br);
int au_dr_rename(struct dentry *src, aufs_bindex_t bindex,
		 const struct qstr *dst_name, void *_rev);
void au_dr_rename_fin(struct dentry *src, aufs_bindex_t btgt, void *rev);
void au_dr_rename_rev(struct dentry *src, aufs_bindex_t bindex, void *rev);
#else
AuStubInt0(au_dr_hino_test_add, struct au_dr_br *dr, ino_t h_ino,
	   struct au_dr_hino *add_ent);
AuStubVoid(au_dr_hino_free, struct au_dr_br *dr);
AuStubInt0(au_dr_br_init, struct super_block *sb, struct au_branch *br,
	   const struct path *path);
AuStubInt0(au_dr_br_fin, struct super_block *sb, struct au_branch *br);
AuStubInt0(au_dr_rename, struct dentry *src, aufs_bindex_t bindex,
	   const struct qstr *dst_name, void *_rev);
AuStubVoid(au_dr_rename_fin, struct dentry *src, aufs_bindex_t btgt, void *rev);
AuStubVoid(au_dr_rename_rev, struct dentry *src, aufs_bindex_t bindex,
	   void *rev);
#endif

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_DIRREN
static inline int au_dr_ihash(ino_t h_ino)
{
	return h_ino % AuDirren_NHASH;
}
#else
AuStubInt0(au_dr_ihash, ino_t h_ino);
#endif

#endif /* __KERNEL__ */
#endif /* __AUFS_DIRREN_H__ */
