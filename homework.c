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
#define IS_DIR 0
#define IS_FILE 1

extern int homework_part;       /* set by '-part n' command-line option */

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
fd_set *inode_map;              /* = malloc(sb.inode_map_size * FS_BLOCK_SIZE); */
fd_set *block_map;

struct fs7600_inode *inodes;

struct fs7600_inode* get_inode_from_inum(int inode_number) {
	return &inodes[inode_number];
}

int get_block_number_from_inum(int inode_number) {
	return inode_number / INODES_PER_BLK;
}

int get_bl_inode_number_from_inum(int inode_number) {
	return inode_number % INODES_PER_BLK;
}

static int fs_translate_path_to_inum(const char* path, int* type) {
	char *curr_dir = NULL;
	char *parent_dir = NULL;
	char *working_path = NULL;
	struct fs7600_dirent entry_list[32];
	struct fs7600_inode *root_inode = NULL;
	int i, dir_block_num, parent_dir_inode,inode ;
	int found = 0;
	int ftype;
	char* forward_path = NULL;
	
	if (path == NULL){
		printf("\nDEBUG : Received a null path ... ");
		return -EOPNOTSUPP;
	}

	printf ("\n DEBUG : Translation Path = %s", path);
	// Assume that the path starts from the root. Thus the entry 
	// should be present in the root inode's block
	// the root is a directory verify if the current file is 
	// present in directory.
	
	working_path = strdup(path);
	curr_dir = strtok(working_path, "/");
	
	if (curr_dir == NULL)
					return 1;	
					
					
	parent_dir_inode = 1;
	
	while(curr_dir != NULL) {
		// read the listing of files from the parent directory
		get_dir_entries_from_dir_inum(parent_dir_inode, &entry_list);
		
		// Check if the curr_file is present in parent directory
		for (i = 0; i< 32; i++) 
		{
			if (strcmp(curr_dir, entry_list[i].name) == 0) {
					printf("\n File present in Directory ... ");
					ftype = entry_list[i].isDir;
					inode = entry_list[i].inode;
					found = 1;
					break;
			}
		}
		
		if (!found) {
			// File not found in direcotry
			return -ENOENT;
		}
			
		forward_path = strtok(0, "/"); 
		printf("Forwward Path = %s",forward_path);
		
		if (forward_path != NULL)
		{
			// if the forward_path is not null means there is more path after this
			// entry. Check if we shud throw the FILE_IN_PATH error?
			if (ftype == 0) {
				// if type is zero signifies that the entry is a file since 
				// there are more entries in the path this condition is an error. 
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
			return inode;
		}
	}
}

int get_dir_entries_from_dir_inum(int inode_number, 
									struct fs7600_dirent* entry_list) {
	//struct fs7600_dirent entry_list[32];
    struct fs7600_inode *inode = NULL;
    int dir_block_num;
    /* get the inode from the inode number */
    inode = get_inode_from_inum(inode_number);
	/* read the first block which is the only block for directory inode */
    if (disk->ops->read(disk, inode->direct[0], 1, entry_list) < 0)
        exit(1);
    //return &entry_list[0];
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
    struct fs7600_super sb;
    int block_num_to_read = 0;
    if (disk->ops->read(disk, block_num_to_read, 1, &sb) < 0)
        exit(1);

	struct fs7600_inode inodes_list[sb.inode_region_sz][INODES_PER_BLK];
    printf("\n DEBUG : fs_init function called ");fflush(stdout);
    /* your code here */
    sb.magic = FS7600_MAGIC;
    //printf("\nINIT called-- magic = %u\n",sb.magic);
    //printf("\ninode_map_sz = %u\n", sb.inode_map_sz);
    //printf("\ninode_region_sz = %u\n", sb.inode_region_sz);
    //printf("\nblock_map_sz = %u\n", sb.block_map_sz);
    //printf("\nnum_blocks = %u\n", sb.num_blocks);
    //printf("\nroot_inode = %u\n", sb.root_inode);
    
    /* Allocate memory for the inode and block bitmaps 
     * and store the read bitmaps from disk */
     
    inode_map = malloc(sb.inode_map_sz * FS_BLOCK_SIZE);
    block_map = malloc(sb.block_map_sz * FS_BLOCK_SIZE);
     
    block_num_to_read += 1;
    /* read the inode bitmap */
    if (disk->ops->read(disk, block_num_to_read, sb.inode_map_sz, &inode_map) < 0)
        exit(1);
    
    /* read the block bitmap */
    block_num_to_read += sb.inode_map_sz;
    if (disk->ops->read(disk, block_num_to_read, sb.block_map_sz, &block_map) < 0)
        exit(1);
    
    /* read the inodes */
    
    //inodes = (struct fs7600_inode **) malloc(sizeof(struct fs7600_inode *) * sb.inode_region_sz);
    //for (i = 0; i < sb.inode_region_sz; i++) {
	//	inodes[i] = (struct fs7600_inode *) malloc(sizeof(struct fs7600_inode) * INODES_PER_BLK);
	//}
	
    block_num_to_read += sb.block_map_sz;
    if (disk->ops->read(disk, block_num_to_read, sb.inode_region_sz, inodes_list) < 0)
        exit(1);
    inodes = &inodes_list[0][0];
     
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
	printf("\n DEBUG : fs_getattr function called ");fflush(stdout);fflush(stderr);
	printf("\n DEBUG : path = %s", path);
	/* translate the path to the inode_number, if possible */
	inode_number = fs_translate_path_to_inum(path, &type);
	/* if path translation returned an error, then return the error */
	if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR)) {
		printf("\n DEBUG : Path translation returned an ERROR ***");
		return inode_number; }
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
	printf("\n DEBUG : fs_readdir function called ");fflush(stdout);
	/* translate the path to the inode_number, if possible */
	inode_number = fs_translate_path_to_inum(path, &type);
	/* if path translation returned an error, then return the error */
	if ((inode_number == -ENOENT) || (inode_number == -ENOTDIR))
		return inode_number;
	/* If the path given is not for a directory, return error */
	if (type != IS_DIR) 
		return -ENOTDIR;
	/* get the directory entries for this directory */
	if (get_dir_entries_from_dir_inum(inode_number, entry_list) != SUCCESS)
		return -EOPNOTSUPP;
	for (i = 0; i < 32; i++) {
		//filler(ptr, entry_list[i].name, fi, 0);
	}
    return -EOPNOTSUPP;
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
	//printf("\n DEBUG : fs_mknod function called ");fflush(stdout);
    return -EOPNOTSUPP;
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
	//printf("\n DEBUG : fs_mkdir function called ");fflush(stdout);
    return -EOPNOTSUPP;
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
    return -EOPNOTSUPP;
}

int fs_utime(const char *path, struct utimbuf *ut)
{
	//printf("\n DEBUG : fs_utime function called ");fflush(stdout);
    return -EOPNOTSUPP;
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
	//printf("\n DEBUG : fs_read function called ");fflush(stdout);
    return -EOPNOTSUPP;
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

