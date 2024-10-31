#ifndef _FS_EXT0_FS
#define _FS_EXT0_FS

#include <linux/fs.h>
#include <linux/version.h>

#ifdef __KERNEL__
#include <linux/spinlock_types.h>
#include <asm/types.h>
#else
#include <linux/byteorder/little_endian.h>

/* These macros are unavailble from linux/fs.h in userspace */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#endif

#define EXT0_ALIGNMENT 4
#define EXT0_ALIGN_TO_SIZE(X) (((EXT0_ALIGNMENT - 1) + X) & ~(EXT0_ALIGNMENT - 1))
#define EXT0_IS_ALIGNED(X) ((X & (EXT0_ALIGNMENT - 1)) == 0)

#define EXT0_FS_MAGIC 0xEF53
#define EXT0_FS_MAX_DIRECT_BLOCKS 12
#define EXT0_FS_BLOCK_BITS 10
#define EXT0_FS_MIN_BLOCK_SIZE (1 << EXT0_FS_BLOCK_BITS)
#define EXT0_FS_MAX_BLOCK_SIZE (1 << (EXT0_FS_BLOCK_BITS + 3))
#define EXT0_ROOT_INO 2
#define EXT0_NAME_LEN 128
#define EXT0_FS_OVERHEAD_BLOCKS 1        /* first block reserved for boot loader{how these things work} */
#define EXT0_GROUP_OVERHEAD_BLOCKS_NUM 4 /* superblock -> block descriptor -> inode -> block bitmap */
#define EXT0_MAX_GROUP 200 /* Default block group number */
#define EXT0_INODE_BITMAP_SIZE 800 // (EXT0_FS_MIN_BLOCK_SIZE * 8)
#define EXT0_IS_ERR(err) (err != 0)
#define EXT0_STATE_NEW 0
#define EXT0_SUPER_BLOCK 1
#define EXT0_DIR_SIZE 8 /* Dir entry size without name length */
#define EXT0_BLOCKS_IN_PAGE (PAGE_SIZE / EXT0_FS_MIN_BLOCK_SIZE)

#define EXT0_MAKE_INO(ino) (ino + 1)
#define EXT0_GET_INO(ino) (ino - 1)
// #define EXT0_INODE_BLOCK(ino) (EXT0_GET_INO(ino) * EXT0_GROUP_OVERHEAD_BLOCKS_NUM - 1)

#define DT_UNKNOWN 0

#ifdef __KERNEL__
#define EXT0_TO_LE32(c) cpu_to_le32(c)
#define EXT0_TO_CPU(l) le32_to_cpu(l)
#else
#define EXT0_TO_LE32(c) __cpu_to_le32(c)
#define EXT0_TO_CPU(l) __le32_to_cpu(l)
#endif

#define ext0_debug(f, a...)                             \
    {                                                   \
        printk(KERN_INFO "EXT0-fs DEBUG (%s, %d): %s:", \
               __FILE__, __LINE__, __func__);           \
        printk(KERN_INFO f, ##a);                       \
    }

struct ext0_block_descriptor
{
    __le32 bg_block_bitmap;
    __le32 bg_first_block;
    __le16 bg_free_blocks_count;
};

struct ext0_dir_entry
{
    __le32 inode;   /* Inode number */
    __le16 rec_len; /* Directory entry length */
    __u8 name_len;  /* Name length */
    __u8 file_type;
    char name[];
};

struct ext0_super_block
{
    __le32 s_inodes_count;
    __le32 s_blocks_count;
    __le32 s_blocks_per_group;
    __le32 s_free_blocks_count;
    __le32 s_free_inodes_count;
    __le32 s_first_data_block;
    __le32 s_inodes_per_group;
    __le32 s_last_block;
    __le32 s_groups_count;
    __le32 s_mtime;
    __le32 s_wtime;
    __le16 s_inode_size;
    __le16 s_block_group_nr;
    __le16 s_magic;
    __le16 s_state;
    __u8 s_uuid[16];
    char s_volume_name[16];
    __u8 s_prealloc_blocks;
    __u8 s_inode_bitmap[EXT0_INODE_BITMAP_SIZE];
};

struct ext0_inode
{
    __le32 i_size;
    __le32 i_atime;
    __le32 i_ctime;
    __le32 i_mtime;
    __le32 i_dtime;
    __le32 i_blocks;
    __le32 i_flags;
    __le16 i_mode;
    __le16 i_pad[1];
    __le32 i_block[EXT0_FS_MAX_DIRECT_BLOCKS];
};

/* Returns inode logical block number starting at 1.
 * Subtract 1 from returned value to get group descriptor block number
*/
static inline unsigned ext0_inode_block(ino_t ino)
{
    int temp = EXT0_GET_INO(ino) <= 0 ? EXT0_GROUP_OVERHEAD_BLOCKS_NUM : ino * EXT0_GROUP_OVERHEAD_BLOCKS_NUM;
    return (temp - 1) + EXT0_FS_OVERHEAD_BLOCKS;
}

#ifdef __KERNEL__

struct ext0_super_block_info
{
    unsigned long s_inodes_per_block;
    unsigned long s_blocks_per_group;
    unsigned long s_inodes_per_group;
    unsigned long s_desc_per_block;
    unsigned long s_groups_count;
    unsigned long s_last_block;
    struct buffer_head *s_sbh;
    struct buffer_head **s_group_desc;
    spinlock_t s_lock;
    struct ext0_super_block *s_es;
    unsigned long s_mount_opt;
    unsigned long s_sb_block;
    unsigned short s_mount_state;
};

struct ext0_inode_info
{
    __u32 i_data[EXT0_FS_MAX_DIRECT_BLOCKS];
    __u32 i_flags;
    __u32 i_dtime;
    __u32 i_block_group;
    __u16 i_state;
    struct inode vfs_inode;
};

static inline struct ext0_super_block_info *EXT0_SB(struct super_block *sb)
{
    return sb->s_fs_info;
}

static inline struct ext0_inode_info *EXT0_I(struct inode *inode)
{
    return container_of(inode, struct ext0_inode_info, vfs_inode);
}

extern int ext0_get_block(struct inode *inode, sector_t iblock,
                          struct buffer_head *bh_result, int create);
unsigned fs_to_dev_block_num(struct super_block *sb, unsigned blk_no, off_t *offset);

int ext0_write_inode(struct inode *inode, struct writeback_control *wbc);
void ext0_evict_inode(struct inode *inode);
struct inode *ext0_iget(struct super_block *sb, ino_t ino);
// int ext0_setattr(struct dentry *dentry, struct iattr *iattr);

extern const struct inode_operations ext0_file_inode_operations;
extern const struct file_operations ext0_file_operations;
extern const struct address_space_operations ext0_aops;

extern const struct inode_operations ext0_dir_inode_operations;
extern const struct file_operations ext0_dir_operations;

#define ext0_test_and_set_bit __test_and_set_bit_le
#define ext0_test_and_clear_bit __test_and_clear_bit_le
#define ext0_test_bit test_bit_le
#define ext0_find_first_zero_bit find_first_zero_bit_le
#define ext0_find_next_zero_bit find_next_zero_bit_le
#else
#define BITOP_LE_SWIZZLE 0

static inline int __test_and_set_bit_le(int nr, void *addr)
{
	return 0;
}
#define ext0_test_and_set_bit __test_and_set_bit_le
#endif

#endif /* _FS_EXT0_FS */