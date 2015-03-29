/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * whiteout for logical deletion and opaque directory
 */

#ifndef __AUFS_WHOUT_H__
#define __AUFS_WHOUT_H__

#ifdef __KERNEL__

/* whout.c */
struct qstr;
int au_wh_name_alloc(struct qstr *wh, const struct qstr *name);
struct path;
struct mnt_idmap;
int au_wh_test(struct mnt_idmap *h_idmap, struct path *h_ppath,
	       const struct qstr *wh_name, int try_sio);
int au_diropq_test(struct mnt_idmap *h_idmap, struct path *h_path);
struct dentry;
struct inode;
int au_wh_unlink_dentry(struct inode *h_dir, struct path *h_path,
			struct dentry *dentry);
struct au_branch;
struct super_block;
int au_wh_init(struct au_branch *br, struct super_block *sb);

/* diropq flags */
#define AuDiropq_CREATE	1
#define au_ftest_diropq(flags, name)	((flags) & AuDiropq_##name)
#define au_fset_diropq(flags, name) \
	do { (flags) |= AuDiropq_##name; } while (0)
#define au_fclr_diropq(flags, name) \
	do { (flags) &= ~AuDiropq_##name; } while (0)

struct dentry *au_diropq_sio(struct dentry *dentry, aufs_bindex_t bindex,
			     unsigned int flags);
struct dentry *au_wh_lkup(struct dentry *h_parent, const struct qstr *base_name,
			  struct au_branch *br);
struct dentry *au_wh_create(struct dentry *dentry, aufs_bindex_t bindex,
			    struct dentry *h_parent);

/* ---------------------------------------------------------------------- */

static inline struct dentry *au_diropq_create(struct dentry *dentry,
					      aufs_bindex_t bindex)
{
	return au_diropq_sio(dentry, bindex, AuDiropq_CREATE);
}

static inline int au_diropq_remove(struct dentry *dentry, aufs_bindex_t bindex)
{
	return PTR_ERR(au_diropq_sio(dentry, bindex, !AuDiropq_CREATE));
}

#endif /* __KERNEL__ */
#endif /* __AUFS_WHOUT_H__ */
