/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * super_block operations
 */

#ifndef __AUFS_SUPER_H__
#define __AUFS_SUPER_H__

#ifdef __KERNEL__

/* flags for si_read_lock()/aufs_read_lock()/di_read_lock() */
#define AuLock_DW		BIT(0)		/* write-lock dentry */
#define AuLock_IR		BIT(1)		/* read-lock inode */
#define AuLock_IW		BIT(2)		/* write-lock inode */
#define au_ftest_lock(flags, name)	((flags) & AuLock_##name)
#define au_fset_lock(flags, name) \
	do { (flags) |= AuLock_##name; } while (0)
#define au_fclr_lock(flags, name) \
	do { (flags) &= ~AuLock_##name; } while (0)

/* ---------------------------------------------------------------------- */

/* super.c */
struct super_block;
struct inode *au_iget_locked(struct super_block *sb, ino_t ino);
extern const struct super_operations aufs_sop;

#endif /* __KERNEL__ */
#endif /* __AUFS_SUPER_H__ */
