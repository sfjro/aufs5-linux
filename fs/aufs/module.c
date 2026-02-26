// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * module global variables and operations
 */

#include <linux/module.h>
#include "aufs.h"

/* shrinkable realloc */
void *au_krealloc(void *p, unsigned int new_sz, gfp_t gfp, int may_shrink)
{
	size_t sz;
	int diff;

	sz = 0;
	diff = -1;
	if (p) {
#if 0 /* unused */
		if (!new_sz) {
			au_kfree_rcu(p);
			p = NULL;
			goto out;
		}
#else
		AuDebugOn(!new_sz);
#endif
		sz = ksize(p);
		diff = au_kmidx_sub(sz, new_sz);
	}
	if (sz && !diff)
		goto out;

	if (sz < new_sz)
		/* expand or SLOB */
		p = krealloc(p, new_sz, gfp);
	else if (new_sz < sz && may_shrink) {
		/* shrink */
		void *q;

		q = kmalloc(new_sz, gfp);
		if (q) {
			if (p) {
				memcpy(q, p, new_sz);
				au_kfree_try_rcu(p);
			}
			p = q;
		} else
			p = NULL;
	}

out:
	return p;
}

void *au_kzrealloc(void *p, unsigned int nused, unsigned int new_sz, gfp_t gfp,
		   int may_shrink)
{
	p = au_krealloc(p, new_sz, gfp, may_shrink);
	if (p && new_sz > nused)
		memset(p + nused, 0, new_sz - nused);
	return p;
}

/* ---------------------------------------------------------------------- */
/*
 * functions for module interface.
 */
MODULE_LICENSE("GPL");
/* MODULE_LICENSE("GPL v2"); */
MODULE_AUTHOR("Junjiro R. Okajima <aufs-users@lists.sourceforge.net>");
MODULE_DESCRIPTION(AUFS_NAME
	" -- Advanced multi layered unification filesystem");
MODULE_VERSION(AUFS_VERSION);

/* ---------------------------------------------------------------------- */

static int __init aufs_init(void)
{
	/* since we define pr_fmt, call printk directly */
	printk(KERN_INFO AUFS_NAME " " AUFS_VERSION "\n");

	return 0;
}

module_init(aufs_init);
