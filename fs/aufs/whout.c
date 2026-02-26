// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * whiteout for logical deletion and opaque directory
 */

#include "aufs.h"

#define WH_MASK			0444

/*
 * generate whiteout name, which is NOT terminated by NULL.
 * @name: original d_name.name
 * @len: original d_name.len
 * @wh: whiteout qstr
 * returns zero when succeeds, otherwise error.
 * succeeded value as wh->name should be freed by kfree().
 */
int au_wh_name_alloc(struct qstr *wh, const struct qstr *name)
{
	char *p;

	if (unlikely(name->len > PATH_MAX - AUFS_WH_PFX_LEN))
		return -ENAMETOOLONG;

	wh->len = name->len + AUFS_WH_PFX_LEN;
	p = kmalloc(wh->len, GFP_NOFS);
	wh->name = p;
	if (p) {
		memcpy(p, AUFS_WH_PFX, AUFS_WH_PFX_LEN);
		memcpy(p + AUFS_WH_PFX_LEN, name->name, name->len);
		/* smp_mb(); */
		return 0;
	}
	return -ENOMEM;
}

/* ---------------------------------------------------------------------- */

/*
 * test if the @wh_name exists under @h_ppath.
 * @try_sio specifies the necessary of super-io.
 */
int au_wh_test(struct mnt_idmap *h_idmap, struct path *h_ppath,
	       const struct qstr *wh_name, int try_sio)
{
	int err;
	struct dentry *wh_dentry;

#if 0 /* re-commit later */
	if (!try_sio)
		wh_dentry = vfsub_lkup_one(wh_name, h_ppath);
	else
		wh_dentry = au_sio_lkup_one(h_idmap, wh_name, h_ppath);
#else
	wh_dentry = ERR_PTR(-ENOENT);
#endif
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry)) {
		if (err == -ENAMETOOLONG)
			err = 0;
		goto out;
	}

	err = 0;
	if (d_is_negative(wh_dentry))
		goto out_wh; /* success */

	err = 1;
	if (d_is_reg(wh_dentry))
		goto out_wh; /* success */

	err = -EIO;
	AuIOErr("%pd Invalid whiteout entry type 0%o.\n",
		wh_dentry, d_inode(wh_dentry)->i_mode);

out_wh:
	dput(wh_dentry);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * lookup whiteout dentry.
 * @h_parent: lower parent dentry which must exist and be locked
 * @base_name: name of dentry which will be whiteouted
 * returns dentry for whiteout.
 */
struct dentry *au_wh_lkup(struct dentry *h_parent, const struct qstr *base_name,
			  struct au_branch *br)
{
	int err;
	struct qstr wh_name;
	struct dentry *wh_dentry;
	struct path h_path;

	err = au_wh_name_alloc(&wh_name, base_name);
	wh_dentry = ERR_PTR(err);
	if (!err) {
		h_path.dentry = h_parent;
		h_path.mnt = au_br_mnt(br);
		wh_dentry = vfsub_lkup_one(&wh_name, &h_path);
		au_kfree_try_rcu(wh_name.name);
	}
	return wh_dentry;
}

/*
 * link/create a whiteout for @dentry on @bindex.
 */
struct dentry *au_wh_create(struct dentry *dentry, aufs_bindex_t bindex,
			    struct dentry *h_parent)
{
	struct dentry *wh_dentry;
	struct super_block *sb;
	int err;

	sb = dentry->d_sb;
	wh_dentry = au_wh_lkup(h_parent, &dentry->d_name, au_sbr(sb, bindex));
	if (!IS_ERR(wh_dentry) && d_is_negative(wh_dentry)) {
		/* err = link_or_create_wh(sb, bindex, wh_dentry); re-commit later */
		err = 0;
		if (!err)
			au_set_dbwh(dentry, bindex);
		else {
			dput(wh_dentry);
			wh_dentry = ERR_PTR(err);
		}
	}

	return wh_dentry;
}
