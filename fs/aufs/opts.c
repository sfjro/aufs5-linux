// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * mount options/flags
 */

#include <linux/types.h> /* a distribution requires */
#include <linux/parser.h>
#include "aufs.h"

/* ---------------------------------------------------------------------- */

static const char *au_parser_pattern(int val, match_table_t tbl)
{
	struct match_token *p;

	p = tbl;
	while (p->pattern) {
		if (p->token == val)
			return p->pattern;
		p++;
	}
	BUG();
	return "??";
}

static const char *au_optstr(int *val, match_table_t tbl)
{
	struct match_token *p;
	int v;

	v = *val;
	if (!v)
		goto out;
	p = tbl;
	while (p->pattern) {
		if (p->token
		    && (v & p->token) == p->token) {
			*val &= ~p->token;
			return p->pattern;
		}
		p++;
	}

out:
	return NULL;
}

/* ---------------------------------------------------------------------- */

static match_table_t brperm = {
	{AuBrPerm_RO, AUFS_BRPERM_RO},
	{AuBrPerm_RR, AUFS_BRPERM_RR},
	{AuBrPerm_RW, AUFS_BRPERM_RW},
	{0, NULL}
};

static match_table_t brattr = {
	/* ro/rr branch */
	{AuBrRAttr_WH, AUFS_BRRATTR_WH},

	/* rw branch */
	{AuBrWAttr_NoLinkWH, AUFS_BRWATTR_NLWH},

	/* add more later */
	{0, NULL}
};

static int br_attr_val(char *str, match_table_t table, substring_t args[])
{
	int attr, v;
	char *p;

	attr = 0;
	do {
		p = strchr(str, '+');
		if (p)
			*p = 0;
		v = match_token(str, table, args);
		if (v)
			attr |= v;
		else {
			if (p)
				*p = '+';
			pr_warn("ignored branch attribute %s\n", str);
			break;
		}
		if (p)
			str = p + 1;
	} while (p);

	return attr;
}

static int au_do_optstr_br_attr(au_br_perm_str_t *str, int perm)
{
	int sz;
	const char *p;
	char *q;

	q = str->a;
	*q = 0;
	p = au_optstr(&perm, brattr);
	if (p) {
		sz = strlen(p);
		memcpy(q, p, sz + 1);
		q += sz;
	} else
		goto out;

	do {
		p = au_optstr(&perm, brattr);
		if (p) {
			*q++ = '+';
			sz = strlen(p);
			memcpy(q, p, sz + 1);
			q += sz;
		}
	} while (p);

out:
	return q - str->a;
}

int au_br_perm_val(char *perm)
{
	int val, bad, sz;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	au_br_perm_str_t attr;

	p = strchr(perm, '+');
	if (p)
		*p = 0;
	val = match_token(perm, brperm, args);
	if (!val) {
		if (p)
			*p = '+';
		pr_warn("ignored branch permission %s\n", perm);
		val = AuBrPerm_RO;
		goto out;
	}
	if (!p)
		goto out;

	val |= br_attr_val(p + 1, brattr, args);

	bad = 0;
	switch (val & AuBrPerm_Mask) {
	case AuBrPerm_RO:
	case AuBrPerm_RR:
		bad = val & AuBrWAttr_Mask;
		val &= ~AuBrWAttr_Mask;
		break;
	case AuBrPerm_RW:
		bad = val & AuBrRAttr_Mask;
		val &= ~AuBrRAttr_Mask;
		break;
	}
	if (unlikely(bad)) {
		sz = au_do_optstr_br_attr(&attr, bad);
		AuDebugOn(!sz);
		pr_warn("ignored branch attribute %s\n", attr.a);
	}

out:
	return val;
}

void au_optstr_br_perm(au_br_perm_str_t *str, int perm)
{
	au_br_perm_str_t attr;
	const char *p;
	char *q;
	int sz;

	q = str->a;
	p = au_optstr(&perm, brperm);
	AuDebugOn(!p || !*p);
	sz = strlen(p);
	memcpy(q, p, sz + 1);
	q += sz;

	sz = au_do_optstr_br_attr(&attr, perm);
	if (sz) {
		*q++ = '+';
		memcpy(q, attr.a, sz + 1);
	}

	AuDebugOn(strlen(str->a) >= sizeof(str->a));
}

/* ---------------------------------------------------------------------- */

static match_table_t udbalevel = {
	{AuOpt_UDBA_REVAL, "reval"},
	{AuOpt_UDBA_NONE, "none"},
	{-1, NULL}
};

int au_udba_val(char *str)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(str, udbalevel, args);
}

const char *au_optstr_udba(int udba)
{
	return au_parser_pattern(udba, udbalevel);
}

/* ---------------------------------------------------------------------- */

static match_table_t au_wbr_create_policy = {
	{AuWbrCreate_TDP, "tdp"},
	{AuWbrCreate_TDP, "top-down-parent"},
	{AuWbrCreate_RR, "rr"},
	{AuWbrCreate_RR, "round-robin"},
	{AuWbrCreate_MFS, "mfs"},
	{AuWbrCreate_MFS, "most-free-space"},
	{AuWbrCreate_MFSV, "mfs:%d"},
	{AuWbrCreate_MFSV, "most-free-space:%d"},

	/* top-down regardless the parent, and then mfs */
	{AuWbrCreate_TDMFS, "tdmfs:%d"},
	{AuWbrCreate_TDMFSV, "tdmfs:%d:%d"},

	{AuWbrCreate_MFSRR, "mfsrr:%d"},
	{AuWbrCreate_MFSRRV, "mfsrr:%d:%d"},
	{AuWbrCreate_PMFS, "pmfs"},
	{AuWbrCreate_PMFSV, "pmfs:%d"},
	{AuWbrCreate_PMFSRR, "pmfsrr:%d"},
	{AuWbrCreate_PMFSRRV, "pmfsrr:%d:%d"},

	{-1, NULL}
};

static int au_wbr_mfs_wmark(substring_t *arg, char *str,
			    struct au_opt_wbr_create *create)
{
	int err;
	unsigned long long ull;

	err = 0;
	if (!match_u64(arg, &ull))
		create->mfsrr_watermark = ull;
	else {
		pr_err("bad integer in %s\n", str);
		err = -EINVAL;
	}

	return err;
}

static int au_wbr_mfs_sec(substring_t *arg, char *str,
			  struct au_opt_wbr_create *create)
{
	int n, err;

	err = 0;
	if (!match_int(arg, &n) && 0 <= n && n <= AUFS_MFS_MAX_SEC)
		create->mfs_second = n;
	else {
		pr_err("bad integer in %s\n", str);
		err = -EINVAL;
	}

	return err;
}

int au_wbr_create_val(char *str, struct au_opt_wbr_create *create)
{
	int err, e;
	substring_t args[MAX_OPT_ARGS];

	err = match_token(str, au_wbr_create_policy, args);
	create->wbr_create = err;
	switch (err) {
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_TDMFSV:
	case AuWbrCreate_PMFSRRV:
		e = au_wbr_mfs_wmark(&args[0], str, create);
		if (!e)
			e = au_wbr_mfs_sec(&args[1], str, create);
		if (unlikely(e))
			err = e;
		break;
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_TDMFS:
	case AuWbrCreate_PMFSRR:
		e = au_wbr_mfs_wmark(&args[0], str, create);
		if (unlikely(e)) {
			err = e;
			break;
		}
		fallthrough;
	case AuWbrCreate_MFS:
	case AuWbrCreate_PMFS:
		create->mfs_second = AUFS_MFS_DEF_SEC;
		break;
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFSV:
		e = au_wbr_mfs_sec(&args[0], str, create);
		if (unlikely(e))
			err = e;
		break;
	}

	return err;
}

const char *au_optstr_wbr_create(int wbr_create)
{
	return au_parser_pattern(wbr_create, au_wbr_create_policy);
}

static match_table_t au_wbr_copyup_policy = {
	{AuWbrCopyup_TDP, "tdp"},
	{AuWbrCopyup_TDP, "top-down-parent"},
	{AuWbrCopyup_BUP, "bup"},
	{AuWbrCopyup_BUP, "bottom-up-parent"},
	{AuWbrCopyup_BU, "bu"},
	{AuWbrCopyup_BU, "bottom-up"},
	{-1, NULL}
};

int au_wbr_copyup_val(char *str)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(str, au_wbr_copyup_policy, args);
}

const char *au_optstr_wbr_copyup(int wbr_copyup)
{
	return au_parser_pattern(wbr_copyup, au_wbr_copyup_policy);
}

/* ---------------------------------------------------------------------- */

int au_opt_add(struct au_opt *opt, char *opt_str, unsigned long sb_flags,
	       aufs_bindex_t bindex)
{
	int err;
	struct au_opt_add *add = &opt->add;
	char *p;

	add->bindex = bindex;
	add->perm = AuBrPerm_RO;
	add->pathname = opt_str;
	p = strchr(opt_str, '=');
	if (p) {
		*p++ = 0;
		if (*p)
			add->perm = au_br_perm_val(p);
	}

	err = vfsub_kern_path(add->pathname, AuOpt_LkupDirFlags, &add->path);
	if (!err) {
		if (!p) {
			add->perm = AuBrPerm_RO;
			if (au_test_fs_rr(add->path.dentry->d_sb))
				add->perm = AuBrPerm_RR;
			else if (!bindex && !(sb_flags & SB_RDONLY))
				add->perm = AuBrPerm_RW;
		}
		opt->type = Opt_add;
		goto out;
	}
	pr_err("lookup failed %s (%d)\n", add->pathname, err);
	err = -EINVAL;

out:
	return err;
}

static int au_opt_wbr_create(struct super_block *sb,
			     struct au_opt_wbr_create *create)
{
	int err;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 1; /* handled */
	sbinfo = au_sbi(sb);
	if (sbinfo->si_wbr_create_ops->fin) {
		err = sbinfo->si_wbr_create_ops->fin(sb);
		if (!err)
			err = 1;
	}

	sbinfo->si_wbr_create = create->wbr_create;
	sbinfo->si_wbr_create_ops = au_wbr_create_ops + create->wbr_create;
	switch (create->wbr_create) {
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_TDMFS:
	case AuWbrCreate_TDMFSV:
	case AuWbrCreate_PMFSRR:
	case AuWbrCreate_PMFSRRV:
		sbinfo->si_wbr_mfs.mfsrr_watermark = create->mfsrr_watermark;
		fallthrough;
	case AuWbrCreate_MFS:
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFS:
	case AuWbrCreate_PMFSV:
		sbinfo->si_wbr_mfs.mfs_expire
			= msecs_to_jiffies(create->mfs_second * MSEC_PER_SEC);
		break;
	}

	if (sbinfo->si_wbr_create_ops->init)
		sbinfo->si_wbr_create_ops->init(sb); /* ignore */

	return err;
}

/*
 * returns,
 * plus: processed without an error
 * zero: unprocessed
 */
static int au_opt_simple(struct super_block *sb, struct au_opt *opt,
			 struct au_opts *opts)
{
	int err;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 1; /* handled */
	sbinfo = au_sbi(sb);
	switch (opt->type) {
	case Opt_plink:
		if (opt->tf)
			au_opt_set(sbinfo->si_mntflags, PLINK);
		else {
			if (au_opt_test(sbinfo->si_mntflags, PLINK))
				au_plink_put(sb, /*verbose*/1);
			au_opt_clr(sbinfo->si_mntflags, PLINK);
		}
		break;
	case Opt_list_plink:
		if (au_opt_test(sbinfo->si_mntflags, PLINK))
			au_plink_list(sb);
		break;

	case Opt_wbr_create:
		err = au_opt_wbr_create(sb, &opt->wbr_create);
		break;
	case Opt_wbr_copyup:
		sbinfo->si_wbr_copyup = opt->wbr_copyup;
		sbinfo->si_wbr_copyup_ops = au_wbr_copyup_ops + opt->wbr_copyup;
		break;

	case Opt_trunc_xino:
		if (opt->tf)
			au_opt_set(sbinfo->si_mntflags, TRUNC_XINO);
		else
			au_opt_clr(sbinfo->si_mntflags, TRUNC_XINO);
		break;

	case Opt_trunc_xino_path:
	case Opt_itrunc_xino:
		err = au_xino_trunc(sb, opt->xino_itrunc.bindex,
				    /*idx_begin*/0);
		if (!err)
			err = 1;
		break;

	case Opt_trunc_xib:
		if (opt->tf)
			au_fset_opts(opts->flags, TRUNC_XIB);
		else
			au_fclr_opts(opts->flags, TRUNC_XIB);
		break;

	default:
		err = 0;
		break;
	}

	return err;
}

/*
 * returns tri-state.
 * plus: processed without an error
 * zero: unprocessed
 * minus: error
 */
static int au_opt_br(struct super_block *sb, struct au_opt *opt,
		     struct au_opts *opts)
{
	int err;

	err = 0;
	switch (opt->type) {
	case Opt_add:
		err = au_br_add(sb, &opt->add);
		if (!err) {
			err = 1;
			/* au_fset_opts(opts->flags, REFRESH); re-commit later */
		}
		break;
	}
	return err;
}

static int au_opt_xino(struct super_block *sb, struct au_opt *opt,
		       struct au_opt_xino **opt_xino,
		       struct au_opts *opts)
{
	int err;

	err = 0;
	switch (opt->type) {
	case Opt_xino:
		err = au_xino_set(sb, &opt->xino);
		if (!err)
			*opt_xino = &opt->xino;
		break;
	case Opt_noxino:
		au_xino_clr(sb);
		*opt_xino = (void *)-1;
		break;
	}

	return err;
}

int au_opts_verify(struct super_block *sb, unsigned long sb_flags,
		   unsigned int pending)
{
	int err;
	aufs_bindex_t bindex, bbot;
	unsigned char do_plink, skip, do_free;
	struct au_branch *br;
	struct au_wbr *wbr;
	struct dentry *root;
	struct inode *dir, *h_dir;
	struct au_sbinfo *sbinfo;
	struct au_hinode *hdir;

	SiMustAnyLock(sb);

	sbinfo = au_sbi(sb);

	if (!(sb_flags & SB_RDONLY)) {
		if (unlikely(!au_br_writable(au_sbr_perm(sb, 0))))
			pr_warn("first branch should be rw\n");
	}

	err = 0;
	root = sb->s_root;
	dir = d_inode(root);
	do_plink = !!au_opt_test(sbinfo->si_mntflags, PLINK);
	bbot = au_sbbot(sb);
	for (bindex = 0; !err && bindex <= bbot; bindex++) {
		skip = 0;
		h_dir = au_h_iptr(dir, bindex);
		br = au_sbr(sb, bindex);
		do_free = 0;

		wbr = br->br_wbr;
		if (wbr)
			wbr_wh_read_lock(wbr);

		if (!au_br_writable(br->br_perm)) {
			do_free = !!wbr;
			skip = (!wbr
				|| (!wbr->wbr_whbase
				    && !wbr->wbr_plink
				    && !wbr->wbr_orph));
		} else if (!au_br_wh_linkable(br->br_perm)) {
			/* skip = (!br->br_whbase && !br->br_orph); */
			skip = (!wbr || !wbr->wbr_whbase);
			if (skip && wbr) {
				if (do_plink)
					skip = !!wbr->wbr_plink;
				else
					skip = !wbr->wbr_plink;
			}
		} else {
			/* skip = (br->br_whbase && br->br_ohph); */
			skip = (wbr && wbr->wbr_whbase);
			if (skip) {
				if (do_plink)
					skip = !!wbr->wbr_plink;
				else
					skip = !wbr->wbr_plink;
			}
		}
		if (wbr)
			wbr_wh_read_unlock(wbr);

		if (skip)
			continue;

		hdir = au_hi(dir, bindex);
		inode_lock_nested(hdir->hi_inode, AuLsc_I_PARENT);
		if (wbr)
			wbr_wh_write_lock(wbr);
		err = au_wh_init(br, sb);
		if (wbr)
			wbr_wh_write_unlock(wbr);
		inode_unlock(hdir->hi_inode);

		if (!err && do_free) {
			au_kfree_rcu(wbr);
			br->br_wbr = NULL;
		}
	}

	return err;
}

int au_opts_mount(struct super_block *sb, struct au_opts *opts)
{
	int err;
	unsigned int tmp;
	aufs_bindex_t bbot;
	struct au_opt *opt;
	struct au_opt_xino *opt_xino, xino;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 0;
	opt_xino = NULL;
	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail)
		err = au_opt_simple(sb, opt++, opts);
	if (err > 0)
		err = 0;
	else if (unlikely(err < 0))
		goto out;

	/* disable xino temporary */
	sbinfo = au_sbi(sb);
	tmp = sbinfo->si_mntflags;
	au_opt_clr(sbinfo->si_mntflags, XINO);

	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail)
		err = au_opt_br(sb, opt++, opts);
	if (err > 0)
		err = 0;
	else if (unlikely(err < 0))
		goto out;

	bbot = au_sbbot(sb);
	if (unlikely(bbot < 0)) {
		err = -EINVAL;
		pr_err("no branches\n");
		goto out;
	}

	if (au_opt_test(tmp, XINO))
		au_opt_set(sbinfo->si_mntflags, XINO);
	opt = opts->opt;
	while (!err && opt->type != Opt_tail)
		err = au_opt_xino(sb, opt++, &opt_xino, opts);
	if (unlikely(err))
		goto out;

	err = au_opts_verify(sb, sb->s_flags, tmp);
	if (unlikely(err))
		goto out;

	/* restore xino */
	if (au_opt_test(tmp, XINO) && !opt_xino) {
		xino.file = au_xino_def(sb);
		err = PTR_ERR(xino.file);
		if (IS_ERR(xino.file))
			goto out;

		err = au_xino_set(sb, &xino);
		fput(xino.file);
		if (unlikely(err))
			goto out;
	}

	bbot = au_sbbot(sb);

out:
	return err;
}

/* ---------------------------------------------------------------------- */

unsigned int au_opt_udba(struct super_block *sb)
{
	return au_mntflags(sb) & AuOptMask_UDBA;
}
