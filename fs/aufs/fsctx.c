// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2026 Junjiro R. Okajima
 */

/*
 * fs context, aka new mount api
 */

#include <linux/fs_context.h>
#include "aufs.h"

struct au_fsctx_opts {
	aufs_bindex_t bindex;
	unsigned char skipped;
	struct au_opt *opt, *opt_tail;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;
	struct au_opts opts;
};

/* ---------------------------------------------------------------------- */

static void au_fsctx_dump(struct au_opts *opts)
{
#ifdef CONFIG_AUFS_DEBUG
	/* reduce stack space */
	union {
		struct au_opt_add *add;
		struct au_opt_xino *xino;
	} u;
	struct au_opt *opt;

	opt = opts->opt;
	while (opt->type != Opt_tail) {
		switch (opt->type) {
		case Opt_add:
			u.add = &opt->add;
			AuDbg("add {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;

		case Opt_xino:
			u.xino = &opt->xino;
			AuDbg("xino {%s %pD}\n", u.xino->path, u.xino->file);
			break;
		case Opt_noxino:
			AuLabel(noxino);
			break;

		default:
			AuDbg("type %d\n", opt->type);
			BUG();
		}
		opt++;
	}
#endif
}

/* ---------------------------------------------------------------------- */

/*
 * For conditionally compiled mount options.
 * Instead of fsparam_flag_no(), use this macro to distinguish ignore_silent.
 */
#define au_ignore_flag(name, action)		\
	fsparam_flag(name, action),		\
	fsparam_flag("no" name, Opt_ignore_silent)

const struct fs_parameter_spec aufs_fsctx_paramspec[] = {
	fsparam_string("br", Opt_br),

	fsparam_path("xino", Opt_xino),
	fsparam_flag("noxino", Opt_noxino),

	/* internal use for the scripts */
	fsparam_string("si", Opt_ignore_silent),

	/* temporary workaround, due to old mount(8)? */
	fsparam_flag("relatime", Opt_ignore_silent),

	{}
};

static int au_fsctx_parse_do_add(struct fs_context *fc, struct au_opt *opt,
				 char *brspec, size_t speclen,
				 aufs_bindex_t bindex)
{
	int err;
	char *p;

	AuDbg("brspec %s\n", brspec);

	err = -ENOMEM;
	if (!speclen)
		speclen = strlen(brspec);
	/* will be freed by au_fsctx_free() */
	p = kmemdup_nul(brspec, speclen, GFP_NOFS);
	if (unlikely(!p)) {
		errorfc(fc, "failed in %s", brspec);
		goto out;
	}
	err = au_opt_add(opt, p, fc->sb_flags, bindex);

out:
	AuTraceErr(err);
	return err;
}

static int au_fsctx_parse_br(struct fs_context *fc, char *brspec)
{
	int err;
	char *p;
	struct au_fsctx_opts *a = fc->fs_private;
	struct au_opt *opt = a->opt;
	aufs_bindex_t bindex = a->bindex;

	AuDbg("brspec %s\n", brspec);

	err = -EINVAL;
	while ((p = strsep(&brspec, ":")) && *p) {
		err = au_fsctx_parse_do_add(fc, opt, p, /*len*/0, bindex);
		AuTraceErr(err);
		if (unlikely(err))
			break;
		bindex++;
		opt++;
		if (unlikely(opt > a->opt_tail)) {
			err = -E2BIG;
			bindex--;
			opt--;
			break;
		}
		opt->type = Opt_tail;
		a->skipped = 1;
	}
	a->bindex = bindex;
	a->opt = opt;

	AuTraceErr(err);
	return err;
}

static int au_fsctx_parse_add(struct fs_context *fc, char *addspec)
{
	int err, n;
	char *p;
	struct au_fsctx_opts *a = fc->fs_private;
	struct au_opt *opt = a->opt;

	err = -EINVAL;
	p = strchr(addspec, ':');
	if (unlikely(!p)) {
		errorfc(fc, "bad arg in %s", addspec);
		goto out;
	}
	*p++ = '\0';
	err = kstrtoint(addspec, 0, &n);
	if (unlikely(err)) {
		errorfc(fc, "bad integer in %s", addspec);
		goto out;
	}
	AuDbg("n %d\n", n);
	err = au_fsctx_parse_do_add(fc, opt, p, /*len*/0, n);

out:
	AuTraceErr(err);
	return err;
}

static int au_fsctx_parse_xino(struct fs_context *fc,
			       struct au_opt_xino *xino,
			       struct fs_parameter *param)
{
	int err;
	struct au_fsctx_opts *a = fc->fs_private;

	err = -ENOMEM;
	/* will be freed by au_opts_free() */
	xino->path = kmemdup_nul(param->string, param->size, GFP_NOFS);
	if (unlikely(!xino->path))
		goto out;
	AuDbg("path %s\n", xino->path);

	xino->file = au_xino_create(a->sb, xino->path, /*silent*/0,
				    /*wbrtop*/0);
	err = PTR_ERR(xino->file);
	if (IS_ERR(xino->file)) {
		xino->file = NULL;
		goto out;
	}

	err = 0;
	if (unlikely(a->sb && xino->file->f_path.dentry->d_sb == a->sb)) {
		err = -EINVAL;
		errorfc(fc, "%s must be outside", xino->path);
	}

out:
	AuTraceErr(err);
	return err;
}

static int au_fsctx_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	int err, token;
	struct fs_parse_result result;
	struct au_fsctx_opts *a = fc->fs_private;
	struct au_opt *opt = a->opt;

	AuDbg("fc %p, param {key %s, string %s}\n",
	      fc, param->key, param->string);
	err = fs_parse(fc, aufs_fsctx_paramspec, param, &result);
	if (unlikely(err < 0))
		goto out;
	token = err;
	AuDbg("token %d, res{negated %d, uint64 %llu}\n",
	      token, result.negated, result.uint_64);

	err = -EINVAL;
	a->skipped = 0;
	switch (token) {
	case Opt_br:
		err = au_fsctx_parse_br(fc, param->string);
		break;
	case Opt_add:
		err = au_fsctx_parse_add(fc, param->string);
		break;

	case Opt_xino:
		err = au_fsctx_parse_xino(fc, &opt->xino, param);
		break;

	case Opt_noxino:
		err = 0;
		break;

	case Opt_ignore:
		warnfc(fc, "ignored %s", param->key);
		fallthrough;
	case Opt_ignore_silent:
		a->skipped = 1;
		err = 0;
		break;
	default:
		a->skipped = 1;
		err = -ENOPARAM;
		break;
	}
	if (unlikely(err))
		goto out;
	if (a->skipped)
		goto out;

	switch (token) {
	case Opt_br:
		break;
	default:
		opt->type = token;
		break;
	}
	opt++;
	if (unlikely(opt > a->opt_tail)) {
		err = -E2BIG;
		opt--;
	}
	opt->type = Opt_tail;
	a->opt = opt;

out:
	return err;
}

/*
 * these options accept both 'name=val' and 'name:val' form.
 * some accept optional '=' in its value.
 * eg. br:/br1=rw:/br2=ro and br=/br1=rw:/br2=ro
 */
static inline unsigned int is_colonopt(char *str)
{
#define do_test(name)					\
	if (!strncmp(str, name ":", sizeof(name)))	\
		return sizeof(name) - 1
	do_test("br");
	do_test("add");
	/* add more later */
#undef do_test

	return 0;
}

static int au_fsctx_parse_monolithic(struct fs_context *fc, void *data)
{
	int err;
	unsigned int u;
	char *str;
	struct au_fsctx_opts *a = fc->fs_private;

	str = data;
	AuDbg("str %s\n", str);
	while (str) {
		u = is_colonopt(str);
		if (u)
			str[u] = '=';
		str = strchr(str, ',');
		if (!str)
			break;
		str++;
	}
	str = data;
	AuDbg("str %s\n", str);

	err = generic_parse_monolithic(fc, str);
	AuTraceErr(err);
	au_fsctx_dump(&a->opts);

	return err;
}

/* ---------------------------------------------------------------------- */

static void au_fsctx_opts_free(struct au_opts *opts)
{
	struct au_opt *opt;

	opt = opts->opt;
	while (opt->type != Opt_tail) {
		switch (opt->type) {
		case Opt_add:
			kfree(opt->add.pathname);
			path_put(&opt->add.path);
			break;
		case Opt_xino:
			kfree(opt->xino.path);
			fput(opt->xino.file);
			break;
		/* add more later */
		}
		opt++;
	}
}

static void au_fsctx_free(struct fs_context *fc)
{
	struct au_fsctx_opts *a = fc->fs_private;

	/* fs_type=%p, root=%pD */
	AuDbg("fc %p{sb_flags 0x%x, sb_flags_mask 0x%x, purpose %u\n",
	      fc, fc->sb_flags, fc->sb_flags_mask, fc->purpose);

	kobject_put(&a->sbinfo->si_kobj);
	au_fsctx_opts_free(&a->opts);
	free_page((unsigned long)a->opts.opt);
	au_kfree_rcu(a);
}

static const struct fs_context_operations au_fsctx_ops = {
	.free			= au_fsctx_free,
	.parse_param		= au_fsctx_parse_param,
	.parse_monolithic	= au_fsctx_parse_monolithic,
	/*
	 * nfs4 requires ->dup()? No.
	 * I don't know what is this ->dup() for.
	 */
};
