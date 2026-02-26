// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * file and vm operations
 */

#include <linux/fs_stack.h>
#include <linux/mman.h>
#include "aufs.h"

int au_do_open_nondir(struct file *file, int flags)
{
	int err;
	aufs_bindex_t bindex;
	struct file *h_file;
	struct dentry *dentry;
	struct au_finfo *finfo;
	struct inode *h_inode;

	FiMustWriteLock(file);

	err = 0;
	dentry = file->f_path.dentry;
	finfo = au_fi(file);
	memset(&finfo->fi_htop, 0, sizeof(finfo->fi_htop));
	atomic_set(&finfo->fi_mmapped, 0);
	bindex = au_dbtop(dentry);
	h_file = au_h_open(dentry, bindex, flags, file);
	if (IS_ERR(h_file))
		err = PTR_ERR(h_file);
	else {
		if (flags & __O_TMPFILE) {
			AuDebugOn(!h_file);
			AuDebugOn(h_file != au_di(dentry)->di_htmpfile);
			au_di(dentry)->di_htmpfile = NULL;

			if (!(flags & O_EXCL)) {
				h_inode = file_inode(h_file);
				spin_lock(&h_inode->i_lock);
				inode_state_set(h_inode, I_LINKABLE);
				spin_unlock(&h_inode->i_lock);
			}
		}
		au_set_fbtop(file, bindex);
		au_set_h_fptr(file, bindex, h_file);
		au_update_figen(file);
		file->f_mode |= h_file->f_mode & FMODE_CAN_ODIRECT;
		/* todo: necessary? */
		/* file->f_ra = h_file->f_ra; */
	}

	return err;
}

static int aufs_open_nondir(struct inode *inode __maybe_unused,
			    struct file *file)
{
	int err;
	struct super_block *sb;
	struct au_do_open_args args = {
		.open	= au_do_open_nondir
	};

	AuDbg("%pD, f_flags 0x%x, f_mode 0x%x\n",
	      file, vfsub_file_flags(file), file->f_mode);

	sb = file->f_path.dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	err = au_do_open(file, &args);
	si_read_unlock(sb);
	return err;
}

int aufs_release_nondir(struct inode *inode __maybe_unused, struct file *file)
{
	struct au_finfo *finfo;
	aufs_bindex_t bindex;
	struct file *h_file;

	finfo = au_fi(file);
	bindex = finfo->fi_btop;
	if (bindex >= 0) {
		if (au_test_mmapped(file)) {
			/* h_file = au_hf_top(file); */
			h_file = finfo->fi_htop.hf_file;
			if (h_file)
				au_mf_del(h_file, file);
		}
		au_set_h_fptr(file, bindex, NULL);
	}

	au_finfo_fin(file);
	return 0;
}

/* ---------------------------------------------------------------------- */

struct au_write_pre {
	/* input */
	unsigned int lsc;

	/* output */
	blkcnt_t blks;
	aufs_bindex_t btop;
};

/*
 * return with iinfo is write-locked
 * callers should call au_write_post() or iinfo_write_unlock() + fput() in the
 * end
 */
static struct file *au_write_pre(struct file *file, int do_ready,
				 struct au_write_pre *wpre)
{
	struct file *h_file;
	struct dentry *dentry;
	int err;
	unsigned int lsc;
	struct au_pin pin;

	lsc = 0;
	if (wpre)
		lsc = wpre->lsc;
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1, lsc);
	h_file = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	dentry = file->f_path.dentry;
	if (do_ready) {
		err = au_ready_to_write(file, -1, &pin);
		if (unlikely(err)) {
			h_file = ERR_PTR(err);
			di_write_unlock(dentry);
			goto out_fi;
		}
	}

	di_downgrade_lock(dentry, /*flags*/0);
	if (wpre)
		wpre->btop = au_fbtop(file);
	h_file = au_hf_top(file);
	get_file(h_file);
	if (wpre)
		wpre->blks = file_inode(h_file)->i_blocks;
	if (do_ready)
		au_unpin(&pin);
	di_read_unlock(dentry, /*flags*/0);
#if 0 /* re-commit later */
	if (do_ready)
		vfsub_file_start_write(h_file);
#endif

out_fi:
	fi_write_unlock(file);
out:
	return h_file;
}

/*
 * The locking order around current->mmap_sem.
 * - in most and regular cases
 *   file I/O syscall -- aufs_read() or something
 *	-- si_rwsem for read -- mmap_sem
 *	(Note that [fdi]i_rwsem are released before mmap_sem).
 * - in mmap case
 *   mmap(2) -- mmap_sem -- aufs_mmap() -- si_rwsem for read -- [fdi]i_rwsem
 * This AB-BA order is definitely bad, but is not a problem since "si_rwsem for
 * read" allows multiple processes to acquire it and [fdi]i_rwsem are not held
 * in file I/O. Aufs needs to stop lockdep in aufs_mmap() though.
 * It means that when aufs acquires si_rwsem for write, the process should never
 * acquire mmap_sem.
 *
 * Actually aufs_iterate() holds [fdi]i_rwsem before mmap_sem, but this is not a
 * problem either since any directory is not able to be mmap-ed.
 * The similar scenario is applied to aufs_readlink() too.
 */

#if 0 /* stop calling security_file_mmap() */
/* cf. linux/include/linux/mman.h: calc_vm_prot_bits() */
#define AuConv_VM_PROT(f, b)	_calc_vm_trans(f, VM_##b, PROT_##b)

static unsigned long au_arch_prot_conv(unsigned long flags)
{
	/* currently ppc64 only */
#ifdef CONFIG_PPC64
	/* cf. linux/arch/powerpc/include/asm/mman.h */
	AuDebugOn(arch_calc_vm_prot_bits(-1) != VM_SAO);
	return AuConv_VM_PROT(flags, SAO);
#else
	AuDebugOn(arch_calc_vm_prot_bits(-1));
	return 0;
#endif
}

static unsigned long au_prot_conv(unsigned long flags)
{
	return AuConv_VM_PROT(flags, READ)
		| AuConv_VM_PROT(flags, WRITE)
		| AuConv_VM_PROT(flags, EXEC)
		| au_arch_prot_conv(flags);
}

/* cf. linux/include/linux/mman.h: calc_vm_flag_bits() */
#define AuConv_VM_MAP(f, b)	_calc_vm_trans(f, VM_##b, MAP_##b)

static unsigned long au_flag_conv(unsigned long flags)
{
	return AuConv_VM_MAP(flags, GROWSDOWN)
		| AuConv_VM_MAP(flags, DENYWRITE)
		| AuConv_VM_MAP(flags, LOCKED);
}
#endif

static int aufs_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err;
	const unsigned char wlock
		= (file->f_mode & FMODE_WRITE) && (vma->vm_flags & VM_SHARED);
	struct super_block *sb;
	struct file *h_file;
	struct inode *inode;

	AuDbgVmRegion(file, vma);

	inode = file_inode(file);
	sb = inode->i_sb;
	lockdep_off();
	si_read_lock(sb, AuLock_NOPLMW);

	h_file = au_write_pre(file, /*do_ready*/wlock, /*wpre*/NULL);
	lockdep_on();
	err = PTR_ERR(h_file);
	if (IS_ERR(h_file))
		goto out;

	err = 0;
	au_set_mmapped(file);
	au_vm_file_reset(vma, h_file);
	/*
	 * we cannot call security_mmap_file() here since it may acquire
	 * mmap_sem or i_mutex.
	 *
	 * err = security_mmap_file(h_file, au_prot_conv(vma->vm_flags),
	 *			 au_flag_conv(vma->vm_flags));
	 */
	if (!err)
		err = vfs_mmap(h_file, vma);
	if (!err) {
		au_mf_add(h_file, file);
		fsstack_copy_attr_atime(inode, file_inode(h_file));
		goto out_fput; /* success */
	}
	au_unset_mmapped(file);
	au_vm_file_reset(vma, file);

out_fput:
	lockdep_off();
	ii_write_unlock(inode);
	lockdep_on();
	fput(h_file);
out:
	lockdep_off();
	si_read_unlock(sb);
	lockdep_on();
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

const struct file_operations aufs_file_fop = {
	.owner		= THIS_MODULE,

	.mmap		= aufs_mmap,
	.open		= aufs_open_nondir,
	.release	= aufs_release_nondir
};
