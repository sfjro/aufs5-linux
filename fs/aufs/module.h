/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * module initialization and module-global
 */

#ifndef __AUFS_MODULE_H__
#define __AUFS_MODULE_H__

#ifdef __KERNEL__

#include <linux/slab.h>
#include "debug.h"

void *au_krealloc(void *p, unsigned int new_sz, gfp_t gfp, int may_shrink);
void *au_kzrealloc(void *p, unsigned int nused, unsigned int new_sz, gfp_t gfp,
		   int may_shrink);

/*
 * Comparing the size of the object with sizeof(struct rcu_head)
 * case 1: object is always larger
 *	--> au_kfree_rcu() or au_kfree_do_rcu()
 * case 2: object is always smaller
 *	--> au_kfree_small()
 * case 3: object can be any size
 *	--> au_kfree_try_rcu()
 */

static inline void au_kfree_do_rcu(const void *p)
{
	struct {
		struct rcu_head rcu;
	} *a = (void *)p;

	kfree_rcu(a, rcu);
}

#define au_kfree_rcu(_p) do {						\
		typeof(_p) p = (_p);					\
		BUILD_BUG_ON(sizeof(*p) < sizeof(struct rcu_head));	\
		if (p)							\
			au_kfree_do_rcu(p);				\
	} while (0)

#define au_kfree_do_sz_test(sz)	(sz >= sizeof(struct rcu_head))
#define au_kfree_sz_test(p)	(p && au_kfree_do_sz_test(ksize(p)))

static inline void au_kfree_try_rcu(const void *p)
{
	if (!p)
		return;
	if (au_kfree_sz_test(p))
		au_kfree_do_rcu(p);
	else
		kfree(p);
}

static inline void au_kfree_small(const void *p)
{
	if (!p)
		return;
	AuDebugOn(au_kfree_sz_test(p));
	kfree(p);
}

static inline int au_kmidx_sub(size_t sz, size_t new_sz)
{
#ifndef CONFIG_SLOB
	return __kmalloc_index(sz, false) - __kmalloc_index(new_sz, false);
#else
	return -1; /* SLOB is untested */
#endif
}

#endif /* __KERNEL__ */
#endif /* __AUFS_MODULE_H__ */
