/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#define F 0 // file
#define D 1 // directory

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <string.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
bitmap_t i_bitmap;
bitmap_t d_bitmap;
static struct superblock *sb;
struct inode *root_inode;
struct dirent *root_dirent;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	printf("==> Getting an available inode <==\n");

	// Step 1: Read inode bitmap from disk
	bio_read(sb->i_bitmap_blk, i_bitmap);
	
	// Step 2: Traverse inode bitmap to find an available slot
	for(int i = 0; i < MAX_INUM; i++) {
		if(!get_bitmap(i_bitmap, i)) {
			// Step 3: Update inode bitmap and write to disk 
			set_bitmap(i_bitmap, i);
			bio_write(sb->i_bitmap_blk, i_bitmap);
			return i;
		}
	}

	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	printf("==> Getting an available data block <==\n");

	// Step 1: Read data block bitmap from disk
	bio_read(sb->d_bitmap_blk, d_bitmap);
	
	// Step 2: Traverse data block bitmap to find an available slot
	for(int i = 0; i < MAX_DNUM; i++) {
		if(!get_bitmap(d_bitmap, i)) {
			// Step 3: Update data block bitmap and write to disk 
			set_bitmap(d_bitmap, i);
			bio_write(sb->d_bitmap_blk, d_bitmap);
			printf("==> Found available data block: %d <==\n", i);
			return i;
		}
	}

	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	printf("==> Reading inode: %d <==\n", ino);

	// Step 1: Get the inode's on-disk block number
	int block_num = sb->i_start_blk + (ino * sizeof(struct inode) / BLOCK_SIZE);

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = ino % (BLOCK_SIZE / sizeof(struct inode));

	// Step 3: Read the block from disk and then copy into inode structure
	struct inode *block = malloc(BLOCK_SIZE);
	bio_read(block_num, block);
	// memcpy(inode, &block[offset], sizeof(struct inode));
	*inode = block[offset];
	// free(block);
	printf("===> inode values: %d %d %d %d %d %d %d <==\n", inode->ino, inode->valid, inode->size, inode->type, inode->link, inode->direct_ptr[0], inode->indirect_ptr[0]);
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	printf("==> Writing inode: %d <==\n", ino);

	// Step 1: Get the block number where this inode resides on disk
	int block_num = sb->i_start_blk + (ino * sizeof(struct inode) / BLOCK_SIZE);

	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = ino % (BLOCK_SIZE / sizeof(struct inode));

	printf("===> block_num: %d <==\n", block_num);
	printf("===> offset: %d <==\n", offset);

	// Step 3: Write inode to disk 
	struct inode *block = malloc(BLOCK_SIZE);
	bio_read(block_num, block);
	block[offset] = *inode;
	// memcpy(&block[offset], inode, sizeof(struct inode));
	bio_write(block_num, block);
	// free(block);

	return 0;
}

/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	printf("==> Finding directory entry: %s <==\n", fname);

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode *inode = malloc(sizeof(struct inode));
	readi(ino, inode);
			
	
	// Step 2: Get data block of current directory from inode
	struct dirent *block = malloc(BLOCK_SIZE);
	for(int i = 0; i < (sizeof(inode->direct_ptr) / sizeof(inode->direct_ptr[0])); i++) {
		struct dirent *block_parser = block;
		if(inode->direct_ptr[i] && bio_read(inode->direct_ptr[i], block)) {
			for(int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++, block_parser++) {
				// Step 3: Read directory's data block and check each directory entry.
				if(block_parser->valid && !strncmp(block_parser->name, fname, name_len)) {
					printf("===> Found directory entry: %s <==\n", fname);
					// memcpy(dirent, block_parser, sizeof(struct dirent));
					// free(block);
					// free(inode);
					*dirent = *block_parser;
					free(block);
					return 0;
				}
			}
		}

	}

	printf("===> Could not find directory entry: %s <==\n", fname);
	free(block);
	return -ENOENT;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	printf("==> Adding directory entry: %s <==\n", fname);

	struct dirent* dirent = malloc(sizeof(struct dirent));
	if(dir_find(dir_inode.ino, fname, name_len, dirent) == 0) {
		return -EEXIST;
	}

	struct dirent *block = malloc(BLOCK_SIZE);

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	for(int i = 0; i < sizeof(dir_inode.direct_ptr) / sizeof(dir_inode.direct_ptr[0]); i++) {
		struct dirent* block_parser = block;

		if(!dir_inode.direct_ptr[i]) {
			dir_inode.direct_ptr[i] = sb->d_start_blk + get_avail_blkno();
		}
		
		if(bio_read(dir_inode.direct_ptr[i], block)) {
			for(int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++, block_parser++) {
				if(!block_parser->valid) {
					block_parser->valid = 1;
					block_parser->ino = f_ino;
					memcpy(block_parser->name, fname, name_len);
					
					dir_inode.size++;
					dir_inode.vstat.st_size = dir_inode.size * sizeof(struct dirent);
					time(&dir_inode.vstat.st_mtime);
					writei(dir_inode.ino, &dir_inode);	

					bio_write(dir_inode.direct_ptr[i], block);
					free(block);
					return 0;
				}
			}

		}
	}

	free(block);
	return -ENOSPC;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
	printf("==> Removing directory entry: %s <==\n", fname);

	struct dirent *block = malloc(BLOCK_SIZE);

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	for(int i = 0; i < sizeof(dir_inode.direct_ptr) / sizeof(dir_inode.direct_ptr[0]); i++) {
		struct dirent* block_parser = block;

		if(bio_read(dir_inode.direct_ptr[i], block) && dir_inode.direct_ptr[i] != -1) {
			for(int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++, block_parser++) {
				// Step 2: Check if fname exist
				if(block_parser->valid && !strncmp(block_parser->name, fname, name_len)) {
					// Step 3: If exist, then remove it from dir_inode's data block and write to disk
					block_parser->valid = 0;

					dir_inode.size--;
					dir_inode.vstat.st_size = dir_inode.size * sizeof(struct dirent);
					time(&dir_inode.vstat.st_mtime);
					writei(dir_inode.ino, &dir_inode);

					bio_write(dir_inode.direct_ptr[i], block);
					free(block);

					return 0;
				}
			}

		}
	}

	free(block);
	return -ENOENT;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	printf("==> Checking if path exists: %s with ino %d <==\n", path, ino);
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	if(strcmp(path, "") == 0) {
		return -ENOENT;
	}

	char* path_dir = strdup(path);
	char* path_base = strdup(path);

	char* base = basename(path_dir);
	char* dir = dirname(path_base);

	printf("===> base: %s <==\n", base);
	printf("===> dir: %s <==\n", dir);

	// if(!strcmp(base, "/")) {
	// 	readi(0, inode);
	// 	return 0;
	// }

	// Recursive
	// Doesn't work too well, trying iterative
	// int temp = get_node_by_path(dir, 0, inode);
	// if(temp == -1) {
	// 	return -1;
	// }

	// struct dirent *dirent = malloc(sizeof(struct dirent));
	// int valid = dir_find(inode->ino, base, strlen(base), dirent);
	// if(valid == -1) {
	// 	return -1;
	// }

	// readi(dirent->ino, inode);

	// Iterative
	struct dirent dirent = {0};
	char* walk;
	char* path_dup = strdup(path);

	if(strcmp(path, "/") == 0) {
		readi(0, inode);
		return 0;
	}
	
	while((walk = strsep(&path_dup, "/")) != NULL) {
		if(*walk && dir_find(dirent.ino, walk, strlen(walk), &dirent) < 0) {
			return -ENOENT;
		}
	}

	readi(dirent.ino, inode);
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
	printf("==> Making file system <==\n");

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	sb = malloc(BLOCK_SIZE);

	// write superblock information
	sb->magic_num = MAGIC_NUM;
	sb->max_inum = MAX_INUM;
	sb->max_dnum = MAX_DNUM;
	sb->i_bitmap_blk = 1;
	sb->d_bitmap_blk = 2;
	sb->i_start_blk = 3;
	sb->d_start_blk = 3 + (sizeof(struct inode) * MAX_INUM / BLOCK_SIZE);
	// sb->i_bitmap_blk = sizeof(struct superblock) % BLOCK_SIZE == 0 ? sizeof(struct superblock) / BLOCK_SIZE : sizeof(struct superblock) / BLOCK_SIZE + 1;
	// sb->d_bitmap_blk = sb->i_bitmap_blk + (MAX_INUM / 8) % BLOCK_SIZE == 0 ? (MAX_INUM / 8) / BLOCK_SIZE : (MAX_INUM / 8) / BLOCK_SIZE + 1;
	// sb->i_start_blk = sb->d_bitmap_blk + (MAX_DNUM / 8) % BLOCK_SIZE == 0 ? (MAX_DNUM / 8) / BLOCK_SIZE : (MAX_DNUM / 8) / BLOCK_SIZE + 1;
	// sb->d_start_blk = sb->i_start_blk + (MAX_INUM * sizeof(struct inode) % BLOCK_SIZE == 0 ? MAX_INUM * sizeof(struct inode) / BLOCK_SIZE : MAX_INUM * sizeof(struct inode) / BLOCK_SIZE + 1);
	bio_write(0, sb);

	printf("==> inode bitmap block number is %d <==\n", sb->i_bitmap_blk);
	printf("==> data block bitmap block number is %d <==\n", sb->d_bitmap_blk);
	printf("==> inode region start block number is %d <==\n", sb->i_start_blk);
	printf("==> data block region start block number is %d <==\n", sb->d_start_blk);

	// initialize inode bitmap
	i_bitmap = (bitmap_t) malloc(BLOCK_SIZE);

	// initialize data block bitmap
	d_bitmap = (bitmap_t) malloc(BLOCK_SIZE);

	memset(i_bitmap, 0, BLOCK_SIZE);
	memset(d_bitmap, 0, BLOCK_SIZE);

	// update bitmap information for root directory
	set_bitmap(i_bitmap, 0);
	set_bitmap(d_bitmap, 0);

	printf("==> writing bitmaps to disk <==\n");
	bio_write(sb->i_bitmap_blk, i_bitmap);
	bio_write(sb->d_bitmap_blk, d_bitmap);

	// update inode for root directory
	root_inode = malloc(sizeof(struct inode));
	root_dirent = malloc(BLOCK_SIZE);

	root_inode->ino = 0;
	root_inode->valid = 1;
	root_inode->size = 2;
	root_inode->type = D;	
	root_inode->link = 2;
	root_inode->direct_ptr[0] = sb->d_start_blk;
	root_inode->vstat.st_uid = 0; 
	root_inode->vstat.st_gid = 0;
	root_inode->vstat.st_nlink = 2;
	root_inode->vstat.st_size = 2 * sizeof(struct dirent);
	time(&root_inode->vstat.st_mtime);
	root_inode->vstat.st_mode = S_IFDIR | 0755;

	root_dirent->ino = 0;
	root_dirent->valid = 1;
	strcpy(root_dirent->name, ".");
	(++root_dirent)->ino = 0;
	root_dirent->valid = 1;
	strcpy(root_dirent->name, "..");

	// dir_add(root_inode, root_inode->ino, "/", 1);
	printf("==> writing root inode to disk <==\n");
	bio_write(sb->i_start_blk, root_inode);
	bio_write(sb->d_start_blk, root_dirent);

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
	printf("==> Initializing file system <==\n");

	// Step 1a: If disk file is not found, call mkfs
	if (dev_open(diskfile_path) < 0) {
		rufs_mkfs();
		return NULL;
	}

	// Step 1b: If disk file is found, just initialize in-memory data structures and read superblock from disk
	bio_read(0, &sb);

	i_bitmap = (bitmap_t) malloc(BLOCK_SIZE);
	d_bitmap = (bitmap_t) malloc(BLOCK_SIZE);

	bio_read(sb->i_bitmap_blk, i_bitmap);
	bio_read(sb->d_bitmap_blk, d_bitmap);

	// readi(0, &root_inode);

	return NULL;
}

static void rufs_destroy(void *userdata) {
	printf("==> Destroying file system <==\n");

	// Step 1: De-allocate in-memory data structures
	free(i_bitmap);
	free(d_bitmap);

	// Step 2: Close diskfile
	dev_close();

	// Print total number of blocks used
	int total_blocks = 0;
	for(int i = 0; i < MAX_DNUM; i++) {
		if(get_bitmap(d_bitmap, i)) {
			total_blocks++;
		}
	}
	printf("Total number of blocks used: %d\n", total_blocks);
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	printf("==> Getting attributes for %s <==\n", path);

	// Step 1: call get_node_by_path() to get inode from path
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	printf("==> is valid: %d <==\n", valid);
	if(valid != 0) {
		// free(inode);
		return -ENOENT;
	}

	printf("===> inode: %d %d %d %d %d <==\n", inode->ino, inode->valid, inode->size, inode->type, inode->link);

	// Step 2: fill attribute of file into stbuf from inode
	*stbuf = inode->vstat;

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	printf("==> Opening directory: %s <==\n", path);

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid == -1) {
		// free(inode);
		return -1;
	}

	return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	printf("==> Reading directory: %s <==\n", path);

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid < 0) {
		// free(inode);
		return -ENOENT;
	}

	struct dirent *block = malloc(BLOCK_SIZE);

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	for(int i = 0; i < sizeof(inode->direct_ptr) / sizeof(inode->direct_ptr[0]); i++) {
		struct dirent *block_parser = block;
		// if(inode->direct_ptr[i] != -1) {
		// 	struct dirent *block = malloc(BLOCK_SIZE);
		// 	bio_read(inode->direct_ptr[i], block);
		// 	for(int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
		// 		if(block[j].valid) {
		// 			filler(buffer, block[j].name, NULL, 0);
		// 		}
		// 	}
		// 	// free(block);
		// }

		if(bio_read(inode->direct_ptr[i], block) && inode->direct_ptr[i]) {
			for(int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++, block_parser++) {
				if(block_parser->valid) {
					struct inode *block_inode = malloc(sizeof(struct inode));
					readi(block_parser->ino, block_inode);
					filler(buffer, block_parser->name, &block_inode->vstat, 0);
				}
			}
		} 
	}

	// free(inode);
	return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {
	printf("==> rufs mkdir called with path: %s <==\n", path);

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char* path_dir = strdup(path);
	char* path_base = strdup(path);

	char* parent = dirname(path_dir);
	char* name = basename(path_base);

	printf("==> Step 1: parent: %s <==\n", parent);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(parent, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	printf("==> Step 2: inode: %d <==\n", inode->ino);

	// Step 3: Call get_avail_ino() to get an available inode number
	int ino = get_avail_ino();
	if(ino < 0) {
		// free(inode);
		return ENOSPC;
	}

	printf("==> Step 3: ino: %d <==\n", ino);

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	int add = dir_add(*inode, ino, name, strlen(name));
	if(add < 0) {
		// unset_bitmap(i_bitmap, ino);
		// bio_write(sb->i_bitmap_blk, i_bitmap);
		// free(inode);
		return add;
	}

	printf("==> Step 4: add: %d <==\n", add);

	// Step 5: Update inode for target directory
	struct inode *target = malloc(sizeof(struct inode));
	target->ino = ino;
	target->valid = 1;
	target->size = 2;
	target->type = D;
	target->link = 2;
	memset(target->direct_ptr, 0, sizeof(target->direct_ptr));
	target->direct_ptr[0] = sb->d_start_blk + get_avail_blkno();
	target->vstat.st_uid = getuid();
	target->vstat.st_gid = getgid();
	target->vstat.st_nlink = 2;
	target->vstat.st_size = 2 * sizeof(struct dirent);
	time(&target->vstat.st_mtime);
	target->vstat.st_mode = S_IFDIR | 0755;

	printf("==> Step 5: target: %d %d %d %d %d <==\n", target->ino, target->valid, target->size, target->type, target->link);

	// Step 6: Call writei() to write inode to disk
	struct dirent *default_block = malloc(BLOCK_SIZE);
	default_block[0].ino = ino;
	default_block[0].valid = 1;
	strcpy(default_block[0].name, ".");
	default_block[1].ino = inode->ino;
	default_block[1].valid = 1;
	strcpy(default_block[1].name, "..");

	writei(ino, target);
	bio_write(target->direct_ptr[0], default_block);

	printf("==> Step 6: writei <==\n");

	// free(inode);
	// free(target);

	return 0;
}

static int rufs_rmdir(const char *path) {
	printf("==> rufs rmdir called with path: %s <==\n", path);

	char* path_dir = strdup(path);
	char* path_base = strdup(path);

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char* parent = dirname(path_dir);
	char* name = basename(path_base);

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	if(inode->size != 2) {
		// free(inode);
		return -ENOTEMPTY;
	}

	// Step 3: Clear data block bitmap of target directory
	for(int i = 0; i < sizeof(inode->direct_ptr) / sizeof(inode->direct_ptr[0]); i++) {
		if(inode->direct_ptr[i]) {
			unset_bitmap(d_bitmap, inode->direct_ptr[i] - sb->d_start_blk);
			inode->direct_ptr[i] = 0;
		}
	}
	bio_write(sb->d_bitmap_blk, d_bitmap);

	// Step 4: Clear inode bitmap and its data block
	unset_bitmap(i_bitmap, inode->ino);
	bio_write(sb->i_bitmap_blk, i_bitmap);
	inode->valid = 0;
	writei(inode->ino, inode);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	valid = get_node_by_path(parent, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	int remove = dir_remove(*inode, name, strlen(name));
	if(remove < 0) {
		// free(inode);
		return remove;
	}

	// free(inode);

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	printf("==> rufs create called with path: %s <==\n", path);

	char* path_dir = strdup(path);
	char* path_base = strdup(path);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* parent = dirname(path_dir);
	char* name = basename(path_base);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(parent, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int ino = get_avail_ino();
	if(ino < 0) {
		// free(inode);
		return ino;
	}

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	int add = dir_add(*inode, ino, name, strlen(name));
	if(add < 0) {
		// unset_bitmap(i_bitmap, ino);
		// bio_write(sb->i_bitmap_blk, i_bitmap);
		// free(inode);
		return add;
	}

	// Step 5: Update inode for target file
	struct inode *target = malloc(sizeof(struct inode));
	target->ino = ino;
	target->valid = 1;
	target->size = 0;
	target->type = F;
	target->link = 1;
	memset(target->direct_ptr, 0, sizeof(target->direct_ptr));
	target->direct_ptr[0] = sb->d_start_blk + get_avail_blkno();
	target->vstat.st_uid = getuid();
	target->vstat.st_gid = getgid();
	target->vstat.st_nlink = 1;
	target->vstat.st_size = 0;
	time(&target->vstat.st_mtime);
	target->vstat.st_mode = S_IFREG | mode;

	// Step 6: Call writei() to write inode to disk
	writei(ino, target);

	// free(inode);
	// free(target);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
	printf("==> rufs open called with path: %s <==\n", path);

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}
	return 0;
}


// May have to fix this function
static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	printf("==> rufs read called with path: %s <==\n", path);

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int bytes_read = 0;
	int max_bytes = inode->vstat.st_size - offset;
	while(bytes_read < max_bytes && bytes_read < size) {
		int block_num = offset / BLOCK_SIZE;
		int block_offset = offset % BLOCK_SIZE;
		int block_size = BLOCK_SIZE - block_offset;
		if(block_size > size - bytes_read) {
			block_size = size - bytes_read;
		}
		// Step 3: copy the correct amount of data from offset to buffer
		char *block = malloc(BLOCK_SIZE);
		bio_read(inode->direct_ptr[block_num], block);
		memcpy(buffer + bytes_read, block + block_offset, block_size);
		bytes_read += block_size;
		offset += block_size;
		free(block);
	}

	// Note: this function should return the amount of bytes you copied to buffer
	return bytes_read;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	printf("==> rufs write called with path: %s <==\n", path);

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int bytes_written = 0;
	int max_bytes = offset + size;
	for(int i = offset; i < max_bytes; i++) {
		int block_num = i / BLOCK_SIZE;
		int block_offset = i % BLOCK_SIZE;
		int block_size = BLOCK_SIZE - block_offset;
		if(block_size > size - bytes_written) {
			block_size = size - bytes_written;
		}
		// Step 3: Write the correct amount of data from offset to disk
		char *block = malloc(BLOCK_SIZE);
		if(!inode->direct_ptr[block_num]) {
			inode->direct_ptr[block_num] = sb->d_start_blk + get_avail_blkno();
		}
		
		// Step 4: Update the inode info and write it to disk
		inode->vstat.st_size += block_size;
		bio_read(inode->direct_ptr[block_num], block);
		memcpy(block + block_offset, buffer + bytes_written, block_size);
		bio_write(inode->direct_ptr[block_num], block);
		bytes_written += block_size;
		free(block);
		writei(inode->ino, inode);
	}


	// Note: this function should return the amount of bytes you write to disk
	printf("==> bytes_written: %d <==\n", bytes_written);
	return bytes_written;
}

static int rufs_unlink(const char *path) {
	printf("==> rufs unlink called with path: %s <==\n", path);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* path_dir = strdup(path);
	char* path_base = strdup(path);

	char* parent = dirname(path_dir);
	char* name = basename(path_base);

	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode *inode = malloc(sizeof(struct inode));
	int valid = get_node_by_path(path, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	// Step 3: Clear data block bitmap of target file
	for(int i = 0; i < sizeof(inode->direct_ptr) / sizeof(inode->direct_ptr[0]); i++) {
		if(inode->direct_ptr[i]) {
			unset_bitmap(d_bitmap, inode->direct_ptr[i] - sb->d_start_blk);
			inode->direct_ptr[i] = 0;
		}
	}
	bio_write(sb->d_bitmap_blk, d_bitmap);

	// Step 4: Clear inode bitmap and its data block
	inode->valid = 0;
	unset_bitmap(i_bitmap, inode->ino);
	bio_write(sb->i_bitmap_blk, i_bitmap);
	writei(inode->ino, inode);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	valid = get_node_by_path(parent, 0, inode);
	if(valid < 0) {
		// free(inode);
		return valid;
	}

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	int remove = dir_remove(*inode, name, strlen(name));
	if(remove < 0) {
		// free(inode);
		return remove;
	}

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

