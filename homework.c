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

#define SUCCESS 0
#define IS_DIR 1
#define IS_FILE 0
#define PATH_CACHE_SIZE 20
#define DIR_ENTRY_CACHE_SIZE 50


int* getListOfBlocksOperate(struct fs7600_inode *inode, int len, 
				off_t offset, int *numOfBlocksToread);
int current_access_count = 0;  /* for LRU in path_cache */
int directory_entry_cache_access = 0; 
/* for LRU in directory entry cache */
extern int homework_part; /* set by '-part n' command-line option */

/* 
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of 1024-byte blocks
 */
extern struct blkdev *disk;

/* structure to store the path translation maping to inode number 
 * This will represent the caching of path translations in disk */
struct path_cache {
	char* path;  /* path */
	int inum;    /* inode number */
	int last_access_time;	/* to save the last access time */
	int valid;    /* to indicate whether valid */
};

/* structure for the directory entry cache */
struct dir_entry_cache {
	int parent_inum; /* to store the parent inode number */
	char* child_name; /* to store the name of the entry in parent */
	int child_inum; /* to store the entry's inode number */
	int valid; /* to indicate whether valid */
	int last_access_time;  /* to save the last access time */
	int ftype;  /* the type of file */
};

struct path_cache path_cache_list[PATH_CACHE_SIZE];
struct dir_entry_cache dir_entry_cache_list[DIR_ENTRY_CACHE_SIZE];

/*
 * cache - you'll need to create a blkdev which "wraps" this one
 * and performs LRU caching with write-back.
 */
int cache_nops(struct blkdev *dev) 
{
    struct blkdev *d = dev->private;
    return d->ops->num_blocks(d);
}
int cache_read(struct blkdev *dev, int first, int n, void *buf)
{
    return SUCCESS;
}
int cache_write(struct blkdev *dev, int first, int n, void *buf)
{
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

/* by defining bitmaps as 'fd_set' pointers, you can use existing
 * macros to handle them. 
 *   FD_ISSET(##, inode_map);
 *   FD_CLR(##, block_map);
 *   FD_SET(##, block_map);
 */
fd_set *inode_map;  /* = malloc(sb.inode_map_size * FS_BLOCK_SIZE); */
fd_set *block_map;

struct fs7600_super sb;  /* to store the super block */

/* to store the inodes list */
/* the number of blocks upper limimted to 1024, can be increased */
struct fs7600_inode inodes_list[INODES_PER_BLK][INODES_PER_BLK];

/* get_inode_from_inum: int -> struct fs7600_inode*
 * Returns the pointer to the inode for the given inode_number */
struct fs7600_inode* get_inode_from_inum(int inode_number) {
	/* Get the block number which contains the inode entry */
	int disk_block = get_block_number_from_inum(inode_number);
	/* get the inode entry from the block */
	int disk_block_entry = get_bl_inode_number_from_inum(inode_number);
	/* Return the address of the entry */
	return &(inodes_list[disk_block][disk_block_entry]);
}

/* get_inodes_region_starting_block: -> int
 * Returns the starting block for the inodes region on disk. */
int get_inodes_region_starting_block() {
	return (sb.block_map_sz + get_block_map_starting_block());
}

/* get_inode_map_starting_block: -> int
 * Returns the starting block for the inodes map on disk. */
int get_inode_map_starting_block() {
	return 1;
}

/* get_block_map_starting_block: -> int
 * Returns the starting block for the blocks map on disk. */
int get_block_map_starting_block() {
	return (sb.inode_map_sz + get_inode_map_starting_block());
}

/* get_block_number_from_inum : int -> int
 * Returns the block number which contains this inode_number 
 * from the inodes region */
int get_block_number_from_inum(int inode_number) {
	return inode_number / INODES_PER_BLK;
}

/* write_block_to_inode_region : int -> int
 * Writes the block for this inode number to disk */
int write_block_to_inode_region(int inode_number) {
	/* get the block number for the inode_number */
	int blnum = get_block_number_from_inum(inode_number);
	/* get the actual corresponding block number on disk */
	int actual_disk_block = get_inodes_region_starting_block() + blnum;
	printf("\n DEBUG: Writing actual inode block = %d to disk \n", 
	actual_disk_block);
	/* write the block to disk */
	if (disk->ops->write(disk, actual_disk_block, 1, 
					&inodes_list[blnum]) < 0)
		/* error on write */
        exit(1);
    /* Return success */
    return SUCCESS;
}

/* get_bl_inode_number_from_inum; int -> int
 * Retuns the inode entry number in a block 
 * for the given inode number  */
int get_bl_inode_number_from_inum(int inode_number) {
	return inode_number % INODES_PER_BLK;
}

/* get_free_block_number: -> int
 * Returns a free block number, if possible, otherwise 0. */
int get_free_block_number() {
	int block_count = sizeof(*block_map);
	int free_entry = 0;
	while (free_entry < block_count) {
		if (!(FD_ISSET(free_entry, block_map)))
		{
			break;
		}
		free_entry ++;
	}
	return free_entry;
}

/* set_block_number : int -> int
 * Sets the bit block_num in block_map. */
int set_block_number(int block_num) {
	FD_SET(block_num, block_map);
	if (disk->ops->write(disk, get_block_map_starting_block(), 
					sb.block_map_sz, block_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* clear_block_number : int -> int
 * Clears the bit block_num in block_map. */
int clear_block_number(int block_num) {
	FD_CLR(block_num, block_map);
	if (disk->ops->write(disk, get_block_map_starting_block(), 
					sb.block_map_sz, block_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* get_free_inode_number: -> int
 * Returns a free inode number, if possible, otherwise 0. */
int get_free_inode_number() {
	int inode_count = sizeof(*inode_map);
	int free_entry = 0;
	while (free_entry < inode_count) {
		if (!(FD_ISSET(free_entry, inode_map)))
		{
			break;
		}
		free_entry ++;
	}
	return free_entry;
}

/* set_inode_number : int -> int
 * Sets the bit inum in inode_map. */
int set_inode_number(int inum) {
	FD_SET(inum, inode_map);
	if (disk->ops->write(disk, get_inode_map_starting_block(), 
					sb.inode_map_sz, inode_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* clear_inode_number : int -> int
 * Clears the bit inum in inode_map. */
int clear_inode_number(int inum) {
	FD_CLR(inum, inode_map);
	if (disk->ops->write(disk, get_inode_map_starting_block(), 
					sb.inode_map_sz, inode_map) < 0)
		/* error on write */
        exit(1);
	return SUCCESS;
}

/* remove_last_token : char*, char**  -> char*
 * Tokenizes the string path based on the separator "/" and
 * removes the last token and returns the string. Also,
 * sets the last token in last_token */
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

int print_dir_entry_cache() {
	int i;
	printf("\n DEBUG : PRINTING DIR ENTRY CACHE\n");
	for (i = 0; i < DIR_ENTRY_CACHE_SIZE; i++) {
		if (dir_entry_cache_list[i].valid == 1) {
			printf("\nCache entry: %d -- parent_inum = %d, Name = %s, child_inum = %d, valid = %d, last_access_time = %d", 
			i, dir_entry_cache_list[i].parent_inum, dir_entry_cache_list[i].child_name, 
			dir_entry_cache_list[i].child_inum, 
			dir_entry_cache_list[i].valid, dir_entry_cache_list[i].last_access_time);
		}
	}
	printf("\n DEBUG : PRINTING DIR ENTRY CACHE --- DONE!!!!!---- \n");
}

/* fetch_entry_from_dir_entry_cache : int, char*, int* -> int
 * Returns the mapped child inode number in cache for the directory 
 * entry of dir_inum for name if exists, else returns -1. */
int fetch_entry_from_dir_entry_cache(int dir_inum, char* name, int* ftype) {
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
								++directory_entry_cache_access;
			*ftype = dir_entry_cache_list[i].ftype;
			break;
		}
	}
	print_dir_entry_cache();
	return child_inode_number;
}

/* replace_dir_cache_entry : int, char*, int, int, int -> int
 * Replaces the entry in directory entry cache at index entry with
 * a mapping of dir_inum, name to child_inum */
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
							++directory_entry_cache_access;
	return SUCCESS;
}

/* add_dir_entry_to_cache : int, char*, int -> int
 * Adds the entry of [dir_inum, name] mapped to child_inum in the 
 * directory entry cache, after finding a free entry if it exists,
 * or replacing an existing entry by LRU. */
int add_dir_entry_to_cache(int dir_inum, char* name, 
								int child_inum, int ftype) {
	/* check for free entry in cache, if found, then put
	 * entry in that position
	 * if not there, then replace the entry with least
	 * access time */
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
	/* put the entry */
	replace_dir_cache_entry(dir_inum, name, child_inum, 
								index_to_replace, ftype);
	print_dir_entry_cache();
	return SUCCESS;
}

/* invalidate_entry_in_dir_cache : int, char* -> int
 * Invalidates the entry of [dir_inum, name] mapped to child_inum 
 * in the directory entry cache, by setting the valid bit to 0. */
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

/* fetch_entry_from_path_cache : char* -> int
 * Returns the mapped inode number in cache for the path if exists,
 * else returns -1. */
int fetch_entry_from_path_cache(const char* path) {
	int inode_number = -1, i;
	for (i = 0; i < PATH_CACHE_SIZE; i++) {
		if ((path_cache_list[i].valid == 1) && 
			(path_cache_list[i].path != NULL) && 
			(strcmp(path_cache_list[i].path, path) == 0)) {
			/* path found in cache */
			inode_number = path_cache_list[i].inum;
			/* increse the access time for LRU implementation */
			path_cache_list[i].last_access_time = ++current_access_count;
			break;
		}
	}
	print_path_cache();
	return inode_number;
}

/* replace_cache_entry : char*, int, int -> int
 * Replaces the netry in path_cache at index entry with
 * path and inum */
int replace_cache_entry(const char* path, int inum, int entry) {
	/* mark the entry as valid */
	path_cache_list[entry].valid = 1;
	/* copy the path to the cache */
	path_cache_list[entry].path = (char*) malloc 
									(sizeof(char) * strlen(path));
	strcpy(path_cache_list[entry].path, path);
	strcat(path_cache_list[entry].path, "\0");
	/* store the inode number */
	path_cache_list[entry].inum = inum;
	/* save the incremented access time */
	path_cache_list[entry].last_access_time = ++current_access_count;
	return SUCCESS;
}

int print_path_cache() {
	int i;
	printf("\n DEBUG : PRINTING CACHE\n");
	for (i = 0; i < PATH_CACHE_SIZE; i++) {
		if (path_cache_list[i].valid == 1) {
			printf("\nCache entry: %d -- Path = %s, inum = %d, valid = %d, last_access_time = %d", 
			i, path_cache_list[i].path, path_cache_list[i].inum, 
			path_cache_list[i].valid, path_cache_list[i].last_access_time);
		}
	}
}

/* add_path_to_cache : char*, int -> int
 * Adds the entry of path and inum to path_cache, after finding
 * a free nedtry if it exists,, or replacing an existing entry
 * by LRU. */
int add_path_to_cache(const char* path, int inum) {
	/* check for free entry in cache, if found, then put
	 * entry in that position
	 * if not there, then replace the entry with least
	 * access time */
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
	/* put the entry */
	replace_cache_entry(path, inum, index_to_replace);
	print_path_cache();
	return SUCCESS;
}

/* fs_translate_path_to_inum : const char*, int* -> int
 * Translates the path provided to the inode of the file
 * pointed to by the path, and returns the inode
 * Returns Error if path is invalid  */
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

/* get_dir_entries_from_dir_inum : 
 * 						int, struct fs7600_dirent* -> int
 * Reads the entry_list for the directory with inode_number 
 * from disk (only 1 block for a directory) 
 * Returns SUCCESS 
 *  */
int get_dir_entries_from_dir_inum(int inode_number, 
						struct fs7600_dirent *entry_list) {
	//struct fs7600_dirent entry_list[32];
    struct fs7600_inode *inode = NULL;
    int dir_block_num;
    /* get the inode from the inode number */
    inode = get_inode_from_inum(inode_number);
	/* read the first block which is the only block for dir inode */
    if (disk->ops->read(disk, inode->direct[0], 1, entry_list) < 0)
        exit(1);
    return SUCCESS;
}

/* write_dir_entries_to_disk_for_dir_inum : 
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


/* init - this is called once by the FUSE framework at startup. Ignore
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

/* readdir - get directory contents.
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
		/* If path translation returned an error, then return the error */
		if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR))
			return inode_number;
			
		/* If the path given is not for a directory, return error */
		if (type != IS_DIR) 
			return -ENOTDIR;
	}
	/* If we have NOT errored out above then get the 
	 * directory entries for this directory */
	if (get_dir_entries_from_dir_inum(inode_number, entry_list) != SUCCESS)
		return -ENOENT;
		
	/* Fill the stat buffer using the filler */
	for (i = 0; i < 32; i++) {
		
		if ((entry_list[i].name != NULL) && (entry_list[i].valid == 1))
			filler(ptr, entry_list[i].name, &sb, 0);
	}
    return SUCCESS;
}

/* see description of Part 2. In particular, you can save information 
 * in fi->fh. If you allocate memory, free it in fs_releasedir.
 */
static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
	printf("\n DEBUG : fs_opendir function called ");fflush(stdout);
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
	printf("\n DEBUG : fs_released function called ");fflush(stdout);
    return 0;
}

/* entry_exists_in_directory : int, char*, int*, int* -> int
 * Checks in the directory entries for the inode number dir_num
 * whether entry exists in it, and sets the type and inode number 
 * for it. */
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

/* mknod - create a new file with permissions (mode & 01777)
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
    //return SUCCESS;
}

/* mkdir - create a directory with the given mode.
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
	//return SUCCESS;
	return add_entry_to_dir_inode(parent_inum, new_dir_name, 
									new_inum, IS_DIR);
}

/* validate_path_for_new_create : const char *, char** -> int
 * Validates the path till the penultimate element for new
 * file/directory creation.
 * Returns the parent dir inode number on success, otherwise
 * returns error. */
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

/* initialize_new_inode_and_block : mode_t -> int
 * Creates a new inode and allocates a block to it.
 * Returns the inode number of the new flie/directory. */
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
	//write_dir_entries_to_disk_for_dir_inum(inode_number, entry_list);
	/* Return the inode number of the new directory/file */
	return inode_number;
}

/* add_entry_to_dir_inode : int, char*, int, int -> int
 * Adds an entry for name and inum in the entries set for 
 * directory with inode number parent_dir_inum, based on isDir.
 * Writes this modified directory block to disk.
 * Returns -ENOSPC if all entries are full */
int add_entry_to_dir_inode(int parent_dir_inum, char* name, 
							int inum, int isDir) {
	struct fs7600_dirent entry_list[32];
	int i, found = 0;
	get_dir_entries_from_dir_inum(parent_dir_inum, entry_list);
	/* Check if entry is present in parent directory entry list */
	for (i = 0; i < 32; i++) {
		//printf("\n DEBUG : Current directory contains file %s with inode %d ",
		//entry_list[i].name, entry_list[i].inode);
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
	if (write_dir_entries_to_disk_for_dir_inum(parent_dir_inum, entry_list) != SUCCESS)
		exit(1);
	if (homework_part > 2) {
		/* add the new entry to directory entry cache */
		add_dir_entry_to_cache(parent_dir_inum, name, inum, isDir);
	}
	/* return success */
	return SUCCESS;
}

/* truncate - truncate file to exactly 'len' bytes
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
     
    printf("\n DEBUG : fs_truncate function called ");fflush(stdout); 
    
    inum = fs_translate_path_to_inum(path, &type);
    
    if(inum < 0)
		return inum;
	
	inode = get_inode_from_inum(inum);
	
    blocksList = getListOfBlocksOperate(inode, inode->size, 0, &numOfBlocks);
    
    for(i=1; i<numOfBlocks; i++)
	{
			clear_block_number(blocksList[i]);
	}
	
	for (i =1; i < N_DIRECT; i++)
		inode->direct[i] = 0;
	
	inode->indir_1 = 0;
	inode->indir_2 = 0;
	
	update_Block_Wid_Buf(inode, inum, inode->direct[0], emptBuf);
	
	inode->size = 0;
	
	write_block_to_inode_region(inum);
    
    return SUCCESS;
}


/* unlink - delete a file
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
	
	if (type == IS_DIR)
			return -EISDIR;
		
	inode = get_inode_from_inum(inum);
	
	// unset all blocks for the inode
	blocklist = getListOfBlocksOperate(inode, inode->size, 0, &numofBlocksInFile);
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

/* validate_path_for_remove : const char *, char** -> int
 * Validates the path till the penultimate element for 
 * directory removal.
 * Returns the parent dir inode number on success, otherwise
 * returns error. */
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

/* remove_dir_is_empty : int -> int
 * Returns 1 if the directory with inode number dir_inum 
 * is empty, otherwise 0. */
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

/* rmdir - remove a directory
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
	/* Remove the entry for this directory from its parent directory */
	return remove_entry_from_dir_inode(parent_inum, to_remove_dir);
	//return SUCCESS;
}

/* validate_path_for_rename : char*, char*, char**, char** -> int
 * Returns the source directory parent inode number after 
 * validating source and dest parent dir to be same, both
 * being valid paths, and src exists, and dest does not exist 
 * */
int validate_path_for_rename(const char *src_path, 
	const char *dest_path, char** old_name, char** new_name) {
	int type, src_parent_inum, found = 0, dest_parent_inum;
	int rename_dir_inum;
	/* check whether src path is valid till penultimate */
	src_parent_inum = validate_path_till_penultimate(src_path, old_name);
	if (src_parent_inum < 0) 
		return src_parent_inum;
	/* check whether dest path is valid till penultimate */
	dest_parent_inum = validate_path_till_penultimate(dest_path, new_name);
	if (dest_parent_inum < 0) 
		return dest_parent_inum;
	/* check whether src path and dest path point to the same location */
	if (src_parent_inum != dest_parent_inum)
		return -EINVAL;
	/* check whether src file/dir does not exist */
	found = entry_exists_in_directory(src_parent_inum, *old_name, 
						&type, &rename_dir_inum);
	if (found == 0) {
		/* The src directory/file to rename does not exist, so error */
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

/* update_entry_in_dir_inode : int, char*, char* -> int
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

/* rename - rename a file or directory
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

/* chmod - change file permissions
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


/* getListOfBlocksOperate : indoe len(max. file size) offset 
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
		

		while(numBlocks2Read > 0){
			
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


int read_Data_From_File_Inode(struct fs7600_inode *inode, int len, 
						off_t offset, char* buf) 
{
		char* buffer = NULL;
		int* readBlocksList = NULL;
		int numBlocks2Read = 0;
		int sizeInBlock01, i;
		readBlocksList = getListOfBlocksOperate(inode, len, 
					offset, &numBlocks2Read);
		sizeInBlock01 = (offset % FS_BLOCK_SIZE);
		// Allocate memory to dynamic buffer and blocks list
		buffer = (char*)malloc((FS_BLOCK_SIZE * numBlocks2Read + 1) * 
										sizeof(char));
	    strcpy(buffer,"");
		// Filling the character buffer
		for (i = 0; i < numBlocks2Read ; i++)
		{
		
			char tempbuf[1024];
			if (disk->ops->read(disk, *(readBlocksList++), 1, 
								&tempbuf) < 0)
				exit(1);
			strcat(buffer, tempbuf);
		}			
		strncpy(buf, buffer + offset , (len - sizeInBlock01));
		free(buffer);
		return SUCCESS;	
}

/* read - read data from an open file.
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
	file_size = inode->size;
	if (offset >= file_size) {
		/* Trying to read from beyond the file size */
		bytes_read = 0;
	}
	else if (offset + len > file_size) {
		/* read all the bytes from offset to EOF */
		read_Data_From_File_Inode(inode, file_size, offset, buf);
		bytes_read = file_size - offset;
	}
	else {
		/* read required no of bytes into the character buffer */
		read_Data_From_File_Inode(inode, len, offset, buf);
		bytes_read = len;
	}
	/* return the number of bytes read */
	return bytes_read;
}

/* getLastBlcokOfFileInode : 
 * Returns the last block for a file inode. 
 * Comsumes the inode of the file. 
 * */
int getLastBlcokOfFileInode(struct fs7600_inode *inode)
{
			int *blockslist = NULL;
			int *tempblocksList = NULL;
			int filesize;
			int lastBlock;
			int numOfBlocksToread;
			filesize = inode->size;
			blockslist = getListOfBlocksOperate(inode, filesize, 
						0, &numOfBlocksToread);
			tempblocksList = (int *)malloc(numOfBlocksToread * 
						sizeof(int));
			memcpy(tempblocksList, blockslist, sizeof(int) + 
						numOfBlocksToread);
			lastBlock = tempblocksList[numOfBlocksToread - 1];
			return lastBlock;
}




#define TRUE 1
#define FALSE 0

/* addBlockNumber2Inode: blocknum inode -> int TRUE/FALSE
 * traverses over all the blocks of an inode and adds the new block 
 * entry at the end.
 * */
void addBlockNumber2Inode(int blockNum, struct fs7600_inode *inode){

	int filesize;
	int* readBlocksList = NULL;
	int indir1blocks[256];
	int count;
	int i;
	int numberOfBlocks;
	int blockPlaced = FALSE;
	
	filesize = inode->size;
	
	numberOfBlocks = filesize / FS_BLOCK_SIZE;
	
	if ((filesize % FS_BLOCK_SIZE) != 0) {
		numberOfBlocks = numberOfBlocks + 1;
	}
	
	i = 0;
	while(i < numberOfBlocks)
	{
		//1. Direct POinter Range 
		if (i < 6) {
			
			if (inode->direct[i] == 0) {
				inode->direct[i] = blockNum;
				break;
			}
			i = i + 1;
			continue;
		}
			
		//2. First indirection pointer reading
		else if ((i > 6) && (i < 256)) {
			
				if (disk->ops->read(disk, inode->indir_1, 1, 
							&indir1blocks) < 0)
					exit(1);
				
				for(count = 0 ; count < 256; count++) {
					// if empty block was found 
					if (indir1blocks[i] == 0) {
						// update the block with new number 
						indir1blocks[i] = blockNum;
						int blockPlaced  = TRUE;
						break;
					}
				}
				
				if (blockPlaced == TRUE)
				{
					if (disk->ops->write(disk, inode->indir_1, 1, 
								&indir1blocks) < 0)
						exit(1);
				} 
				else 
				{
					i = i + 256;		
				}
				
			}
			
		//3. Second level indirection pointer reading
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
					
					for(count = 0 ; count < 256; count++) 
					{
							
						if (tempBlocksList2[count] == 0) {
							tempBlocksList2[count] = blockNum;
							int blockPlaced = TRUE;
						}
											
						if (blockPlaced == TRUE)
						{
							if (disk->ops->write(disk, 
							tempBlocksList[i], 1, &tempBlocksList2) < 0)
								exit(1);
							break;
						} 
						else 
						{
							i = i + 256;		
						}
					}
				
				if (blockPlaced == TRUE)
					break;
			}	
		}
	}
}

void printInode(int inodenum) {
	
	struct fs7600_inode *inode = NULL;
	int indir1blocks[256];
	int indir2blocks[256];
	int i;
	printf("\n DEBUG : Printing the inode number = %d",inodenum);
	inode = get_inode_from_inum(inodenum);
	if (inode != NULL) {
			printf("\n DEBUG : Size = %d", inode->size);
			printf("\n DIRECT BLOCK NUMBERS = ");
			for(i = 0 ; i < 6; i++) {
				printf("%d \t",inode->direct[i]);
			}		
			printf("\n INDIRECT BLOCK NUMBERS = ");
			if (disk->ops->read(disk, inode->indir_1, 1, &indir1blocks) < 0)
					exit(1);
			for(i = 0; i < 256; i++) {
				printf("%d \t",indir1blocks[i]);
			}
			printf("\n INDIRECT 2 BLOCK NUMBERS = ");
			if (disk->ops->read(disk, inode->indir_2, 1, &indir1blocks) < 0)
					exit(1);
			for(i = 0; i < 256; i++) {
					if (disk->ops->read(disk, indir1blocks[i], 1, &indir2blocks) < 0)
						exit(1);
					for(i = 0 ; i < 6; i++) {
							printf("%d \t",indir2blocks[i]);
					}
			}	
	}
}


/* writeBufToBlockList:
 * Consumes alist and buf and writes 
 * */
int writeBufToBlockList(struct fs7600_inode *inode, int inodenum,
						int *list, char *buf, int listlen)
{
	char tempbuf[1024];
	int length;
	int i;
	length = strlen(buf);
		
	if(length > FS_BLOCK_SIZE)
	{
		strncpy(tempbuf, buf, FS_BLOCK_SIZE);
		tempbuf[FS_BLOCK_SIZE] = '\0';
		strncpy(buf, buf + FS_BLOCK_SIZE, length - FS_BLOCK_SIZE);
	}
	else
	{
		strcpy(tempbuf, buf);
	}
	
	for(i = 0 ; i < listlen ; i++) {
		update_Block_Wid_Buf(inode, inodenum, list[i], tempbuf);
	}
	return 0;
}


/* write - write data to a file
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
	int  inodenum, filesize, numOfBlocsInFile, i, blocksReqd, length;
	int  *blocklist = NULL;
	int  *reqdBlocklist = NULL;
	char *megabuf = NULL;
	char *f = NULL;
	char *e = NULL;
	int  *tempblocklist= NULL;
	int type;
	int startingIndex = 0;
	
	printf("\n DEBUG : fs_write is called with following parameters");
	printf("\n Name = %s", path);
	printf("\n BUffer to insert of length = %d",strlen(buf));
	printf("\n len ie numb of bytes axpected = %d",len);
	printf("\n offset for insertion = %jd",(intmax_t)offset);
	printf("\n and some fuse information structure  ....... ");
	
	
	// get the inode number from the path
	inodenum = fs_translate_path_to_inum(path, &type);
	
	// Check for path resolution errors
	if (inodenum < 0)
		return inodenum;
	
	inode = get_inode_from_inum(inodenum);
	filesize = inode->size;
	
	// get list of all blocks in file
	tempblocklist = blocklist = getListOfBlocksOperate(inode, 
					filesize, 0, &numOfBlocsInFile);
	
	printf("\n DEBUG : Total number of blocks in file = %d",numOfBlocsInFile);
		
	// manipulate list to start from block drom where writing is required 
	startingIndex = (int)(offset / FS_BLOCK_SIZE);
	
	printf("\n DEBUG : Starting index of blocks from offset = %d",startingIndex);

	tempblocklist = blocklist = blocklist + startingIndex;

	numOfBlocsInFile = numOfBlocsInFile - startingIndex;
	
	printf("\n DEBUG : Number of blocksleft in Blocklist = %d",numOfBlocsInFile);
		
	megabuf = (char*)malloc(sizeof(char) * (numOfBlocsInFile * FS_BLOCK_SIZE));
		
	strcpy(megabuf, "");	
	
	offset = offset % FS_BLOCK_SIZE;
	
	printf("\n DEBUG : Offset from starting block");
	
	// Read teh data present in all blocks in blockslist 
	for (i = 0; i < numOfBlocsInFile; i++) 
	{
		char tempbuf[FS_BLOCK_SIZE];
		strcpy(tempbuf, "");

		if (disk->ops->read(disk, *(blocklist++), 1, &tempbuf) < 0)
			exit(1);
			
		strcat(megabuf, tempbuf);
	}
		
	// insert buf at the correct index inside megabuf 
	length = strlen(megabuf);
	
	printf("\n DEBUG : Length of the megabuffer is = %d",length);
	printf("\n DEBUG : Offset for insertion into buffer is = %jd",(intmax_t)offset);
	printf("\n Length to be written in buffer is = %d", strlen(buf));
	
	printf("\n increase megabuf to length = %d",(length + strlen(buf)));
	
	megabuf = (char *)realloc(megabuf, (length + strlen(buf) + 1)  * sizeof(char));
		
	f = (char*)malloc(sizeof(char) * offset);
	strcpy(f, "");
	e = (char*)malloc(sizeof(char) * length - offset);
	strcpy(e, "");
	
		
	strncpy(f, megabuf, offset);      
	f[offset] = '\0';
	
	strncpy(e, megabuf + offset, length-offset);
	e[length-offset] = '\0';
	
	strcpy(megabuf, "");
	strcat(megabuf, f);	
	strcat(megabuf, buf);
	strcat(megabuf, e);
	
	megabuf[(length + strlen(buf))] = '\0';
	
	printf("\n DEBUG : Length of the megabuffer is = %d",strlen(megabuf));
	printf("\n DEBUG : Size of megabuf = %d",sizeof(megabuf));
	
	/* write the new megabuf into the list of blocks FS_FILE_SIZE 
	 * bytes at a time */
	blocksReqd = (int)strlen(megabuf)/FS_BLOCK_SIZE;
	
	if ((strlen(megabuf) % FS_BLOCK_SIZE) != 0)
		blocksReqd = blocksReqd + 1;
	
	blocksReqd = blocksReqd - numOfBlocsInFile;
	
	printf("\n DEBUG : Number of blocks reqd to write this data = %d",blocksReqd);
	printf("\n DEBUG : Number of blocks already in file = %d", numOfBlocsInFile);
	
	printf("\n DEBUG : Filling the buffers already present in file ... ");
	writeBufToBlockList(inode, inodenum, tempblocklist, megabuf, numOfBlocsInFile);
		
	if(blocksReqd > 0) {
		// seek 
		megabuf = megabuf + FS_BLOCK_SIZE * numOfBlocsInFile;
		printf("\n megabuf size after writing to block existing = %d",strlen(megabuf));
		
		reqdBlocklist = (int *)malloc(sizeof(int) * blocksReqd);
		
		for(i = 0; i < blocksReqd; i++) {
			reqdBlocklist[i] = get_free_block_number();
			set_block_number(reqdBlocklist[i]);
		}	
		// Fill Blocks With Buffer
		printf("\n DEBUG : Filling the buffers NOT present in file ... ");
		writeBufToBlockList(inode, inodenum, reqdBlocklist, megabuf, blocksReqd);
	
		printf("\n DEBUG : Trying to fill the inode with reqd blockslist... ");
		writeReqdBlocks2Inode(reqdBlocklist, blocksReqd, inode, offset, numOfBlocsInFile);
	}

	inode->size = inode->size + len;
	write_block_to_inode_region(inodenum);	
    return len;
}


/* createIndrectionWithEmptyList :
 * Allocates an empty block and writes an array of 
 * all zeros to it 
 * Returns the new block number 
 * */
int createIndrectionWithEmptyList()
{
	int templist[256] = {0};
	int blk_number;
	blk_number = get_free_block_number();
	set_block_number(blk_number);

	if(disk->ops->write(disk, blk_number, 1, templist) < 0)
		exit(1);

	return blk_number;
}


/* write2newIndir1Block : 
 * Allocates a new block and writes the list 
 * required nodes to that block then adds this block to 
 * indir1 in inode
 * */
int write2newIndir1Block(struct fs7600_inode *inode, int *list, 
					int *numofblocksReqd)	
{
	int tempBlockList[256] = {0};
	int i;
	
	inode->indir_1 = createIndrectionWithEmptyList();
	
	for (i = 0 ; ((i < 256) && (*numofblocksReqd > 0)); i++)
	{
		tempBlockList[i] = *(list++);
		--(*numofblocksReqd);
	}
	
	if (disk->ops->write(disk, inode->indir_1, 1, tempBlockList) < 0)
			exit(1);
	
	return SUCCESS;
}

/* write2newIndir2Block : 
 * Allocates a new block and writes the list 
 * creates more sub-blocks if required
 * writes the new block to inode->indir_2
 * */
int write2newIndir2Block(struct fs7600_inode *inode, int *list, 
						int *numofblocksReqd)
{
	int i;
	int tempIndir1List[256] = {0};
	
	inode->indir_2 = createIndrectionWithEmptyList();
	
	for(i = 0 ; i < 256 ; i++)
	{
		int tempIndir2List[256] = {0};
		tempIndir1List[i] = createIndrectionWithEmptyList();
		
		for(i = 0 ; ((i < 256) && (*numofblocksReqd > 0)) ; i++) 
		{
			tempIndir2List[i] = *(list++);
			--(*numofblocksReqd);
		}
		if(disk->ops->write(disk, tempIndir1List[i], 1, 
					tempIndir2List) < 0)
			exit(1);
	}
	if(disk->ops->write(disk, inode->indir_2, 1, tempIndir1List) < 0)
		exit(1);
	
	return SUCCESS;
}


/* writeStartingDirectBlocks : 
 * Start writing with direct blocks 
 * keep allocating new pointers when you rub out of space.
 * */
int writeStartingDirectBlocks(struct fs7600_inode *inode, int *list,  int *numofblocksReqd, int lastBlockIndex)
{
		int i;
		for(i = lastBlockIndex + 1; ((i < N_DIRECT) && (*numofblocksReqd > 0)) ; i++) {
			inode->direct[i] = *(list++);
			--(*numofblocksReqd);
		}
		if(*numofblocksReqd > 0) {
			write2newIndir1Block(inode, list, numofblocksReqd);
		}
		if(*numofblocksReqd > 0) {
			write2newIndir2Block(inode, list, numofblocksReqd);
		}
		
	return SUCCESS;
}

/* writeStartingIndir1Blocks : 
 * Start writing from indir1 block and keep adding new 
 * pointers when required
 * */
int writeStartingIndir1Blocks(struct fs7600_inode *inode, int *list,  
				int *numofblocksReqd, int lastBlockIndex)
{
	int i;
	int indir1list[256] = {0};
	printf("\n DEBUg : file ends in indir1 blocks");
	if (disk->ops->read(disk, inode->indir_1, 1, indir1list))
		exit(1);
	for(i = lastBlockIndex + 1 ; ((i < 256) && 
							(*numofblocksReqd > 0)); i++)
	{
		indir1list[i] = *(list++);
		--(*numofblocksReqd);
	}
	
	if (disk->ops->write(disk, inode->indir_1, 1, indir1list) < 0)
		exit(1);
	if(*numofblocksReqd > 0) 
	{
			write2newIndir2Block(inode, list, numofblocksReqd);
	}
	return SUCCESS;
}

/* writeStartingIndir2Blocks : 
 * Start writing from indir2 blocks and error out if no more 
 * space is required to be written but this check shud have
 * been made earlier in fs_write
 * */
int writeStartingIndir2Blocks(struct fs7600_inode *inode, int *list,  
					int *numofblocksReqd, int lastBlockIndex) 
{
	int i, currindex, j;
	int indir2list[256] = {0};
	printf("\n DEBUG : file ends in indir2 blocks");
	disk->ops->read(disk, inode->indir_2, 1, indir2list);
	currindex = 256 + 6 - 1;
	for(i = 0; i < 256; i++)
	{
			int indir2list_2[256] = {0};
			if(disk->ops->read(disk, indir2list[i], 1, indir2list_2) < 0)
				exit(1);
			for(j = 0; ((j < 256) && (*numofblocksReqd > 0))  ; j++) 
			{	
				if (currindex > lastBlockIndex)
				{
					indir2list_2[j] = *(list++);
					--(*numofblocksReqd);
					currindex = currindex + 1;
				}
			}
			if(disk->ops->write(disk, indir2list[i], 1, indir2list_2) < 0)
				exit(1);
	}
	if(disk->ops->write(disk, inode->indir_2, 1, indir2list) < 0)
			exit(1);
	return SUCCESS;
}


/* allocateReqdBlocks:
 * Take the list of blocks to be allocated newly and start writing the 
 * list of blocks to the inode
 * */
int allocateReqdBlocks(struct fs7600_inode *inode, int* list, 
						int listlen, int lastblockindex)
{
	int numofblocksReqd = listlen;
	printf("\n DEBUG : Trying to write to required blocks to ");
	if(lastblockindex < N_DIRECT) 
		writeStartingDirectBlocks(inode, list,  &numofblocksReqd, 
						lastblockindex);
	else if ((lastblockindex > 6) && (lastblockindex < 256))
		writeStartingIndir1Blocks(inode, list, &numofblocksReqd, 
						lastblockindex);
	else if(lastblockindex > 256)
		writeStartingIndir2Blocks(inode, list, &numofblocksReqd, 
						lastblockindex);
	return SUCCESS;
}

/* writeReqdBlocks2Inode :
 * Stub forcalling the writing function 
 * */
int writeReqdBlocks2Inode(int* reqdBlockList, int numBlkReqd, 
	struct fs7600_inode *inode, off_t offset, int numOfBlocsInFile)
{
	int startingBlockIndex, lastBlockIndex;
	startingBlockIndex = offset / FS_BLOCK_SIZE;
	lastBlockIndex = startingBlockIndex + numOfBlocsInFile;
	allocateReqdBlocks(inode, reqdBlockList, numBlkReqd, 
				lastBlockIndex);	
}


/* Update block with buf : Com=nsumes a block number and a char* buf 
 * and updates teh block with that buffer
 */
int update_Block_Wid_Buf(struct fs7600_inode *inode, int inodenum, 
				int blocknum, char* buf) {
	char blkbuf[1024];
	strcpy(blkbuf, "");
	strcat(blkbuf, buf);
	if (disk->ops->write(disk, blocknum, 1, blkbuf) < 0) 
			exit(1);
	if(write_block_to_inode_region(inodenum) < 0)
				exit(1);
	return SUCCESS;
}



//-------------------------------------------------------------------------

static int fs_open(const char *path, struct fuse_file_info *fi)
{
	printf("\n DEBUG : fs_open function called ");fflush(stdout);
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
	printf("\n DEBUG : fs_release function called ");fflush(stdout);
    return 0;
}

/* get_free_block_count : int -> int
 * Returns the number of blocks which are free in the file system */
int get_free_block_count(int start_block) {
	int block_count = sizeof(*block_map);
	int free_count = 0;
	int temp_block = start_block;
	while (temp_block < block_count) {
		//printf("\n Checking for block number %d \n", free_entry);
		if (!(FD_ISSET(temp_block, block_map)))
		{
			free_count += 1;
		}
		temp_block ++;
	}
	return free_count;
}

/* statfs - get file system statistics
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
    printf("\n DEBUG : fs_statfs function called ");fflush(stdout);
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

/* operations vector. Please don't rename it, as the skeleton code in
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

