#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/string.h>
#include <linux/vfs.h>
#include <linux/writeback.h>

#include "ext0.h"

static int ext0_writepage(struct page *page, struct writeback_control *wbc);
static int ext0_readpage(struct file *file, struct page *page);
static int ext0_readpages(struct file *file, struct address_space *mapping,
                          struct list_head *pages, unsigned nr_pages);
static int ext0_write_begin(struct file *file, struct address_space *mapping,
                            loff_t pos, unsigned len, unsigned flags,
                            struct page **pagep, void **fsdata);
static int ext0_write_end(struct file *file, struct address_space *mapping,
                          loff_t pos, unsigned len, unsigned copied,
                          struct page *page, void *fsdata);
static sector_t ext0_bmap(struct address_space *mapping, sector_t block);
static int ext0_writepages(struct address_space *mapping, struct writeback_control *wbc);

int ext0_get_block(struct inode *inode, sector_t iblock,
                   struct buffer_head *bh_result, int create)
{
    struct super_block *sb = inode->i_sb;
    struct ext0_super_block_info *in_mem_sb = EXT0_SB(sb);
    struct ext0_block_descriptor *gdesc;
    struct buffer_head *bh, *dir_bh;
    unsigned blk_no;
    off_t offset = 0;
    unsigned long phys_start;

    struct ext0_dir_entry *de;

    blk_no = ext0_inode_block(inode->i_ino) - 1;
    if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
        blk_no = fs_to_dev_block_num(sb, blk_no, &offset);

    bh = in_mem_sb->s_group_desc[EXT0_GET_INO(inode->i_ino)];
    gdesc = (struct ext0_block_descriptor *)(bh->b_data + offset);

    if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
        blk_no = fs_to_dev_block_num(sb, gdesc->bg_first_block, &offset);

    dir_bh = sb_bread(sb, blk_no);
    de = (struct ext0_dir_entry *)(dir_bh->b_data + offset);

    spin_lock(&in_mem_sb->s_lock);

    ext0_debug("Now here we are=> iblock: %zu, create: %i, ino: %lu, grp desc first block: %u: root directory ino: %u, name: %s", iblock, create, inode->i_ino, gdesc->bg_first_block, de->inode, de->name);

    phys_start = gdesc->bg_first_block + iblock - 1;
    if (!create)
    {
        map_bh(bh_result, sb, phys_start);
        spin_unlock(&in_mem_sb->s_lock);
        return 0;
    }

    if (inode->i_blocks < (iblock + 1))
    {
        ext0_debug("Invalid block number: %zu", iblock);
        spin_unlock(&in_mem_sb->s_lock);
        return -ENOSPC;
    }

    map_bh(bh_result, sb, phys_start);
    set_buffer_new(bh_result);
    spin_unlock(&in_mem_sb->s_lock);
    return 0;
}

const struct address_space_operations ext0_aops = {
    .readpage = ext0_readpage,
    .readpages = ext0_readpages,
    .writepage = ext0_writepage,
    .write_begin = ext0_write_begin,
    .write_end = ext0_write_end,
    .bmap = ext0_bmap,
    .writepages = ext0_writepages,
    .migratepage = buffer_migrate_page,
    .is_partially_uptodate = block_is_partially_uptodate,
    .error_remove_page = generic_error_remove_page,
};

static void ext0_write_failed(struct address_space *mapping, loff_t to)
{
    struct inode *inode = mapping->host;

    if (to > inode->i_size)
    {
        truncate_pagecache(inode, inode->i_size);
        // ext0_truncate(inode);
    }
}

static int ext0_writepage(struct page *page, struct writeback_control *wbc)
{
    return block_write_full_page(page, ext0_get_block, wbc);
}

static int ext0_readpage(struct file *file, struct page *page)
{
    return mpage_readpage(page, ext0_get_block);
}

static int ext0_readpages(struct file *file, struct address_space *mapping,
                          struct list_head *pages, unsigned nr_pages)
{
    return mpage_readpages(mapping, pages, nr_pages, ext0_get_block);
}

static int ext0_write_begin(struct file *file, struct address_space *mapping,
                            loff_t pos, unsigned len, unsigned flags,
                            struct page **pagep, void **fsdata)
{
    int ret;
    ret = block_write_begin(mapping, pos, len, flags, pagep, ext0_get_block);
    if (EXT0_IS_ERR(ret))
        ext0_write_failed(mapping, pos + len);
    return ret;
}

static int ext0_write_end(struct file *file, struct address_space *mapping,
                          loff_t pos, unsigned len, unsigned copied,
                          struct page *page, void *fsdata)
{
    int ret;
    ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
    if (EXT0_IS_ERR(ret))
        ext0_write_failed(mapping, pos + len);
    return ret;
}

static sector_t ext0_bmap(struct address_space *mapping, sector_t block)
{
    return generic_block_bmap(mapping, block, ext0_get_block);
}

static int ext0_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
    return mpage_writepages(mapping, wbc, ext0_get_block);
}

static struct ext0_inode *ext0_get_inode(struct super_block *sb, ino_t ino, struct buffer_head **ptr)
{
    struct ext0_inode *on_disk_inode;
    struct buffer_head *bh;
    off_t offset;
    unsigned long blk_no;

    offset = 0;
    blk_no = ext0_inode_block(ino);
    if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
        blk_no = fs_to_dev_block_num(sb, blk_no, &offset);

    ext0_debug("get inode block: %zu, offset: %li, inode_no: %zu", blk_no, offset, ino);

    bh = sb_bread(sb, blk_no);
    if (!bh)
        return ERR_PTR(-EIO);
    on_disk_inode = (struct ext0_inode *)(bh->b_data + offset);

    *ptr = bh;
    return on_disk_inode;
}

int ext0_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    int do_sync = wbc->sync_mode == WB_SYNC_ALL;
    struct ext0_inode_info *in_mem_inode = EXT0_I(inode);
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh;
    struct ext0_inode *on_disk_inode = ext0_get_inode(sb, inode->i_ino, &bh);
    unsigned long i;

    on_disk_inode->i_flags = cpu_to_le32(in_mem_inode->i_flags);
    if (!on_disk_inode->i_dtime)
        on_disk_inode->i_dtime = cpu_to_le32(in_mem_inode->i_dtime);
    on_disk_inode->i_size = cpu_to_le32(inode->i_size);
    on_disk_inode->i_blocks = cpu_to_le32(inode->i_blocks);
    on_disk_inode->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
    on_disk_inode->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
    on_disk_inode->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
    on_disk_inode->i_mode = cpu_to_le16(inode->i_mode);

    for (i = 0; i < EXT0_FS_MAX_DIRECT_BLOCKS; i++)
    {
        on_disk_inode->i_block[i] = in_mem_inode->i_data[i];
    }

    mark_buffer_dirty(bh);
    if (do_sync)
        sync_dirty_buffer(bh);
    brelse(bh);
    return 0;
}

void ext0_evict_inode(struct inode *inode)
{
    struct ext0_inode_info *in_mem_inode = EXT0_I(inode);
    struct writeback_control wbc;
    struct super_block *sb = inode->i_sb;
    struct ext0_super_block *on_disk_sb = EXT0_SB(sb)->s_es;
    unsigned long blk_no;

    blk_no = ext0_inode_block(inode->i_ino) - 1;

    ext0_test_and_clear_bit(blk_no, (void *)on_disk_sb->s_inode_bitmap);

    in_mem_inode->i_dtime = ktime_get_real_seconds();
    wbc.sync_mode = 1;
    ext0_write_inode(inode, &wbc);

    memset(in_mem_inode->i_data, 0, EXT0_FS_MAX_DIRECT_BLOCKS * sizeof(__le32));
    truncate_inode_pages_final(inode->i_mapping);
    clear_inode(inode);
}

struct inode *ext0_iget(struct super_block *sb, ino_t ino)
{
    struct inode *inode;
    struct ext0_inode *on_disk_inode;
    struct ext0_inode_info *in_mem_inode;
    unsigned long i;
    struct buffer_head *bh;

    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    in_mem_inode = EXT0_I(inode);
    on_disk_inode = ext0_get_inode(sb, ino, &bh);

    in_mem_inode->i_flags = le32_to_cpu(on_disk_inode->i_flags);
    in_mem_inode->i_block_group = ino;

    for (i = 0; i < EXT0_FS_MAX_DIRECT_BLOCKS; i++)
    {
        in_mem_inode->i_data[i] = le32_to_cpu(on_disk_inode->i_block[i]);
    }

    inode->i_mode = le32_to_cpu(on_disk_inode->i_mode);
    inode->i_size = le32_to_cpu(on_disk_inode->i_size);
    inode->i_atime.tv_sec = le32_to_cpu(on_disk_inode->i_atime);
    inode->i_ctime.tv_sec = le32_to_cpu(on_disk_inode->i_ctime);
    inode->i_mtime.tv_sec = le32_to_cpu(on_disk_inode->i_mtime);
    inode->i_flags = in_mem_inode->i_flags;
    inode->i_blocks = le32_to_cpu(on_disk_inode->i_blocks);
    inode->i_sb = sb;
    inode->i_ino = ino;
    in_mem_inode->i_dtime = 0;
    inode->i_blkbits = EXT0_FS_BLOCK_BITS;

    if (S_ISREG(inode->i_mode))
    {
        inode->i_op = &ext0_file_inode_operations;
        inode->i_mapping->a_ops = &ext0_aops;
        inode->i_fop = &ext0_file_operations;
    }
    else if (S_ISDIR(inode->i_mode))
    {
        inode->i_op = &ext0_dir_inode_operations;
        inode->i_mapping->a_ops = &ext0_aops;
        inode->i_fop = &ext0_dir_operations;
    }

    brelse(bh);
    unlock_new_inode(inode);

    return inode;
}
