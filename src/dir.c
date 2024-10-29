#include <linux/pagemap.h>
#include <linux/buffer_head.h>

#include "ext0.h"

static inline void ext0_put_page(struct page *page)
{
	kunmap(page);
	put_page(page);
}

static struct page *ext0_get_page(struct inode *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page))
		kmap(page);
	return page;
}

static int ext0_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode, struct inode **ret_inode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct ext0_super_block *on_disk_sb = EXT0_SB(sb)->s_es;
	struct ext0_inode_info *in_mem_inode;
	unsigned long ino;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;

	ino = EXT0_GET_INO(EXT0_ROOT_INO); /* Start past root inode */

retry:
	ino = ext0_find_next_zero_bit(&on_disk_sb->s_inode_bitmap, EXT0_INODE_BITMAP_SIZE, ino);

	if (ino >= le32_to_cpu(on_disk_sb->s_groups_count))
		return -ENOSPC;

	/* Protect root dir */
	if (ino == EXT0_GET_INO(EXT0_ROOT_INO))
	{
		ino++;
		goto retry;
	}

	ext0_test_and_set_bit(ino, (void *)on_disk_sb->s_inode_bitmap);

	inode->i_mode = mode;
	inode->i_ino = EXT0_MAKE_INO(ino);
	inode->i_sb = sb;
	inode->i_blocks = EXT0_FS_MAX_DIRECT_BLOCKS;
	inode->i_flags = 0;
	inode->i_state = EXT0_STATE_NEW | I_LINKABLE | I_NEW; /* The fs crashes without the I_NEW flag. Need to investigate */
	inode->i_size = sizeof(struct ext0_inode);

	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);

	if (S_ISREG(inode->i_mode))
	{
		inode->i_op = &ext0_file_inode_operations;
		inode->i_fop = &ext0_file_operations;
	}
	else if (S_ISDIR(inode->i_mode))
	{
		inode->i_op = &ext0_dir_inode_operations;
		inode->i_fop = &ext0_dir_operations;
	}

	if (inode->i_mapping)
		inode->i_mapping->a_ops = &ext0_aops;

	in_mem_inode = EXT0_I(inode);
	in_mem_inode->i_flags = inode->i_flags;

	memset(in_mem_inode->i_data, 0, EXT0_FS_MAX_DIRECT_BLOCKS);
	in_mem_inode->i_state = inode->i_state;
	in_mem_inode->i_block_group = EXT0_GET_INO(inode->i_ino);

	mark_inode_dirty(inode);
	*ret_inode = inode;
	return 0;
}

static int ext0_new_inode(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode = NULL;
	int ret;

	ret = ext0_create_inode(dir, dentry, mode, &inode);
	if (EXT0_IS_ERR(ret))
	{
		ext0_debug("Unable to create inode: %i", ret);
		return ret;
	}
	unlock_new_inode(inode);
	d_instantiate(dentry, inode);
	return 0;
}

static int ext0_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode = NULL;
	int ret = ext0_create_inode(dir, dentry, mode, &inode);
	if (EXT0_IS_ERR(ret))
	{
		ext0_debug("Unable to create inode: %i", ret);
		return ret;
	}
	unlock_new_inode(inode);
	d_tmpfile(dentry, inode);
	return 0;
}

static int ext0_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode *inode = NULL;
	int ret = ext0_create_inode(dir, dentry, mode, &inode);
	if (!EXT0_IS_ERR(ret))
	{
		init_special_inode(inode, inode->i_mode, rdev);
		mark_inode_dirty(inode);
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);
	}
	return ret;
}

static int ext0_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct inode *inode = NULL;
	int ret = ext0_create_inode(dir, dentry, S_IFLNK | 0777, &inode);
	int i = strlen(symname) + 1;
	if (EXT0_IS_ERR(ret))
	{
		ext0_debug("Unable to create inode: %i", ret);
		return ret;
	}

	ret = page_symlink(inode, symname, i);
	if (ret)
	{
		inode_dec_link_count(inode);
		iput(inode);
		return ret;
	}

	inode_inc_link_count(inode);
	mark_inode_dirty(inode);
	d_instantiate(dentry, inode);
	return 0;
}

static int ext0_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	inode->i_ctime = current_time(inode);
	inode_inc_link_count(inode);
	mark_inode_dirty(inode);
	d_instantiate(dentry, inode);
	return 0;
}

static int link_dir(struct inode *inode, struct dentry *dentry)
{
	unsigned long npages = dir_pages(inode);
	struct page *page;
	struct ext0_dir_entry *de;
	unsigned long i;

	for (i = 0; i < npages; i++)
	{
		unsigned long page_off = 0, page_end;
		char *kaddr;

		page = ext0_get_page(inode, i);
		if (!page)
		{
			ext0_debug("Invalid page while reading directory contents: page id=%zu", i);
			continue;
		}
		kaddr = (char *)page_address(page);

		page_end = page_off + PAGE_SIZE;
		while (page_off < page_end)
		{
			de = (struct ext0_dir_entry *)(kaddr + page_off);

			/* Ensure we have enough free space */
			if (!de->inode && ((dentry->d_name.len + EXT0_DIR_SIZE) <= (page_end - page_off)))
			{
				de = (struct ext0_dir_entry *)(kaddr + page_off);
				de->name_len = dentry->d_name.len;
				de->rec_len = EXT0_ALIGN_TO_SIZE(EXT0_DIR_SIZE + de->name_len);
				memcpy(de->name, dentry->d_name.name, dentry->d_name.len);
				de->inode = cpu_to_le32(inode->i_ino);
				de->file_type = DT_DIR;
				
				inode->i_size += de->rec_len;
				mark_inode_dirty(inode);
				return 0;
			}

			page_off += de->rec_len;
		}

		ext0_put_page(page);
	}
	return -ENOSPC; /* We need to map new pages in */
}

static int ext0_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode = NULL;
	struct buffer_head *bitmap_bh, *gdesc_bh;
	struct ext0_block_descriptor *gdesc;
	struct super_block *sb = dir->i_sb;
	struct ext0_super_block_info *in_mem_sb = EXT0_SB(sb);
	int ret;
	off_t offset;
	char *bitmap_data;
	struct page *page;
	struct ext0_dir_entry *de;
	unsigned chunk_size = sb->s_blocksize;
	void *kaddr;
	unsigned long blk_no = 0;

	inode_inc_link_count(dir);

	ret = ext0_create_inode(dir, dentry, S_IFDIR | mode, &inode);
	if (EXT0_IS_ERR(ret) || !inode)
	{
		ext0_debug("Unable to create inode: %i", ret);
		inode_dec_link_count(dir);
		return ret;
	}

	inode_inc_link_count(inode);

	/* Create default directory contents */
	page = grab_cache_page(inode->i_mapping, 0);
	if (!page)
	{
		ext0_debug("Could not get any pages: %i", (int)0);
		ret = -ENOMEM;
		goto err;
	}

	ret = __block_write_begin(page, 0, chunk_size, ext0_get_block);
	if (ret)
	{
		ext0_debug("Failed preparing page for write: %i", ret);
		goto page_err;
	}

	kaddr = kmap_atomic(page);
	memset(kaddr, 0, chunk_size);

	de = (struct ext0_dir_entry *)kaddr;
	de->name_len = 1;
	de->rec_len = EXT0_ALIGN_TO_SIZE(EXT0_DIR_SIZE + de->name_len);
	memcpy(de->name, ".", 2);
	de->inode = cpu_to_le32(inode->i_ino);
	de->file_type = DT_DIR;

	inode->i_size += de->rec_len;

	de = (struct ext0_dir_entry *)(kaddr + de->rec_len);
	de->name_len = 2;
	de->rec_len = EXT0_ALIGN_TO_SIZE(EXT0_DIR_SIZE + de->name_len);
	memcpy(de->name, "..", 3);
	de->inode = cpu_to_le32(dir->i_ino);
	de->file_type = DT_DIR;

	inode->i_size += de->rec_len;
	mark_inode_dirty(inode);

	ret = link_dir(dir, dentry);
	if (EXT0_IS_ERR(ret)) {
		goto k_err;
	}
	kunmap_atomic(kaddr);
	block_write_end(NULL, inode->i_mapping, 0, chunk_size, chunk_size, page, NULL);
	/* Default directory contents creation done */

	offset = 0;
	blk_no = ext0_inode_block(dir->i_ino) - 1;
	if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
		fs_to_dev_block_num(sb, blk_no, &offset);

	gdesc_bh = in_mem_sb->s_group_desc[EXT0_GET_INO(dir->i_ino)];
	gdesc = (struct ext0_block_descriptor *)(gdesc_bh->b_data + offset);

	offset = 0;
	blk_no = 0;
	if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
		blk_no = fs_to_dev_block_num(sb, gdesc->bg_block_bitmap, &offset);

	bitmap_bh = sb_bread(sb, blk_no);
	if (!bitmap_bh)
	{
		ext0_debug("Could not perform I/O for block bitmap: %zu, orig: %i", blk_no, gdesc->bg_block_bitmap);
		ret = -EIO;
		goto page_err;
	}
	bitmap_data = bitmap_bh->b_data + offset;

	unlock_page(page);

	mark_buffer_dirty(bitmap_bh);

	unlock_new_inode(inode);
	d_instantiate(dentry, inode);

	put_page(page);

	return 0;

k_err:
	kunmap_atomic(kaddr);

page_err:
	unlock_page(page);

err:
	inode_dec_link_count(inode);
	inode_dec_link_count(dir);
	iput(inode);
	return ret;
}

static int ext0_unlink(struct inode *dir, struct dentry *dentry)
{
	struct page *page = NULL;
	struct inode *inode = dir;
	unsigned long npages = dir_pages(dir);
	struct ext0_dir_entry *de;
	int ret;
	unsigned long i;

	for (i = 0; i < npages; i++)
	{
		void *kaddr;
		unsigned long page_off = 0, page_end;

		page = ext0_get_page(inode, i);
		if (!page)
		{
			ext0_debug("Invalid page for directory entry unlinking: page id=%zu", i);
			continue;
		}
		kaddr = page_address(page);

		page_end = page_off + PAGE_SIZE;
		while (page_off < page_end)
		{
			unsigned long chunk_size;
			de = (struct ext0_dir_entry *)(kaddr + page_off);
			if (!de->rec_len)
			{
				page_off += EXT0_ALIGNMENT;
				continue;
			}

			if (!de->inode)
			{
				page_off += de->rec_len;
				continue;
			}

			if (memcmp(de->name, dentry->d_name.name, dentry->d_name.len) == 0)
			{
				chunk_size = page_end - page_off;
				lock_page(page);
				ret = __block_write_begin(page, page_off, chunk_size, ext0_get_block);
				if (ret)
				{
					ext0_debug("Failed preparing page for deletion: %i", ret);
					unlock_page(page);
					goto out;
				}
				de->inode = 0;
				block_write_end(NULL, inode->i_mapping, page_off, chunk_size, chunk_size, page, NULL);

				if ((page_off + chunk_size) > dir->i_size)
				{
					i_size_write(dir, page_off + chunk_size);
					mark_inode_dirty(dir);
				}

				unlock_page(page);
				inode->i_ctime = inode->i_mtime = current_time(inode);
			}
			page_off += de->rec_len;
		}

		ext0_put_page(page);
	}

	inode_inc_link_count(inode);
	mark_inode_dirty(inode);
	return 0;
out:
	ext0_put_page(page);
	return ret;
}

static int ext0_rmdir(struct inode *dir, struct dentry *dentry)
{
	return 0;
}

static int ext0_rename(struct inode *old_dir, struct dentry *old_dentry,
					   struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
	return 0;
}

static int ext0_inode_by_name(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry->d_parent);
	unsigned long npages = dir_pages(inode);
	struct page *page;
	struct ext0_dir_entry *de;
	unsigned long i;

	for (i = 0; i < npages; i++)
	{
		unsigned long page_off = 0, page_end;
		void *kaddr;

		page = ext0_get_page(inode, i);
		if (!page)
		{
			ext0_debug("Invalid page in directory lookup id=%zu", i);
			continue;
		}
		kaddr = page_address(page);

		page_end = page_off + PAGE_SIZE;
		while (page_off < page_end)
		{
			de = (struct ext0_dir_entry *)(kaddr + page_off);
			if (!de->rec_len)
			{
				page_off += EXT0_ALIGNMENT;
				continue;
			}

			if (!de->inode)
			{
				page_off += de->rec_len;
				continue;
			}

			if (memcmp(de->name, dentry->d_name.name, dentry->d_name.len) == 0)
			{
				return le32_to_cpu(de->inode);
			}
			page_off += de->rec_len;
		}
		ext0_put_page(page);
	}
	return 0;
}

static struct dentry *ext0_lookup_by_name(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct inode *inode;
	ino_t ino;

	if (dentry->d_name.len > EXT0_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = ext0_inode_by_name(dentry);
	inode = NULL;
	if (ino)
	{
		inode = ext0_iget(dir->i_sb, ino);
		if (inode == ERR_PTR(-ESTALE))
		{
			ext0_debug("deleted inode referenced: %lu", (unsigned long)ino);
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias(inode, dentry);
}

static int ext0_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file->f_inode;
	unsigned long npages = dir_pages(inode);
	struct page *page;
	struct ext0_dir_entry *de;
	unsigned long i;

	for (i = ctx->pos; i < npages; i++)
	{
		unsigned long page_off = 0, page_end;
		char *kaddr;

		page = ext0_get_page(inode, i);
		if (!page)
		{
			ext0_debug("Invalid page while reading directory contents: page id=%zu", i);
			ctx->pos += PAGE_SIZE;
			continue;
		}
		kaddr = (char *)page_address(page);

		page_end = page_off + PAGE_SIZE;
		while (page_off < page_end)
		{
			de = (struct ext0_dir_entry *)(kaddr + page_off);
			if (!de->rec_len)
			{
				page_off += EXT0_ALIGNMENT; /* Too small jumps. Needs better approach */
				continue;
			}

			/* Deleted entry or unoccupied? */
			if (!de->inode)
			{
				page_off += de->rec_len;
				continue;
			}

			if (!dir_emit(ctx, de->name, de->name_len, le32_to_cpu(de->inode), le32_to_cpu(de->file_type)))
			{
				ext0_put_page(page);
				return 0;
			}

			page_off += de->rec_len;
			ctx->pos += de->rec_len;
		}
		ext0_put_page(page);
	}

	return 0;
}

const struct file_operations ext0_dir_operations = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate_shared = ext0_readdir,
	.fsync = generic_file_fsync,
};

const struct inode_operations ext0_dir_inode_operations = {
	.create = ext0_new_inode,
	.lookup = ext0_lookup_by_name,
	.link = ext0_link,
	.unlink = ext0_unlink,
	.symlink = ext0_symlink,
	.mkdir = ext0_mkdir,
	.rmdir = ext0_rmdir,
	.mknod = ext0_mknod,
	.rename = ext0_rename,
	// .setattr = ext0_setattr,
	.tmpfile = ext0_tmpfile,
};
