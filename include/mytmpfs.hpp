#ifndef MYTMPFS_H_
#define MYTMPFS_H_

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE              512

#define STATS_ROOTS             ((BLOCK_SIZE - 2 * sizeof(unsigned long)) / sizeof(void*))
#define BLOCKS_PER_PAGE         8
#define STATS_PER_PAGE          ((BLOCK_SIZE * BLOCKS_PER_PAGE - 3 * sizeof(unsigned long)) / sizeof(struct stat))

struct mytmpfs_data
{
    unsigned long stats_pages_allocated;
    void **stats_pages;

    unsigned long userdata_blocks_allocated;
    unsigned long userdata_count;
    void **userdata;

    #ifdef DEBUG
    FILE* dbg;
    #endif
};

#define DATA ((mytmpfs_data*)(fuse_get_context()->private_data))

#ifdef DEBUG
#define DBG(...) fprintf(DATA->dbg, __VA_ARGS__)
#else
#define DBG(...) void(0);
#endif

int mytmpfs_resolve_path(const char *path, ino_t *inobuf);
void* mytmpfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
int mytmpfs_getattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi);
int mytmpfs_opendir(const char *path, struct fuse_file_info *fi);
int mytmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int mytmpfs_releasedir(const char *path, struct fuse_file_info *fi);
int mytmpfs_mkdir(const char *path, mode_t mode);
int mytmpfs_rmdir(const char *path);
int mytmpfs_open(const char *path, struct fuse_file_info *fi);
int mytmpfs_release(const char *path, struct fuse_file_info *fi);
int mytmpfs_read(const char *path, char *buf, size_t len, off_t offset, struct fuse_file_info *fi);
int mytmpfs_write(const char *path, const char *buf, size_t len, off_t offset, struct fuse_file_info *fi);
int mytmpfs_mknod(const char *path, mode_t mode, dev_t dev);

#endif
