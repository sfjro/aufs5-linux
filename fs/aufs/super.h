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

#include <linux/fs.h>
#include <linux/kobject.h>
#include "rwsem.h"

struct au_branch;
struct au_sbinfo {
	/*
	 * tried sb->s_umount, but failed due to the dependency between i_mutex.
	 * rwsem for au_sbinfo is necessary.
	 */
	struct au_rwsem		si_rwsem;

	/* branch management */
	unsigned int		si_generation;

	aufs_bindex_t		si_bbot;
	struct au_branch	**si_branch;

	/*
	 * sysfs and lifetime management.
	 * this is not a small structure and it may be a waste of memory in case
	 * of sysfs is disabled, particularly when many aufs-es are mounted.
	 * but using sysfs is majority.
	 */
	struct kobject		si_kobj;
};

/* ---------------------------------------------------------------------- */

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
struct inode *au_iget_locked(struct super_block *sb, ino_t ino);
extern const struct super_operations aufs_sop;

/* sbinfo.c */
void au_si_free(struct kobject *kobj);
struct au_sbinfo *au_si_alloc(struct super_block *sb);

unsigned int au_sigen_inc(struct super_block *sb);

/* ---------------------------------------------------------------------- */

static inline struct au_sbinfo *au_sbi(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* ---------------------------------------------------------------------- */

/* lock superblock. mainly for entry point functions */
#define __si_read_lock(sb)	au_rw_read_lock(&au_sbi(sb)->si_rwsem)
#define __si_write_lock(sb)	au_rw_write_lock(&au_sbi(sb)->si_rwsem)
#define __si_read_trylock(sb)	au_rw_read_trylock(&au_sbi(sb)->si_rwsem)
#define __si_write_trylock(sb)	au_rw_write_trylock(&au_sbi(sb)->si_rwsem)
/*
#define __si_read_trylock_nested(sb) \
	au_rw_read_trylock_nested(&au_sbi(sb)->si_rwsem)
#define __si_write_trylock_nested(sb) \
	au_rw_write_trylock_nested(&au_sbi(sb)->si_rwsem)
*/

#define __si_read_unlock(sb)	au_rw_read_unlock(&au_sbi(sb)->si_rwsem)
#define __si_write_unlock(sb)	au_rw_write_unlock(&au_sbi(sb)->si_rwsem)
#define __si_downgrade_lock(sb)	au_rw_dgrade_lock(&au_sbi(sb)->si_rwsem)

#define SiMustNoWaiters(sb)	AuRwMustNoWaiters(&au_sbi(sb)->si_rwsem)
#define SiMustAnyLock(sb)	AuRwMustAnyLock(&au_sbi(sb)->si_rwsem)
#define SiMustWriteLock(sb)	AuRwMustWriteLock(&au_sbi(sb)->si_rwsem)

/* ---------------------------------------------------------------------- */

static inline aufs_bindex_t au_sbbot(struct super_block *sb)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_bbot;
}

static inline unsigned int au_sigen(struct super_block *sb)
{
	SiMustAnyLock(sb);
	return au_sbi(sb)->si_generation;
}

#endif /* __KERNEL__ */
#endif /* __AUFS_SUPER_H__ */
