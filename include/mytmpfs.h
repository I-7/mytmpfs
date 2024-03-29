#ifndef MYTMPFS_H_
#define MYTMPFS_H_

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <linux/fs.h>

#define MYTMPFS_BLOCK_SIZE      512
#define STATS_ROOTS             ((MYTMPFS_BLOCK_SIZE - sizeof(unsigned long) - 2 * sizeof(void*)) / sizeof(void*))
#define BLOCKS_PER_PAGE         8
#define STATS_PER_PAGE          ((MYTMPFS_BLOCK_SIZE * BLOCKS_PER_PAGE - 3 * sizeof(unsigned long)) / sizeof(struct stat))

struct mytmpfs_data
{
    struct stat root_premount_stat;

    unsigned long stats_pages_allocated;
    void **stats_pages;

    unsigned long userdata_blocks_allocated;
    unsigned long userdata_count;
    void **userdata;

    #ifdef DEBUG
    FILE* dbg;
    #endif
};

#define DATA ((struct mytmpfs_data*)(fuse_get_context()->private_data))
#define USERDATA_SIZE(ino) (((unsigned long*)DATA->userdata[ino - 1])[0])
#define USERDATA_ACNT(ino) (((unsigned long*)DATA->userdata[ino - 1])[1])
#define USERDATA_SHIFT (2 * sizeof(unsigned long))
#define USERDATA_RAW(ino) ((char*)DATA->userdata[ino - 1] + USERDATA_SHIFT)

#ifdef DEBUG
#define DBG(...) fprintf(DATA->dbg, __VA_ARGS__), fflush(DATA->dbg)
#else
#define DBG(...) 0
#endif

int mytmpfs_resolve_path(const char *path, ino_t *inobuf);
int mytmpfs_create_dirent(const ino_t ino, const ino_t itemino, const char *path);
int mytmpfs_remove_dirent(const ino_t ino, unsigned long offset);

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
int mytmpfs_unlink(const char *path);
int mytmpfs_rename(const char *path, const char *newpath, unsigned int flags);
int mytmpfs_link(const char *path, const char *newpath);
void mytmpfs_destroy(void *private_data);
int mytmpfs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi);

#endif
