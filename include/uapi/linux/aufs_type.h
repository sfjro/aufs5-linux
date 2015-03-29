/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

#ifndef __AUFS_TYPE_H__
#define __AUFS_TYPE_H__

#define AUFS_NAME	"aufs"

#ifdef __KERNEL__
/*
 * define it before including all other headers.
 * sched.h may use pr_* macros before defining "current", so define the
 * no-current version first, and re-define later.
 */
#define pr_fmt(fmt)	AUFS_NAME " %s:%d: " fmt, __func__, __LINE__
#include <linux/sched.h>
#undef pr_fmt
#define pr_fmt(fmt) \
		AUFS_NAME " %s:%d:%.*s[%d]: " fmt, __func__, __LINE__, \
		(int)sizeof(current->comm), current->comm, current->pid
#include <linux/limits.h>
#else
#include <stdint.h>
#include <sys/types.h>
#include <limits.h>
#endif /* __KERNEL__ */

#define AUFS_VERSION	"7.0"

/* todo? move this to linux-2.6.19/include/magic.h */
#define AUFS_SUPER_MAGIC	('a' << 24 | 'u' << 16 | 'f' << 8 | 's')

/* ---------------------------------------------------------------------- */

#ifdef __KERNEL__
#ifdef CONFIG_AUFS_BRANCH_MAX_127
typedef int8_t aufs_bindex_t;
#define AUFS_BRANCH_MAX 127
#else
typedef int16_t aufs_bindex_t;
#ifdef CONFIG_AUFS_BRANCH_MAX_511
#define AUFS_BRANCH_MAX 511
#elif defined(CONFIG_AUFS_BRANCH_MAX_1023)
#define AUFS_BRANCH_MAX 1023
#elif defined(CONFIG_AUFS_BRANCH_MAX_32767)
#define AUFS_BRANCH_MAX 32767
#endif
#endif

#ifndef AUFS_BRANCH_MAX
#error unknown CONFIG_AUFS_BRANCH_MAX value
#endif
#endif /* __KERNEL__ */

/* ---------------------------------------------------------------------- */

#define AUFS_FSTYPE		AUFS_NAME

#define AUFS_ROOT_INO		2
#define AUFS_FIRST_INO		11

#define AUFS_WH_PFX		".wh."
#define AUFS_WH_PFX_LEN		((int)sizeof(AUFS_WH_PFX) - 1)
#define AUFS_WH_TMP_LEN		4
/* a limit for rmdir/rename a dir and copyup */
#define AUFS_MAX_NAMELEN	(NAME_MAX \
				- AUFS_WH_PFX_LEN * 2	/* doubly whiteouted */\
				- 1			/* dot */\
				- AUFS_WH_TMP_LEN)	/* hex */
#define AUFS_XINO_FNAME		"." AUFS_NAME ".xino"
#define AUFS_XINO_DEFPATH	"/tmp/" AUFS_XINO_FNAME
#define AUFS_XINO_DEF_SEC	30 /* seconds */
#define AUFS_XINO_DEF_TRUNC	45 /* percentage */
#define AUFS_WKQ_NAME		AUFS_NAME "d"
#define AUFS_PLINK_WARN		50 /* number of plinks in a single bucket */

/* pseudo-link maintenace under /proc */
#define AUFS_PLINK_MAINT_NAME	"plink_maint"
#define AUFS_PLINK_MAINT_DIR	"fs/" AUFS_NAME
#define AUFS_PLINK_MAINT_PATH	AUFS_PLINK_MAINT_DIR "/" AUFS_PLINK_MAINT_NAME

#define AUFS_DIROPQ_NAME	AUFS_WH_PFX ".opq" /* whiteouted doubly */
#define AUFS_WH_DIROPQ		AUFS_WH_PFX AUFS_DIROPQ_NAME

#define AUFS_BASE_NAME		AUFS_WH_PFX AUFS_NAME
#define AUFS_PLINKDIR_NAME	AUFS_WH_PFX "plnk"
#define AUFS_ORPHDIR_NAME	AUFS_WH_PFX "orph"

/* doubly whiteouted */
#define AUFS_WH_BASE		AUFS_WH_PFX AUFS_BASE_NAME
#define AUFS_WH_PLINKDIR	AUFS_WH_PFX AUFS_PLINKDIR_NAME
#define AUFS_WH_ORPHDIR		AUFS_WH_PFX AUFS_ORPHDIR_NAME

/* branch permissions and attributes */
#define AUFS_BRPERM_RW		"rw"
#define AUFS_BRPERM_RO		"ro"
#define AUFS_BRRATTR_WH		"wh"
#define AUFS_BRWATTR_NLWH	"nolwh"

#define AuBrPerm_RW		1		/* writable, hardlinkable wh */
#define AuBrPerm_RO		(1 << 1)	/* readonly */
#define AuBrPerm_Mask		(AuBrPerm_RW | AuBrPerm_RO)

#define AuBrRAttr_WH		(1 << 7)	/* whiteout-able */
#define AuBrRAttr_Mask		AuBrRAttr_WH

#define AuBrWAttr_NoLinkWH	(1 << 8)	/* un-hardlinkable whiteouts */
#define AuBrWAttr_Mask		AuBrWAttr_NoLinkWH

/* the longest combination */
#define AuBrPermStrSz	sizeof(AUFS_BRPERM_RO		\
			       "+" AUFS_BRWATTR_NLWH)

typedef struct {
	char a[AuBrPermStrSz];
} au_br_perm_str_t;

static inline int au_br_writable(int brperm)
{
	return brperm & AuBrPerm_RW;
}

static inline int au_br_whable(int brperm)
{
	return brperm & (AuBrPerm_RW | AuBrRAttr_WH);
}

static inline int au_br_wh_linkable(int brperm)
{
	return !(brperm & AuBrWAttr_NoLinkWH);
}

#endif /* __AUFS_TYPE_H__ */
