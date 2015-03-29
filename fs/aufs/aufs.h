/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * all header files
 */

#ifndef __AUFS_H__
#define __AUFS_H__

#ifdef __KERNEL__

#define AuStub(type, name, body, ...) \
	static inline type name(__VA_ARGS__) { body; }

#define AuStubVoid(name, ...) \
	AuStub(void, name, , __VA_ARGS__)
#define AuStubInt0(name, ...) \
	AuStub(int, name, return 0, __VA_ARGS__)

#include "debug.h"

#include "cpup.h"
#include "dcsub.h"
#include "dentry.h"
#include "fstype.h"
#include "inode.h"
#include "module.h"
#include "rwsem.h"
#include "super.h"
#include "vfsub.h"
/* add more later */

#endif /* __KERNEL__ */
#endif /* __AUFS_H__ */
