/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/dir.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

static int autofs_dir_readdir(struct inode *inode, struct file *filp,
			       void *dirent, filldir_t filldir)
{
	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	switch((unsigned long) filp->f_pos)
	{
	case 0:
		if (filldir(dirent, ".", 1, 0, inode->i_ino) < 0)
			return 0;
		filp->f_pos++;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, 1, AUTOFS_ROOT_INO) < 0)
			return 0;
		filp->f_pos++;
		/* fall through */
	}
	return 1;
}

/*
 * No entries except for "." and "..", both of which are handled by the VFS layer
 */
static int autofs_dir_lookup(struct inode *dir, struct dentry * dentry)
{
	d_add(dentry, NULL);
	return 0;
}

static struct file_operations autofs_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	autofs_dir_readdir,	/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL			/* fsync */
};

struct inode_operations autofs_dir_inode_operations = {
	&autofs_dir_operations,	/* file operations */
	NULL,			/* create */
	autofs_dir_lookup,	/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* read_page */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

