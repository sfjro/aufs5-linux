// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * file private data
 */

#include "aufs.h"

void au_hfput(struct au_hfile *hf, int execed)
{
	fput(hf->hf_file);
	hf->hf_file = NULL;
	au_lcnt_dec(&hf->hf_br->br_nfiles);
	hf->hf_br = NULL;
}

void au_set_h_fptr(struct file *file, aufs_bindex_t bindex, struct file *val)
{
	struct au_finfo *finfo = au_fi(file);
	struct au_hfile *hf;

	AuDebugOn(finfo->fi_btop != bindex);
	hf = &finfo->fi_htop;

	if (hf && hf->hf_file)
		au_hfput(hf, vfsub_file_execed(file));
	if (val) {
		FiMustWriteLock(file);
		AuDebugOn(IS_ERR_OR_NULL(file->f_path.dentry));
		hf->hf_file = val;
		hf->hf_br = au_sbr(file->f_path.dentry->d_sb, bindex);
	}
}

void au_update_figen(struct file *file)
{
	atomic_set(&au_fi(file)->fi_generation, au_digen(file->f_path.dentry));
	/* smp_mb(); */ /* atomic_set */
}

/* ---------------------------------------------------------------------- */

void au_finfo_fin(struct file *file)
{
	struct au_finfo *finfo;

	finfo = au_fi(file);
	AuRwDestroy(&finfo->fi_rwsem);
	au_cache_free_finfo(finfo);
}

void au_fi_init_once(void *_finfo)
{
	struct au_finfo *finfo = _finfo;

	au_rw_init(&finfo->fi_rwsem);
}

int au_finfo_init(struct file *file)
{
	int err;
	struct au_finfo *finfo;
	struct dentry *dentry;

	err = -ENOMEM;
	dentry = file->f_path.dentry;
	finfo = au_cache_alloc_finfo();
	if (unlikely(!finfo))
		goto out;

	err = 0;
	au_rw_write_lock(&finfo->fi_rwsem);
	finfo->fi_btop = -1;
	atomic_set(&finfo->fi_generation, au_digen(dentry));
	/* smp_mb(); */ /* atomic_set */

	file->private_data = finfo;

out:
	return err;
}
