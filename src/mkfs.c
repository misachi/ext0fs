#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "ext0.h"

int main(int argc, char *argv[])
{
    printf("Setting up EXT0-fs...\n");
    struct ext0_super_block *sb;
    struct ext0_inode *inode;
    struct stat statinfo;
    struct ext0_dir_entry *de;
    struct ext0_block_descriptor *gdesc;
    uint32_t blk_no;
    int fd;
    char buf[EXT0_FS_MIN_BLOCK_SIZE];
    unsigned blocks_per_group;
    unsigned long last_block, group_count;

    if (argc < 2)
    {
        fprintf(stderr, "Device-backed file required\n");
        return EXIT_FAILURE;
    }

    fd = open(argv[1], O_RDWR);
    if (!fd)
    {
        perror("open");
        return EXIT_FAILURE;
    }

    if (fstat(fd, &statinfo) == -1)
    {
        perror("fstat");
        goto cleanup;
    }

    blocks_per_group = EXT0_FS_MAX_DIRECT_BLOCKS + EXT0_GROUP_OVERHEAD_BLOCKS_NUM;
    group_count = (statinfo.st_size - (EXT0_FS_MIN_BLOCK_SIZE * EXT0_FS_OVERHEAD_BLOCKS)) / (EXT0_FS_MIN_BLOCK_SIZE * blocks_per_group);
    printf("fs_size=%li\ngroups=%zu\nblocks_per_group=%u\nlogical_block_size=%i\n\n", statinfo.st_size, group_count, blocks_per_group, EXT0_FS_MIN_BLOCK_SIZE);

    last_block = EXT0_GROUP_OVERHEAD_BLOCKS_NUM * group_count + EXT0_FS_OVERHEAD_BLOCKS;

    printf("Preparing root inode\n");
    memset(buf, 0, EXT0_FS_MIN_BLOCK_SIZE);
    inode = (struct ext0_inode *)buf;

    inode->i_mode |= S_IFDIR;
    inode->i_blocks = EXT0_FS_MAX_DIRECT_BLOCKS;
    inode->i_size = sizeof(struct ext0_inode);

    inode->i_mtime = inode->i_atime = inode->i_ctime = 1; // Use correct time

    blk_no = ext0_inode_block(EXT0_ROOT_INO) - 1;
    if (lseek(fd, blk_no * EXT0_FS_MIN_BLOCK_SIZE, SEEK_SET) < 0)
        printf("lseek: Something wrong");

    if (write(fd, buf, EXT0_FS_MIN_BLOCK_SIZE) != EXT0_FS_MIN_BLOCK_SIZE)
    {
        perror("root inode write");
        goto cleanup;
    }
    fsync(fd);

    // lseek(fd, 4096, SEEK_SET);
    // lseek(fd, 3072, SEEK_CUR);
    // memset(buf, 0, EXT0_FS_MIN_BLOCK_SIZE);
    // read(fd, buf, EXT0_FS_MIN_BLOCK_SIZE);
    // inode = (struct ext0_inode *)buf;
    // // printf("Data: %u\n", inode->i_blocks);

    printf("Setting up root inode default directories\n");
    memset(buf, 0, EXT0_FS_MIN_BLOCK_SIZE);
    de = (struct ext0_dir_entry *)buf;
    de->name_len = 1;
    de->rec_len = EXT0_ALIGN_TO_SIZE(EXT0_DIR_SIZE + de->name_len);
    memcpy(de->name, ".\0", 3); /* Remove hard coded */
    de->inode = EXT0_ROOT_INO;
    de->file_type = DT_DIR;

    de = (struct ext0_dir_entry *)(buf + de->rec_len);
    de->name_len = 2;
    de->rec_len = EXT0_ALIGN_TO_SIZE(EXT0_DIR_SIZE + de->name_len);
    memcpy(de->name, "..\0", 4);
    de->inode = EXT0_ROOT_INO;
    de->file_type = DT_DIR;

    /*
     * FS |boot--->group1----  ---->groupN--->group1 data blocks--- --->groupN data blocks|
     * Group |superblock--->descriptor--->inode--->block bitmap|
     */
    blk_no = last_block + EXT0_FS_MAX_DIRECT_BLOCKS + 1;
    lseek(fd, (blk_no - 1) * EXT0_FS_MIN_BLOCK_SIZE, SEEK_SET);
    if (write(fd, buf, EXT0_FS_MIN_BLOCK_SIZE) != EXT0_FS_MIN_BLOCK_SIZE)
    {
        perror("directory write");
        goto cleanup;
    }
    fsync(fd);
    printf("Done setting up root inode\n");

    printf("Setting up group descriptors\n");
    blk_no = EXT0_SUPER_BLOCK + EXT0_FS_OVERHEAD_BLOCKS + 1; /* Block descriptor immediately follows superblock */
    memset(buf, 0, EXT0_FS_MIN_BLOCK_SIZE);
    gdesc = (struct ext0_block_descriptor *)buf;
    for (size_t i = 0; i < group_count; i++)
    {
        gdesc->bg_block_bitmap = EXT0_TO_LE32(blk_no + 1); /* Block lookup is zero-based */

        /* Take care of root dir */
        gdesc->bg_free_blocks_count = EXT0_GET_INO(EXT0_ROOT_INO) == i ? EXT0_FS_MAX_DIRECT_BLOCKS - 1 : EXT0_FS_MAX_DIRECT_BLOCKS;
        gdesc->bg_first_block = EXT0_TO_LE32(last_block + 1);

        lseek(fd, (blk_no - 1) * EXT0_FS_MIN_BLOCK_SIZE, SEEK_SET);
        if (write(fd, (char *)buf, EXT0_FS_MIN_BLOCK_SIZE) != EXT0_FS_MIN_BLOCK_SIZE)
        {
            perror("block descriptor write");
            goto cleanup;
        }

        blk_no += EXT0_GROUP_OVERHEAD_BLOCKS_NUM;
        last_block += EXT0_FS_MAX_DIRECT_BLOCKS;
    }
    fsync(fd);
    printf("Done setting up group descriptors\n");

    printf("Setting up superblocks per group\n");
    memset(buf, 0, EXT0_FS_MIN_BLOCK_SIZE);
    sb = (struct ext0_super_block *)buf;

    sb->s_inode_size = EXT0_TO_LE32(sizeof(struct ext0_inode));
    sb->s_inodes_per_group = 1;
    sb->s_magic = EXT0_FS_MAGIC;
    sb->s_blocks_count = EXT0_TO_LE32(statinfo.st_size / EXT0_FS_MIN_BLOCK_SIZE);
    sb->s_blocks_per_group = blocks_per_group;
    sb->s_inodes_count = sb->s_blocks_count;
    sb->s_free_inodes_count = sb->s_inodes_count - 1;
    sb->s_groups_count = group_count;
    sb->s_last_block = last_block;
    sb->s_free_blocks_count = sb->s_blocks_count - sb->s_last_block - 1;
    memset(sb->s_inode_bitmap, 0, EXT0_INODE_BITMAP_SIZE);

    blk_no = EXT0_GROUP_OVERHEAD_BLOCKS_NUM * group_count + EXT0_FS_OVERHEAD_BLOCKS + EXT0_FS_MAX_DIRECT_BLOCKS;
    ext0_test_and_set_bit(EXT0_GET_INO(EXT0_ROOT_INO), (void *)sb->s_inode_bitmap);

    blk_no = EXT0_SUPER_BLOCK;

    for (size_t i = 0; i < sb->s_groups_count; i++)
    {
        lseek(fd, blk_no * EXT0_FS_MIN_BLOCK_SIZE, SEEK_SET);
        if (write(fd, (char *)buf, EXT0_FS_MIN_BLOCK_SIZE) != EXT0_FS_MIN_BLOCK_SIZE)
        {
            perror("superblock write");
            goto cleanup;
        }
        blk_no += EXT0_GROUP_OVERHEAD_BLOCKS_NUM;
    }
    fsync(fd);
    printf("Done setting up superblocks\n");

    close(fd);
    printf("\nFilesystem setup complete\n");
    return EXIT_SUCCESS;

cleanup:
    close(fd);
    return EXIT_FAILURE;
}