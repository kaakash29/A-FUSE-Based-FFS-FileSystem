/*
 * file:        cs7600fs.h
 * description: Data structures for CS 7600 homework 3 file system.
 *
 * CS 7600, Intensive Computer Systems, Northeastern CCIS
 * Peter Desnoyers,  November 2015
 */
#ifndef __CS7600FS_H__
#define __CS7600FS_H__

#define FS_BLOCK_SIZE 1024
#define FS7600_MAGIC 0x37363030

/* Entry in a directory
 */
struct fs7600_dirent {
    uint32_t valid : 1;
    uint32_t isDir : 1;
    uint32_t inode : 30;
    char name[28];              /* with trailing NUL */
};

/* Superblock - holds file system parameters. 
 */
struct fs7600_super {
    uint32_t magic;
    uint32_t inode_map_sz;       /* in blocks */
    uint32_t inode_region_sz;    /* in blocks */
    uint32_t block_map_sz;       /* in blocks */
    uint32_t num_blocks;         /* total, including SB, bitmaps, inodes */
    uint32_t root_inode;        /* always inode 1 */

    /* pad out to an entire block */
    char pad[FS_BLOCK_SIZE - 6 * sizeof(uint32_t)]; 
};

#define N_DIRECT 6
struct fs7600_inode {
    uint16_t uid;
    uint16_t gid;
    uint32_t mode;
    uint32_t ctime;
    uint32_t mtime;
     int32_t size;
    uint32_t direct[N_DIRECT];
    uint32_t indir_1;
    uint32_t indir_2;
    uint32_t pad[3];            /* 64 bytes per inode */
};

enum {INODES_PER_BLK = FS_BLOCK_SIZE / sizeof(struct fs7600_inode)};

#endif


