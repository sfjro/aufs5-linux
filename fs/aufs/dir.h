/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * directory operations
 */

#ifndef __AUFS_DIR_H__
#define __AUFS_DIR_H__

#ifdef __KERNEL__

#include <linux/fs.h>

/* ---------------------------------------------------------------------- */

/* need to be faster and smaller */

struct au_nhash {
	unsigned int		nh_num;
	struct hlist_head	*nh_head;
};

struct au_vdir_destr {
	unsigned char	len;
	unsigned char	name[];
} __packed;

struct au_vdir_dehstr {
	struct hlist_node	hash;
	struct au_vdir_destr	*str;
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;

struct au_vdir_de {
	ino_t			de_ino;
	unsigned char		de_type;
	/* caution: packed */
	struct au_vdir_destr	de_str;
} __packed;

struct au_vdir_wh {
	struct hlist_node	wh_hash;
	aufs_bindex_t		wh_bindex;
	/* caution: packed */
	struct au_vdir_destr	wh_str;
} __packed;

union au_vdir_deblk_p {
	unsigned char		*deblk;
	struct au_vdir_de	*de;
};

struct au_vdir {
	unsigned char	**vd_deblk;
	unsigned long	vd_nblk;
	struct {
		unsigned long		ul;
		union au_vdir_deblk_p	p;
	} vd_last;

	u64		vd_version;
	unsigned int	vd_deblk_sz;
	unsigned long	vd_jiffy;
	struct rcu_head	rcu;
} ____cacheline_aligned_in_smp;

/* ---------------------------------------------------------------------- */

/* dir.c */
void au_add_nlink(struct inode *dir, struct inode *h_dir);
void au_sub_nlink(struct inode *dir, struct inode *h_dir);
loff_t au_dir_size(struct file *file, struct dentry *dentry);
void au_dir_ts(struct inode *dir, aufs_bindex_t bsrc);

/* vdir.c */
void au_vdir_free(struct au_vdir *vdir);

#endif /* __KERNEL__ */
#endif /* __AUFS_DIR_H__ */
