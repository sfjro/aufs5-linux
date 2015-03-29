// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * lookup and dentry operations
 */

#include <linux/iversion.h>
#include "aufs.h"

/*
 * returns positive/negative dentry, NULL or an error.
 * NULL means whiteout-ed or not-found.
 */
static struct dentry*
au_do_lookup(struct dentry *h_parent, struct dentry *dentry,
	     aufs_bindex_t bindex, struct au_do_lookup_args *args)
{
	struct dentry *h_dentry;
	struct inode *h_inode;
	struct au_branch *br;
	struct mnt_idmap *h_idmap;
	struct path h_path;
	int wh_found, opq;
	unsigned char wh_able;
	const unsigned char allow_neg = !!au_ftest_lkup(args->flags, ALLOW_NEG);
	const unsigned char ignore_perm = !!au_ftest_lkup(args->flags,
							  IGNORE_PERM);

	wh_found = 0;
	br = au_sbr(dentry->d_sb, bindex);
	h_path.dentry = h_parent;
	h_path.mnt = au_br_mnt(br);
	h_idmap = au_br_idmap(br);
	wh_able = !!au_br_whable(br->br_perm);
	if (wh_able)
		wh_found = au_wh_test(h_idmap, &h_path, &args->whname,
				      ignore_perm);
	h_dentry = ERR_PTR(wh_found);
	if (!wh_found)
		goto real_lookup;
	if (unlikely(wh_found < 0))
		goto out;

	/* We found a whiteout */
	/* au_set_dbbot(dentry, bindex); */
	au_set_dbwh(dentry, bindex);
	if (!allow_neg)
		return NULL; /* success */

real_lookup:
	if (!ignore_perm)
		h_dentry = vfsub_lkup_one(args->name, &h_path);
	else
		h_dentry = au_sio_lkup_one(h_idmap, args->name, &h_path);
	if (IS_ERR(h_dentry)) {
		if (PTR_ERR(h_dentry) == -ENAMETOOLONG
		    && !allow_neg)
			h_dentry = NULL;
		goto out;
	}

	h_inode = d_inode(h_dentry);
	if (d_is_negative(h_dentry)) {
		if (!allow_neg)
			goto out_neg;
	} else if (wh_found
		   || (args->type && args->type != (h_inode->i_mode & S_IFMT)))
		goto out_neg;

	if (au_dbbot(dentry) <= bindex)
		au_set_dbbot(dentry, bindex);
	if (au_dbtop(dentry) < 0 || bindex < au_dbtop(dentry))
		au_set_dbtop(dentry, bindex);
	au_set_h_dptr(dentry, bindex, h_dentry);

	if (!d_is_dir(h_dentry)
	    || !wh_able
	    || (d_really_is_positive(dentry) && !d_is_dir(dentry)))
		goto out; /* success */

	h_path.dentry = h_dentry;
	inode_lock_shared_nested(h_inode, AuLsc_I_CHILD);
	opq = au_diropq_test(h_idmap, &h_path);
	inode_unlock_shared(h_inode);
	if (opq > 0)
		au_set_dbdiropq(dentry, bindex);
	else if (unlikely(opq < 0)) {
		au_set_h_dptr(dentry, bindex, NULL);
		h_dentry = ERR_PTR(opq);
	}
	goto out;

out_neg:
	dput(h_dentry);
	h_dentry = NULL;
out:
	return h_dentry;
}

/*
 * returns the number of lower positive dentries,
 * otherwise an error.
 * can be called at unlinking with @type is zero.
 */
int au_lkup_dentry(struct dentry *dentry, aufs_bindex_t btop,
		   unsigned int flags)
{
	int npositive, err;
	aufs_bindex_t bindex, btail, bdiropq;
	unsigned char isdir;
	struct au_do_lookup_args args = {
		.flags		= flags,
		.name		= &dentry->d_name
	};
	struct dentry *parent;
	struct super_block *sb;

	sb = dentry->d_sb;
	err = au_wh_name_alloc(&args.whname, args.name);
	if (unlikely(err))
		goto out;

	isdir = !!d_is_dir(dentry);

	npositive = 0;
	parent = dget_parent(dentry);
	btail = au_dbtaildir(parent);
	for (bindex = btop; bindex <= btail; bindex++) {
		struct dentry *h_parent, *h_dentry;
		struct inode *h_inode, *h_dir;

		h_dentry = au_h_dptr(dentry, bindex);
		if (h_dentry) {
			if (d_is_positive(h_dentry))
				npositive++;
			break;
		}
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent || !d_is_dir(h_parent))
			continue;

		h_dir = d_inode(h_parent);
		inode_lock_shared_nested(h_dir, AuLsc_I_PARENT);
		h_dentry = au_do_lookup(h_parent, dentry, bindex, &args);
		inode_unlock_shared(h_dir);
		err = PTR_ERR(h_dentry);
		if (IS_ERR(h_dentry))
			goto out_parent;
		if (h_dentry)
			au_fclr_lkup(args.flags, ALLOW_NEG);

		if (au_dbwh(dentry) == bindex)
			break;
		if (!h_dentry)
			continue;
		if (d_is_negative(h_dentry))
			continue;
		h_inode = d_inode(h_dentry);
		npositive++;
		if (!args.type)
			args.type = h_inode->i_mode & S_IFMT;
		if (args.type != S_IFDIR)
			break;
		else if (isdir) {
			/* the type of lower may be different */
			bdiropq = au_dbdiropq(dentry);
			if (bdiropq >= 0 && bdiropq <= bindex)
				break;
		}
	}

	if (npositive) {
		AuLabel(positive);
		au_update_dbtop(dentry);
	}
	err = npositive;
	if (unlikely(!au_opt_test(au_mntflags(sb), UDBA_NONE)
		     && au_dbtop(dentry) < 0)) {
		err = -EIO;
		AuIOErr("both of real entry and whiteout found, %pd, err %d\n",
			dentry, err);
	}

out_parent:
	dput(parent);
	au_kfree_try_rcu(args.whname.name);
out:
	return err;
}

struct dentry *au_sio_lkup_one(struct mnt_idmap *idmap, const struct qstr *name,
			       struct path *ppath)
{
	struct dentry *dentry;
	int wkq_err;

	if (!au_test_h_perm_sio(idmap, d_inode(ppath->dentry), MAY_EXEC))
		dentry = vfsub_lkup_one(name, ppath);
	else {
		struct vfsub_lkup_one_args args = {
			.errp	= &dentry,
			.name	= name,
			.ppath	= ppath
		};

		wkq_err = au_wkq_wait(vfsub_call_lkup_one, &args);
		if (unlikely(wkq_err))
			dentry = ERR_PTR(wkq_err);
	}

	return dentry;
}

/*
 * lookup @dentry on @bindex which should be negative.
 */
int au_lkup_neg(struct dentry *dentry, aufs_bindex_t bindex, int wh)
{
	int err;
	struct dentry *parent, *h_dentry;
	struct au_branch *br;
	struct mnt_idmap *h_idmap;
	struct path h_ppath;

	parent = dget_parent(dentry);
	br = au_sbr(dentry->d_sb, bindex);
	h_ppath.dentry = au_h_dptr(parent, bindex);
	h_ppath.mnt = au_br_mnt(br);
	h_idmap = au_br_idmap(br);
	if (wh)
		h_dentry = au_whtmp_lkup(h_ppath.dentry, br, &dentry->d_name);
	else
		h_dentry = au_sio_lkup_one(h_idmap, &dentry->d_name, &h_ppath);
	err = PTR_ERR(h_dentry);
	if (IS_ERR(h_dentry))
		goto out;
	if (unlikely(d_is_positive(h_dentry))) {
		err = -EIO;
		AuIOErr("%pd should be negative on b%d.\n", h_dentry, bindex);
		dput(h_dentry);
		goto out;
	}

	err = 0;
	if (bindex < au_dbtop(dentry))
		au_set_dbtop(dentry, bindex);
	if (au_dbbot(dentry) < bindex)
		au_set_dbbot(dentry, bindex);
	au_set_h_dptr(dentry, bindex, h_dentry);

out:
	dput(parent);
	return err;
}

/* ---------------------------------------------------------------------- */

/* subset of struct inode */
struct au_iattr {
	unsigned long		i_ino;
	/* unsigned int		i_nlink; */
	kuid_t			i_uid;
	kgid_t			i_gid;
	u64			i_version;
/*
	loff_t			i_size;
	blkcnt_t		i_blocks;
*/
	umode_t			i_mode;
};

static void au_iattr_save(struct au_iattr *ia, struct inode *h_inode)
{
	ia->i_ino = h_inode->i_ino;
	/* ia->i_nlink = h_inode->i_nlink; */
	ia->i_uid = h_inode->i_uid;
	ia->i_gid = h_inode->i_gid;
	ia->i_version = inode_query_iversion(h_inode);
/*
	ia->i_size = h_inode->i_size;
	ia->i_blocks = h_inode->i_blocks;
*/
	ia->i_mode = (h_inode->i_mode & S_IFMT);
}

static int au_iattr_test(struct au_iattr *ia, struct inode *h_inode)
{
	return ia->i_ino != h_inode->i_ino
		/* || ia->i_nlink != h_inode->i_nlink */
		|| !uid_eq(ia->i_uid, h_inode->i_uid)
		|| !gid_eq(ia->i_gid, h_inode->i_gid)
		|| !inode_eq_iversion(h_inode, ia->i_version)
/*
		|| ia->i_size != h_inode->i_size
		|| ia->i_blocks != h_inode->i_blocks
*/
		|| ia->i_mode != (h_inode->i_mode & S_IFMT);
}

static int au_h_verify_dentry(struct dentry *h_dentry, struct dentry *h_parent,
			      struct au_branch *br)
{
	int err;
	struct au_iattr ia;
	struct inode *h_inode;
	struct dentry *h_d;
	struct super_block *h_sb;
	struct path h_ppath;

	err = 0;
	memset(&ia, -1, sizeof(ia));
	h_sb = h_dentry->d_sb;
	h_inode = NULL;
	if (d_is_positive(h_dentry)) {
		h_inode = d_inode(h_dentry);
		au_iattr_save(&ia, h_inode);
	} else if (au_test_nfs(h_sb))
		/* nfs d_revalidate may return 0 for negative dentry */
		goto out;

	/* main purpose is namei.c:cached_lookup() and d_revalidate */
	h_ppath.dentry = h_parent;
	h_ppath.mnt = au_br_mnt(br);
	h_d = vfsub_lkup_one(&h_dentry->d_name, &h_ppath);
	err = PTR_ERR(h_d);
	if (IS_ERR(h_d))
		goto out;

	err = 0;
	if (unlikely(h_d != h_dentry
		     || d_inode(h_d) != h_inode
		     || (h_inode && au_iattr_test(&ia, h_inode))))
		err = -EBUSY;
	dput(h_d);

out:
	AuTraceErr(err);
	return err;
}

int au_h_verify(struct dentry *h_dentry, unsigned int udba, struct inode *h_dir,
		struct dentry *h_parent, struct au_branch *br)
{
	int err;

	err = 0;
	if (udba == AuOpt_UDBA_REVAL
	    && !au_test_fs_remote(h_dentry->d_sb)) {
		IMustLock(h_dir);
		err = (d_inode(h_dentry->d_parent) != h_dir);
	} else if (udba != AuOpt_UDBA_NONE)
		err = au_h_verify_dentry(h_dentry, h_parent, br);

	return err;
}
