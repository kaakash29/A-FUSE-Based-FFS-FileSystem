#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>

#include "fs7600.h"

char *disk;
fd_set *inode_map;
fd_set *block_map;
void *next_ptr;

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
    void *ptr = disk + FS_BLOCK_SIZE;
    
    inode_map = ptr; ptr += FS_BLOCK_SIZE;
    block_map = ptr; ptr += FS_BLOCK_SIZE;

    *sb = (struct fs7600_super){.magic = FS7600_MAGIC, .inode_map_sz = 1,
                                .inode_region_sz = n_ino_blks, .block_map_sz = 1,
                                .num_blocks = n_blks, .root_inode = 1};
    FD_SET(0, inode_map);

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
                      
    struct fs7600_inode *inodes = ptr; ptr += 4*FS_BLOCK_SIZE;

    /* root directory
     */
    int inum = 1;
    int root_inum = inum++;
    FD_SET(root_inum, inode_map);
    int root_blk = (ptr - (void*)disk) / FS_BLOCK_SIZE;
    struct fs7600_dirent *root_de = ptr; ptr += FS_BLOCK_SIZE;

    int t = 0x50000000;
    inodes[root_inum] = (struct fs7600_inode){.uid = 1000, .gid = 1000, .mode = 0040777, 
                                              .ctime = t, .mtime = t,
                                              .size = 1024,
                                              .direct = {root_blk, 0, 0, 0, 0, 0},
                                              .indir_1 = 0, .indir_2 = 0};


    /*  "/file.A", 1000 bytes, permission 777
     */
    int f1_inode = inum++;
    root_de[0] = (struct fs7600_dirent){.valid = 0, .isDir = 0,
                                        .inode = 1717, .name = "file.A"};

    root_de[1] = (struct fs7600_dirent){.valid = 1, .isDir = 0,
                                        .inode = f1_inode, .name = "file.A"};
    int f1_blk = (ptr - (void*)disk) / FS_BLOCK_SIZE;
    void *f1_ptr = ptr; ptr += FS_BLOCK_SIZE;
    
    memset(f1_ptr, 'A', 1000);
    inodes[f1_inode] = (struct fs7600_inode){.uid = 1000, .gid = 1000, .mode = 0100777, 
                                             .ctime = t+200, .mtime = t+200,
                                             .size = 1000,
                                             .direct = {f1_blk, 0, 0, 0, 0, 0},
                                             .indir_1 = 0, .indir_2 = 0};
    /* "/dir1/", directory, permission 755
     */
    int d1_inode = inum++;
    root_de[3] = (struct fs7600_dirent){.valid = 0, .isDir = 1,
                                        .inode = f1_inode, .name = "dir1"};
    root_de[5] = (struct fs7600_dirent){.valid = 1, .isDir = 1,
                                        .inode = d1_inode, .name = "dir1"};
    int d1_blk = (ptr - (void*)disk) / FS_BLOCK_SIZE;
    struct fs7600_dirent *d1_de = ptr; ptr += FS_BLOCK_SIZE;
    
    inodes[d1_inode] = (struct fs7600_inode){.uid = 1000, .gid = 1000, .mode = 0040755, 
                                             .ctime = t+400, .mtime = t+400,
                                             .size = 0,
                                             .direct = {d1_blk, 0, 0, 0, 0, 0},
                                             .indir_1 = 0, .indir_2 = 0};
    
    int f2_inode = inum++;
    int f2_blk1 = (ptr - (void*)disk) / FS_BLOCK_SIZE;
    void *f2_ptr = ptr; ptr += FS_BLOCK_SIZE;
    int f2_blk2 = (ptr - (void*)disk) / FS_BLOCK_SIZE; ptr += FS_BLOCK_SIZE;

    d1_de[3] = (struct fs7600_dirent){.valid = 1, .isDir = 0,
                                        .inode = f2_inode, .name = "file.2"};

    memset(f2_ptr, '2', 2 * FS_BLOCK_SIZE);
    inodes[f2_inode] = (struct fs7600_inode){.uid = 1000, .gid = 1000, .mode = 0100777, 
                                             .ctime = t+200, .mtime = t+200,
                                             .size = 2012,
                                             .direct = {f2_blk2, f2_blk1, 0, 0, 0, 0},
                                             .indir_1 = 0, .indir_2 = 0};


    /* zero-length file
     */
    int f3_inode = inum++;
    d1_de[4] = (struct fs7600_dirent){.valid = 1, .isDir = 0,
                                        .inode = f3_inode, .name = "file.0"};
    inodes[f3_inode] = (struct fs7600_inode){.uid = 1000, .gid = 1000, .mode = 0100777, 
                                             .ctime = t+200, .mtime = t+200,
                                             .size = 0,
                                             .direct = {0, 0, 0, 0, 0, 0},
                                             .indir_1 = 0, .indir_2 = 0};
    
    for (i = 0; i <= inum; i++)
        FD_SET(i, inode_map);
    for (i = 0; i < (ptr - (void*)disk)/FS_BLOCK_SIZE; i++)
        FD_SET(i, block_map);

    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0777);
    write(fd, disk, n_blks * FS_BLOCK_SIZE);
    close(fd);

    return 0;
}


