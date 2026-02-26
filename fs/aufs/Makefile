# SPDX-License-Identifier: GPL-2.0

include ${src}/magic.mk
-include ${src}/priv_def.mk

# For aufs as an outsider, it is still useful to specify CFLAGS from
# the command-line especially in its module building.
ccflags-y += ${EXTRA_CFLAGS}
# cf. include/linux/kernel.h
# enable pr_debug
ccflags-y += -DDEBUG
# sparse requires the full pathname
ccflags-y += -include ${srctree}/include/uapi/linux/aufs_type.h

obj-$(CONFIG_AUFS_FS) += aufs.o
aufs-y := module.o sbinfo.o super.o branch.o opts.o fsctx.o \
	vfsub.o dcsub.o \
	wkq.o vfsub.o dcsub.o \
	cpup.o \
	dinfo.o \
	iinfo.o inode.o
# add more later

# all are boolean
aufs-$(CONFIG_AUFS_DEBUG) += debug.o
# add more later
