/*
 * file:        misc.c
 * description: various support functions for CS 7600 homework 3
 *              startup argument parsing and checking, etc.
 *
 * CS 7600, Intensive Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2015
 */

#define FUSE_USE_VERSION 27
#define _XOPEN_SOURCE 500
#define _ATFILE_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <fuse.h>
#include "blkdev.h"

#include "fs7600.h"		/* only for BLOCK_SIZE */

/*********** DO NOT MODIFY THIS FILE *************/

/* All homework functions are accessed through the operations
 * structure.  
 */
extern struct fuse_operations fs_ops;

struct blkdev *disk;
struct data {
    char *image_name;
    int   part;
} _data;
int homework_part;

/*
 * See comments in /usr/include/fuse/fuse_opts.h for details of 
 * FUSE argument processing.
 * 
 *  usage: ./homework -image disk.img [-part #] directory
 *              disk.img  - name of the image file to mount
 *              directory - directory to mount it on
 */
static struct fuse_opt opts[] = {
    {"-image %s", offsetof(struct data, image_name), 0},
    {"-part %d", offsetof(struct data, part), 0},
    FUSE_OPT_END
};

int main(int argc, char **argv)
{
    /* Argument processing and checking
     */
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, &_data, opts, NULL) == -1)
	exit(1);

    char *file = _data.image_name;
    if (strcmp(file+strlen(file)-4, ".img") != 0) {
        printf("bad image file (must end in .img): %s\n", file);
        exit(1);
    }
    if ((disk = image_create(file)) == NULL) {
        printf("cannot open image file '%s': %s\n", file, strerror(errno));
        exit(1);
    }

    homework_part = _data.part;
    return fuse_main(args.argc, args.argv, &fs_ops, NULL);
}


