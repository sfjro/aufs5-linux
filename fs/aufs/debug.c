// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2026 Junjiro R. Okajima
 */

/*
 * debug print functions
 */

#include <linux/iversion.h>
#include "aufs.h"

/* Returns 0, or -errno.  arg is in kp->arg. */
static int param_atomic_t_set(const char *val, const struct kernel_param *kp)
{
	int err, n;

	err = kstrtoint(val, 0, &n);
	if (!err) {
		if (n > 0)
			au_debug_on();
		else
			au_debug_off();
	}
	return err;
}

/* Returns length written or -errno.  Buffer is 4k (ie. be short!) */
static int param_atomic_t_get(char *buffer, const struct kernel_param *kp)
{
	atomic_t *a;

	a = kp->arg;
	return sprintf(buffer, "%d", atomic_read(a));
}

static const struct kernel_param_ops param_ops_atomic_t = {
	.set = param_atomic_t_set,
	.get = param_atomic_t_get
	/* void (*free)(void *arg) */
};

atomic_t aufs_debug = ATOMIC_INIT(0);
MODULE_PARM_DESC(debug, "debug print");
module_param_named(debug, aufs_debug, atomic_t, 0664);

DEFINE_MUTEX(au_dbg_mtx);	/* just to serialize the dbg msgs */
char *au_plevel = KERN_DEBUG;
#define dpri(fmt, ...) do {					\
	if ((au_plevel						\
	     && strcmp(au_plevel, KERN_DEBUG))			\
	    || au_debug_test())					\
		printk("%s" fmt, au_plevel, ##__VA_ARGS__);	\
} while (0)

/* ---------------------------------------------------------------------- */

static int do_pri_inode(aufs_bindex_t bindex, struct inode *inode)
{
	struct timespec64 ctime;

	if (!inode || IS_ERR(inode)) {
		dpri("i%d: err %ld\n", bindex, PTR_ERR(inode));
		return -1;
	}

	/* the type of i_blocks depends upon CONFIG_LBDAF */
	BUILD_BUG_ON(sizeof(inode->i_blocks) != sizeof(unsigned long)
		     && sizeof(inode->i_blocks) != sizeof(u64));

	ctime = inode_get_ctime(inode);
	dpri("i%d: %p, i%lu, %s, cnt %d, nl %u?, 0%o, sz %llu, blk %llu,"
	     " ct %lld, np %lu, st 0x%x, f 0x%x, v %llu, g %x\n",
	     bindex, inode,
	     inode->i_ino, inode->i_sb ? au_sbtype(inode->i_sb) : "??",
	     atomic_read(&inode->i_count), inode->i_nlink, inode->i_mode,
	     i_size_read(inode), (unsigned long long)inode->i_blocks,
	     (long long)timespec64_to_ns(&ctime) & 0x0ffff,
	     inode->i_mapping ? inode->i_mapping->nrpages : 0,
	     inode_state_read_once(inode), inode->i_flags,
	     inode_peek_iversion(inode), inode->i_generation);
	return 0;
}

void au_dpri_inode(struct inode *inode)
{
	struct au_iinfo *iinfo;
	aufs_bindex_t bindex;
	int err;

	err = do_pri_inode(-1, inode);
	if (err || !au_test_aufs(inode->i_sb) || au_is_bad_inode(inode))
		return;

	iinfo = au_ii(inode);
	dpri("i-1: btop %d, bbot %d\n",
	     iinfo->ii_btop, iinfo->ii_bbot);
	if (iinfo->ii_btop < 0)
		return;
	for (bindex = iinfo->ii_btop; bindex <= iinfo->ii_bbot; bindex++)
		do_pri_inode(bindex, iinfo->ii_hinode[0 + bindex].hi_inode);
}
