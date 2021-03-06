/*
 * file:        homework.c
 * description: skeleton file for CS 7600 homework 3
 *
 * CS 7600, Intensive Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2015
 */

#define FUSE_USE_VERSION 27

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "fs7600.h"
#include "blkdev.h"

/* required constants */

#define TRUE 1  		/* true */
#define FALSE 0			/* false */
#define SUCCESS 0		/* return value for success case */
#define IS_DIR 1		/* to indicate directory */
#define IS_FILE 0		/* to indicate regular file */
#define PATH_CACHE_SIZE 50			/* size of path cache */
#define DIR_ENTRY_CACHE_SIZE 50  	/* size of directory cache */
#define WRITE_BK_CLN_CACHE_SIZE 30	/* size of write back clean cache */
#define WRITE_BK_DRTY_CACHE_SIZE 10 /* size of write back dirty cache */


int* getListOfBlocksOperate(struct fs7600_inode *inode, int len, 
				off_t offset, int *numOfBlocksToread);
				
/* global variables */

int path_cache_access_time = 0;  /* for LRU in path_cache */
int dir_cache_access_time = 0;   /* for LRU in directory entry cache */
int wrt_bck_cache_access_time = 0;  /* for LRU in write back cache */
extern int homework_part; /* set by '-part n' command-line option */

/* 
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of 1024-byte blocks
 */
extern struct blkdev *disk;

/* 
 * structure to store the path translation mapping to inode number 
 * This will represent the caching of path translations in disk. 
 * */
struct path_cache {
	char* path;  			/* path 						*/
	int inum;    			/* inode number 				*/
	int last_access_time;	/* to save the last access time */
	int valid;    			/* to indicate whether valid 	*/
};

/* 
 * structure for the directory entry cache for accessed 
 * directory entries from disk. 
 * */
struct dir_entry_cache {
	int parent_inum; 		/* the parent inode number 			*/
	char* child_name; 		/* the name of the entry in parent 	*/
	int child_inum; 		/* the entry's inode number 		*/
	int valid; 				/* to indicate whether valid 		*/
	int last_access_time;  	/* to save the last access time 	*/
	int ftype;  			/* the type of file 				*/
};

/* 
 * structure to store clean and dirty pages accessed from disk 
 * to implement write-back cache.  
 * */
struct write_back_cache {
	int block_num;  			/* the block number 			*/
	char block[FS_BLOCK_SIZE]; 	/* the entire page 				*/
	int valid;  				/* to indicate whether a valid 	*/
	int last_access_time;  		/* to save the last access time */
};

/* path tranlsation cache */
struct path_cache path_cache_list[PATH_CACHE_SIZE];
/* directory entry cache */
struct dir_entry_cache dir_entry_cache_list[DIR_ENTRY_CACHE_SIZE];
/* clean pages write back cache */
struct write_back_cache write_bk_cln_cache[WRITE_BK_CLN_CACHE_SIZE];
/* dirty pages write back cache */
struct write_back_cache write_bk_drty_cache[WRITE_BK_DRTY_CACHE_SIZE];

/* 
 * fetch_entry_from_clean_wrt_bck_cache : int -> int
 * Returns the cache index for the block number blknum if 
 * found in write back clean cache, otherwise -1. 
 * */
int fetch_entry_from_clean_wrt_bck_cache(int blknum) {
	int i;
	int cache_entry = -1;
	for (i = 0; i < WRITE_BK_CLN_CACHE_SIZE; i++) {
		if ((write_bk_cln_cache[i].valid == 1) && 
		(write_bk_cln_cache[i].block_num == blknum)) {
			/* block found in cache, no need to go to disk */
			cache_entry = i;
			/* increse the access time for LRU implementation */
			write_bk_cln_cache[i].last_access_time = 
								++wrt_bck_cache_access_time;
			break;
		}
	}
	return cache_entry;
}

/* 
 * add_entry_to_clean_wrt_bck_cache : int, char* -> int
 * Finds a free entry or replaces an entry using LRU 
 * technique to add an entry for blknum and buf in clean
 * write back cache. 
 * */
int add_entry_to_clean_wrt_bck_cache(int blknum, char* buf) {
	int minimum = write_bk_cln_cache[0].last_access_time;
	int index_to_replace = 0, i;
	for (i = 0; i < WRITE_BK_CLN_CACHE_SIZE; i++) {
		if (write_bk_cln_cache[i].valid == 0) {
			/* free entry found */
			index_to_replace = i;
			/* no need to search further */
			break;
		}
		if (minimum >= write_bk_cln_cache[i].last_access_time) {
			/* old accessed entry found */
			minimum = write_bk_cln_cache[i].last_access_time;
			index_to_replace = i;
		}
	}
	/* put the entry */
	replace_clean_wrt_bck_cache_entry(blknum, buf, index_to_replace);
	return SUCCESS;
}

/* 
 * replace_clean_wrt_bck_cache_entry : int, char*, int -> int
 * Replaces the entry in write_bk_cln_cache at index entry with
 * blknum and buf 
 * */
int replace_clean_wrt_bck_cache_entry(int blknum, 
										char* buf, int entry) {
	/* mark the entry as valid */
	write_bk_cln_cache[entry].valid = 1;
	/* copy the path to the cache */
	memcpy(&write_bk_cln_cache[entry].block, buf, FS_BLOCK_SIZE);
	/* store the inode number */
	write_bk_cln_cache[entry].block_num = blknum;
	/* save the incremented access time */
	write_bk_cln_cache[entry].last_access_time = 
							++wrt_bck_cache_access_time;
	return SUCCESS;
}

/* 
 * fetch_entry_from_dirty_wrt_bck_cache : int -> int
 * Returns the cache index for the block number blknum if 
 * found in write back dirty cache, otherwise -1. 
 * */
int fetch_entry_from_dirty_wrt_bck_cache(int blknum) {
	int i;
	int cache_entry = -1;
	for (i = 0; i < WRITE_BK_DRTY_CACHE_SIZE; i++) {
		if ((write_bk_drty_cache[i].valid == 1) && 
		(write_bk_drty_cache[i].block_num == blknum)) {
			/* block found in cache, no need to go to disk */
			cache_entry = i;
			/* increse the access time for LRU implementation */
			write_bk_drty_cache[i].last_access_time = 
								++wrt_bck_cache_access_time;
			break;
		}
	}
	return cache_entry;
}

/* 
 * add_entry_to_dirty_wrt_bck_cache : int, char* -> int
 * Finds a free entry or replaces an entry using LRU 
 * technique to add an entry for blknum and buf in dirty
 * write back cache. 
 * */
int add_entry_to_dirty_wrt_bck_cache(struct blkdev *old_dev, 
									int blknum, char* buf) {
	int minimum = write_bk_drty_cache[0].last_access_time;
	int index_to_replace = 0, i;
	for (i = 0; i < WRITE_BK_DRTY_CACHE_SIZE; i++) {
		if (write_bk_drty_cache[i].valid == 0) {
			/* free entry found */
			index_to_replace = i;
			/* no need to search further */
			break;
		}
		if (minimum >= write_bk_drty_cache[i].last_access_time) {
			/* old accessed entry found */
			minimum = write_bk_drty_cache[i].last_access_time;
			index_to_replace = i;
		}
	}
	/* put the entry */
	replace_dirty_wrt_bck_cache_entry(old_dev, 
							blknum, buf, index_to_replace);
	return SUCCESS;
}

/* 
 * replace_dirty_wrt_bck_cache_entry : blkdev, int, char*, int -> int
 * Replaces the entry in write_bk_drty_cache at index entry with
 * blknum and buf, and writes the evicted entry if it was valid 
 * one to disk 
 * */
int replace_dirty_wrt_bck_cache_entry(struct blkdev *old_dev, 
							int blknum, char* buf, int entry) {
	/* mark the entry as valid */
	if (write_bk_drty_cache[entry].valid == 1) {
		/* need to write to disk, as replacing a dirty page which
		 * is a valid one */
		 if (old_dev->ops->write(old_dev, 
					write_bk_drty_cache[entry].block_num, 
					1, write_bk_drty_cache[entry].block) < 0)
				/* error on write */
				exit(1);
	}
	write_bk_drty_cache[entry].valid = 1;
	memcpy(&write_bk_drty_cache[entry].block, buf, FS_BLOCK_SIZE);
	/* store the inode number */
	write_bk_drty_cache[entry].block_num = blknum;
	/* save the incremented access time */
	write_bk_drty_cache[entry].last_access_time = 
							++wrt_bck_cache_access_time;
	return SUCCESS;
}

/*
 * cache - you'll need to create a blkdev which "wraps" this one
 * and performs LRU caching with write-back.
 */
int cache_nops(struct blkdev *dev) 
{
    struct blkdev *d = dev->private;
    return d->ops->num_blocks(d);
}

/*
 * cache_read : blkdev, int, int, void* -> int
 * Reads the block with block number first from clean or dirty 
 * cache if present, otherwise reads from disk and adds to clean
 * cache.
 * */
int cache_read(struct blkdev *dev, int first, int n, void *buf)
{
	int entry_in_cache = -1;
	struct blkdev *old_dev = dev->private;
	/* first try to get from clean cache */
	entry_in_cache = fetch_entry_from_clean_wrt_bck_cache(first);
	if (entry_in_cache < 0)
	{
		/* try to get from dirty cache */
		entry_in_cache = fetch_entry_from_dirty_wrt_bck_cache(first);
		if (entry_in_cache >= 0) {
			/* entry found in dirty write back cache */
			memcpy(buf, 
			write_bk_drty_cache[entry_in_cache].block, FS_BLOCK_SIZE);
		}
	}
	else {
		/* entry found in clean write back cache */
		memcpy(buf, write_bk_cln_cache[entry_in_cache].block, 
											FS_BLOCK_SIZE);
	}
	/* if not found, then fetch from disk and add to clean cache */
	if (entry_in_cache < 0)
	{
		/* fetch from disk */
		if (old_dev->ops->read(old_dev, first, 1, buf) < 0) {
			/* error on read */
			exit(1);
		}
		/* add to clean cache */
		add_entry_to_clean_wrt_bck_cache(first, buf);	
	}
    return SUCCESS;
}

/*
 * cache_write : blkdev, int, int, void* -> int
 * Writes the block with block number first to dirty cache if found
 * otherwise invalidates page in clean cache if found and moves 
 * to dirty cache, otherwise writes to disk and adds to dirty cache
 * evicting a page if required.
 * */
int cache_write(struct blkdev *dev, int first, int n, void *buf)
{
    int block_num = first, i, k, j = 0;
	int entry_in_cache = -1;
	struct blkdev *old_dev = dev->private;
	
	/* first try to get from dirty cache */
	entry_in_cache = fetch_entry_from_dirty_wrt_bck_cache(block_num);
	if (entry_in_cache < 0)
	{
		/* try to get from clean cache */
		entry_in_cache = fetch_entry_from_clean_wrt_bck_cache(block_num);
		if (entry_in_cache >= 0) {
			/* entry found in clean write back cache */
			/* invalidate the clean cache page, 
			 * to be moved to dirty cache */
			write_bk_cln_cache[entry_in_cache].valid = 0;
			/* add the clean page to the dirty page */
			add_entry_to_dirty_wrt_bck_cache(old_dev, block_num, buf);
		}
	}
	else {
		/* entry found in dirty write back cache */
		memcpy(write_bk_drty_cache[entry_in_cache].block, buf,  
											FS_BLOCK_SIZE);
	}
	/* if not found, then fetch from disk and add to dirty cache */
	if (entry_in_cache < 0)
	{
		/* write from disk */
		if (old_dev->ops->write(old_dev, block_num, 1, buf) < 0) {
			/* error on write */
			exit(1);
		}
		/* add to dirty cache */
		add_entry_to_dirty_wrt_bck_cache(old_dev, block_num, buf);	
	}
    return SUCCESS;
}

struct blkdev_ops cache_ops = {
    .num_blocks = cache_nops,
    .read = cache_read,
    .write = cache_write
};
struct blkdev *cache_create(struct blkdev *d)
{
    struct blkdev *dev = malloc(sizeof(*d));
    dev->ops = &cache_ops;
    dev->private = d;
    return dev;
}

/* 
 * by defining bitmaps as 'fd_set' pointers, you can use existing
 * macros to handle them. 
 *   FD_ISSET(##, inode_map);
 *   FD_CLR(##, block_map);
 *   FD_SET(##, block_map);
 */
fd_set *inode_map;  /* = malloc(sb.inode_map_size * FS_BLOCK_SIZE); */
fd_set *block_map;

struct fs7600_super sb;  /* to store the super block */

/* 
 * to store the inodes list */
/* the number of blocks upper limimted to 1024, can be increased 
 * */
struct fs7600_inode inodes_list[INODES_PER_BLK][INODES_PER_BLK];

/* 
 * get_inode_from_inum: int -> struct fs7600_inode*
 * Returns the pointer to the inode for the given inode_number 
 * */
struct fs7600_inode* get_inode_from_inum(int inode_number) {
	/* Get the block number which contains the inode entry */
	int disk_block = get_block_number_from_inum(inode_number);
	/* get the inode entry from the block */
	int disk_block_entry = get_bl_inode_number_from_inum(inode_number);
	/* Return the address of the entry */
	return &(inodes_list[disk_block][disk_block_entry]);
}

/* 
 * get_inodes_region_starting_block: -> int
 * Returns the starting block for the inodes region on disk. 
 * */
int get_inodes_region_starting_block() {
	return (sb.block_map_sz + get_block_map_starting_block());
}

/* 
 * get_inode_map_starting_block: -> int
 * Returns the starting block for the inodes map on disk. 
 * */
int get_inode_map_starting_block() {
	return 1;
}

/* 
 * get_block_map_starting_block: -> int
 * Returns the starting block for the blocks map on disk. 
 * */
int get_block_map_starting_block() {
	return (sb.inode_map_sz + get_inode_map_starting_block());
}

/* 
 * get_block_number_from_inum : int -> int
 * Returns the block number which contains this inode_number 
 * from the inodes region 
 * */
int get_block_number_from_inum(int inode_number) {
	return inode_number / INODES_PER_BLK;
}

/* 
 * write_block_to_inode_region : int -> int
 * Writes the block for this inode number to disk 
 * */
int write_block_to_inode_region(int inode_number) {
	/* get the block number for the inode_number */
	int blnum = get_block_number_from_inum(inode_number);
	/* get the actual corresponding block number on disk */
	int actual_disk_block = get_inodes_region_starting_block() + blnum;
	/* write the block to disk */
	if (disk->ops->write(disk, actual_disk_block, 1, 
					&inodes_list[blnum]) < 0)
		/* error on write */
        exit(1);
    /* Return success */
    return SUCCESS;
}

/* 
 * get_bl_inode_number_from_inum; int -> int
 * Retuns the inode entry number in a block 
 * for the given inode number  
 * */
int get_bl_inode_number_from_inum(int inode_number) {
	return inode_number % INODES_PER_BLK;
}

/* 
 * get_free_block_number: -> int
 * Returns a free block number, if possible, otherwise 0. 
 * */
int get_free_block_number() {
	int block_count = sb.num_blocks;
	int free_entry = 0;
	int index = 0;
	while (index < block_count) {
		if (!(FD_ISSET(index, block_map)))
		{
			free_entry = index; 
			break;
		}
		index ++;
	}
	return free_entry;
}

/* 
 * set_block_number : int -> int
 * Sets the bit block_num in block_map. 
 * */
int set_block_number(int block_num) {
	FD_SET(block_num, block_map);
	if (disk->ops->write(disk, get_block_map_starting_block(), 
					sb.block_map_sz, block_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* 
 * clear_block_number : int -> int
 * Clears the bit block_num in block_map. 
 * */
int clear_block_number(int block_num) {
	FD_CLR(block_num, block_map);
	if (disk->ops->write(disk, get_block_map_starting_block(), 
					sb.block_map_sz, block_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* 
 * get_free_inode_number: -> int
 * Returns a free inode number, if possible, otherwise 0. 
 * */
int get_free_inode_number() {
	int inode_count = sizeof(*inode_map);
	int free_entry = 0;
	int index = 0;
	while (index < inode_count) {
		if (!(FD_ISSET(index, inode_map)))
		{
			free_entry = index;
			break;
		}
		index ++;
	}
	return free_entry;
}

/* 
 * set_inode_number : int -> int
 * Sets the bit inum in inode_map. 
 * */
int set_inode_number(int inum) {
	FD_SET(inum, inode_map);
	if (disk->ops->write(disk, get_inode_map_starting_block(), 
					sb.inode_map_sz, inode_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* 
 * clear_inode_number : int -> int
 * Clears the bit inum in inode_map. 
 * */
int clear_inode_number(int inum) {
	FD_CLR(inum, inode_map);
	if (disk->ops->write(disk, get_inode_map_starting_block(), 
					sb.inode_map_sz, inode_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* 
 * remove_last_token : char*, char**  -> char*
 * Tokenizes the string path based on the separator "/" and
 * removes the last token and returns the string. Also,
 * sets the last token in last_token 
 * */
char* remove_last_token(const char *path, char** last_token) {
	char* working_string = NULL;
	char* curr_substring = NULL;
	char* separator = "/";
	/* Allocate memory for return string */
	char* return_string = (char*) malloc(sizeof(char) * strlen(path));
	char* prev_string = NULL;
	if (path != NULL) {
		/* to start path with "/" if it already does */
		if (strncmp(separator, path, 1) == 0)
			strcpy(return_string, separator);
		working_string = strdup(path);
		curr_substring = strtok(working_string, separator);
		while(curr_substring != NULL) {
			prev_string = curr_substring;
			curr_substring = strtok(0, separator);
			if (curr_substring != NULL) {
				/* append prev_string to result if curr string is not 
				 * null indicating prev_string is not the last token  */
				strcat(return_string, prev_string);
				strcat(return_string, separator);
			}
			else {
				/* set the last token */
				*last_token = prev_string;
			}
		}
		/* replace the last "/" with a null terminator */
		return_string[strlen(return_string) - 1] = '\0';
	}
	return return_string;
}

/* 
 * fetch_entry_from_dir_entry_cache : int, char*, int* -> int
 * Returns the mapped child inode number in cache for the directory 
 * entry of dir_inum for name if exists, else returns -1. 
 * */
int fetch_entry_from_dir_entry_cache(int dir_inum, 
							char* name, int* ftype) {
	int child_inode_number = -1, i;
	for (i = 0; i < DIR_ENTRY_CACHE_SIZE; i++) {
		if ((dir_entry_cache_list[i].valid == 1) && 
			(dir_entry_cache_list[i].parent_inum == dir_inum) &&
			(dir_entry_cache_list[i].child_name != NULL) && 
			(strcmp(dir_entry_cache_list[i].child_name, name) == 0)) {
			/* directory entry found in cache */
			child_inode_number = dir_entry_cache_list[i].child_inum;
			/* increse the access time for LRU implementation */
			dir_entry_cache_list[i].last_access_time = 
								++dir_cache_access_time;
			*ftype = dir_entry_cache_list[i].ftype;
			break;
		}
	}
	return child_inode_number;
}

/* 
 * replace_dir_cache_entry : int, char*, int, int, int -> int
 * Replaces the entry in directory entry cache at index entry with
 * a mapping of dir_inum, name to child_inum 
 * */
int replace_dir_cache_entry(int dir_inum, char* name, 
							int child_inum, int entry, int ftype) {
	/* mark the entry as valid */
	dir_entry_cache_list[entry].valid = 1;
	/* copy the name to the directory entry cache */
	dir_entry_cache_list[entry].child_name = (char*) malloc 
									(sizeof(char) * strlen(name));
	strcpy(dir_entry_cache_list[entry].child_name, name);
	strcat(dir_entry_cache_list[entry].child_name, "\0");
	/* store the directory inode number */
	dir_entry_cache_list[entry].parent_inum = dir_inum;
	/* store the child inode number */
	dir_entry_cache_list[entry].child_inum = child_inum;
	dir_entry_cache_list[entry].ftype = ftype;
	/* save the incremented access time */
	dir_entry_cache_list[entry].last_access_time = 
							++dir_cache_access_time;
	return SUCCESS;
}

/* 
 * add_dir_entry_to_cache : int, char*, int -> int
 * Adds the entry of [dir_inum, name] mapped to child_inum in the 
 * directory entry cache, after finding a free entry if it exists,
 * or replacing an existing entry by LRU. 
 * */
int add_dir_entry_to_cache(int dir_inum, char* name, 
								int child_inum, int ftype) {
	int minimum = dir_entry_cache_list[0].last_access_time;
	int index_to_replace = 0, i;
	for (i = 0; i < DIR_ENTRY_CACHE_SIZE; i++) {
		if (dir_entry_cache_list[i].valid == 0) {
			/* free entry found */
			index_to_replace = i;
			/* no need to search further */
			break;
		}
		if (minimum >= dir_entry_cache_list[i].last_access_time) {
			/* old accessed entry found */
			minimum = dir_entry_cache_list[i].last_access_time;
			index_to_replace = i;
		}
	}
	/* found the index to replace by LRU */
	replace_dir_cache_entry(dir_inum, name, child_inum, 
								index_to_replace, ftype);
	return SUCCESS;
}

/* 
 * invalidate_entry_in_dir_cache : int, char* -> int
 * Invalidates the entry of [dir_inum, name] mapped to child_inum 
 * in the directory entry cache, by setting the valid bit to 0. 
 * */
int invalidate_entry_in_dir_cache(int dir_inum, char* name) {
	/* Find the corresponding entry in dir_entry_cache
	 * and set the valid bit to 0 */
	int i;
	for (i = 0; i < DIR_ENTRY_CACHE_SIZE; i++) {
		if ((dir_entry_cache_list[i].valid == 1) && 
			(dir_entry_cache_list[i].parent_inum == dir_inum) &&
			(dir_entry_cache_list[i].child_name != NULL) && 
			(strcmp(dir_entry_cache_list[i].child_name, name) == 0))
		 {
			/* matching entry found */
			dir_entry_cache_list[i].valid = 0;
			dir_entry_cache_list[i].child_name = NULL;
			/* no need to search further */
			break;
		}
	}
	return SUCCESS;
}

/* 
 * fetch_entry_from_path_cache : char* -> int
 * Returns the mapped inode number in cache for the path if exists,
 * else returns -1. 
 * */
int fetch_entry_from_path_cache(const char* path) {
	int inode_number = -1, i;
	for (i = 0; i < PATH_CACHE_SIZE; i++) {
		if ((path_cache_list[i].valid == 1) && 
			(path_cache_list[i].path != NULL) && 
			(strcmp(path_cache_list[i].path, path) == 0)) {
			/* path found in cache */
			inode_number = path_cache_list[i].inum;
			/* increse the access time for LRU implementation */
			path_cache_list[i].last_access_time = ++path_cache_access_time;
			break;
		}
	}
	return inode_number;
}

/* 
 * replace_cache_entry : char*, int, int -> int
 * Replaces the netry in path_cache at index entry with
 * path and inum 
 * */
int replace_cache_entry(const char* path, int inum, int entry) {
	/* mark the entry as valid */
	path_cache_list[entry].valid = 1;
	/* copy the path to the cache */
	path_cache_list[entry].path = (char*) malloc 
						(sizeof(char) * (strlen(path) + 1));
	strcpy(path_cache_list[entry].path, path);
	strcat(path_cache_list[entry].path, "\0");
	/* store the inode number */
	path_cache_list[entry].inum = inum;
	/* save the incremented access time */
	path_cache_list[entry].last_access_time = 
									++path_cache_access_time;
	return SUCCESS;
}

/* 
 * add_path_to_cache : char*, int -> int
 * Adds the entry of path and inum to path_cache, after finding
 * a free nedtry if it exists,, or replacing an existing entry
 * by LRU. 
 * */
int add_path_to_cache(const char* path, int inum) {
	int minimum = path_cache_list[0].last_access_time;
	int index_to_replace = 0, i;
	for (i = 0; i < PATH_CACHE_SIZE; i++) {
		if (path_cache_list[i].valid == 0) {
			/* free entry found */
			index_to_replace = i;
			/* no need to search further */
			break;
		}
		if (minimum >= path_cache_list[i].last_access_time) {
			/* old accessed entry found */
			minimum = path_cache_list[i].last_access_time;
			index_to_replace = i;
		}
	}
	/* found the index to replace by LRU */
	replace_cache_entry(path, inum, index_to_replace);
	return SUCCESS;
}

/* 
 * fs_translate_path_to_inum : const char*, int* -> int
 * Translates the path provided to the inode of the file
 * pointed to by the path, and returns the inode
 * Returns Error if path is invalid  
 * */
static int fs_translate_path_to_inum(const char* path, int* type) {
	char *curr_dir = NULL;
	char *parent_dir = NULL;
	char *working_path = NULL;
	struct fs7600_inode *root_inode = NULL;
	int i, dir_block_num, parent_dir_inode, inode;
	int found = 0;
	int ftype;
	char* forward_path = NULL;
	if (path == NULL){
		return -ENOENT;
	}
	fflush(stdout);fflush(stderr);
	/* Assume that the path starts from the root. Thus the entry 
	 *  should be present in the root inode's block
	 * the root is a directory verify if the current file is 
	 * present in directory. */
	working_path = strdup(path);
	curr_dir = strtok(working_path, "/");
	if (curr_dir == NULL){
		*type = 1;
		return 1;
	}	
	/* Set the inode of the parent directory to be 1 for the root */			
	parent_dir_inode = 1;
	/* traverse over the token in the path */
	while(curr_dir != NULL) {
		found = 0;
		found = entry_exists_in_directory(parent_dir_inode, curr_dir,
						&ftype, &inode);
		if (found == 0) {
			/* File not found in direcotry */
			return -ENOENT;
		}
		forward_path = strtok(0, "/"); 
		if (forward_path != NULL)
		{
			/* if the forward_path is not null means there is 
			 * more path after this entry. Check if we shud 
			 * throw the FILE_IN_PATH error? */
			if (ftype == 0) {
				/* signifies that the entry is a file since 
				 * there are more entries in the path 
				 * this condition is an error */
				*type = ftype; 
				return -ENOTDIR;
			}
			else
			{
				/* id type is one this means that this entry 
				 * is a directory and we should continue 
				 * path traversals */
				parent_dir_inode = inode;
				strcpy(curr_dir, forward_path);
			}
		}
		else 
		{
			/* if the forward_path is null means there is no 
			 * more path after this entry */
			*type = ftype; 
			return inode;
		}
	}
}

/* 
 * get_dir_entries_from_dir_inum : 
 * 						int, struct fs7600_dirent* -> int
 * Reads the entry_list for the directory with inode_number 
 * from disk (only 1 block for a directory) 
 * Returns SUCCESS 
 *  */
int get_dir_entries_from_dir_inum(int inode_number, 
						struct fs7600_dirent *entry_list) {
    struct fs7600_inode *inode = NULL;
    int dir_block_num;
    /* get the inode from the inode number */
    inode = get_inode_from_inum(inode_number);
	/* read the first block which is the only block for dir inode */
    if (disk->ops->read(disk, inode->direct[0], 1, entry_list) < 0)
        exit(1);
    return SUCCESS;
}

/* 
 * write_dir_entries_to_disk_for_dir_inum : 
 * 							int, struct fs7600_dirent* -> int
 * Writes the entry_list for the directory with inode_number 
 * to disk (only 1 block for a directory) 
 * Returns SUCCESS 
 * */
int write_dir_entries_to_disk_for_dir_inum(int inode_number, 
							struct fs7600_dirent* entry_list) {
    struct fs7600_inode *inode = NULL;
    int dir_block_num;
    /* get the inode from the inode number */
    inode = get_inode_from_inum(inode_number);
	/* write the first block which is the only block for dir inode */
    if (disk->ops->write(disk, inode->direct[0], 1, entry_list) < 0)
        exit(1);
    return SUCCESS;
}


/* 
 * init - this is called once by the FUSE framework at startup. Ignore
 * the 'conn' argument.
 * recommended actions:
 *   - read superblock
 *   - allocate memory, read bitmaps and inodes
 */
void* fs_init(struct fuse_conn_info *conn)
{
	/* read the super block */
    int block_num_to_read = 0;
    if (disk->ops->read(disk, block_num_to_read, 1, &sb) < 0)
        exit(1);
    sb.magic = FS7600_MAGIC;
    
    /* Allocate memory for the inode and block bitmaps 
     * and store the read bitmaps from disk */ 
    inode_map = malloc(sb.inode_map_sz * FS_BLOCK_SIZE);
    block_map = malloc(sb.block_map_sz * FS_BLOCK_SIZE);
     
    block_num_to_read += 1;
    /* read the inode bitmap */
    if (disk->ops->read(disk, block_num_to_read, sb.inode_map_sz, 
		inode_map) < 0)
        exit(1);
    
    /* read the block bitmap */
    block_num_to_read += sb.inode_map_sz;
    if (disk->ops->read(disk, block_num_to_read, sb.block_map_sz, 
		block_map) < 0)
        exit(1);
    
    /* read the inodes */	
    block_num_to_read += sb.block_map_sz;
    if (disk->ops->read(disk, block_num_to_read, sb.inode_region_sz, 
		inodes_list) < 0)
        exit(1);
     
    if (homework_part > 3)
        disk = cache_create(disk);

    return NULL;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path is not present.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */

/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - fields not provided in CS5600fs are:
 *    st_nlink - always set to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * errors - path translation, ENOENT
 */
static int fs_getattr(const char *path, struct stat *sb)
{
	int inode_number;
	struct fs7600_inode* inode;
	int type;
	/* translate the path to the inode_number, if possible */
	inode_number = fs_translate_path_to_inum(path, &type);
	/* if path translation returned an error, then return the error */
	if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR)) {
		fflush(stdout);fflush(stderr);
		return inode_number; 
	}
	
	/* get the inode from the inode number */
	inode = get_inode_from_inum(inode_number);
	
	/* initialize all the entries of sb to 0 */
	memset(sb, 0, sizeof(sb));
	sb->st_uid = inode->uid;
	sb->st_gid = inode->gid;
	sb->st_mode = inode->mode;
	sb->st_ctime = inode->ctime;
	sb->st_mtime = inode->mtime;
	sb->st_size = inode->size;
    return SUCCESS;
}

/* 
 * readdir - get directory contents.
 *
 * for each entry in the directory, invoke the 'filler' function,
 * which is passed as a function pointer, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a struct stat, just like in getattr.
 *
 * Errors - path resolution, ENOTDIR, ENOENT
 */
static int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	int inode_number;
	int type, i;
	struct fs7600_dirent entry_list[32];
	struct stat sb;
	if (homework_part > 1) {
		inode_number = fi->fh;
	}
	else {
		/* translate the path to the inode_number, if possible */
		inode_number = fs_translate_path_to_inum(path, &type);
		/* If path translation returned an error, then 
		 * return the error */
		if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR))
			return inode_number;
			
		/* If the path given is not for a directory, return error */
		if (type != IS_DIR) 
			return -ENOTDIR;
	}
	/* If we have NOT errored out above then get the 
	 * directory entries for this directory */
	if (get_dir_entries_from_dir_inum(inode_number, entry_list) 
														!= SUCCESS)
		return -ENOENT;
	/* Fill the stat buffer using the filler */
	for (i = 0; i < 32; i++) {
		
		if ((entry_list[i].name != NULL) && (entry_list[i].valid == 1))
			filler(ptr, entry_list[i].name, &sb, 0);
	}
    return SUCCESS;
}

/* 
 * see description of Part 2. In particular, you can save information 
 * in fi->fh. If you allocate memory, free it in fs_releasedir.
 */
static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
	//printf("\n DEBUG : fs_opendir function called ");fflush(stdout);
	/* for part > 1 of homework, with path caching */
	if (homework_part > 1) {
		int inode_number = -1;
		int type;
		/* get the entry from path_cache if it exists */
		inode_number = fetch_entry_from_path_cache(path);
		if (inode_number < 0) {
			/* entry does not exist in path_cache */
			/* translate the path to inode_number */
			inode_number = fs_translate_path_to_inum(path, &type);
			if (inode_number < 0) {
				/* error */
				return inode_number;
			}
			/* If the path given is not for a directory, return error */
			if (type != IS_DIR) 
				return -ENOTDIR;
			/* add the entry to path_cache */
			add_path_to_cache(path, inode_number);
		}
		/* save the inode_number to fuse_file_info structure */
		fi->fh = (uint64_t)inode_number;
	}
	return SUCCESS;
}

static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

/* 
 * entry_exists_in_directory : int, char*, int*, int* -> int
 * Checks in the directory entries for the inode number dir_num
 * whether entry exists in it, and sets the type and inode number 
 * for it. 
 * */
int entry_exists_in_directory(int dir_inum, char* entry, 
						int* ftype, int* inode) {
	struct fs7600_dirent entry_list[32];
	int found = 0, i;
	int entry_inode = -1;
	/* if directory entry caching is enabled */
	if (homework_part > 2) {
		/* fetch the entry from cache */
		entry_inode = fetch_entry_from_dir_entry_cache(dir_inum, 
													entry, ftype);
		if (entry_inode > 0) {
			/* entry exists */
			found = 1;
			*inode = entry_inode;
		}
	}
	if (found == 0) {
		/* meaning that entry not found in cache, or no caching */
		/* read the listing of files from the parent directory */
		get_dir_entries_from_dir_inum(dir_inum, &(entry_list[0]));
		/* Check if entry is present in parent directory entry list */
		for (i = 0; i < 32; i++) {
			if ((entry_list[i].valid == 1) &&
			 (strcmp(entry, entry_list[i].name) == 0)) {
				*ftype = entry_list[i].isDir;
				*inode = entry_list[i].inode;
				found = 1;
				break;
			}
		}
		if ((homework_part > 2) && (found == 1)) {
			/* caching is enabled, but entry not present in cache, 
			 * thus need to add to cache in LRU, for a valid entry */
			add_dir_entry_to_cache(dir_inum, entry, *inode, *ftype);
		}
	}
	return found;
}

/* 
 * mknod - create a new file with permissions (mode & 01777)
 *
 * Errors - path resolution, EEXIST
 *          in particular, for mknod("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If this would result in >32 entries in a directory, return -ENOSPC
 * if !S_ISREG(mode) return -EINVAL [i.e. 'mode' specifies a device special
 * file or other non-file object]
 */
static int fs_mknod(const char *path, mode_t mode, dev_t dev)
{
	char* new_dir_name = NULL;
	int new_inum = 0;
	/* validate path and get the parent directory inode number */
	int parent_inum = validate_path_for_new_create(path, &new_dir_name);
	if (parent_inum < 0) {
		/* error */
		return parent_inum;
	}
	/* initialize a new file inode */
	new_inum = initialize_new_inode_and_block(mode | S_IFREG);
	return add_entry_to_dir_inode(parent_inum, new_dir_name, 
									new_inum, IS_FILE);
}

/* 
 * mkdir - create a directory with the given mode.
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create. 
 * If this would result in >32 entries in a directory, return -ENOSPC
 *
 * Note that you may want to combine the logic of fs_mknod and
 * fs_mkdir. 
 */ 
static int fs_mkdir(const char *path, mode_t mode)
{
	char* new_dir_name = NULL;
	int new_inum = 0;
	/* validate path and get the parent directory inode number */
	int parent_inum = validate_path_for_new_create(path, &new_dir_name);
	if (parent_inum < 0) {
		/* error */
		return parent_inum;
	}
	/* initialize a new dir inode */
	new_inum = initialize_new_inode_and_block(mode | S_IFDIR);
	return add_entry_to_dir_inode(parent_inum, new_dir_name, 
									new_inum, IS_DIR);
}

/* 
 * validate_path_for_new_create : const char *, char** -> int
 * Validates the path till the penultimate element for new
 * file/directory creation.
 * Returns the parent dir inode number on success, otherwise
 * returns error. 
 * */
int validate_path_for_new_create(const char *path, char** new_name) {
	int type, parent_inum, existing_inode_number, found = 0;
	parent_inum = validate_path_till_penultimate(path, new_name);
	if (parent_inum < 0) 
		return parent_inum;
	found = entry_exists_in_directory(parent_inum, *new_name, 
						&type, &existing_inode_number);
	if (found == 1) {
		/* The directory/file to be created already exists, so error */
		return -EEXIST;
	}
	return parent_inum;
}

/* 
 * initialize_new_inode_and_block : mode_t -> int
 * Creates a new inode and allocates a block to it.
 * Returns the inode number of the new flie/directory. 
 * */
int initialize_new_inode_and_block(mode_t mode) {
	struct fs7600_inode* inode_ptr;
	struct fs7600_dirent entry_list[32];
	/* get a free inode number */
	int inode_number = get_free_inode_number();
	/* get a free block number */
	int block_number = get_free_block_number();
	if ((inode_number <= 0) || (block_number <= 0)) {
		/* Error: no free inode number, no space 
		 * No free blocks */
		return -ENOSPC;
	}
	/* Set the bit in inode map to mark as allocated */
	set_inode_number(inode_number);
	/* Set the bit in block map to mark as allocated */
	set_block_number(block_number);
	/* fetch the inode from inode number */
	inode_ptr = get_inode_from_inum(inode_number);
	/* set its attributes */
	inode_ptr->uid = getuid();
	inode_ptr->gid = getgid();
	inode_ptr->mode = mode;
	if (S_ISREG(mode)) {
		inode_ptr->size = 0;
	}
	else {
		inode_ptr->size = FS_BLOCK_SIZE;
	}
	inode_ptr->ctime = time(NULL);
	inode_ptr->mtime = time(NULL);
	inode_ptr->direct[0] = (uint32_t) block_number;
	inode_ptr->direct[1] = 0;
	inode_ptr->direct[2] = 0;
	inode_ptr->direct[3] = 0;
	inode_ptr->direct[4] = 0;
	inode_ptr->direct[5] = 0;
	inode_ptr->indir_1 = 0;
	inode_ptr->indir_2 = 0;
	/* write the inode block for this new inode number to disk */
	write_block_to_inode_region(inode_number);
	/* Return the inode number of the new directory/file */
	return inode_number;
}

/* 
 * add_entry_to_dir_inode : int, char*, int, int -> int
 * Adds an entry for name and inum in the entries set for 
 * directory with inode number parent_dir_inum, based on isDir.
 * Writes this modified directory block to disk.
 * Returns -ENOSPC if all entries are full 
 * */
int add_entry_to_dir_inode(int parent_dir_inum, char* name, 
							int inum, int isDir) {
	struct fs7600_dirent entry_list[32];
	int i, found = 0;
	get_dir_entries_from_dir_inum(parent_dir_inum, entry_list);
	/* Check if entry is present in parent directory entry list */
	for (i = 0; i < 32; i++) {
		if (entry_list[i].valid == 0) {
			/* mark as valid */
			entry_list[i].valid = 1;
			/* set for directory */
			entry_list[i].isDir = isDir;
			/* the inode number for directory/file */
			entry_list[i].inode = (uint32_t) inum;
			/* the name of directory */
			memcpy(&entry_list[i].name, name, 
								sizeof(entry_list[i].name));
			found = 1;
			break;
		}
	}
	if (found == 0) {
		/* No invalid entries found, directory full, Error */
		return -ENOSPC;
	}
	/* write the changed entries to disk */
	if (write_dir_entries_to_disk_for_dir_inum(parent_dir_inum, entry_list)
					!= SUCCESS)
		exit(1);
	if (homework_part > 2) {
		/* add the new entry to directory entry cache */
		add_dir_entry_to_cache(parent_dir_inum, name, inum, isDir);
	}
	/* return success */
	return SUCCESS;
}

/* 
 * truncate - truncate file to exactly 'len' bytes
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
static int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    int inum;
    struct fs7600_inode *inode = NULL;
    char emptBuf[FS_BLOCK_SIZE];
    int numOfBlocks;
    int *blocksList = NULL;
    int type = 0;
    int i;
    if (len != 0)
			return -EINVAL;		/* invalid argument */
    strcpy(emptBuf, ""); 
    inum = fs_translate_path_to_inum(path, &type);
    if(inum < 0)
		return inum;
	inode = get_inode_from_inum(inum);
    blocksList = getListOfBlocksOperate(inode, 
							inode->size, 0, &numOfBlocks);
    for(i=1; i<numOfBlocks; i++)
	{
			clear_block_number(blocksList[i]);
	}
	for (i =1; i < N_DIRECT; i++)
		inode->direct[i] = 0;
	inode->indir_1 = 0;
	inode->indir_2 = 0;
	inode->size = 0;
	write_block_to_inode_region(inum);
    return SUCCESS;
}


/* 
 * unlink - delete a file
 *  Errors - path resolution, ENOENT, EISDIR
 * Note that you have to delete (i.e. truncate) all the data.
 */
static int fs_unlink(const char *path)
{
	int type, inum, numofBlocksInFile, parent_inum,i;
	int* blocklist = NULL;
	char *to_remove = NULL;
	struct fs7600_inode *inode = NULL;
	inum  = fs_translate_path_to_inum(path, &type);
	if (inum < 0) {
		/* error */
		return inum;
	}
	if (type == IS_DIR)
		return -EISDIR;
	inode = get_inode_from_inum(inum);
	// unset all blocks for the inode
	blocklist = getListOfBlocksOperate(inode, inode->size,
					0, &numofBlocksInFile);
	for(i=0; i<numofBlocksInFile; i++) 
		clear_block_number(blocklist[i]);
	// remove inode entry from parent directory
	parent_inum = validate_path_till_penultimate(path, &to_remove);
	if(parent_inum < 0)
		return parent_inum;
	remove_entry_from_dir_inode(parent_inum, to_remove);
	// unset inode 
	clear_inode_number(inum);
    return SUCCESS;
}

/*
 * validate_path_till_penultimate : char*, char** -> int
 * validates the path till the second last entry, and sets 
 * last_token to the last value.
 * */
int validate_path_till_penultimate(const char* path, char** last_token) {
	int type, parent_inum;
	const char* stripped_path = remove_last_token(path, last_token);
	/* get the inode number of the directory/file where to create the 
	 * new directory from the stripped path */
	parent_inum = fs_translate_path_to_inum(stripped_path, &type);
	if(type == IS_FILE) {
		/* the penultimate token is not a directory */
		return -ENOTDIR;
	}
	return parent_inum;
}

/* 
 * validate_path_for_remove : const char *, char** -> int
 * Validates the path till the penultimate element for 
 * directory removal.
 * Returns the parent dir inode number on success, otherwise
 * returns error. 
 * */
int validate_path_for_remove(const char *path, char** to_remove, 
										int* remove_dir_inum) {

	int type, parent_inum, found = 0;
	parent_inum = validate_path_till_penultimate(path, to_remove);
	if (parent_inum < 0) 
		return parent_inum;
	found = entry_exists_in_directory(parent_inum, *to_remove, 
						&type, remove_dir_inum);
	if (found == 0) {
		/* The directory to remove does not exist, so error */
		return -ENOENT;
	}
	return parent_inum;
}

/* 
 * remove_dir_is_empty : int -> int
 * Returns 1 if the directory with inode number dir_inum 
 * is empty, otherwise 0. 
 * */
int remove_dir_is_empty(int dir_inum) {
	struct fs7600_dirent entry_list[32];
	int empty = 1, i;
	/* read the listing of files from the directory */
	get_dir_entries_from_dir_inum(dir_inum, &(entry_list[0]));
	/* Check if a valid entry is present in directory entry list */
	for (i = 0; i < 32; i++) {
		if (entry_list[i].valid == 1) {
			empty = 0;
			break;
		}
	}
	return empty;
}

/*
 * remove_entry_from_dir_inode : int, char* -> int
 * Unsets the entry for name in directory with inode number
 * parent_dir_inum if found, otherwise returns -ENOENT.
 * Writes the changed block to disk
 * Optionally, for directory entry cache, invalidates the 
 * entry in the cache.
 * */
int remove_entry_from_dir_inode(int parent_dir_inum, char* name) {
	struct fs7600_dirent entry_list[32];
	int i, found = 0;
	get_dir_entries_from_dir_inum(parent_dir_inum, entry_list);
	/* Check if entry is present in parent directory entry list */
	for (i = 0; i < 32; i++) {
		if ((entry_list[i].valid == 1) &&
		 (strcmp(name, entry_list[i].name) == 0)) {
			/* mark as invalid */
			entry_list[i].valid = 0;
			found = 1;
			break;
		}
	}
	if (found == 0) {
		/* Entry not found, Error */
		return -ENOENT;
	}
	/* write the modified entry list for the directory to disk */
	if(write_dir_entries_to_disk_for_dir_inum(parent_dir_inum, 
								entry_list) != SUCCESS)
		exit(1);
	if (homework_part > 2) {
		/* caching is enabled, so need to invalidate the entry
		 * if present in cache, in LRU method */
		invalidate_entry_in_dir_cache(parent_dir_inum, name);
	}										 
	return SUCCESS;
}

/* 
 * rmdir - remove a directory
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
static int fs_rmdir(const char *path)
{
	char* to_remove_dir = NULL;
	int remove_dir_inum = -1;
	/* validate path and get the parent directory inode number */
	int parent_inum = validate_path_for_remove(path, 
							&to_remove_dir, &remove_dir_inum);
	if (parent_inum < 0) {
		/* error */
		return parent_inum;
	}
	/* Check whether the directory to remove is empty */
	if (remove_dir_is_empty(remove_dir_inum) != 1) {
		/* the directory to remove is not empty. ERROR */
		return -ENOTEMPTY;
	}
	/* Remove the entry for this directory 
	 * from its parent directory */
	return remove_entry_from_dir_inode(parent_inum, to_remove_dir);
}

/* 
 * validate_path_for_rename : char*, char*, char**, char** -> int
 * Returns the source directory parent inode number after 
 * validating source and dest parent dir to be same, both
 * being valid paths, and src exists, and dest does not exist 
 * */
int validate_path_for_rename(const char *src_path, 
	const char *dest_path, char** old_name, char** new_name) {
	int type, src_parent_inum, found = 0, dest_parent_inum;
	int rename_dir_inum;
	/* check whether src path is valid till penultimate */
	src_parent_inum = 
			validate_path_till_penultimate(src_path, old_name);
	if (src_parent_inum < 0) 
		return src_parent_inum;
	/* check whether dest path is valid till penultimate */
	dest_parent_inum = 
			validate_path_till_penultimate(dest_path, new_name);
	if (dest_parent_inum < 0) 
		return dest_parent_inum;
	/* check whether src path and dest path 
	 * point to the same location */
	if (src_parent_inum != dest_parent_inum)
		return -EINVAL;
	/* check whether src file/dir does not exist */
	found = entry_exists_in_directory(src_parent_inum, *old_name, 
						&type, &rename_dir_inum);
	if (found == 0) {
		/* The src directory/file to rename doesn't exist, so error */
		return -ENOENT;
	}
	/* check whether dest file/dir already exists */
	found = 1;
	found = entry_exists_in_directory(dest_parent_inum, *new_name, 
						&type, &rename_dir_inum);
	if (found == 1) {
		/* The dest directory/file for rename exists, so error */
		return -EEXIST;
	}
	/* return the src parent inode number */
	return src_parent_inum;
}

/* 
 * update_entry_in_dir_inode : int, char*, char* -> int
 * Updates the entry for old_name to new_name in entries list
 * for parent_dir_inum inode number of directory, and writes
 * to disk. 
 * */
int update_entry_in_dir_inode(int parent_dir_inum, char* old_name,
	char* new_name) {
	struct fs7600_dirent entry_list[32];
	int i, found = 0;
	get_dir_entries_from_dir_inum(parent_dir_inum, entry_list);
	/* Check if entry is present in parent directory entry list */
	for (i = 0; i < 32; i++) {
		if ((entry_list[i].valid == 1) &&
		 (strcmp(old_name, entry_list[i].name) == 0)) {
			/* update the name field to the new value */
			memcpy(&entry_list[i].name, new_name, 
								sizeof(entry_list[i].name));
			found = 1;
			break;
		}
	}
	if (found == 0) {
		/* Entry not found, Error */
		return -ENOENT;
	}
	/* write the changes to disk */
	if(write_dir_entries_to_disk_for_dir_inum(parent_dir_inum, 
								entry_list) != SUCCESS)
		exit(1);
	/* update the entry in directory entry cache is enabled */
	if ((homework_part > 2) && (found > 0)) {
		/* invalidate the old entry */
		invalidate_entry_in_dir_cache(parent_dir_inum, old_name);
		/* add the new entry to cache */
		add_dir_entry_to_cache(parent_dir_inum, new_name, 
		entry_list[i].inode, entry_list[i].isDir);
	}
	/* return success */
	return SUCCESS;										
}

/* 
 * rename - rename a file or directory
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
static int fs_rename(const char *src_path, const char *dst_path)
{
	char* old_name = NULL;
	char* new_name = NULL;
	int src_parent_inum = validate_path_for_rename(src_path, 
	dst_path, &old_name, &new_name);
	if (src_parent_inum < 0) {
		/* error */
		return src_parent_inum;
	}
	/* Update the entry for src dir/file in its parent directory
	 * to the dest file/dir name */
	return update_entry_in_dir_inode(src_parent_inum, 
										old_name, new_name);
}

/* 
 * chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * Errors - path resolution, ENOENT.
 */
static int fs_chmod(const char *path, mode_t mode)
{
	struct fs7600_inode* inode;
	int type;
	/* fetch the inode number from the path */
	int inum = fs_translate_path_to_inum(path, &type);
	if (inum < 0) {
		/* return error */
		return inum;
	}
	/* Get the inode from the inode number */
	inode = get_inode_from_inum(inum);
	/* Set the mode */
	inode->mode = mode;
	/* Write the modified inode block to disk */
	return write_block_to_inode_region(inum);
}

int fs_utime(const char *path, struct utimbuf *ut)
{
    struct fs7600_inode* inode;
	int type;
	/* fetch the inode number from the path */
	int inum = fs_translate_path_to_inum(path, &type);
	if (inum < 0) {
		/* return error */
		return inum;
	}
	/* Get the inode from the inode number */
	inode = get_inode_from_inum(inum);
	/* Set the ctime and mtime */
	inode->ctime = ut->actime;
	inode->mtime = ut->modtime;
	/* Write the modified inode block to disk */
    return write_block_to_inode_region(inum);
}

/* 
 * getListOfBlocksOperate : indoe len(max. file size) offset 
 * Returns an integer pointer to list of blcoks in the file 
 * starting at given offset and accumulating given length
 * */
int* getListOfBlocksOperate(struct fs7600_inode *inode, int len, 
				off_t offset, int *numOfBlocksToread) {

	int filesize, startBlockInd, sizeInBlock01, numBlocks2Read;
	int sizeOfReadList;
	int *readBlocksList = NULL;
	int indir1blocks[256];
	int i = 0;
	int j = 0;
	filesize = inode->size;
	startBlockInd = (int) (offset / FS_BLOCK_SIZE);
	sizeInBlock01 = (offset % FS_BLOCK_SIZE);
	sizeOfReadList = numBlocks2Read = 
		(int)((len - sizeInBlock01)/FS_BLOCK_SIZE) + 1;
	readBlocksList = (int*)malloc(numBlocks2Read * sizeof(int));
	j = 0;
	i = startBlockInd;	//current index of reading from blocks
	while(numBlocks2Read > 0) {	
		// 1. Direct Pointer area reading
		if (i < 6) {
			readBlocksList[j] = inode->direct[i];
			numBlocks2Read = numBlocks2Read - 1;
			j = j + 1;
		}	
		// 2. First indirection pointer reading
		else if ((i > 6) && (i < 256)) {
		
			if (disk->ops->read(disk, inode->indir_1, 1, 
					&indir1blocks) < 0)
				exit(1);
			
			if(numBlocks2Read > 256) {
				memcpy(readBlocksList + j, indir1blocks, 
							256 * sizeof(int));
				numBlocks2Read = numBlocks2Read - 256;
				i = i + 256;
				j = j + 256;
			} else {
				memcpy(readBlocksList + j, indir1blocks, 
							numBlocks2Read * sizeof(int));
				i = i + numBlocks2Read;
				j = j + numBlocks2Read;
				numBlocks2Read = 0;
			}
		}	
		// 3. Second level indirection pointer reading
		else {
			int tempBlocksList[256];
			int tempBlocksList2[256];
			if (disk->ops->read(disk, inode->indir_2, 1, 
						&tempBlocksList) < 0)
				exit(1);
			for(i = 0; i < 256 ; i++)
			{
				if (disk->ops->read(disk, tempBlocksList[i], 1, 
							&tempBlocksList2) < 0)
					exit(1);
			}
			if(numBlocks2Read > 256) {
				memcpy(readBlocksList + j, tempBlocksList2, 
							256 * sizeof(int));
				numBlocks2Read = numBlocks2Read - 256;
				j = j + 256;
				i = i + 256;
			} else {
				memcpy(readBlocksList + j, indir1blocks, 
							numBlocks2Read * sizeof(int));
				i = i + numBlocks2Read;
				j = j + numBlocks2Read;
				numBlocks2Read = 0;
			}			
		}
	}
	*numOfBlocksToread = sizeOfReadList;
	return readBlocksList;
}

/* 
 * read - read data from an open file.
 * should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return bytes from offset to EOF
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
static int fs_read(const char *path, char *buf, size_t len, off_t offset,
		    struct fuse_file_info *fi)
{
	int type; /* to check the type of file */
	int inode_num; /* to hold the inode number of the file */
	int file_size; /* to hold the size of a file on disk */
	int bytes_read = 0;
	struct fs7600_inode *inode = NULL;
	char* tempbuf = NULL;
	/* usng the path translation cache */
	if (homework_part > 1) {
		inode_num = fi->fh;
	}
	else {
		inode_num = fs_translate_path_to_inum(path, &type);
		strcpy(buf, "");
		if (inode_num < 0) {
			/* error */
			return inode_num;
		}
		if (type == IS_DIR) {
				/* trying to read a directory 
				 * ** ERROR ** */
				return -EISDIR;
		}
	}
	/* get the inode for the file */
	inode = get_inode_from_inum(inode_num);
	/* size of file */
	if (offset >= inode->size) {
		/* Trying to read beyond the file size 
		 * No support for holes in file */
		return 0;
	}
	else if((offset + len) > inode->size) {
		/* to read from offset till the end of file */
		len = inode->size - offset;
	}
	bytes_read = read_bytes_from_disk(inode_num, buf, len, offset);
	return bytes_read;
}

/*
 * read_bytes_from_disk : int, char*, int, off_t -> int
 * Reads len number of bytes from disk and returns it.
 * */
int read_bytes_from_disk(int inode_num, char *buf, 
				int len, off_t offset) 
{
	int block_index = get_starting_block_index(offset);
	int start_block_index = block_index, i = 0, j = 0;
	char local_buf[FS_BLOCK_SIZE] = {'\0'};
	/* read location is starting block */
	int start_write_loc = get_starting_block_write_location(offset);
	int block_num = 0, k;
	struct fs7600_inode *inode = NULL;
	inode = get_inode_from_inum(inode_num);
	/* get the block number to start */
	block_num = get_blocknum_from_blkindex(block_index, inode_num);
	/* read the first block */
	if (disk->ops->read(disk, block_num, 1, local_buf) < 0)
		exit(1);
	/* filling local buf from buf and writing to disk */
	i = start_write_loc; /* the index of local buf */
	j = 0; /* the index of buf */
	while (j < len) {
		buf[j] = local_buf[i];
		j++;
		i++;
		if ((i % FS_BLOCK_SIZE) == 0) /* 1 page size data is ready */
		{
			/* write local_buf to disk */			
			block_index++;
			/* clear the local buf */
			for (k = 0; k < FS_BLOCK_SIZE; k++)
				local_buf[k] = '\0';
			/* get the next block num to write to */
			block_num = get_blocknum_from_blkindex(block_index, inode_num);
			if (disk->ops->read(disk, block_num, 1, local_buf) < 0)
				exit(1);
			i = 0; /* reset the index to 0 */
		}
	}
	buf[j] = '\0';
	return len;
}

/*
 * get_starting_block_index : onn_t -> int
 * Returns the starting index of block based on offest.
 * */
int get_starting_block_index(off_t offset) {
	int start_index = (int)offset / FS_BLOCK_SIZE;
	return start_index;
}

/*
 * get_starting_block_write_location : off_t -> int
 * Returns the starting index in start block to start read/write.
 * */
int get_starting_block_write_location(off_t offset) {
	int start_location = (int)offset % FS_BLOCK_SIZE;
	return start_location;
}

/*
 * get_blocknum_from_blkindex : int, int -> int
 * Returns the next block number to read/write based on the 
 * blkindex.
 * */
int get_blocknum_from_blkindex(int blkindex, int inum) {
	struct fs7600_inode *inode = NULL;
	int direct_count = 6;
	char level[2] = {'\0'};
	int indir_1_count = 256;
	int indir_2_count = (256 * 256);
	int rel_index, level_block_num_to_allocate = 0, rel_2_index;
	int level_1_block, level1_index;
	int block_num;
	int indir1blocks[256] = {0};
	int indir2blocks[256] = {0};
	inode = get_inode_from_inum(inum);
	
	if (blkindex < direct_count) {
		rel_index = blkindex;
		block_num = inode->direct[rel_index];
		level[0] = 'D';
	}
	else if (blkindex < (indir_1_count + direct_count)) {
		level[0] = '1';  /* indicating indirect level 1 */
		/* read the array of 256 entries if block for 
		 * indir1 is already allocated */
		if (inode->indir_1 > 0) {
			if (disk->ops->read(disk, inode->indir_1, 1, 
						&indir1blocks) < 0)
				exit(1);
		}
		else {
			/* allocate block for indir1 level */
			level_block_num_to_allocate = 0;
			level_block_num_to_allocate = get_free_block_number();
			if (level_block_num_to_allocate > 0) /* free block found */
			{
				inode->indir_1 = level_block_num_to_allocate;
				set_block_number(level_block_num_to_allocate);
			}
		}
		/* to index into indirect block count */
		rel_index = blkindex - direct_count;
		block_num = indir1blocks[rel_index];
	}
	else {
		level[0] = '2';
		if (inode->indir_2 > 0) {
			/* already allocated indir2 */
			if (disk->ops->read(disk, inode->indir_2, 1,
								&indir2blocks) < 0)
				exit(1);
		}
		else {
			/* allocate block for indir2 level */
			level_block_num_to_allocate = 0;
			level_block_num_to_allocate = get_free_block_number();
			if (level_block_num_to_allocate > 0) /* free block found */
			{
				inode->indir_2 = level_block_num_to_allocate;
				set_block_number(level_block_num_to_allocate);
			}
		}
		/* to index into indirect block count */
		rel_2_index = blkindex - (direct_count + indir_1_count);
		/* index to page for actual block */
		level1_index = (int)rel_2_index / 256;
		if (rel_2_index == 0) {
			rel_index = 0;
		}
		else 
			rel_index = rel_2_index % 256;  /* index to actual block */
		//block_index -= (direct_count + indir_1_count);
		level_1_block = indir2blocks[level1_index];
		if (level_1_block == 0) {
			/* allocate the first block in level 2 of indir2 */
			level_block_num_to_allocate = 0;
			level_block_num_to_allocate = get_free_block_number();
			if (level_block_num_to_allocate > 0) /* free block found */
			{
				indir2blocks[level1_index] = level_block_num_to_allocate;
				set_block_number(level_block_num_to_allocate);
			}
		}
		else {
			/* level 1 already present */
			if (disk->ops->read(disk, level_1_block, 1, 
						&indir1blocks) < 0)
				exit(1);
		}
		block_num = indir1blocks[rel_index];
	}
	if (block_num <= 0) /* need to allocate the block */
	{	
		level_block_num_to_allocate = 0;
		level_block_num_to_allocate = get_free_block_number();
		if (level_block_num_to_allocate > 0) /* free block found */
		{
			block_num = level_block_num_to_allocate;
			set_block_number(level_block_num_to_allocate);
			if (level[0] == 'D') {
				inode->direct[rel_index] = block_num;
			}
			else if (level[0] == '1') {
				indir1blocks[rel_index] = block_num;
				if (disk->ops->write(disk, inode->indir_1, 1, 
							&indir1blocks) < 0)
					exit(1);
			}
			else {
				/* need to write for indir 2 lower level */
				indir1blocks[rel_index] = block_num;
				if (disk->ops->write(disk, level_1_block, 1, 
							&indir1blocks) < 0)
					exit(1);
				/* need to write for indir 2 upper level */
				if (disk->ops->write(disk, inode->indir_2, 1, 
							&indir2blocks) < 0)
					exit(1);
			}
		}
	}
	write_block_to_inode_region(inum);
	return block_num;
}

/* 
 * write_bytes_to_disk : int, char*, size_t, off_t -> int 
 * Writes the data in buf of size len, from offset to file with 
 * inode* as given. 
 * Returns the number of bytes written to disk 
 * */
int write_bytes_to_disk(int inode_num, 
			const char *buf, size_t len, off_t offset) {
	/* starting block index to start writing in file */
	int block_index = get_starting_block_index(offset);
	int start_block_index = block_index, i = 0, j = 0;
	char local_buf[FS_BLOCK_SIZE] = {'\0'};
	char last_buf[FS_BLOCK_SIZE] = {'\0'};
	/* write location is starting block */
	int start_write_loc = get_starting_block_write_location(offset);
	int block_num = 0, k;
	struct fs7600_inode *inode = NULL;
	inode = get_inode_from_inum(inode_num);

	/* get the block number to start */
	block_num = get_blocknum_from_blkindex(block_index, inode_num);
	/* read the first block */
	if (disk->ops->read(disk, block_num, 1, local_buf) < 0)
		exit(1);
	
	/* filling local buf from buf and writing to disk */
	i = start_write_loc; /* the index of local buf */
	j = 0; /* the index of buf */
	while (j < len) {
		local_buf[i] = buf[j];
		j++;
		i++;
		if ((i % FS_BLOCK_SIZE) == 0) /* 1 page size data is ready */
		{
			/* write local_buf to disk */			
			if (disk->ops->write(disk, block_num, 1, local_buf) < 0)
				exit(1);
			block_index++;
			/* get the next block num to write to */
			block_num = get_blocknum_from_blkindex(block_index, 
						inode_num);
			i = 0; /* reset the index to 0 */
			/* clear the local buf */
			for (k = 0; k < FS_BLOCK_SIZE; k++)
				local_buf[k] = '\0';
		}
	}
	/* read the last block to write to */
	if (disk->ops->read(disk, block_num, 1, last_buf) < 0)
		exit(1);
	memcpy(last_buf, local_buf, strlen(local_buf));
	/* write the last block to disk */
	if (disk->ops->write(disk, block_num, 1, last_buf) < 0)
		exit(1);
	/* update the size of file */
	inode->size += len;
	/* write the inode */
	write_block_to_inode_region(inode_num);
	return len;
}

/* 
 * write - write data to a file
 * It should return exactly the number of bytes requested, except on
 * error.
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them, 
 *   but we don't)
 */ 
static int fs_write(const char *path, const char *buf, size_t len,
		     off_t offset, struct fuse_file_info *fi)
{
	
	struct fs7600_inode *inode = NULL;
	int i, type, inode_num;
	int bytes_written = 0;
	inode_num = fs_translate_path_to_inum(path, &type);
	if (inode_num < 0) {
		/* error */
		return inode_num;
	}
	if (type == IS_DIR) {
			/* trying to write a directory 
			 * ** ERROR ** */
			return -EISDIR;
	}
	/* get the inode for the file */
	inode = get_inode_from_inum(inode_num);
	/* size of file */
	if (offset > inode->size) {
		/* Trying to write beyond the file size 
		 * No support for holes in file */
		return -EINVAL;
	}
	bytes_written = write_bytes_to_disk(inode_num, buf, len, offset);
	return bytes_written;
}

//---------------------------------------------------------------------
/*
 * fs_open : char*, fuse_file_info -> int
 * implements path traslation cache.
 * */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
	/* for part > 1 of homework, with path caching */
	if (homework_part > 1) {
		int inode_number = -1;
		int type;
		/* get the entry from path_cache if it exists */
		inode_number = fetch_entry_from_path_cache(path);
		if (inode_number < 0) {
			/* entry does not exist in path_cache */
			/* translate the path to inode_number */
			inode_number = fs_translate_path_to_inum(path, &type);
			if (inode_number < 0) {
				/* error */
				return inode_number;
			}
			if (type == IS_DIR) {
			/* trying to read a directory 
			 * ** ERROR ** */
			return -EISDIR;
			}
			/* add the netry to path_cache */
			add_path_to_cache(path, inode_number);
		}
		/* save the inode_number to fuse_file_info structure */
		fi->fh = (uint64_t)inode_number;
	}
    return SUCCESS;
}

static int fs_release(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

/* 
 * get_free_block_count : int -> int
 * Returns the number of blocks which are free in the file system 
 * */
int get_free_block_count(int start_block) {
	int block_count = sizeof(*block_map);
	int free_count = 0;
	int temp_block = start_block;
	while (temp_block < block_count) {
		if (!(FD_ISSET(temp_block, block_map)))
		{
			free_count += 1;
		}
		temp_block ++;
	}
	return free_count;
}

/* 
 * statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. 
 */
static int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - metadata
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namelen = <whatever your max namelength is>
     *
     * this should work fine, but you may want to add code to
     * calculate the correct values later.
     */
    int data_blocks_start_block_number = 
		get_inodes_region_starting_block() + sb.inode_region_sz;
    st->f_bsize = FS_BLOCK_SIZE;
    /* to get value of f_blocks = total image - metadata */
    st->f_blocks = sb.num_blocks - data_blocks_start_block_number + 1;
    /* f_bfree = f_blocks - blocks used */
    st->f_bfree = get_free_block_count(data_blocks_start_block_number);  
    /* f_bavail = f_bfree */      
    st->f_bavail = st->f_bfree;           
    st->f_namemax = 27;
    return SUCCESS;
}

/* 
 * operations vector. Please don't rename it, as the skeleton code in
 * misc.c assumes it is named 'fs_ops'.
 */
struct fuse_operations fs_ops = {
    .init = fs_init,
    .getattr = fs_getattr,
    .opendir = fs_opendir,
    .readdir = fs_readdir,
    .releasedir = fs_releasedir,
    .mknod = fs_mknod,
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .release = fs_release,
    .statfs = fs_statfs,
};

