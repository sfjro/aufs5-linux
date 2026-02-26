/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * sub-routines for dentry cache
 */

#ifndef __AUFS_DCSUB_H__
#define __AUFS_DCSUB_H__

#ifdef __KERNEL__

#include <linux/dcache.h>

/*
 * by the commit
 * 360f547 2015-01-25 dcache: let the dentry count go down to zero without
 *			taking d_lock
 * the type of d_lockref.count became int, but the inlined function d_count()
 * still returns unsigned int.
 * I don't know why. Maybe it is for every d_count() users?
 * Anyway au_dcount() lives on.
 */
static inline int au_dcount(struct dentry *d)
{
	return (int)d_count(d);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_DCSUB_H__ */
