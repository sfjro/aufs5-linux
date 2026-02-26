// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * inode operations (except add/del/rename)
 */

#include "aufs.h"

void au_pin_hdir_unlock(struct au_pin *p)
{
	if (p->hdir)
		inode_unlock(p->hdir->hi_inode);
}

int au_pin_hdir_lock(struct au_pin *p)
{
	int err;

	err = 0;
	if (!p->hdir)
		goto out;

	/* even if an error happens later, keep this lock */
	inode_lock_nested(p->hdir->hi_inode, p->lsc_hi);

	err = -EBUSY;
	if (unlikely(p->hdir->hi_inode != d_inode(p->h_parent)))
		goto out;

	err = 0;
	if (p->h_dentry)
		err = au_h_verify(p->h_dentry, p->hdir->hi_inode, p->h_parent,
				  p->br);

out:
	return err;
}

int au_pin_hdir_relock(struct au_pin *p)
{
	int err, i;
	struct inode *h_i;
	struct dentry *h_d[] = {
		p->h_dentry,
		p->h_parent
	};

	err = au_pin_hdir_lock(p);
	if (unlikely(err))
		goto out;

	for (i = 0; !err && i < sizeof(h_d)/sizeof(*h_d); i++) {
		if (!h_d[i])
			continue;
		if (d_is_positive(h_d[i])) {
			h_i = d_inode(h_d[i]);
			err = !vfsub_inode_nlink(h_i, AU_I_BRANCH);
		}
	}

out:
	return err;
}

static void au_pin_hdir_set_owner(struct au_pin *p, struct task_struct *task)
{
	AuRwMustWriteLock(&p->hdir->hi_inode->i_rwsem);
#ifndef CONFIG_PREEMPT_RT
	atomic_long_set(&p->hdir->hi_inode->i_rwsem.owner, (long)task);
#else
	p->hdir->hi_inode->i_rwsem.rwbase.rtmutex.owner = task;
	smp_mb(); /* advertise the owner */
#endif
}

void au_pin_hdir_acquire_nest(struct au_pin *p)
{
	if (p->hdir) {
		rwsem_acquire_nest(&p->hdir->hi_inode->i_rwsem.dep_map,
				   p->lsc_hi, 0, NULL, _RET_IP_);
		au_pin_hdir_set_owner(p, current);
	}
}

void au_pin_hdir_release(struct au_pin *p)
{
	if (p->hdir) {
		au_pin_hdir_set_owner(p, p->task);
		rwsem_release(&p->hdir->hi_inode->i_rwsem.dep_map, _RET_IP_);
	}
}

struct dentry *au_pinned_h_parent(struct au_pin *pin)
{
	if (pin && pin->parent)
		return au_h_dptr(pin->parent, pin->bindex);
	return NULL;
}

void au_unpin(struct au_pin *p)
{
	if (p->hdir)
		au_pin_hdir_unlock(p);
	if (p->h_mnt && au_ftest_pin(p->flags, MNT_WRITE))
		vfsub_mnt_drop_write(p->h_mnt);
	if (!p->hdir)
		return;

	if (!au_ftest_pin(p->flags, DI_LOCKED))
		di_read_unlock(p->parent, AuLock_IR);
	iput(p->hdir->hi_inode);
	dput(p->parent);
	p->parent = NULL;
	p->hdir = NULL;
	p->h_mnt = NULL;
	/* do not clear p->task */
}

int au_do_pin(struct au_pin *p)
{
	int err;
	struct super_block *sb;
	struct inode *h_dir;

	err = 0;
	sb = p->dentry->d_sb;
	p->br = au_sbr(sb, p->bindex);
	if (IS_ROOT(p->dentry)) {
		if (au_ftest_pin(p->flags, MNT_WRITE)) {
			p->h_mnt = au_br_mnt(p->br);
			err = vfsub_mnt_want_write(p->h_mnt);
			if (unlikely(err)) {
				au_fclr_pin(p->flags, MNT_WRITE);
				goto out_err;
			}
		}
		goto out;
	}

	p->h_dentry = NULL;
	if (p->bindex <= au_dbbot(p->dentry))
		p->h_dentry = au_h_dptr(p->dentry, p->bindex);

	p->parent = dget_parent(p->dentry);
	if (!au_ftest_pin(p->flags, DI_LOCKED))
		di_read_lock(p->parent, AuLock_IR, p->lsc_di);

	h_dir = NULL;
	p->h_parent = au_h_dptr(p->parent, p->bindex);
	p->hdir = au_hi(d_inode(p->parent), p->bindex);
	if (p->hdir)
		h_dir = p->hdir->hi_inode;

	/*
	 * udba case, or
	 * if DI_LOCKED is not set, then p->parent may be different
	 * and h_parent can be NULL.
	 */
	if (unlikely(!p->hdir || !h_dir || !p->h_parent)) {
		err = -EBUSY;
		if (!au_ftest_pin(p->flags, DI_LOCKED))
			di_read_unlock(p->parent, AuLock_IR);
		dput(p->parent);
		p->parent = NULL;
		goto out_err;
	}

	if (au_ftest_pin(p->flags, MNT_WRITE)) {
		p->h_mnt = au_br_mnt(p->br);
		err = vfsub_mnt_want_write(p->h_mnt);
		if (unlikely(err)) {
			au_fclr_pin(p->flags, MNT_WRITE);
			if (!au_ftest_pin(p->flags, DI_LOCKED))
				di_read_unlock(p->parent, AuLock_IR);
			dput(p->parent);
			p->parent = NULL;
			goto out_err;
		}
	}

	au_igrab(h_dir);
	err = au_pin_hdir_lock(p);
	if (!err)
		goto out; /* success */

	au_unpin(p);

out_err:
	pr_err("err %d\n", err);
	err = -EBUSY;
out:
	return err;
}

void au_pin_init(struct au_pin *p, struct dentry *dentry,
		 aufs_bindex_t bindex, int lsc_di, int lsc_hi,
		 unsigned char flags)
{
	p->dentry = dentry;
	p->lsc_di = lsc_di;
	p->lsc_hi = lsc_hi;
	p->flags = flags;
	p->bindex = bindex;

	p->parent = NULL;
	p->hdir = NULL;
	p->h_mnt = NULL;

	p->h_dentry = NULL;
	p->h_parent = NULL;
	p->br = NULL;
	p->task = current;
}

int au_pin(struct au_pin *pin, struct dentry *dentry, aufs_bindex_t bindex,
	   unsigned char flags)
{
	au_pin_init(pin, dentry, bindex, AuLsc_DI_PARENT, AuLsc_I_PARENT2,
		    flags);
	return au_do_pin(pin);
}
