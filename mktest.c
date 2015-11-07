#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>

#include "fs7600.h"

char *disk;
fd_set *inode_map;
fd_set *block_map;

int main(int argc, char **argv)
{
    int i;
    char *file = argv[1];

    int n_blks = 1024;
    int n_map_blks = 1;
    int n_inos = 64;
    int n_ino_map_blks = 1;
    int n_ino_blks = n_inos * sizeof(struct fs7600_inode) / FS_BLOCK_SIZE;

    disk = malloc(n_blks * FS_BLOCK_SIZE);
    memset(disk, 0, n_blks * FS_BLOCK_SIZE);

    struct fs7600_super *sb = (void*)disk;
    inode_map = (void*)(disk + FS_BLOCK_SIZE);
    block_map = (void*)(disk + 2*FS_BLOCK_SIZE);
 
    *sb = (struct fs7600_super){.magic = FS7600_MAGIC, .inode_map_sz = 1,
                                .inode_region_sz = n_ino_blks, .block_map_sz = 1,
                                .num_blocks = n_blks, .root_inode = 1};
    FD_SET(0, inode_map);
    FD_SET(1, inode_map);
    FD_SET(2, inode_map);

    /* remember (from /usr/include/i386-linux-gnu/bits/stat.h)
     *    S_IFDIR = 0040000 - directory
     *    S_IFREG = 0100000 - regular file
     */
    /* block 0 - superblock
     *       1 - inode map
     *       2 - block map
     *       3,4,5,6 - inodes
     *       7 - root directory (inode 1)
     *      [8 - file]
     */
                      
    struct fs7600_inode *inodes = (void*)(disk + 3*FS_BLOCK_SIZE);
    inodes[1] = (struct fs7600_inode){.uid = 1001, .gid = 125, .mode = 0040777, 
                                      .ctime = 1446757488, .mtime = 1446757499,
                                      .size = 1024, .direct = {7, 0, 0, 0, 0, 0},
                                      .indir_1 = 0, .indir_2 = 0};

    struct fs7600_dirent *de = (void*)(disk + 7*FS_BLOCK_SIZE);
    de[0] = (struct fs7600_dirent){.valid = 1, .isDir = 0, .inode = 2, .name = "file.A"};

    memset(disk + 8*FS_BLOCK_SIZE, 'A', FS_BLOCK_SIZE);
    inodes[2] = (struct fs7600_inode){.uid = 1001, .gid = 125, .mode = 0100777, 
                                      .ctime = 1446757488, .mtime = 1446757499,
                                      .size = 1000, .direct = {8, 0, 0, 0, 0, 0},
                                      .indir_1 = 0, .indir_2 = 0};
    for (i = 0; i <= 8; i++)
        FD_SET(i, block_map);

    /* for now the root directory is empty */

    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0777);
    write(fd, disk, n_blks * FS_BLOCK_SIZE);
    close(fd);

    return 0;
}


