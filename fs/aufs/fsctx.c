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

/* stop extra interpretation of errno in mount(8), and strange error messages */
static int cvt_err(int err)
{
	AuTraceErr(err);

	switch (err) {
	case -ENOENT:
	case -ENOTDIR:
	case -EEXIST:
	case -EIO:
		err = -EINVAL;
	}
	return err;
}

static int au_fsctx_reconfigure(struct fs_context *fc)
{
	int err, do_dx;
	unsigned int mntflags;
	struct dentry *root;
	struct super_block *sb;
	struct inode *inode;
	struct au_fsctx_opts *a = fc->fs_private;

	AuDbg("fc %p\n", fc);

	root = fc->root;
	sb = root->d_sb;
	root = sb->s_root; /* "bind"-mount may give us non-root */
	AuDebugOn(!IS_ROOT(root));
	err = si_write_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (!err) {
		di_write_lock_child(root);
		err = au_opts_verify(sb, fc->sb_flags, /*pending*/0);
		aufs_write_unlock(root);
	}

	inode = d_inode(root);
	inode_lock(inode);
	err = si_write_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out;
	di_write_lock_child(root);

	/* au_opts_remount() may return an error */
	err = au_opts_remount(sb, &a->opts);

	if (au_ftest_opts(a->opts.flags, REFRESH))
		au_remount_refresh(sb, au_ftest_opts(a->opts.flags,
						     REFRESH_IDOP));

	if (au_ftest_opts(a->opts.flags, REFRESH_DYAOP)) {
		mntflags = au_mntflags(sb);
		do_dx = !!au_opt_test(mntflags, DIO);
		au_dy_arefresh(do_dx);
	}

	aufs_write_unlock(root);

out:
	inode_unlock(inode);
	err = cvt_err(err);
	if (unlikely(err))
		pr_err("remount err %d\n", err);

	return err;
}

/* ---------------------------------------------------------------------- */

static int au_fsctx_fill_super(struct super_block *sb, struct fs_context *fc)
{
	int err;
	struct au_fsctx_opts *a = fc->fs_private;
	struct au_sbinfo *sbinfo = a->sbinfo;
	struct dentry *root;
	struct inode *inode;

	sbinfo->si_sb = sb;
	sb->s_fs_info = sbinfo;
	kobject_get(&sbinfo->si_kobj);

	__si_write_lock(sb);
	si_pid_set(sb);
	au_sbilist_add(sb);

	/* all timestamps always follow the ones on the branch */
	sb->s_flags |= SB_NOATIME | SB_NODIRATIME | SB_I_VERSION;
	sb->s_op = &aufs_sop;
	set_default_d_op(sb, &aufs_dop);
	sb->s_magic = AUFS_SUPER_MAGIC;
	sb->s_maxbytes = 0;
	sb->s_stack_depth = 1;
	au_export_init(sb);
	au_xattr_init(sb);

	err = au_alloc_root(sb);
	if (unlikely(err)) {
		si_write_unlock(sb);
		goto out;
	}
	root = sb->s_root;
	inode = d_inode(root);
	ii_write_lock_parent(inode);
	aufs_write_unlock(root);

	/* lock vfs_inode first, then aufs. */
	inode_lock(inode);
	aufs_write_lock(root);
	err = au_opts_mount(sb, &a->opts);
	AuTraceErr(err);
	if (!err && au_ftest_si(sbinfo, NO_DREVAL)) {
		set_default_d_op(sb, &aufs_dop_noreval);
		/* infofc(fc, "%ps", sb->__s_d_op); */
		pr_info("%ps\n", sb->__s_d_op);
		au_refresh_dop(root, /*force_reval*/0);
		sbinfo->si_iop_array = aufs_iop_nogetattr;
		au_refresh_iop(inode, /*force_getattr*/0);
	}
	aufs_write_unlock(root);
	inode_unlock(inode);
	if (!err)
		goto out; /* success */

	dput(root);
	sb->s_root = NULL;

out:
	if (unlikely(err))
		kobject_put(&sbinfo->si_kobj);
	AuTraceErr(err);
	err = cvt_err(err);
	AuTraceErr(err);
	return err;
}

static int au_fsctx_get_tree(struct fs_context *fc)
{
	int err;

	AuDbg("fc %p\n", fc);
	err = get_tree_nodev(fc, au_fsctx_fill_super);

	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static void au_fsctx_dump(struct au_opts *opts)
{
#ifdef CONFIG_AUFS_DEBUG
	/* reduce stack space */
	union {
		struct au_opt_add *add;
		struct au_opt_xino *xino;
		struct au_opt_xino_itrunc *xino_itrunc;
		struct au_opt_wbr_create *create;
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
		case Opt_append:
			u.add = &opt->add;
			AuDbg("append {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;
		case Opt_prepend:
			u.add = &opt->add;
			AuDbg("prepend {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;

		case Opt_rdcache:
			AuDbg("rdcache %d\n", opt->rdcache);
			break;
		case Opt_rdblk:
			AuDbg("rdblk %d\n", opt->rdblk);
			break;
		case Opt_rdhash:
			AuDbg("rdhash %u\n", opt->rdhash);
			break;

		case Opt_xino:
			u.xino = &opt->xino;
			AuDbg("xino {%s %pD}\n", u.xino->path, u.xino->file);
			break;

#define au_fsctx_TF(name)					  \
			case Opt_##name:			  \
				if (opt->tf)			  \
					AuLabel(name);		  \
				else				  \
					AuLabel(no##name);	  \
				break;

		/* simple true/false flag */
		au_fsctx_TF(trunc_xino);
		au_fsctx_TF(trunc_xib);
		au_fsctx_TF(plink);
		au_fsctx_TF(dio);
		au_fsctx_TF(acl);
#undef au_fsctx_TF

		case Opt_trunc_xino_path:
			fallthrough;
		case Opt_itrunc_xino:
			u.xino_itrunc = &opt->xino_itrunc;
			AuDbg("trunc_xino %d\n", u.xino_itrunc->bindex);
			break;
		case Opt_noxino:
			AuLabel(noxino);
			break;

		case Opt_list_plink:
			AuLabel(list_plink);
			break;
		case Opt_udba:
			AuDbg("udba %d, %s\n",
				  opt->udba, au_optstr_udba(opt->udba));
			break;
		case Opt_wbr_create:
			u.create = &opt->wbr_create;
			AuDbg("create %d, %s\n", u.create->wbr_create,
				  au_optstr_wbr_create(u.create->wbr_create));
			switch (u.create->wbr_create) {
			case AuWbrCreate_MFSV:
				fallthrough;
			case AuWbrCreate_PMFSV:
				AuDbg("%d sec\n", u.create->mfs_second);
				break;
			case AuWbrCreate_MFSRR:
				fallthrough;
			case AuWbrCreate_TDMFS:
				AuDbg("%llu watermark\n",
					  u.create->mfsrr_watermark);
				break;
			case AuWbrCreate_MFSRRV:
				fallthrough;
			case AuWbrCreate_TDMFSV:
				fallthrough;
			case AuWbrCreate_PMFSRRV:
				AuDbg("%llu watermark, %d sec\n",
					  u.create->mfsrr_watermark,
					  u.create->mfs_second);
				break;
			}
			break;
		case Opt_wbr_copyup:
			AuDbg("copyup %d, %s\n", opt->wbr_copyup,
				  au_optstr_wbr_copyup(opt->wbr_copyup));
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

	/* "add=%d:%s" or "ins=%d:%s" */
	fsparam_string("add", Opt_add),
	fsparam_string("ins", Opt_add),
	fsparam_path("append", Opt_append),
	fsparam_path("prepend", Opt_prepend),

	fsparam_path("xino", Opt_xino),
	fsparam_flag("noxino", Opt_noxino),
	fsparam_flag_no("trunc_xino", Opt_trunc_xino),
	/* "trunc_xino_v=%d:%d" */
	/* fsparam_string("trunc_xino_v", Opt_trunc_xino_v), */
	fsparam_path("trunc_xino", Opt_trunc_xino_path),
	fsparam_s32("itrunc_xino", Opt_itrunc_xino),
	/* fsparam_path("zxino", Opt_zxino), */
	fsparam_flag_no("trunc_xib", Opt_trunc_xib),

#ifdef CONFIG_PROC_FS
	fsparam_flag_no("plink", Opt_plink),
#else
	au_ignore_flag("plink", Opt_ignore),
#endif

#ifdef CONFIG_AUFS_DEBUG
	fsparam_flag("list_plink", Opt_list_plink),
#endif

	fsparam_string("udba", Opt_udba),

	fsparam_flag_no("dio", Opt_dio),

	fsparam_s32("rdcache", Opt_rdcache),
	/* "def" or s32 */
	fsparam_string("rdblk", Opt_rdblk),
	/* "def" or s32 */
	fsparam_string("rdhash", Opt_rdhash),

	fsparam_string("create", Opt_wbr_create),
	fsparam_string("create_policy", Opt_wbr_create),
	fsparam_string("cpup", Opt_wbr_copyup),
	fsparam_string("copyup", Opt_wbr_copyup),
	fsparam_string("copyup_policy", Opt_wbr_copyup),

	/* generic VFS flag */
#ifdef CONFIG_FS_POSIX_ACL
	fsparam_flag_no("acl", Opt_acl),
#else
	au_ignore_flag("acl", Opt_ignore),
#endif

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

static
int au_fsctx_parse_xino_itrunc_path(struct fs_context *fc,
				    struct au_opt_xino_itrunc *xino_itrunc,
				    char *pathname)
{
	int err;
	aufs_bindex_t bbot, bindex;
	struct path path;
	struct dentry *root;
	struct au_fsctx_opts *a = fc->fs_private;

	AuDebugOn(!a->sb);

	err = vfsub_kern_path(pathname, AuOpt_LkupDirFlags, &path);
	if (unlikely(err)) {
		errorfc(fc, "lookup failed %s (%d)", pathname, err);
		goto out;
	}

	xino_itrunc->bindex = -1;
	root = a->sb->s_root;
	aufs_read_lock(root, AuLock_FLUSH);
	bbot = au_sbbot(a->sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		if (au_h_dptr(root, bindex) == path.dentry) {
			xino_itrunc->bindex = bindex;
			break;
		}
	}
	aufs_read_unlock(root, !AuLock_IR);
	path_put(&path);

	if (unlikely(xino_itrunc->bindex < 0)) {
		err = -EINVAL;
		errorfc(fc, "no such branch %s", pathname);
	}

out:
	AuTraceErr(err);
	return err;
}

static int au_fsctx_parse_xino_itrunc(struct fs_context *fc,
				      struct au_opt_xino_itrunc *xino_itrunc,
				      unsigned int bindex)
{
	int err;
	aufs_bindex_t bbot;
	struct super_block *sb;
	struct au_fsctx_opts *a = fc->fs_private;

	sb = a->sb;
	AuDebugOn(!sb);

	err = 0;
	si_noflush_read_lock(sb);
	bbot = au_sbbot(sb);
	si_read_unlock(sb);
	if (bindex <= bbot)
		xino_itrunc->bindex = bindex;
	else {
		err = -EINVAL;
		errorfc(fc, "out of bounds, %u", bindex);
	}

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
	case Opt_append:
		err = au_fsctx_parse_do_add(fc, opt, param->string, param->size,
					    /*dummy bindex*/1);
		break;
	case Opt_prepend:
		err = au_fsctx_parse_do_add(fc, opt, param->string, param->size,
					    /*bindex*/0);
		break;

	case Opt_xino:
		err = au_fsctx_parse_xino(fc, &opt->xino, param);
		break;
	case Opt_trunc_xino_path:
		if (!a->sb) {
			errorfc(fc, "no such branch %s", param->string);
			break;
		}
		err = au_fsctx_parse_xino_itrunc_path(fc, &opt->xino_itrunc,
						      param->string);
		break;
#if 0
	case Opt_trunc_xino_v:
		if (!a->sb) {
			err = 0;
			a->skipped = 1;
			break;
		}
		err = au_fsctx_parse_xino_itrunc_path(fc, &opt->xino_itrunc,
						      param->string);
		break;
#endif
	case Opt_itrunc_xino:
		if (!a->sb) {
			errorfc(fc, "out of bounds %s", param->string);
			break;
		}
		err = au_fsctx_parse_xino_itrunc(fc, &opt->xino_itrunc,
						 result.int_32);
		break;

	case Opt_rdcache:
		if (unlikely(result.int_32 > AUFS_RDCACHE_MAX)) {
			errorfc(fc, "rdcache must be smaller than %d",
				AUFS_RDCACHE_MAX);
			break;
		}
		err = 0;
		opt->rdcache = result.int_32;
		break;

	case Opt_rdblk:
		err = 0;
		opt->rdblk = AUFS_RDBLK_DEF;
		if (!strcmp(param->string, "def"))
			break;

		err = kstrtoint(param->string, 0, &result.int_32);
		if (unlikely(err)) {
			errorfc(fc, "bad value in %s", param->key);
			break;
		}
		err = -EINVAL;
		if (unlikely(result.int_32 < 0
			     || result.int_32 > KMALLOC_MAX_SIZE)) {
			errorfc(fc, "bad value in %s", param->key);
			break;
		}
		if (unlikely(result.int_32 && result.int_32 < NAME_MAX)) {
			errorfc(fc, "rdblk must be larger than %d", NAME_MAX);
			break;
		}
		err = 0;
		opt->rdblk = result.int_32;
		break;

	case Opt_rdhash:
		err = 0;
		opt->rdhash = AUFS_RDHASH_DEF;
		if (!strcmp(param->string, "def"))
			break;

		err = kstrtoint(param->string, 0, &result.int_32);
		if (unlikely(err)) {
			errorfc(fc, "bad value in %s", param->key);
			break;
		}
		/* how about zero? */
		if (result.int_32 < 0
		    || result.int_32 * sizeof(struct hlist_head)
		    > KMALLOC_MAX_SIZE) {
			err = -EINVAL;
			errorfc(fc, "bad integer in %s", param->key);
			break;
		}
		opt->rdhash = result.int_32;
		break;

	case Opt_udba:
		opt->udba = au_udba_val(param->string);
		if (opt->udba >= 0)
			err = 0;
		else
			errorf(fc, "wrong value, %s", param->string);
		break;

	case Opt_wbr_create:
		opt->wbr_create.wbr_create
			= au_wbr_create_val(param->string, &opt->wbr_create);
		if (opt->wbr_create.wbr_create >= 0)
			err = 0;
		else
			errorf(fc, "wrong value, %s", param->key);
		break;

	case Opt_wbr_copyup:
		opt->wbr_copyup = au_wbr_copyup_val(param->string);
		if (opt->wbr_copyup >= 0)
			err = 0;
		else
			errorfc(fc, "wrong value, %s", param->key);
		break;

	/* simple true/false flag */
#define au_fsctx_TF(name)				\
		case Opt_##name:			\
			err = 0;			\
			opt->tf = !result.negated;	\
			break
	au_fsctx_TF(trunc_xino);
	au_fsctx_TF(trunc_xib);
	au_fsctx_TF(plink);
	au_fsctx_TF(dio);
	au_fsctx_TF(acl);
#undef au_fsctx_TF

	case Opt_noxino:
		fallthrough;
	case Opt_list_plink:
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
	do_test("ins");
	do_test("append");
	do_test("prepend");
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
			fallthrough;
		case Opt_append:
			fallthrough;
		case Opt_prepend:
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
	.get_tree		= au_fsctx_get_tree,
	.reconfigure		= au_fsctx_reconfigure
	/*
	 * nfs4 requires ->dup()? No.
	 * I don't know what is this ->dup() for.
	 */
};

int aufs_fsctx_init(struct fs_context *fc)
{
	int err;
	struct au_fsctx_opts *a;

	/* fs_type=%p, root=%pD */
	AuDbg("fc %p{sb_flags 0x%x, sb_flags_mask 0x%x, purpose %u\n",
	      fc, fc->sb_flags, fc->sb_flags_mask, fc->purpose);

	/* they will be freed by au_fsctx_free() */
	err = -ENOMEM;
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;
	a->bindex = 0;
	a->opts.opt = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!a->opts.opt))
		goto out_a;
	a->opt = a->opts.opt;
	a->opt->type = Opt_tail;
	a->opts.max_opt = PAGE_SIZE / sizeof(*a->opts.opt);
	a->opt_tail = a->opt + a->opts.max_opt - 1;
	a->opts.sb_flags = fc->sb_flags;

	a->sb = NULL;
	if (fc->root) {
		AuDebugOn(fc->purpose != FS_CONTEXT_FOR_RECONFIGURE);
		a->opts.flags = AuOpts_REMOUNT;
		a->sb = fc->root->d_sb;
		a->sbinfo = au_sbi(a->sb);
		kobject_get(&a->sbinfo->si_kobj);
	} else {
		a->sbinfo = au_si_alloc(a->sb);
		AuDebugOn(!a->sbinfo);
		err = PTR_ERR(a->sbinfo);
		if (IS_ERR(a->sbinfo))
			goto out_opt;
		au_rw_write_unlock(&a->sbinfo->si_rwsem);
	}

	err = 0;
	fc->fs_private = a;
	fc->ops = &au_fsctx_ops;
	goto out; /* success */

out_opt:
	free_page((unsigned long)a->opts.opt);
out_a:
	au_kfree_rcu(a);
out:
	AuTraceErr(err);
	return err;
}
