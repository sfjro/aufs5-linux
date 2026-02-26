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

#include <linux/fs_parser.h>
#include <linux/path.h>

enum {
	Opt_br,
	Opt_add,
	Opt_tail, Opt_ignore, Opt_ignore_silent, Opt_err
};

/* ---------------------------------------------------------------------- */

/* mount flags */
#define AuOpt_XINO		BIT(0)		/* external inode number bitmap
						   and translation table */

#define AuOpt_Def	AuOpt_XINO

#define AuOpt_LkupDirFlags	(LOOKUP_FOLLOW | LOOKUP_DIRECTORY)

#define au_opt_test(flags, name)	(flags & AuOpt_##name)
#define au_opt_set(flags, name) do { \
	((flags) |= AuOpt_##name); \
} while (0)
#define au_opt_clr(flags, name) do { \
	((flags) &= ~AuOpt_##name); \
} while (0)

/* ---------------------------------------------------------------------- */

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

	unsigned long	sb_flags;
};

/* ---------------------------------------------------------------------- */

/* opts.c */
int au_br_perm_val(char *perm);
void au_optstr_br_perm(au_br_perm_str_t *str, int perm);

int au_opt_add(struct au_opt *opt, char *opt_str, unsigned long sb_flags,
	       aufs_bindex_t bindex);
struct super_block;
int au_opts_mount(struct super_block *sb, struct au_opts *opts);

/* fsctx.c */
int aufs_fsctx_init(struct fs_context *fc);
extern const struct fs_parameter_spec aufs_fsctx_paramspec[];

#endif /* __KERNEL__ */
#endif /* __AUFS_OPTS_H__ */
