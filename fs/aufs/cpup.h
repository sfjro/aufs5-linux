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

struct inode;

void au_cpup_igen(struct inode *inode, struct inode *h_inode);

#endif /* __KERNEL__ */
#endif /* __AUFS_CPUP_H__ */
