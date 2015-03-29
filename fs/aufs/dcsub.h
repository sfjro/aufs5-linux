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

struct au_dpage {
	int ndentry;
	struct dentry **dentries;
};

struct au_dcsub_pages {
	int ndpage;
	struct au_dpage *dpages;
};

/* ---------------------------------------------------------------------- */

/* dcsub.c */
int au_dpages_init(struct au_dcsub_pages *dpages, gfp_t gfp);
void au_dpages_free(struct au_dcsub_pages *dpages);
typedef int (*au_dpages_test)(struct dentry *dentry, void *arg);
int au_dcsub_pages_rev(struct au_dcsub_pages *dpages, struct dentry *dentry,
		       int do_include, au_dpages_test test, void *arg);
int au_dcsub_pages_rev_aufs(struct au_dcsub_pages *dpages,
			    struct dentry *dentry, int do_include);

/* ---------------------------------------------------------------------- */

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
