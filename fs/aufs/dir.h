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

/* dir.c */
void au_add_nlink(struct inode *dir, struct inode *h_dir);
void au_sub_nlink(struct inode *dir, struct inode *h_dir);
void au_dir_ts(struct inode *dir, aufs_bindex_t bsrc);

#endif /* __KERNEL__ */
#endif /* __AUFS_DIR_H__ */
