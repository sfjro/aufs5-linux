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

#endif /* __KERNEL__ */
#endif /* __AUFS_DEBUG_H__ */
