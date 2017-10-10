/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * mount options/flags
 */

#ifndef __AUFS_OPTS_H__
#define __AUFS_OPTS_H__

#ifdef __KERNEL__

#include <linux/path.h>

struct au_opt_add {
	aufs_bindex_t	bindex;
	char		*pathname;
	int		perm;
	struct path	path;
};

struct au_opt {
	int type;
	union {
		struct au_opt_add	add;
		/* add more later */
	};
};

struct au_opts {
	struct au_opt	*opt;
	int		max_opt;
};

#endif /* __KERNEL__ */
#endif /* __AUFS_OPTS_H__ */
