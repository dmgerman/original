/* $Id: openpromfs.c,v 1.15 1997/06/05 01:28:11 davem Exp $
 * openpromfs.c: /proc/openprom handling routines
 *
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>

#define ALIASES_NNODES 64

typedef struct {
	u16	parent;
	u16	next;
	u16	child;
	u16	first_prop;
	u32	node;
} openpromfs_node;

typedef struct {
#define OPP_STRING	0x10
#define OPP_BINARY	0x20
#define OPP_DIRTY	0x01
#define OPP_QUOTED	0x02
#define OPP_NOTQUOTED	0x04
#define OPP_ASCIIZ	0x08
	u32	flag;
	u32	alloclen;
	u32	len;
	char	*value;
	char	name[8];
} openprom_property;

static openpromfs_node *nodes = NULL;
static int alloced = 0;
static u16 last_node = 0;
static u16 first_prop = 0;
static u16 options = 0xffff;
static u16 aliases = 0xffff;
static int aliases_nodes = 0;
static char *alias_names [ALIASES_NNODES];
static struct inode_operations *proc_openprom_iops = 0;
static struct openpromfs_dev **devices;

#define NODE(ino) nodes[ino - PROC_OPENPROM_FIRST]
#define NODE2INO(node) (node + PROC_OPENPROM_FIRST)
#define NODEP2INO(no) (no + PROC_OPENPROM_FIRST + last_node)

static int openpromfs_create (struct inode *, const char *, int, int,
			      struct inode **);
static int openpromfs_readdir(struct inode *, struct file *, void *, filldir_t);
static int openpromfs_lookup(struct inode *, const char *, int,
			     struct inode **);
static int openpromfs_unlink (struct inode *, const char *, int);

static long nodenum_read(struct inode *inode, struct file *file,
			 char *buf, unsigned long count)
{
	char buffer[10];
	
	if (count < 0 || !inode->u.generic_ip)
		return -EINVAL;
	sprintf (buffer, "%8.8x\n", (u32)(inode->u.generic_ip));
	if (file->f_pos >= 9)
		return 0;
	if (count > 9 - file->f_pos)
		count = 9 - file->f_pos;
	copy_to_user(buf, buffer + file->f_pos, count);
	file->f_pos += count;
	return count;
}

static long property_read(struct inode *inode, struct file *filp,
			  char *buf, unsigned long count)
{
	int i, j, k;
	u32 node;
	char *p;
	u32 *q;
	openprom_property *op;
	
	if (filp->f_pos >= 0xffffff)
		return -EINVAL;
	if (!filp->private_data) {
		node = nodes[(u16)((uint)inode->u.generic_ip)].node;
		i = ((u32)inode->u.generic_ip) >> 16;
		if ((u16)((uint)inode->u.generic_ip) == aliases) {
			if (i >= aliases_nodes)
				p = 0;
			else
				p = alias_names [i];
		} else
			for (p = prom_firstprop (node);
			     i && p && *p;
			     p = prom_nextprop (node, p), i--)
				/* nothing */ ;
		if (!p || !*p)
			return -EIO;
		i = prom_getproplen (node, p);
		if (i < 0) {
			if ((u16)((uint)inode->u.generic_ip) == aliases)
				i = 0;
			else
				return -EIO;
		}
		k = i;
		if (i < 64) i = 64;
		filp->private_data = kmalloc (sizeof (openprom_property)
					      + (j = strlen (p)) + 2 * i,
					      GFP_KERNEL);
		if (!filp->private_data)
			return -ENOMEM;
		op = (openprom_property *)filp->private_data;
		op->flag = 0;
		op->alloclen = 2 * i;
		strcpy (op->name, p);
		op->value = (char *)(((unsigned long)(op->name + j + 4)) & ~3);
		op->len = k;
		if (k && prom_getproperty (node, p, op->value, i) < 0)
			return -EIO;
		op->value [k] = 0;
		if (k) {
			for (p = op->value; *p >= ' ' && *p <= '~'; p++);
			if (p >= op->value + k - 1 && !*p) {
				op->flag |= OPP_STRING;
				if (p == op->value + k - 1) {
					op->flag |= OPP_ASCIIZ;
					op->len--;
				}
			} else if (!(k & 3))
				op->flag |= OPP_BINARY;
			else {
				printk ("/proc/openprom: Strange property "
					"size %d\n", i);
				return -EIO;
			}
		}
	} else
		op = (openprom_property *)filp->private_data;
	if (!count || !op->len) return 0;
	if (op->flag & OPP_STRING)
		i = op->len + 3;
	else
		i = (op->len * 9)>>2;
	k = filp->f_pos;
	if (k >= i) return 0;
	if (count > i - k) count = i - k;
	if (op->flag & OPP_STRING) {
		if (!k) {
			*buf = '\'';
			k++;
			count--;
		}
		if (k + count >= i - 2)
			j = i - 2 - k;
		else
			j = count;
		if (j >= 0) {
			copy_to_user(buf + k - filp->f_pos,
				     op->value + k - 1, j);
			count -= j;
			k += j;
		}
		if (count)
			buf [k++ - filp->f_pos] = '\'';
		if (count > 1)
			buf [k++ - filp->f_pos] = '\n';
	} else if (op->flag & OPP_BINARY) {
		char buffer[10];
		u32 *first, *last;
		int first_off, last_cnt;

		first = ((u32 *)op->value) + k / 9;
		first_off = k % 9;
		last = ((u32 *)op->value) + (k + count - 1) / 9;
		last_cnt = (k + count) % 9;
		if (!last_cnt) last_cnt = 9;

		if (first == last) {
			sprintf (buffer, "%08x.", *first);
			memcpy (buf, buffer + first_off, last_cnt - first_off);
			buf += last_cnt - first_off;
		} else {		
			for (q = first; q <= last; q++) {
				sprintf (buffer, "%08x.", *q);
				if (q == first) {
					memcpy (buf, buffer + first_off,
						9 - first_off);
					buf += 9 - first_off;
				} else if (q == last) {
					memcpy (buf, buffer, last_cnt);
					buf += last_cnt;
				} else {
					memcpy (buf, buffer, 9);
					buf += 9;
				}
			}
		}
		if (last == (u32 *)(op->value + op->len - 4) && last_cnt == 9)
			*(buf - 1) = '\n';
		k += count;
	}
	count = k - filp->f_pos;
	filp->f_pos = k;
	return count;
}

static long property_write(struct inode *inode, struct file *filp,
			   const char *buf, unsigned long count)
{
	int i, j, k;
	char *p;
	u32 *q;
	void *b;
	openprom_property *op;
	
	if (filp->f_pos >= 0xffffff)
		return -EINVAL;
	if (!filp->private_data) {
		i = property_read (inode, filp, NULL, 0);
		if (i)
			return i;
	}
	k = filp->f_pos;
	op = (openprom_property *)filp->private_data;
	if (!(op->flag & OPP_STRING)) {
		u32 *first, *last;
		int first_off, last_cnt;
		u32 mask, mask2;
		char tmp [9];
		int forcelen = 0;
		
		j = k % 9;
		for (i = 0; i < count; i++, j++) {
			if (j == 9) j = 0;
			if (!j) {
				if (buf [i] != '.') {
					if (buf [i] != '\n') {
						if (op->flag & OPP_BINARY)
							return -EINVAL;
						else
							goto write_try_string;
					} else {
						count = i + 1;
						forcelen = 1;
						break;
					}
				}
			} else {
				if (buf [i] < '0' || 
				    (buf [i] > '9' && buf [i] < 'A') ||
				    (buf [i] > 'F' && buf [i] < 'a') ||
				    buf [i] > 'f') {
					if (op->flag & OPP_BINARY)
						return -EINVAL;
					else
						goto write_try_string;
				}
			}
		}
		op->flag |= OPP_BINARY;
		tmp [8] = 0;
		i = ((count + k + 8) / 9) << 2;
		if (op->alloclen <= i) {
			b = kmalloc (sizeof (openprom_property) + 2 * i,
				     GFP_KERNEL);
			if (!b)
				return -ENOMEM;
			memcpy (b, filp->private_data,
				sizeof (openprom_property)
				+ strlen (op->name) + op->alloclen);
			memset (((char *)b) + sizeof (openprom_property)
				+ strlen (op->name) + op->alloclen, 
				0, 2 * i - op->alloclen);
			op = (openprom_property *)b;
			op->alloclen = 2*i;
			b = filp->private_data;
			filp->private_data = (void *)op;
			kfree (b);
		}
		first = ((u32 *)op->value) + (k / 9);
		first_off = k % 9;
		last = (u32 *)(op->value + i);
		last_cnt = (k + count) % 9;
		if (first + 1 == last) {
			memset (tmp, '0', 8);
			memcpy (tmp + first_off, buf, (count + first_off > 8) ?
						      8 - first_off : count);
			mask = 0xffffffff;
			mask2 = 0xffffffff;
			for (j = 0; j < first_off; j++)
				mask >>= 1;
			for (j = 8 - count - first_off; j > 0; j--)
				mask2 <<= 1;
			mask &= mask2;
			if (mask) {
				*first &= ~mask;
				*first |= simple_strtoul (tmp, 0, 16);
				op->flag |= OPP_DIRTY;
			}
		} else {
			op->flag |= OPP_DIRTY;
			for (q = first; q < last; q++) {
				if (q == first) {
					if (first_off < 8) {
						memset (tmp, '0', 8);
						memcpy (tmp + first_off, buf,
							8 - first_off);
						mask = 0xffffffff;
						for (j = 0; j < first_off; j++)
							mask >>= 1;
						*q &= ~mask;
						*q |= simple_strtoul (tmp,0,16);
					}
					buf += 9;
				} else if ((q == last - 1) && last_cnt
					   && (last_cnt < 8)) {
					memset (tmp, '0', 8);
					memcpy (tmp, buf, last_cnt);
					mask = 0xffffffff;
					for (j = 0; j < 8 - last_cnt; j++)
						mask <<= 1;
					*q &= ~mask;
					*q |= simple_strtoul (tmp, 0, 16);
					buf += last_cnt;
				} else {
					*q = simple_strtoul (buf, 0, 16);
					buf += 9;
				}
			}
		}
		if (!forcelen) {
			if (op->len < i)
				op->len = i;
		} else
			op->len = i;
		filp->f_pos += count;
	}
write_try_string:
	if (!(op->flag & OPP_BINARY)) {
		if (!(op->flag & (OPP_QUOTED | OPP_NOTQUOTED))) {
			/* No way, if somebody starts writing from the middle, 
			 * we don't know whether he uses quotes around or not 
			 */
			if (k > 0)
				return -EINVAL;
			if (*buf == '\'') {
				op->flag |= OPP_QUOTED;
				buf++;
				count--;
				filp->f_pos++;
				if (!count) {
					op->flag |= OPP_STRING;
					return 1;
				}
			} else
				op->flag |= OPP_NOTQUOTED;
		}
		op->flag |= OPP_STRING;
		if (op->alloclen <= count + filp->f_pos) {
			b = kmalloc (sizeof (openprom_property)
				     + 2 * (count + filp->f_pos), GFP_KERNEL);
			if (!b)
				return -ENOMEM;
			memcpy (b, filp->private_data,
				sizeof (openprom_property)
				+ strlen (op->name) + op->alloclen);
			memset (((char *)b) + sizeof (openprom_property)
				+ strlen (op->name) + op->alloclen, 
				0, 2*(count - filp->f_pos) - op->alloclen);
			op = (openprom_property *)b;
			op->alloclen = 2*(count + filp->f_pos);
			b = filp->private_data;
			filp->private_data = (void *)op;
			kfree (b);
		}
		p = op->value + filp->f_pos - ((op->flag & OPP_QUOTED) ? 1 : 0);
		memcpy (p, buf, count);
		op->flag |= OPP_DIRTY;
		for (i = 0; i < count; i++, p++)
			if (*p == '\n') {
				*p = 0;
				break;
			}
		if (i < count) {
			op->len = p - op->value;
			filp->f_pos += i + 1;
			if ((p > op->value) && (op->flag & OPP_QUOTED)
			    && (*(p - 1) == '\''))
				op->len--;
		} else {
			if (p - op->value > op->len)
				op->len = p - op->value;
			filp->f_pos += count;
		}
	}
	return filp->f_pos - k;
}

int property_release (struct inode *inode, struct file *filp)
{
	openprom_property *op = (openprom_property *)filp->private_data;
	unsigned long flags;
	int error;
	u32 node;
	
	if (!op)
		return 0;
	node = nodes[(u16)((uint)inode->u.generic_ip)].node;
	if ((u16)((uint)inode->u.generic_ip) == aliases) {
		if ((op->flag & OPP_DIRTY) && (op->flag & OPP_STRING)) {
			char *p = op->name;
			int i = (op->value - op->name) - strlen (op->name) - 1;
			op->value [op->len] = 0;
			*(op->value - 1) = ' ';
			if (i) {
				for (p = op->value - i - 2; p >= op->name; p--)
					p[i] = *p;
				p = op->name + i;
			}
			memcpy (p - 8, "nvalias ", 8);
			prom_feval (p - 8);
		}
	} else if (op->flag & OPP_DIRTY) {
		if (op->flag & OPP_STRING) {
			op->value [op->len] = 0;
			save_and_cli (flags);
			error = prom_setprop (node, op->name,
					      op->value, op->len + 1);
			restore_flags (flags);
			if (error <= 0)
				printk (KERN_WARNING "/proc/openprom: "
					"Couldn't write property %s\n",
					op->name);
		} else if ((op->flag & OPP_BINARY) || !op->len) {
			save_and_cli (flags);
			error = prom_setprop (node, op->name,
					      op->value, op->len);
			restore_flags (flags);
			if (error <= 0)
				printk (KERN_WARNING "/proc/openprom: "
					"Couldn't write property %s\n",
					op->name);
		} else {
			printk (KERN_WARNING "/proc/openprom: "
				"Unknown property type of %s\n",
				op->name);
		}
	}
	kfree (filp->private_data);
	return 0;
}

static struct file_operations openpromfs_prop_ops = {
	NULL,			/* lseek - default */
	property_read,		/* read */
	property_write,		/* write - bad */
	NULL,			/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	property_release,	/* no special release code */
	NULL			/* can't fsync */
};

static struct inode_operations openpromfs_prop_inode_ops = {
	&openpromfs_prop_ops,	/* default property file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct file_operations openpromfs_nodenum_ops = {
	NULL,			/* lseek - default */
	nodenum_read,		/* read */
	NULL,			/* write - bad */
	NULL,			/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

static struct inode_operations openpromfs_nodenum_inode_ops = {
	&openpromfs_nodenum_ops,/* default .node file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct file_operations openprom_alias_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	openpromfs_readdir,	/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

static struct inode_operations openprom_alias_inode_operations = {
	&openprom_alias_operations,/* default aliases directory file-ops */
	openpromfs_create,	/* create */
	openpromfs_lookup,	/* lookup */
	NULL,			/* link */
	openpromfs_unlink,	/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int lookup_children(u16 n, const char * name, int len)
{
	int ret;
	u16 node;
	for (; n != 0xffff; n = nodes[n].next) {
		node = nodes[n].child;
		if (node != 0xffff) {
			char buffer[128];
			int i;
			char *p;
			
			while (node != 0xffff) {
				if (prom_getname (nodes[node].node,
						  buffer, 128) >= 0) {
					i = strlen (buffer);
					if ((len == i)
					    && !strncmp (buffer, name, len))
						return NODE2INO(node);
					p = strchr (buffer, '@');
					if (p && (len == p - buffer)
					    && !strncmp (buffer, name, len))
						return NODE2INO(node);
				}
				node = nodes[node].next;
			}
		} else
			continue;
		ret = lookup_children (nodes[n].child, name, len);
		if (ret) return ret;
	}
	return 0;
}

static int openpromfs_lookup(struct inode * dir, const char * name, int len,
	struct inode ** result)
{
	int ino = 0;
#define OPFSL_DIR	0
#define OPFSL_PROPERTY	1
#define OPFSL_NODENUM	2
#define OPFSL_DEVICE	3
	int type = 0;
	char buffer[128];
	char *p;
	u32 n;
	u16 dirnode;
	int i;
	struct inode *inode;
	struct openpromfs_dev *d = NULL;
	
	*result = NULL;
	if (!dir || !S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOTDIR;
	}
	*result = dir;
	if (!len) return 0;
	if (name [0] == '.') {
		if (len == 1)
			return 0;
		if (name [1] == '.' && len == 2) {
			if (dir->i_ino == PROC_OPENPROM) {
				inode = proc_get_inode (dir->i_sb,
							PROC_ROOT_INO,
							&proc_root);
				iput(dir);
				if (!inode)
					return -EINVAL;
				*result = inode;
				return 0;
			}
			ino = NODE2INO(NODE(dir->i_ino).parent);
			type = OPFSL_DIR;
		} else if (len == 5 && !strncmp (name + 1, "node", 4)) {
			ino = NODEP2INO(NODE(dir->i_ino).first_prop);
			type = OPFSL_NODENUM;
		}
	}
	if (!ino) {
		u16 node = NODE(dir->i_ino).child;
		while (node != 0xffff) {
			if (prom_getname (nodes[node].node, buffer, 128) >= 0) {
				i = strlen (buffer);
				if (len == i && !strncmp (buffer, name, len)) {
					ino = NODE2INO(node);
					type = OPFSL_DIR;
					break;
				}
				p = strchr (buffer, '@');
				if (p && (len == p - buffer)
				    && !strncmp (buffer, name, len)) {
					ino = NODE2INO(node);
					type = OPFSL_DIR;
					break;
				}
			}
			node = nodes[node].next;
		}
	}
	n = NODE(dir->i_ino).node;
	dirnode = dir->i_ino - PROC_OPENPROM_FIRST;
	if (!ino) {
		int j = NODEP2INO(NODE(dir->i_ino).first_prop);
		if (dirnode != aliases) {
			for (p = prom_firstprop (n);
			     p && *p;
			     p = prom_nextprop (n, p)) {
				j++;
				if ((len == strlen (p))
				    && !strncmp (p, name, len)) {
					ino = j;
					type = OPFSL_PROPERTY;
					break;
				}
			}
		} else {
			int k;
			for (k = 0; k < aliases_nodes; k++) {
				j++;
				if (alias_names [k]
				    && (len == strlen (alias_names [k]))
				    && !strncmp (alias_names [k], name, len)) {
					ino = j;
					type = OPFSL_PROPERTY;
					break;
				}
			}
		}
	}
	if (!ino) {
		for (d = *devices; d; d = d->next)
			if ((d->node == n) && (strlen (d->name) == len)
			    && !strncmp (d->name, name, len)) {
				ino = d->inode;
				type = OPFSL_DEVICE;
				break;
			}
	}
	if (!ino) {
		ino = lookup_children (NODE(dir->i_ino).child, name, len);
		if (ino)
			type = OPFSL_DIR;
		else {
			iput(dir);
			return -ENOENT;
		}
	}
	inode = proc_get_inode (dir->i_sb, ino, 0);
	iput(dir);
	if (!inode)
		return -EINVAL;
	switch (type) {
	case OPFSL_DIR:
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
		if (ino == PROC_OPENPROM_FIRST + aliases) {
			inode->i_mode |= S_IWUSR;
			inode->i_op = &openprom_alias_inode_operations;
		} else
			inode->i_op = proc_openprom_iops;
		inode->i_nlink = 2;
		break;
	case OPFSL_NODENUM:
		inode->i_mode = S_IFREG | S_IRUGO;
		inode->i_op = &openpromfs_nodenum_inode_ops;
		inode->i_nlink = 1;
		inode->u.generic_ip = (void *)(n);
		break;
	case OPFSL_PROPERTY:
		if ((dirnode == options) && (len == 17)
		    && !strncmp (name, "security-password", 17))
			inode->i_mode = S_IFREG | S_IRUSR | S_IWUSR;
		else {
			inode->i_mode = S_IFREG | S_IRUGO;
			if (dirnode == options || dirnode == aliases) {
				if (len != 4 || strncmp (name, "name", 4))
					inode->i_mode |= S_IWUSR;
			}
		}
		inode->i_op = &openpromfs_prop_inode_ops;
		inode->i_nlink = 1;
		if (inode->i_size < 0)
			inode->i_size = 0;
		inode->u.generic_ip = (void *)(((u16)dirnode) | 
			(((u16)(ino - NODEP2INO(NODE(dir->i_ino).first_prop) - 1)) << 16));
		break;
	case OPFSL_DEVICE:
		inode->i_mode = d->mode;
		inode->i_op = &chrdev_inode_operations;
		inode->i_nlink = 1;
		inode->i_rdev = d->rdev;
		break;
	}		
	*result = inode;
	return 0;
}

static int openpromfs_readdir(struct inode * inode, struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int ino;
	u32 n;
	int i, j;
	char buffer[128];
	u16 node;
	char *p;
	struct openpromfs_dev *d;
	
	if (!inode || !S_ISDIR (inode->i_mode)) return -ENOTDIR;
	ino = inode->i_ino;
	i = filp->f_pos;
	switch (i) {
	case 0:
		if (filldir(dirent, ".", 1, i, ino) < 0) return 0;
		i++;
		filp->f_pos++;
		/* fall thru */
	case 1:
		if (filldir(dirent, "..", 2, i, 
			(NODE(ino).parent == 0xffff) ? 
			PROC_ROOT_INO : NODE2INO(NODE(ino).parent)) < 0) 
			return 0;
		i++;
		filp->f_pos++;
		/* fall thru */
	default:
		i -= 2;
		node = NODE(ino).child;
		while (i && node != 0xffff) {
			node = nodes[node].next;
			i--;
		}
		while (node != 0xffff) {
			if (prom_getname (nodes[node].node, buffer, 128) < 0)
				return 0;
			if (filldir(dirent, buffer, strlen(buffer),
				    filp->f_pos, NODE2INO(node)) < 0)
				return 0;
			filp->f_pos++;
			node = nodes[node].next;
		}
		j = NODEP2INO(NODE(ino).first_prop);
		if (!i) {
			if (filldir(dirent, ".node", 5, filp->f_pos, j) < 0)
				return 0;
			filp->f_pos++;
		} else
			i--;
		n = NODE(ino).node;
		if (ino == PROC_OPENPROM_FIRST + aliases) {
			for (j++; i < aliases_nodes; i++, j++) {
				if (alias_names [i]) {
					if (filldir (dirent, alias_names [i], 
						strlen (alias_names [i]), 
						filp->f_pos, j) < 0) return 0;
					filp->f_pos++;
				}
			}
		} else {
			for (p = prom_firstprop (n);
			     p && *p;
			     p = prom_nextprop (n, p)) {
				j++;
				if (i) i--;
				else {
					if (filldir(dirent, p, strlen(p),
						    filp->f_pos, j) < 0)
						return 0;
					filp->f_pos++;
				}
			}
		}
		for (d = *devices; d; d = d->next) {
			if (d->node == n) {
				if (i) i--;
				else {
					if (filldir(dirent, d->name,
						    strlen(d->name),
						    filp->f_pos, d->inode) < 0)
						return 0;
					filp->f_pos++;
				}
			}
		}
	}
	return 0;
}

static int openpromfs_create (struct inode *dir, const char *name, int len, 
			      int mode, struct inode **result)
{
	char *p;
	struct inode *inode;
	
	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (len > 256) {
		iput (dir);
		return -EINVAL;
	}
	if (aliases_nodes == ALIASES_NNODES) {
		iput (dir);
		return -EIO;
	}
	p = kmalloc (len + 1, GFP_KERNEL);
	if (!p) {
		iput (dir);
		return -ENOMEM;
	}
	strncpy (p, name, len);
	p [len] = 0;
	alias_names [aliases_nodes++] = p;
	inode = proc_get_inode (dir->i_sb,
				NODEP2INO(NODE(dir->i_ino).first_prop)
				+ aliases_nodes, 0);
	iput (dir);
	if (!inode)
		return -EINVAL;
	inode->i_mode = S_IFREG | S_IRUGO | S_IWUSR;
	inode->i_op = &openpromfs_prop_inode_ops;
	inode->i_nlink = 1;
	if (inode->i_size < 0) inode->i_size = 0;
	inode->u.generic_ip = (void *)(((u16)aliases) | 
			(((u16)(aliases_nodes - 1)) << 16));
	*result = inode;
	return 0;
}

static int openpromfs_unlink (struct inode *dir, const char *name, int len)
{
	char *p;
	int i;
	
	if (!dir)
		return -ENOENT;
	for (i = 0; i < aliases_nodes; i++)
		if ((strlen (alias_names [i]) == len)
		    && !strncmp (name, alias_names[i], len)) {
			char buffer[512];
			
			p = alias_names [i];
			alias_names [i] = NULL;
			kfree (p);
			strcpy (buffer, "nvunalias ");
			memcpy (buffer + 10, name, len);
			buffer [10 + len] = 0;
			prom_feval (buffer);
		}
	iput (dir);
	return 0;
}

/* {{{ init section */
#ifndef MODULE
__initfunc(static int check_space (u16 n))
#else
static int check_space (u16 n)
#endif
{
	unsigned long pages;

	if ((1 << alloced) * PAGE_SIZE < (n + 2) * sizeof(openpromfs_node)) {
		pages = __get_free_pages (GFP_KERNEL, alloced + 1, 0);
		if (!pages)
			return -1;

		if (nodes) {
			memcpy ((char *)pages, (char *)nodes,
				(1 << alloced) * PAGE_SIZE);
			free_pages ((unsigned long)nodes, alloced);
		}
		alloced++;
		nodes = (openpromfs_node *)pages;
	}
	return 0;
}

#ifndef MODULE
__initfunc(static u16 get_nodes (u16 parent, u32 node))
#else
static u16 get_nodes (u16 parent, u32 node)
#endif
{
	char *p;
	u16 n = last_node++, i;

	if (check_space (n) < 0)
		return 0xffff;
	nodes[n].parent = parent;
	nodes[n].node = node;
	nodes[n].next = 0xffff;
	nodes[n].child = 0xffff;
	nodes[n].first_prop = first_prop++;
	if (!parent) {
		char buffer[8];
		int j;
		
		if ((j = prom_getproperty (node, "name", buffer, 8)) >= 0) {
		    buffer[j] = 0;
		    if (!strcmp (buffer, "options"))
			options = n;
		    else if (!strcmp (buffer, "aliases"))
		        aliases = n;
		}
	}
	if (n != aliases)
		for (p = prom_firstprop (node);
		     p && p != (char *)-1 && *p;
		     p = prom_nextprop (node, p))
			first_prop++;
	else {
		char *q;
		for (p = prom_firstprop (node);
		     p && p != (char *)-1 && *p;
		     p = prom_nextprop (node, p)) {
			if (aliases_nodes == ALIASES_NNODES)
				break;
			for (i = 0; i < aliases_nodes; i++)
				if (!strcmp (p, alias_names [i]))
					break;
			if (i < aliases_nodes)
				continue;
			q = kmalloc (strlen (p) + 1, GFP_KERNEL);
			if (!q)
				return 0xffff;
			strcpy (q, p);
			alias_names [aliases_nodes++] = q;
		}
		first_prop += ALIASES_NNODES;
	}
	node = prom_getchild (node);
	if (node) {
		parent = get_nodes (n, node);
		if (parent == 0xffff)
			return 0xffff;
		nodes[n].child = parent;
		while ((node = prom_getsibling (node)) != 0) {
			i = get_nodes (n, node);
			if (i == 0xffff)
				return 0xffff;
			nodes[parent].next = i;
			parent = i;
		}
	}
	return n;
}


#ifdef MODULE
void openpromfs_use (struct inode *inode, int inc)
{
	static int root_fresh = 1;
	static int dec_first = 1;
#ifdef OPENPROM_DEBUGGING
	static int usec = 0;

	if (inc) {
		if (atomic_read(&inode->i_count) == 1)
			usec++;
		else if (root_fresh && inode->i_ino == PROC_OPENPROM_FIRST) {
			root_fresh = 0;
			usec++;
		}
	} else {
		if (inode->i_ino == PROC_OPENPROM_FIRST)
			root_fresh = 0;
		if (!dec_first)
			usec--;
	}
	printk ("openpromfs_use: %d %d %d %d\n",
		inode->i_ino, inc, usec, atomic_read(&inode->i_count));
#else
	if (inc) {
		if (atomic_read(&inode->i_count) == 1)
			MOD_INC_USE_COUNT;
		else if (root_fresh && inode->i_ino == PROC_OPENPROM_FIRST) {
			root_fresh = 0;
			MOD_INC_USE_COUNT;
		}
	} else {
		if (inode->i_ino == PROC_OPENPROM_FIRST)
			root_fresh = 0;
		if (!dec_first)
			MOD_DEC_USE_COUNT;
	}
#endif	
	dec_first = 0;
}

#else
#define openpromfs_use 0
#endif

#ifndef MODULE
#define RET(x)
__initfunc(void openpromfs_init (void))
#else

EXPORT_NO_SYMBOLS;

#define RET(x) -x
int init_module (void)
#endif
{
	if (!romvec->pv_romvers)
		return RET(ENODEV);
	nodes = (openpromfs_node *)__get_free_pages(GFP_KERNEL, 0, 0);
	if (!nodes) {
		printk (KERN_WARNING "/proc/openprom: can't get free page\n");
		return RET(EIO);
	}
	if (get_nodes (0xffff, prom_root_node) == 0xffff) {
		printk (KERN_WARNING "/proc/openprom: couldn't setup tree\n");
		return RET(EIO);
	}
	nodes[last_node].first_prop = first_prop;
	proc_openprom_iops = proc_openprom_register (openpromfs_readdir,
				                     openpromfs_lookup,
						     openpromfs_use,
						     &devices);
	return RET(0);
}

#ifdef MODULE
void cleanup_module (void)
{
	int i;
	proc_openprom_deregister ();
	free_pages ((unsigned long)nodes, alloced);
	for (i = 0; i < aliases_nodes; i++)
		if (alias_names [i])
			kfree (alias_names [i]);
	nodes = NULL;
}
#endif