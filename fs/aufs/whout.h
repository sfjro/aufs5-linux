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
struct dentry;
struct inode;
int au_wh_unlink_dentry(struct inode *h_dir, struct path *h_path,
			struct dentry *dentry);
struct au_branch;
struct dentry *au_wh_lkup(struct dentry *h_parent, const struct qstr *base_name,
			  struct au_branch *br);
struct dentry *au_wh_create(struct dentry *dentry, aufs_bindex_t bindex,
			    struct dentry *h_parent);

#endif /* __KERNEL__ */
#endif /* __AUFS_WHOUT_H__ */
