#ifndef MYTMPFS_H_
#define MYTMPFS_H_

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <dirent.h>
#include <errno.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>

#define BLOCK_SIZE              512

#define STATS_ROOTS             ((BLOCK_SIZE - 2 * sizeof(unsigned long)) / sizeof(void*))
#define BLOCKS_PER_PAGE         16
#define STATS_PER_PAGE          ((BLOCK_SIZE * BLOCKS_PER_PAGE - 3 * sizeof(unsigned long)) / sizeof(struct stat))

struct mytmpfs_data
{
    unsigned long stats_pages_allocated;
    void **stats_pages;

    unsigned long userdata_blocks_allocated;
    unsigned long userdata_count;
    void **userdata;

    #ifdef DEBUG
    std::ofstream dbg;
    #endif
};

#define DATA ((mytmpfs_data*)(fuse_get_context()->private_data))

#ifdef DEBUG
#define DBG (((mytmpfs_data*)(fuse_get_context()->private_data))->dbg)
#else
#define DBG //
#endif

int resolve_path(const char *path, ino_t *inobuf);
void* mytmpfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
int mytmpfs_getattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi);
int mytmpfs_opendir(const char *path, struct fuse_file_info *fi);
int mytmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int mytmpfs_releasedir(const char *path, struct fuse_file_info *fi);
int mytmpfs_mkdir(const char *path, mode_t mode);

#endif
