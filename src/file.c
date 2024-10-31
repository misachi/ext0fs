#include <linux/fs.h>
#include <linux/buffer_head.h>

#include "ext0.h"

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 18, 0)
static int ext0_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo, u64 start, u64 len)
{
    return generic_block_fiemap(inode, fieinfo, start, len, ext0_get_block);
}

const struct inode_operations ext0_file_inode_operations = {
    // .setattr = ext0_setattr,
    .fiemap = ext0_fiemap,
};
#else
// #include <linux/iomap.h>
// static int ext0_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo, u64 start, u64 len)
// {
//     int ret;
// 	inode_lock(inode);
// 	len = min_t(u64, len, i_size_read(inode));
// 	ret = iomap_fiemap(inode, fieinfo, start, len, &ext0_iomap_ops);
// 	inode_unlock(inode);
// 	return ret;
// }
const struct inode_operations ext0_file_inode_operations = {};
#endif

const struct file_operations ext0_file_operations = {
    .llseek = generic_file_llseek,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap = generic_file_mmap,
    .open = generic_file_open,
    .fsync = generic_file_fsync,
    .get_unmapped_area = thp_get_unmapped_area,
    .splice_read = generic_file_splice_read,
    .splice_write = iter_file_splice_write,
};
