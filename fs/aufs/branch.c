// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * branch management
 */

#include <linux/statfs.h>
#include "aufs.h"

/*
 * free a single branch
 */
static void au_br_do_free(struct au_branch *br)
{
	int i;
	struct au_wbr *wbr;

	au_xino_put(br);

	AuLCntZero(au_lcnt_read(&br->br_nfiles, /*do_rev*/0));
	au_lcnt_fin(&br->br_nfiles, /*do_sync*/0);
	AuLCntZero(au_lcnt_read(&br->br_count, /*do_rev*/0));
	au_lcnt_fin(&br->br_count, /*do_sync*/0);

	wbr = br->br_wbr;
	if (wbr) {
		for (i = 0; i < AuBrWh_Last; i++)
			dput(wbr->wbr_wh[i]);
		AuDebugOn(atomic_read(&wbr->wbr_wh_running));
		AuRwDestroy(&wbr->wbr_wh_rwsem);
	}

	/* recursive lock, s_umount of branch's */
	/* synchronize_rcu(); */ /* why? */
	lockdep_off();
	path_put(&br->br_path);
	lockdep_on();
	au_kfree_rcu(wbr);
	au_lcnt_wait_for_fin(&br->br_nfiles);
	au_lcnt_wait_for_fin(&br->br_count);
	/* I don't know why, but percpu_refcount requires this */
	/* synchronize_rcu(); */
	au_kfree_rcu(br);
}

/*
 * frees all branches
 */
void au_br_free(struct au_sbinfo *sbinfo)
{
	aufs_bindex_t bmax;
	struct au_branch **br;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	bmax = sbinfo->si_bbot + 1;
	br = sbinfo->si_branch;
	while (bmax--)
		au_br_do_free(*br++);
}

/*
 * find the index of a branch which is specified by @br_id.
 */
int au_br_index(struct super_block *sb, aufs_bindex_t br_id)
{
	aufs_bindex_t bindex, bbot;

	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++)
		if (au_sbr_id(sb, bindex) == br_id)
			return bindex;
	return -1;
}

/* ---------------------------------------------------------------------- */

/*
 * add a branch
 */

static int test_overlap(struct super_block *sb, struct dentry *h_adding,
			struct dentry *h_root)
{
	if (unlikely(h_adding == h_root))
		return 1;
	if (h_adding->d_sb != h_root->d_sb)
		return 0;
	return au_test_subdir(h_adding, h_root)
		|| au_test_subdir(h_root, h_adding);
}

/*
 * returns a newly allocated branch. @new_nbranch is a number of branches
 * after adding a branch.
 */
static struct au_branch *au_br_alloc(struct super_block *sb, int new_nbranch,
				     int perm)
{
	struct au_branch *add_branch;
	struct dentry *root;
	struct inode *inode;
	int err;

	err = -ENOMEM;
	add_branch = kzalloc(sizeof(*add_branch), GFP_NOFS);
	if (unlikely(!add_branch))
		goto out;
	add_branch->br_xino = au_xino_alloc(/*nfile*/1);
	if (unlikely(!add_branch->br_xino))
		goto out_br;

	if (au_br_writable(perm)) {
		/* may be freed separately at changing the branch permission */
		add_branch->br_wbr = kzalloc(sizeof(*add_branch->br_wbr),
					     GFP_NOFS);
		if (unlikely(!add_branch->br_wbr))
			goto out_xino;
	}

	root = sb->s_root;
	err = au_sbr_realloc(au_sbi(sb), new_nbranch, /*may_shrink*/0);
	if (!err)
		err = au_di_realloc(au_di(root), new_nbranch, /*may_shrink*/0);
	if (!err) {
		inode = d_inode(root);
		err = au_hinode_realloc(au_ii(inode), new_nbranch,
					/*may_shrink*/0);
	}
	if (!err)
		return add_branch; /* success */

	au_kfree_rcu(add_branch->br_wbr);

out_xino:
	au_xino_put(add_branch);
out_br:
	au_kfree_rcu(add_branch);
out:
	return ERR_PTR(err);
}

/*
 * test if the branch permission is legal or not.
 */
static int test_br(struct inode *inode, int brperm, char *path)
{
	int err;

	err = (au_br_writable(brperm) && IS_RDONLY(inode));
	if (!err)
		goto out;

	err = -EINVAL;
	pr_err("write permission for readonly mount or inode, %s\n", path);

out:
	return err;
}

/*
 * returns:
 * 0: success, the caller will add it
 * plus: success, it is already unified, the caller should ignore it
 * minus: error
 */
static int test_add(struct super_block *sb, struct au_opt_add *add)
{
	int err;
	aufs_bindex_t bbot, bindex;
	struct dentry *root;
	struct inode *inode;

	root = sb->s_root;
	bbot = au_sbbot(sb);
	if (unlikely(bbot >= 0
		     && au_find_dbindex(root, add->path.dentry) >= 0)) {
		err = -EINVAL;
		pr_err("%s duplicated\n", add->pathname);
		goto out;
	}

	err = -ENOSPC; /* -E2BIG; */
	if (unlikely(AUFS_BRANCH_MAX <= add->bindex
		     || AUFS_BRANCH_MAX - 1 <= bbot)) {
		pr_err("number of branches exceeded %s\n", add->pathname);
		goto out;
	}

	err = -EDOM;
	if (unlikely(add->bindex < 0 || bbot + 1 < add->bindex)) {
		pr_err("bad index %d\n", add->bindex);
		goto out;
	}

	inode = d_inode(add->path.dentry);
	err = -ENOENT;
	if (unlikely(!vfsub_inode_nlink(inode, AU_I_UNKNOWN))) {
		pr_err("no existence %s\n", add->pathname);
		goto out;
	}

	err = -EINVAL;
	if (unlikely(inode->i_sb == sb)) {
		pr_err("%s must be outside\n", add->pathname);
		goto out;
	}

	if (unlikely(au_test_fs_unsuppoted(inode->i_sb))) {
		pr_err("unsupported filesystem, %s (%s)\n",
		       add->pathname, au_sbtype(inode->i_sb));
		goto out;
	}

	if (unlikely(inode->i_sb->s_stack_depth)) {
		pr_err("already stacked, %s (%s)\n",
		       add->pathname, au_sbtype(inode->i_sb));
		goto out;
	}

	err = test_br(d_inode(add->path.dentry), add->perm, add->pathname);
	if (unlikely(err))
		goto out;

	if (bbot < 0)
		return 0; /* success */

	err = -EINVAL;
	for (bindex = 0; bindex <= bbot; bindex++)
		if (unlikely(test_overlap(sb, add->path.dentry,
					  au_h_dptr(root, bindex)))) {
			pr_err("%s is overlapped\n", add->pathname);
			goto out;
		}

	err = 0;

out:
	return err;
}

/*
 * initialize or clean the whiteouts for an adding branch
 */
static int au_br_init_wh(struct super_block *sb, struct au_branch *br,
			 int new_perm)
{
	int err, old_perm;
	aufs_bindex_t bindex;
	struct inode *h_inode;
	struct au_wbr *wbr;
	struct au_hinode *hdir;
	struct dentry *h_dentry;

	err = vfsub_mnt_want_write(au_br_mnt(br));
	if (unlikely(err))
		goto out;

	wbr = br->br_wbr;
	old_perm = br->br_perm;
	br->br_perm = new_perm;
	hdir = NULL;
	h_inode = NULL;
	bindex = au_br_index(sb, br->br_id);
	if (0 <= bindex) {
		hdir = au_hi(d_inode(sb->s_root), bindex);
		inode_lock_nested(hdir->hi_inode, AuLsc_I_PARENT);
	} else {
		h_dentry = au_br_dentry(br);
		h_inode = d_inode(h_dentry);
		inode_lock_nested(h_inode, AuLsc_I_PARENT);
	}
	if (!wbr)
		err = au_wh_init(br, sb);
	else {
		wbr_wh_write_lock(wbr);
		err = au_wh_init(br, sb);
		wbr_wh_write_unlock(wbr);
	}
	if (hdir)
		inode_unlock(hdir->hi_inode);
	else
		inode_unlock(h_inode);
	vfsub_mnt_drop_write(au_br_mnt(br));
	br->br_perm = old_perm;

	if (!err && wbr && !au_br_writable(new_perm)) {
		au_kfree_rcu(wbr);
		br->br_wbr = NULL;
	}

out:
	return err;
}

static int au_wbr_init(struct au_branch *br, struct super_block *sb,
		       int perm)
{
	int err;
	struct kstatfs kst;
	struct au_wbr *wbr;

	wbr = br->br_wbr;
	au_rw_init(&wbr->wbr_wh_rwsem);
	atomic_set(&wbr->wbr_wh_running, 0);

	/*
	 * a limit for rmdir/rename a dir
	 * cf. AUFS_MAX_NAMELEN in include/uapi/linux/aufs_type.h
	 */
	err = vfs_statfs(&br->br_path, &kst);
	if (unlikely(err))
		goto out;
	err = -EINVAL;
	if (kst.f_namelen >= NAME_MAX)
		err = au_br_init_wh(sb, br, perm);
	else
		pr_err("%pd(%s), unsupported namelen %ld\n",
		       au_br_dentry(br),
		       au_sbtype(au_br_dentry(br)->d_sb), kst.f_namelen);

out:
	return err;
}

/* initialize a new branch */
static int au_br_init(struct au_branch *br, struct super_block *sb,
		      struct au_opt_add *add)
{
	int err;
	struct au_branch *brbase;
	struct file *xf;
	struct inode *h_inode;

	err = 0;
	br->br_perm = add->perm;
	br->br_path = add->path; /* set first, path_get() later */
	au_lcnt_init(&br->br_nfiles, /*release*/NULL);
	au_lcnt_init(&br->br_count, /*release*/NULL);
	br->br_id = au_new_br_id(sb);
	AuDebugOn(br->br_id < 0);

	if (au_br_writable(add->perm)) {
		err = au_wbr_init(br, sb, add->perm);
		if (unlikely(err))
			goto out_err;
	}

	if (au_opt_test(au_mntflags(sb), XINO)) {
		brbase = au_sbr(sb, 0);
		xf = au_xino_file(brbase->br_xino, /*idx*/-1);
		AuDebugOn(!xf);
		h_inode = d_inode(add->path.dentry);
		err = au_xino_init_br(sb, br, h_inode->i_ino, &xf->f_path);
		if (unlikely(err)) {
			AuDebugOn(au_xino_file(br->br_xino, /*idx*/-1));
			goto out_err;
		}
	}

	sysaufs_br_init(br);
	path_get(&br->br_path);
	goto out; /* success */

out_err:
	memset(&br->br_path, 0, sizeof(br->br_path));
out:
	return err;
}

static void au_br_do_add_brp(struct au_sbinfo *sbinfo, aufs_bindex_t bindex,
			     struct au_branch *br, aufs_bindex_t bbot,
			     aufs_bindex_t amount)
{
	struct au_branch **brp;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	brp = sbinfo->si_branch + bindex;
	memmove(brp + 1, brp, sizeof(*brp) * amount);
	*brp = br;
	sbinfo->si_bbot++;
	if (unlikely(bbot < 0))
		sbinfo->si_bbot = 0;
}

static void au_br_do_add_hdp(struct au_dinfo *dinfo, aufs_bindex_t bindex,
			     aufs_bindex_t bbot, aufs_bindex_t amount)
{
	struct au_hdentry *hdp;

	AuRwMustWriteLock(&dinfo->di_rwsem);

	hdp = au_hdentry(dinfo, bindex);
	memmove(hdp + 1, hdp, sizeof(*hdp) * amount);
	au_h_dentry_init(hdp);
	dinfo->di_bbot++;
	if (unlikely(bbot < 0))
		dinfo->di_btop = 0;
}

static void au_br_do_add_hip(struct au_iinfo *iinfo, aufs_bindex_t bindex,
			     aufs_bindex_t bbot, aufs_bindex_t amount)
{
	struct au_hinode *hip;

	AuRwMustWriteLock(&iinfo->ii_rwsem);

	hip = au_hinode(iinfo, bindex);
	memmove(hip + 1, hip, sizeof(*hip) * amount);
	au_hinode_init(hip);
	iinfo->ii_bbot++;
	if (unlikely(bbot < 0))
		iinfo->ii_btop = 0;
}

static void au_br_do_add(struct super_block *sb, struct au_branch *br,
			 aufs_bindex_t bindex)
{
	struct dentry *root, *h_dentry;
	struct inode *root_inode, *h_inode;
	aufs_bindex_t bbot, amount;

	root = sb->s_root;
	root_inode = d_inode(root);
	bbot = au_sbbot(sb);
	amount = bbot + 1 - bindex;
	h_dentry = au_br_dentry(br);
	au_br_do_add_brp(au_sbi(sb), bindex, br, bbot, amount);
	au_br_do_add_hdp(au_di(root), bindex, bbot, amount);
	au_br_do_add_hip(au_ii(root_inode), bindex, bbot, amount);
	au_set_h_dptr(root, bindex, dget(h_dentry));
	h_inode = d_inode(h_dentry);
	au_set_h_iptr(root_inode, bindex, au_igrab(h_inode), /*flags*/0);
}

int au_br_add(struct super_block *sb, struct au_opt_add *add)
{
	int err;
	aufs_bindex_t bbot, add_bindex;
	struct dentry *root, *h_dentry;
	struct inode *root_inode;
	struct au_branch *add_branch;

	root = sb->s_root;
	root_inode = d_inode(root);
	IMustLock(root_inode);
	IiMustWriteLock(root_inode);
	err = test_add(sb, add);
	if (unlikely(err < 0))
		goto out;
	if (err) {
		err = 0;
		goto out; /* success */
	}

	bbot = au_sbbot(sb);
	add_branch = au_br_alloc(sb, bbot + 2, add->perm);
	err = PTR_ERR(add_branch);
	if (IS_ERR(add_branch))
		goto out;

	err = au_br_init(add_branch, sb, add);
	if (unlikely(err)) {
		au_br_do_free(add_branch);
		goto out;
	}

	add_bindex = add->bindex;
	au_br_do_add(sb, add_branch, add_bindex);

	h_dentry = add->path.dentry;
	if (!add_bindex) {
		au_cpup_attr_all(root_inode, /*force*/1);
		sb->s_maxbytes = h_dentry->d_sb->s_maxbytes;
	} else
		au_add_nlink(root_inode, d_inode(h_dentry));

out:
	return err;
}
