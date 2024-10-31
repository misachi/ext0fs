#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/vfs.h>

#include "ext0.h"

static struct kmem_cache *ext0_inode_cachep;

static void init_once(void *buf)
{
    struct ext0_inode_info *in_mem_inode = (struct ext0_inode_info *)buf;
    inode_init_once(&in_mem_inode->vfs_inode);
}

static int __init init_inodecache(void)
{
    ext0_inode_cachep = kmem_cache_create("ext0_inode_cache",
                                          sizeof(struct ext0_inode_info),
                                          0, (SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT),
                                          init_once);
    if (ext0_inode_cachep == NULL)
    {
        ext0_debug("Out of memory");
        return -ENOMEM;
    }
    return 0;
}

static void destroy_inodecache(void)
{
    ext0_debug("Releasing memory");
    rcu_barrier();
    kmem_cache_destroy(ext0_inode_cachep);
}

static void ext0_i_callback(struct rcu_head *head)
{
    struct inode *inode = container_of(head, struct inode, i_rcu);
    kmem_cache_free(ext0_inode_cachep, EXT0_I(inode));
}

static void ext0_destroy_inode(struct inode *inode)
{
    call_rcu(&inode->i_rcu, ext0_i_callback);
}

static struct inode *ext0_alloc_inode(struct super_block *sb)
{
    struct ext0_inode_info *in_mem_inode;
    in_mem_inode = kmem_cache_alloc(ext0_inode_cachep, GFP_KERNEL);
    if (!in_mem_inode)
    {
        ext0_debug("Unable to allocate memory for inode");
        return NULL;
    }
    return &in_mem_inode->vfs_inode;
}

static int ext0_sync_fs(struct super_block *sb, int wait)
{
    struct ext0_super_block_info *in_mem_sb = EXT0_SB(sb);
    struct ext0_super_block *on_disk_sb = in_mem_sb->s_es;

    spin_lock(&in_mem_sb->s_lock);
    on_disk_sb->s_wtime = cpu_to_le32(ktime_get_real_seconds());
    spin_unlock(&in_mem_sb->s_lock);

    mark_buffer_dirty(in_mem_sb->s_sbh);
    if (wait)
        sync_dirty_buffer(in_mem_sb->s_sbh);
    return 0;
}

void ext0_write_super(struct super_block *sb)
{
    ext0_sync_fs(sb, 1);
}

void ext0_put_super(struct super_block *sb)
{
    struct ext0_super_block_info *in_mem_sb = EXT0_SB(sb);
    unsigned long i;
    ext0_sync_fs(sb, 1);

    for (i = 0; i < in_mem_sb->s_groups_count; i++)
    {
        if (in_mem_sb->s_group_desc[i])
            brelse(in_mem_sb->s_group_desc[i]);
    }

    brelse(in_mem_sb->s_sbh);
    kfree(in_mem_sb->s_group_desc);
    sb->s_fs_info = NULL;
    kfree(in_mem_sb);
}

static int ext0_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    struct super_block *sb = dentry->d_sb;
    struct ext0_super_block_info *in_mem_sb = EXT0_SB(sb);
    struct ext0_super_block *on_disk_sb = in_mem_sb->s_es;

    spin_lock(&in_mem_sb->s_lock);
    buf->f_type = EXT0_FS_MAGIC;
    buf->f_namelen = EXT0_NAME_LEN;
    buf->f_files = le32_to_cpu(on_disk_sb->s_inodes_count);
    buf->f_bsize = sb->s_blocksize;
    spin_unlock(&in_mem_sb->s_lock);
    return 0;
}

static int ext0_freeze(struct super_block *sb)
{
    ext0_sync_fs(sb, 1);
    return 0;
}

static int ext0_unfreeze(struct super_block *sb)
{
    ext0_write_super(sb);
    return 0;
}

static const struct super_operations ext0_sops = {
    .alloc_inode = ext0_alloc_inode,
    .destroy_inode = ext0_destroy_inode,
    .write_inode = ext0_write_inode,
    .evict_inode = ext0_evict_inode,
    .put_super = ext0_put_super,
    .sync_fs = ext0_sync_fs,
    .freeze_fs = ext0_freeze,
    .unfreeze_fs = ext0_unfreeze,
    .statfs = ext0_statfs,
};

/* Given a logical blk_no(start at 1) get the equivalent device
 * block number(starting at 0)
 */
unsigned fs_to_dev_block_num(struct super_block *sb, unsigned blk_no, off_t *offset)
{
    unsigned long blocks_offset;
    unsigned _blk_no;

    blk_no = blk_no - 1;

    if (blk_no < 0)
        blocks_offset = 0;
    else if (blk_no == 0)
        blocks_offset = EXT0_FS_MIN_BLOCK_SIZE;
    else
        blocks_offset = blk_no * EXT0_FS_MIN_BLOCK_SIZE;

    _blk_no = blocks_offset / sb->s_blocksize;

    if (blocks_offset > sb->s_blocksize)
        *offset = blocks_offset % sb->s_blocksize;
    else
        *offset = blocks_offset;
    return _blk_no;
}

static int ext0_fill_super(struct super_block *sb, void *data, int silent)
{
    struct ext0_super_block_info *in_mem_sb;
    struct ext0_super_block *on_disk_sb;
    struct inode *root;
    struct buffer_head *bh, *desc_bh;
    int ret = 0, groups_count;
    off_t offset;
    unsigned long sb_block = EXT0_SUPER_BLOCK, desc_block;
    unsigned desc_group_no;
    unsigned long i;

    if (!sb->s_blocksize)
    {
        ext0_debug("Invalid blocksize");
        return -EINVAL;
    }

    offset = 0;
    if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
        sb_block = fs_to_dev_block_num(sb, sb_block, &offset);
    else if (EXT0_FS_MIN_BLOCK_SIZE > sb->s_blocksize)
        sb_block = (int)EXT0_FS_MIN_BLOCK_SIZE / sb->s_blocksize;
    else
        sb_block = sb_block;

    in_mem_sb = kzalloc(sizeof(struct ext0_super_block_info), GFP_KERNEL);
    if (!in_mem_sb)
    {
        ext0_debug("Unable to allocate memory for super block");
        return -ENOMEM;
    }

    spin_lock_init(&in_mem_sb->s_lock);

    in_mem_sb->s_sb_block = sb_block;
    bh = sb_bread(sb, sb_block);
    if (!bh)
    {
        ext0_debug("Could not perform I/O for super block");
        sb->s_fs_info = NULL;
        kfree(in_mem_sb);
        return -ENOMEM;
    }
    on_disk_sb = (struct ext0_super_block *)(bh->b_data + offset);

    groups_count = le32_to_cpu(on_disk_sb->s_groups_count);

    sb->s_magic = le32_to_cpu(on_disk_sb->s_magic);

    if (sb->s_magic != EXT0_FS_MAGIC)
    {
        ext0_debug("EXT0 filesystem does not exist: %zu", sb_block);
        brelse(bh);
        return -EINVAL;
    }

    in_mem_sb->s_group_desc = kmalloc(groups_count * sizeof(struct buffer_head *), GFP_KERNEL);
    if (!in_mem_sb->s_group_desc)
    {
        ext0_debug("Unable to allocate memory for group descriptors with bytes=%zu", (unsigned long)(groups_count * sizeof(struct buffer_head *)));
        brelse(bh);
        kfree(in_mem_sb);
        return -ENOMEM;
    }

    in_mem_sb->s_blocks_per_group = le32_to_cpu(on_disk_sb->s_blocks_per_group);

    offset = 0;
    desc_group_no = 3;
    for (i = 0; i < groups_count; i++)
    {
        unsigned long j;

        if (EXT0_FS_MIN_BLOCK_SIZE < sb->s_blocksize)
            desc_block = fs_to_dev_block_num(sb, desc_group_no, &offset);
        else
        {
            desc_block = desc_group_no;
            offset = 0;
        }

        desc_bh = sb_bread(sb, desc_block);
        if (!desc_bh)
        {
            /* We failed. Cleanup allocated mem */
            for (j = 0; j < i; j++)
            {
                if (desc_bh)
                    brelse(desc_bh);
            }

            ext0_debug("Unable to perform I/O for descriptor index=%zu", i);
            ret = -ENOMEM;
            brelse(bh);
            sb->s_fs_info = NULL;
            kfree(in_mem_sb);
            return -ENOMEM;
        }

        in_mem_sb->s_group_desc[i] = desc_bh;
        desc_group_no += EXT0_GROUP_OVERHEAD_BLOCKS_NUM;
    }

    in_mem_sb->s_inodes_per_block = 1;
    in_mem_sb->s_desc_per_block = 1;
    in_mem_sb->s_inodes_per_group = le32_to_cpu(on_disk_sb->s_inodes_per_group);
    in_mem_sb->s_groups_count = groups_count;
    in_mem_sb->s_es = on_disk_sb;
    in_mem_sb->s_sbh = bh;
    in_mem_sb->s_last_block = le32_to_cpu(on_disk_sb->s_last_block);

    sb->s_op = &ext0_sops;
    sb->s_fs_info = in_mem_sb;

    root = ext0_iget(sb, EXT0_ROOT_INO);
    if (!root)
    {
        ext0_debug("Unable to find root directory inode: %i", EXT0_ROOT_INO);
        brelse(bh);
        kfree(in_mem_sb);
        return -ENOMEM;
    }

    sb->s_root = d_make_root(root);
    if (!sb->s_root)
    {
        ext0_debug("Unable to create root directory entry");
        brelse(bh);
        kfree(in_mem_sb);
        return -ENOMEM;
    }

    ext0_write_super(sb);
    return 0;
}

static struct dentry *ext0_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, ext0_fill_super);
}

static struct file_system_type ext0_fs_type = {
    .owner = THIS_MODULE,
    .name = "ext0",
    .mount = ext0_mount,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ext0");

static int __init init_ext0_fs(void)
{
    int ret;

    ret = init_inodecache();
    if (EXT0_IS_ERR(ret))
    {
        ext0_debug("Unable to initialize inode cache");
        return ret;
    }

    ret = register_filesystem(&ext0_fs_type);
    if (EXT0_IS_ERR(ret))
    {
        ext0_debug("Unable to regeister filesystem");
        destroy_inodecache();
    }
    ext0_debug("EXT0 Loaded :)");
    return ret;
}

static void __exit exit_ext0_fs(void)
{
    unregister_filesystem(&ext0_fs_type);
    destroy_inodecache();
    ext0_debug("EXT0 UnLoaded :(");
}

MODULE_AUTHOR("Brian Misachi");
MODULE_DESCRIPTION("Zeroth Extended Filesystem");
MODULE_LICENSE("GPL");
module_init(init_ext0_fs);
module_exit(exit_ext0_fs);
