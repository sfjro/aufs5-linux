/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * judging filesystem type
 */

#ifndef __AUFS_FSTYPE_H__
#define __AUFS_FSTYPE_H__

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/romfs_fs.h>

static inline int au_test_aufs(struct super_block *sb)
{
	return sb->s_magic == AUFS_SUPER_MAGIC;
}

static inline const char *au_sbtype(struct super_block *sb)
{
	return sb->s_type->name;
}

static inline int au_test_iso9660(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_ISO9660_FS)
	return sb->s_magic == ISOFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_romfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_ROMFS_FS)
	return sb->s_magic == ROMFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_cramfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_CRAMFS)
	return sb->s_magic == CRAMFS_MAGIC;
#endif
	return 0;
}

static inline int au_test_nfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_NFS_FS)
	return sb->s_magic == NFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_xfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_XFS_FS)
	return sb->s_magic == XFS_SB_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_tmpfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_TMPFS
	return sb->s_magic == TMPFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_ecryptfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_ECRYPT_FS)
	return !strcmp(au_sbtype(sb), "ecryptfs");
#else
	return 0;
#endif
}

static inline int au_test_ramfs(struct super_block *sb)
{
	return sb->s_magic == RAMFS_MAGIC;
}

static inline int au_test_ubifs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_UBIFS_FS)
	return sb->s_magic == UBIFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_procfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_PROC_FS
	return sb->s_magic == PROC_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_sysfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_SYSFS
	return sb->s_magic == SYSFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_configfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_CONFIGFS_FS)
	return sb->s_magic == CONFIGFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_minix(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_MINIX_FS)
	return sb->s_magic == MINIX3_SUPER_MAGIC
		|| sb->s_magic == MINIX2_SUPER_MAGIC
		|| sb->s_magic == MINIX2_SUPER_MAGIC2
		|| sb->s_magic == MINIX_SUPER_MAGIC
		|| sb->s_magic == MINIX_SUPER_MAGIC2;
#else
	return 0;
#endif
}

static inline int au_test_securityfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_SECURITYFS
	return sb->s_magic == SECURITYFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_squashfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_SQUASHFS)
	return sb->s_magic == SQUASHFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_btrfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_BTRFS_FS)
	return sb->s_magic == BTRFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_xenfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_XENFS)
	return sb->s_magic == XENFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_debugfs(struct super_block *sb __maybe_unused)
{
#ifdef CONFIG_DEBUG_FS
	return sb->s_magic == DEBUGFS_MAGIC;
#else
	return 0;
#endif
}

static inline int au_test_nilfs(struct super_block *sb __maybe_unused)
{
#if IS_ENABLED(CONFIG_NILFS)
	return sb->s_magic == NILFS_SUPER_MAGIC;
#else
	return 0;
#endif
}

/* ---------------------------------------------------------------------- */
/*
 * they can't be an aufs branch.
 */
static inline int au_test_fs_unsuppoted(struct super_block *sb)
{
	return
		au_test_ramfs(sb) ||
		au_test_procfs(sb)
		|| au_test_sysfs(sb)
		|| au_test_configfs(sb)
		|| au_test_debugfs(sb)
		|| au_test_securityfs(sb)
		|| au_test_xenfs(sb)
		|| au_test_ecryptfs(sb)
		/* || !strcmp(au_sbtype(sb), "unionfs") */
		|| au_test_aufs(sb); /* will be supported in next version */
}

static inline int au_test_fs_remote(struct super_block *sb)
{
	return !au_test_tmpfs(sb)
		&& !(sb->s_type->fs_flags & FS_REQUIRES_DEV);
}

/* ---------------------------------------------------------------------- */

/*
 * Note: these functions (below) are created after reading ->getattr() in all
 * filesystems under linux/fs. it means we have to do so in every update...
 */

/*
 * filesystems which don't maintain i_size or i_blocks.
 */
static inline int au_test_fs_bad_iattr_size(struct super_block *sb)
{
	return au_test_xfs(sb)
		|| au_test_btrfs(sb)
		|| au_test_ubifs(sb)
		/* || au_test_minix(sb) */	/* untested */
		;
}

/* they don't check i_nlink in link(2) */
static inline int au_test_fs_no_limit_nlink(struct super_block *sb)
{
	return au_test_tmpfs(sb)
		|| au_test_ubifs(sb);
}

/* ---------------------------------------------------------------------- */

/*
 * the filesystem where the xino files placed must support i/o after unlink and
 * maintain i_size and i_blocks.
 */
static inline int au_test_fs_bad_xino(struct super_block *sb)
{
	return au_test_fs_remote(sb)
		|| au_test_fs_bad_iattr_size(sb)
		/* don't want unnecessary work for xino */
		|| au_test_aufs(sb)
		|| au_test_ecryptfs(sb)
		|| au_test_nilfs(sb);
}

static inline int au_test_fs_trunc_xino(struct super_block *sb)
{
	return au_test_tmpfs(sb)
		|| au_test_ramfs(sb);
}

/*
 * test if the @sb is real-readonly.
 */
static inline int au_test_fs_rr(struct super_block *sb)
{
	return au_test_squashfs(sb)
		|| au_test_iso9660(sb)
		|| au_test_cramfs(sb)
		|| au_test_romfs(sb);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_FSTYPE_H__ */
