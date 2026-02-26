/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * copy-up/down functions
 */

#ifndef __AUFS_CPUP_H__
#define __AUFS_CPUP_H__

#ifdef __KERNEL__

#include <linux/path.h>
#include <linux/time.h>

struct inode;

void au_cpup_attr_flags(struct inode *dst, unsigned int iflags);
void au_cpup_attr_timesizes(struct inode *inode);
void au_cpup_attr_nlink(struct inode *inode, int force);
void au_cpup_attr_changeable(struct inode *inode);
void au_cpup_igen(struct inode *inode, struct inode *h_inode);
void au_cpup_attr_all(struct inode *inode, int force);

/* ---------------------------------------------------------------------- */

/* keep timestamps when copyup */
struct au_dtime {
	struct dentry *dt_dentry;
	struct path dt_h_path;
	struct timespec64 dt_atime, dt_mtime;
};
void au_dtime_store(struct au_dtime *dt, struct dentry *dentry,
		    struct path *h_path);
void au_dtime_revert(struct au_dtime *dt);

#endif /* __KERNEL__ */
#endif /* __AUFS_CPUP_H__ */
