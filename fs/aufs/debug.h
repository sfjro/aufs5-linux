/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * debug print functions
 */

#ifndef __AUFS_DEBUG_H__
#define __AUFS_DEBUG_H__

#ifdef __KERNEL__

#include <linux/atomic.h>
#include <linux/module.h>

#ifdef CONFIG_AUFS_DEBUG
#define AuDebugOn(a)		BUG_ON(a)

/* module parameter */
extern atomic_t aufs_debug;
static inline void au_debug_on(void)
{
	atomic_inc(&aufs_debug);
}
static inline void au_debug_off(void)
{
	atomic_dec_if_positive(&aufs_debug);
}

static inline int au_debug_test(void)
{
	return atomic_read(&aufs_debug) > 0;
}
#else
AuStubVoid(AuDebugOn, bool expr __maybe_unused)
AuStubVoid(au_debug_on, void)
AuStubVoid(au_debug_off, void)
AuStubInt0(au_debug_test, void)
#endif /* CONFIG_AUFS_DEBUG */

#define param_check_atomic_t(name, p) __param_check(name, p, atomic_t)

/* ---------------------------------------------------------------------- */

/* debug print */

#define AuDbg(fmt, ...) do { \
	if (au_debug_test()) \
		pr_debug("DEBUG: " fmt, ##__VA_ARGS__); \
} while (0)
#define AuLabel(l)		AuDbg(#l "\n")
#define AuIOErr(fmt, ...)	pr_err("I/O Error, " fmt, ##__VA_ARGS__)
#define AuWarn1(fmt, ...) do { \
	static unsigned char _c; \
	if (!_c++) \
		pr_warn(fmt, ##__VA_ARGS__); \
} while (0)

#define AuErr1(fmt, ...) do { \
	static unsigned char _c; \
	if (!_c++) \
		pr_err(fmt, ##__VA_ARGS__); \
} while (0)

#define AuIOErr1(fmt, ...) do { \
	static unsigned char _c; \
	if (!_c++) \
		AuIOErr(fmt, ##__VA_ARGS__); \
} while (0)

#define AuTraceErr(e) do { \
	if (unlikely((e) < 0)) \
		AuDbg("err %d\n", (int)(e)); \
} while (0)

#define AuTraceErrPtr(p) do { \
	if (IS_ERR(p)) \
		AuDbg("err %ld\n", PTR_ERR(p)); \
} while (0)

/* ---------------------------------------------------------------------- */

struct dentry;
struct inode;
#ifdef CONFIG_AUFS_DEBUG
extern struct mutex au_dbg_mtx;
extern char *au_plevel;
void au_dpri_inode(struct inode *inode);
void au_dpri_dalias(struct inode *inode);
void au_dpri_dentry(struct dentry *dentry);
struct super_block;
void au_dpri_sb(struct super_block *sb);

#define au_dbg_verify_dinode(d) __au_dbg_verify_dinode(d, __func__, __LINE__)
void __au_dbg_verify_dinode(struct dentry *dentry, const char *func, int line);
void au_dbg_verify_gen(struct dentry *parent, unsigned int sigen);
void au_dbg_verify_kthread(void);

#define AuDbgInode(i) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#i "\n"); \
	au_dpri_inode(i); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgDAlias(i) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#i "\n"); \
	au_dpri_dalias(i); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgDentry(d) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#d "\n"); \
	au_dpri_dentry(d); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgSb(sb) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#sb "\n"); \
	au_dpri_sb(sb); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)
#else
AuStubVoid(au_dbg_verify_dinode, struct dentry *dentry)
AuStubVoid(au_dbg_verify_gen, struct dentry *parent, unsigned int sigen)
AuStubVoid(au_dbg_verify_kthread, void)

AuStubVoid(AuDbgInode, struct inode *inode)
AuStubVoid(AuDbgDAlias, struct inode *inode)
AuStubVoid(AuDbgDentry, struct dentry *dentry)
AuStubVoid(AuDbgSb, struct super_block *sb)
#endif /* CONFIG_AUFS_DEBUG */

#endif /* __KERNEL__ */
#endif /* __AUFS_DEBUG_H__ */
