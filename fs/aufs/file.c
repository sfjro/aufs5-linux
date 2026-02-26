// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * handling file/dir, and address_space operation
 */

#ifdef CONFIG_AUFS_DEBUG
#include <linux/migrate.h>
#endif
#include <linux/fsnotify.h>
#include <linux/pagemap.h>
#include "aufs.h"

/* drop flags for writing */
unsigned int au_file_roflags(unsigned int flags)
{
	flags &= ~(O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC);
	flags |= O_RDONLY | O_NOATIME;
	return flags;
}

/* common functions to regular file and dir */
struct file *au_h_open(struct dentry *dentry, aufs_bindex_t bindex, int flags,
		       struct file *file)
{
	struct file *h_file;
	struct dentry *h_dentry;
	struct inode *h_inode;
	struct super_block *sb;
	struct au_branch *br;
	struct path h_path;
	int err;

	/* a race condition can happen between open and unlink/rmdir */
	h_file = ERR_PTR(-ENOENT);
	h_dentry = au_h_dptr(dentry, bindex);
	if (!h_dentry || d_is_negative(h_dentry))
		goto out;
	h_inode = d_inode(h_dentry);
	spin_lock(&h_dentry->d_lock);
	err = (!d_unhashed(dentry) && d_unlinked(h_dentry))
		/* || !d_inode(dentry)->i_nlink */
		;
	spin_unlock(&h_dentry->d_lock);
	if (unlikely(err))
		goto out;

	sb = dentry->d_sb;
	br = au_sbr(sb, bindex);
	err = au_br_test_oflag(flags, br);
	h_file = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	/* drop flags for writing */
	if (au_test_ro(sb, bindex, d_inode(dentry)))
		flags = au_file_roflags(flags);
	flags &= ~O_CREAT;
	au_lcnt_inc(&br->br_nfiles);
	h_path.dentry = h_dentry;
	h_path.mnt = au_br_mnt(br);
	/*
	 * vfs::backing_file_open() looks promising since it can get rid of
	 * mm::vm_prfile approach from my mind.
	 * but I keep current approach for a while.
	 */
	h_file = vfsub_dentry_open(&h_path, flags);
	if (IS_ERR(h_file))
		goto out_br;

	fsnotify_open(h_file);
	goto out; /* success */

out_br:
	au_lcnt_dec(&br->br_nfiles);
out:
	return h_file;
}

/* ---------------------------------------------------------------------- */

/* cf. aufs_nopage() */
/* for madvise(2) */
static int aufs_read_folio(struct file *file __maybe_unused, struct folio *folio)
{
	folio_unlock(folio);
	return 0;
}

/* it will never be called, but necessary to support O_DIRECT */
static ssize_t aufs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{ BUG(); return 0; }

/* they will never be called. */
#ifdef CONFIG_AUFS_DEBUG
static int aufs_write_begin(const struct kiocb *iocb,
			    struct address_space *mapping, loff_t pos,
			    unsigned len, struct folio **foliop, void **fsdata)
{ AuUnsupport(); return 0; }
static int aufs_write_end(const struct kiocb *iocb,
			  struct address_space *mapping, loff_t pos,
			  unsigned len, unsigned copied, struct folio *folio,
			  void *fsdata)
{ AuUnsupport(); return 0; }

static bool aufs_dirty_folio(struct address_space *mapping, struct folio *folio)
{ AuUnsupport(); return true; }
static void aufs_invalidate_folio(struct folio *folio, size_t offset, size_t len)
{ AuUnsupport(); }
static bool aufs_release_folio(struct folio *folio, gfp_t gfp)
{ AuUnsupport(); return true; }
#if 0 /* called by memory compaction regardless file */
static int aufs_migrate_folio(struct address_space *mapping, struct folio *dst,
			      struct folio *src, enum migrate_mode mode)
{ AuUnsupport(); return 0; }
#endif
static int aufs_launder_folio(struct folio *folio)
{ AuUnsupport(); return 0; }
static bool aufs_is_partially_uptodate(struct folio *folio, size_t from,
				      size_t count)
{ AuUnsupport(); return true; }
static void aufs_is_dirty_writeback(struct folio *folio, bool *dirty,
				    bool *writeback)
{ AuUnsupport(); }
static int aufs_error_remove_folio(struct address_space *mapping,
				   struct folio *folio)
{ AuUnsupport(); return 0; }
static int aufs_swap_activate(struct swap_info_struct *sis, struct file *file,
			      sector_t *span)
{ AuUnsupport(); return 0; }
static void aufs_swap_deactivate(struct file *file)
{ AuUnsupport(); }
static int aufs_swap_rw(struct kiocb *iocb, struct iov_iter *iter)
{ AuUnsupport(); return 0; }
#endif /* CONFIG_AUFS_DEBUG */

const struct address_space_operations aufs_aop = {
	.read_folio		= aufs_read_folio,
	.direct_IO		= aufs_direct_IO,
#ifdef CONFIG_AUFS_DEBUG
	.dirty_folio		= aufs_dirty_folio,
	/* no readpages, because of readpage */
	.write_begin		= aufs_write_begin,
	.write_end		= aufs_write_end,
	/* no bmap, no block device */
	.invalidate_folio	= aufs_invalidate_folio,
	.release_folio		= aufs_release_folio,
	/* is fallback_migrate_page ok? */
	/* .migrate_folio	= aufs_migrate_folio, */
	.launder_folio		= aufs_launder_folio,
	.is_partially_uptodate	= aufs_is_partially_uptodate,
	.is_dirty_writeback	= aufs_is_dirty_writeback,
	.error_remove_folio	= aufs_error_remove_folio,
	.swap_activate		= aufs_swap_activate,
	.swap_deactivate	= aufs_swap_deactivate,
	.swap_rw		= aufs_swap_rw
#endif /* CONFIG_AUFS_DEBUG */
};
