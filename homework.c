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

extern int homework_part; /* set by '-part n' command-line option */

/* 
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of 1024-byte blocks
 */
extern struct blkdev *disk;

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
	printf("\n disk_block = %d \n", disk_block);
	/* get the inode entry from the block */
	int disk_block_entry = get_bl_inode_number_from_inum(inode_number);
	printf("\n disk_block_entry = %d \n", disk_block_entry);
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
		//printf("\n Checking for block number %d \n", free_entry);
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
		//printf("\n Checking for inode number %d \n", free_entry);
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
		//printf("\n DEBUG : Received a null path ... ");
		return -ENOENT;
	}

	printf ("\n DEBUG : Translation Path = %s", path);
	fflush(stdout);fflush(stderr);
	
	// Assume that the path starts from the root. Thus the entry 
	// should be present in the root inode's block
	// the root is a directory verify if the current file is 
	// present in directory.
	working_path = strdup(path);
	curr_dir = strtok(working_path, "/");
	 
	if (curr_dir == NULL){
		*type = 1;
		return 1;
	}	
		
	//Set the inode of the parent directory to be 1 for the root.				
	parent_dir_inode = 1;
	
	//traverse over the token in the path.
	while(curr_dir != NULL) {
		found = 0;
		found = entry_exists_in_directory(parent_dir_inode, curr_dir,
						&ftype, &inode);
		
		if (found == 0) {
			// File not found in direcotry
			return -ENOENT;
		}
			
		forward_path = strtok(0, "/"); 
		printf("\n DEBUG : Forward Path = %s",forward_path);
		
		if (forward_path != NULL)
		{
			// if the forward_path is not null means there is more path after this
			// entry. Check if we shud throw the FILE_IN_PATH error?
			if (ftype == 0) {
				// if type is zero signifies that the entry is a file since 
				// there are more entries in the path this condition is an error. 
				*type = ftype; 
				return -ENOTDIR;
			}
			else
			{
				// id type is one this means that this entry is a directory 
				// and we should continue path traversals 
				parent_dir_inode = inode;
				strcpy(curr_dir, forward_path);
			}
		}
		else 
		{
			// if the forward_path is null means there is no more path after this
			// entry
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
    printf("\n DEBUG : fs_init function called ");fflush(stdout);
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
	//printf("\n DEBUG : fs_getattr function called trying to get attributes for path = %s",path);
	fflush(stdout);fflush(stderr);
	
	/* translate the path to the inode_number, if possible */
	inode_number = fs_translate_path_to_inum(path, &type);
	//printf("\n DEBUG : Path translated to inode number = %d",inode_number);
	fflush(stdout);fflush(stderr);
	
	/* if path translation returned an error, then return the error */
	if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR)) {
		//printf("\n DEBUG : *** Path translation returned an ERROR ***");
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
	//printf("\n DEBUG : fs_readdir function called ");fflush(stdout);
	//printf("\n DEBUG : Trying to read path = %s",path); fflush(stdout);
	
	/* translate the path to the inode_number, if possible */
	inode_number = fs_translate_path_to_inum(path, &type);
	
	//printf("\n DEBUG : Path %s Translated to inode %d of type %s", path, inode_number, type == IS_DIR?"DIR":"FILE"); fflush(stdout);
	
	/* If path translation returned an error, then return the error */
	if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR))
		return inode_number;
		
	/* If the path given is not for a directory, return error */
	if (type != IS_DIR) 
		return -ENOTDIR;
		
	/* If we have NOT errored out above then get the 
	 * directory entries for this directory */
	if (get_dir_entries_from_dir_inum(inode_number, entry_list) != SUCCESS)
		return -EOPNOTSUPP;
		
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
	//printf("\n DEBUG : fs_opendir function called ");fflush(stdout);
    return 0;
}

static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{
	//printf("\n DEBUG : fs_released function called ");fflush(stdout);
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
	if (!S_ISREG(mode)) {
		/* File type expected, ERROR */
		return -EINVAL;
	}
	/* validate path and get the parent directory inode number */
	int parent_inum = validate_path_for_new_create(path, &new_dir_name);
	if (parent_inum < 0) {
		/* error */
		return parent_inum;
	}
	/* initialize a new file inode */
	new_inum = initialize_new_inode_and_block(mode);
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
	//if (!(S_ISDIR(mode))) {
		/* wrong mode */
	//	exit(1);
	//}
	/* validate path and get the parent directory inode number */
	int parent_inum = validate_path_for_new_create(path, &new_dir_name);
	if (parent_inum < 0) {
		/* error */
		return parent_inum;
	}
	/* initialize a new dir inode */
	new_inum = initialize_new_inode_and_block(mode);
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
	//printf("\n DEBUG : fs_mkdir function called ");fflush(stdout);
	int type, parent_inum, existing_inode_number, found = 0;
	int new_inum = 0;
	const char* stripped_path = remove_last_token(path, new_name);
	/* get the inode number of the directory/file where to create the 
	 * new directory from the stripped path */
	printf("\n DEBUG: mkdir stripped path = %s with size = %d\n", 
	stripped_path, strlen(stripped_path));
	parent_inum = fs_translate_path_to_inum(stripped_path, &type);
	printf("\n DEBUG: Parent inum = %d to disk \n", parent_inum);
	if (parent_inum < 0) {
		/* error */
		return parent_inum;
	}
	else if(type == IS_FILE) {
		/* the penultimate token is not a directory */
		return -ENOTDIR;
	}
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
	printf("\n DEBUG: new inode number = %d \n", inode_number);
	/* get a free block number */
	int block_number = get_free_block_number();
	printf("\n DEBUG: new block_number = %d \n", block_number);
	if ((inode_number <= 0) || (block_number <= 0)) {
		/* Error: no free inode number, no space 
		 * No free blocks */
		return -ENOSPC;
	}
	/* Set the bit in inode map to mark as allocated */
	printf("\n DEBUG: set_inode_number called ");
	set_inode_number(inode_number);
	/* Set the bit in block map to mark as allocated */
	printf("\n DEBUG: set_block_number called ");
	set_block_number(block_number);
	/* fetch the inode from inode number */
	inode_ptr = get_inode_from_inum(inode_number);
	/* set its attributes */
	inode_ptr->uid = 1000;
	inode_ptr->gid = 1000;
	if (S_ISREG(mode)) {
		inode_ptr->mode = (mode & 01777);
		inode_ptr->size = 0;
	}
	else {
		inode_ptr->mode = (mode & 0040755);
		inode_ptr->size = FS_BLOCK_SIZE;
	}
	inode_ptr->ctime = time(NULL);
	inode_ptr->mtime = time(NULL);
	printf("\n DEBUG: inode_ptr->size =  = %d ", inode_ptr->size);
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
			//memcpy(&entry_list[i].inode, inum, sizeof(entry_list[i].inode));
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
	return write_dir_entries_to_disk_for_dir_inum(parent_dir_inum, entry_list);
	//return SUCCESS;
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
    
    //printf("\n DEBUG : fs_truncate function called ");fflush(stdout);
    if (len != 0)
	return -EINVAL;		/* invalid argument */
    return -EOPNOTSUPP;
}

/* unlink - delete a file
 *  Errors - path resolution, ENOENT, EISDIR
 * Note that you have to delete (i.e. truncate) all the data.
 */
static int fs_unlink(const char *path)
{
	//printf("\n DEBUG : fs_unlink function called ");fflush(stdout);
    return -EOPNOTSUPP;
}

/* rmdir - remove a directory
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
static int fs_rmdir(const char *path)
{
	//printf("\n DEBUG : fs_rmdir function called ");fflush(stdout);
    return -EOPNOTSUPP;
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
	//printf("\n DEBUG : fs_rename function called ");fflush(stdout);
    return -EOPNOTSUPP;
}

/* chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * Errors - path resolution, ENOENT.
 */
static int fs_chmod(const char *path, mode_t mode)
{
	//printf("\n DEBUG : fs_chmod function called ");fflush(stdout);
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
	//printf("\n DEBUG : fs_utime function called ");fflush(stdout);
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

int read_Data_From_File_Inode(struct fs7600_inode *inode, int len, off_t offset, char* buf) 
{
		int filesize, startBlockInd, sizeInBlock01, numBlocks2Read, sizeOfReadList;
		char* buffer = NULL;
		int* readBlocksList = NULL;
		int indir1blocks[256];
		int i = 0;
		int j = 0;
		
		printf("\n DEBUG : Reading Data Off the INODE \n ");
		
		filesize = inode->size;
		startBlockInd = (int) (offset / FS_BLOCK_SIZE);
		sizeInBlock01 = (offset % FS_BLOCK_SIZE);
		sizeOfReadList = numBlocks2Read = (int)((len - sizeInBlock01)/FS_BLOCK_SIZE) + 1;
		
		// Allocate memory to dynamic buffer and blocks list
		buffer = (char*)malloc((FS_BLOCK_SIZE * numBlocks2Read + 1) * sizeof(char));
		
		readBlocksList = (int*)malloc(numBlocks2Read * sizeof(int));
		
		j = 0;
		i = startBlockInd;	//current index of reading from blocks
		
		strcpy(buffer, "");
		
		while(numBlocks2Read > 0){
			
			// 1. Direct Pointer area reading
			if (i < 6) {
				readBlocksList[j] = inode->direct[i];
				numBlocks2Read = numBlocks2Read - 1;
				j = j + 1;
			}
			
			// 2. First indirection pointer reading
			else if ((i > 6) && (i < 256)) {
			
				if (disk->ops->read(disk, inode->indir_1, 1, &indir1blocks) < 0)
					exit(1);
				
				if(numBlocks2Read > 256) {
					memcpy(readBlocksList + j, indir1blocks, 256 * sizeof(int));
					numBlocks2Read = numBlocks2Read - 256;
					i = i + 256;
					j = j + 256;
				} else {
					memcpy(readBlocksList + j, indir1blocks, numBlocks2Read * sizeof(int));
					i = i + numBlocks2Read;
					j = j + numBlocks2Read;
					numBlocks2Read = 0;
				}
			}
			
			// 3. Second level indirection pointer reading
			else {
				int tempBlocksList[256];
				int tempBlocksList2[256];
				
				if (disk->ops->read(disk, inode->indir_2, 1, &tempBlocksList) < 0)
					exit(1);
				
				for(i = 0; i < 256 ; i++)
				{
					if (disk->ops->read(disk, tempBlocksList[i], 1, &tempBlocksList2) < 0)
						exit(1);
				}
				
				if(numBlocks2Read > 256) {
					memcpy(readBlocksList + j, tempBlocksList2, 256 * sizeof(int));
					numBlocks2Read = numBlocks2Read - 256;
					j = j + 256;
					i = i + 256;
				} else {
					memcpy(readBlocksList + j, indir1blocks, numBlocks2Read * sizeof(int));
					i = i + numBlocks2Read;
					j = j + numBlocks2Read;
					numBlocks2Read = 0;
				}			
			}
	}
	
	// Filling the character buffer
	for (i = 0; i < sizeOfReadList ; i++){
		char tempbuf[1024];
		if (disk->ops->read(disk, readBlocksList[i], 1, &tempbuf) < 0)
				exit(1);
				
		strcat(buffer, tempbuf);
	}			
	
	//printf("\n DEBUG : BUFFER = %s of length = %d",buffer,strlen(buffer));
	
	strncpy(buf, buffer + offset , (len - sizeInBlock01));
	buf[(len - sizeInBlock01)] = '\0';
	
	//printf("\n DEBUG : BUF = %s of LENGTH = %d",buf, strlen(buf));
	
	free(readBlocksList);
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
	int type; // local variable to check the type of the input path
	int inode_num; //local variable to hold the inoe number of the file
	int file_size; //local variable to hold the size of a file on disk
	struct fs7600_inode *inode = NULL;
	
	//printf("\n DEBUG : fs_read function called ");fflush(stdout);
	//printf("\n DEBUG : Parameters for the READ --> PATH = %s REQ_LEN = %d OFFSET = %jd",path, len, (intmax_t)offset);
	
	inode_num = fs_translate_path_to_inum(path, &type);
	
	strcpy(buf, "");
	
	if (inode_num < 0) {
		return inode_num;
	}
		
	if (type == IS_DIR) {
			//trying to read a directory 
			return -EISDIR;
	}
		
	inode = get_inode_from_inum(inode_num);
	
	file_size = inode->size;
	
	//printf("\n DEBUG : The size of the file is = %d",file_size);
	
	if (offset >= file_size ) {
		return 0;
	}
	
	else if (offset + len > file_size) {
		// read all the bytes from start to EOF
		read_Data_From_File_Inode(inode, file_size, 0, buf);
		//printf("\n DEBUG : Buffer Read Here = %s", buf);
		return file_size;
	}
	
	else {
		// read required number of bytes into the final character buffer
		read_Data_From_File_Inode(inode, len, offset, buf);
		//printf("\n DEBUG : Buffer Read Here = %s", buf);
		return len;
	}
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
	//printf("\n DEBUG : fs_write function called ");fflush(stderr);fflush(stderr);
	//printf("\n DEBUG : fs_write function called path =  %s", path);fflush(stderr);fflush(stderr);
	
    return -EOPNOTSUPP;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
	//printf("\n DEBUG : fs_open function called ");fflush(stdout);
    return 0;
}

static int fs_release(const char *path, struct fuse_file_info *fi)
{
	//printf("\n DEBUG : fs_release function called ");fflush(stdout);
    return 0;
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
    //printf("\n DEBUG : fs_statfs function called ");fflush(stdout);
    st->f_bsize = FS_BLOCK_SIZE;
    st->f_blocks = 0;           /* probably want to */
    st->f_bfree = 0;            /* change these */
    st->f_bavail = 0;           /* values */
    st->f_namemax = 27;

    return 0;
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

