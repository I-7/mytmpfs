#include "mytmpfs.h"
#include "stat_tree.h"

static const struct fuse_operations mytmpfs_op = {
    .getattr = mytmpfs_getattr,
    .mknod = mytmpfs_mknod,
    .mkdir = mytmpfs_mkdir,
    .unlink = mytmpfs_unlink,
    .rmdir = mytmpfs_rmdir,
    .open = mytmpfs_open,
    .read = mytmpfs_read,
    .write = mytmpfs_write,
    .release = mytmpfs_release,
    .opendir = mytmpfs_opendir,
    .readdir = mytmpfs_readdir,
    .releasedir = mytmpfs_releasedir,
    .init = mytmpfs_init,
};

int mytmpfs_resolve_path(const char *path, ino_t *ino)
{
    *ino = 1;
    struct dirent* de;
    size_t i = 1;
    while (i < strlen(path)) {
        char* tmp = strchr(path + i, '/');
        size_t len = (tmp != NULL ? tmp : path + strlen(path)) - (path + i);
        int found = 0;
        for (unsigned long j = 0; j < USERDATA_SIZE(*ino); j += sizeof(struct dirent)) {
            de = (struct dirent*)(USERDATA_RAW(*ino) + j);
            if (memcmp(de->d_name, path + i, len) == 0) {
                found = 1;
                *ino = de->d_ino;
                break;
            }
        }
        if (!found) {
            *ino = 1;
            return -1;
        }
        i += len + 1;
    }
    return 0;
}

void* mytmpfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    cfg->use_ino = 1;

    struct stat stbuf;
    memcpy(&stbuf, &DATA->root_premount_stat, sizeof(struct stat));

    stbuf.st_mtime = time(&stbuf.st_atime);
    stbuf.st_size = 2 * sizeof(struct dirent);
    stbuf.st_blocks = (USERDATA_SHIFT + stbuf.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    mytmpfs_create_stat(&stbuf, NULL, DATA);

    struct dirent *de = (struct dirent*)USERDATA_RAW(1);
    de->d_ino = 1;
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, ".");
    USERDATA_SIZE(1) += sizeof(struct dirent);

    de++;
    de->d_ino = 1; // This won't be used because this leads out of mytmpfs
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, "..");
    USERDATA_SIZE(1) += sizeof(struct dirent);

    return (void*)DATA;
}

int mytmpfs_getattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    ino_t ino;
    if (mytmpfs_resolve_path(path, &ino) != 0) {
        return -ENOENT;
    }
    mytmpfs_get_stat(ino, statbuf, DATA);
    return 0;
}

int mytmpfs_opendir(const char *path, struct fuse_file_info *fi)
{
    ino_t ino;
    if (mytmpfs_resolve_path(path, &ino) != 0) {
        return -ENOENT;
    }
    fi->fh = ino;
    USERDATA_ACNT(fi->fh)++;
    return 0;
}

int mytmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    struct dirent *de = (struct dirent*)USERDATA_RAW(fi->fh);

    struct stat stbuf;
    mytmpfs_get_stat(fi->fh, &stbuf, DATA);
    time(&stbuf.st_atime);
    mytmpfs_set_stat(fi->fh, &stbuf, DATA);

    for (unsigned long i = 0; i < USERDATA_SIZE(fi->fh); i += sizeof(struct dirent), de++) {
        if (filler(buf, de->d_name, NULL, 0, 0) != 0) {
            return -ENOMEM;
        }
    }
    return 0;
}

int mytmpfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    if (fi != NULL) {
        USERDATA_ACNT(fi->fh)--;
        fi->fh = (unsigned long)NULL;
    }
    return 0;
}

int mytmpfs_mkdir(const char *path, mode_t mode)
{
    const char *lst = strrchr(path, '/');
    const char *name = lst + 1;
    char *ppath = (char*)malloc(lst - path + 1);
    memcpy(ppath, path, lst - path);
    ppath[lst - path] = '\0';

    ino_t ino;
    if (mytmpfs_resolve_path(ppath, &ino) != 0) {
        free(ppath);
        return -ENOENT;
    }

    struct stat ostat, nstat;
    mytmpfs_get_stat(ino, &ostat, DATA);

    memcpy(&nstat, &ostat, sizeof(struct stat));

    ostat.st_nlink++;
    ostat.st_size += sizeof(struct dirent);
    ostat.st_blocks = (USERDATA_SHIFT + ostat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    time(&ostat.st_atime);
    ostat.st_mtime = ostat.st_atime;

    nstat.st_mode = mode | S_IFDIR;
    nstat.st_nlink = 2;
    nstat.st_size = 2 * sizeof(struct dirent);
    nstat.st_blocks = (USERDATA_SHIFT + nstat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    nstat.st_ctime = ostat.st_atime;
    nstat.st_atime = ostat.st_atime;
    nstat.st_mtime = ostat.st_atime;

    ino_t nino;
    if (mytmpfs_create_stat(&nstat, &nino, DATA) != 0) {
        free(ppath);
        return -ENOMEM;
    }

    if (nino >= DATA->userdata_count + 1) {
        void **tmp = (void**)realloc(DATA->userdata, 2 * DATA->userdata_count * sizeof(void*));
        if (tmp == NULL) {
            mytmpfs_delete_stat(nino, DATA);
            free(ppath);
            return -ENOMEM;
        }
        DATA->userdata = tmp;
        DATA->userdata_count *= 2;
    }

    void *tmp = (void*)realloc(DATA->userdata[ino - 1], ostat.st_blocks * BLOCK_SIZE);
    if (tmp == NULL) {
        mytmpfs_delete_stat(nino, DATA);
        free(ppath);
        return -ENOMEM;
    }
    DATA->userdata[ino - 1] = tmp;
    
    DATA->userdata[nino - 1] = (void*)malloc(nstat.st_blocks * BLOCK_SIZE);
    if (DATA->userdata[nino - 1] == NULL) {
        mytmpfs_delete_stat(nino, DATA);
        free(ppath);
        ostat.st_size -= sizeof(struct dirent);
        ostat.st_blocks = (USERDATA_SHIFT + ostat.st_blocks + BLOCK_SIZE - 1) / BLOCK_SIZE;
        DATA->userdata[ino - 1] = (void*)realloc(DATA->userdata[ino - 1], ostat.st_blocks * BLOCK_SIZE);
        return -ENOMEM;
    }
    USERDATA_SIZE(nino) = 0;
    USERDATA_ACNT(nino) = 0;

    mytmpfs_set_stat(ino, &ostat, DATA);

    struct dirent *de = (struct dirent*)(USERDATA_RAW(ino) + USERDATA_SIZE(ino));
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, name);
    de->d_ino = nino;
    USERDATA_SIZE(ino) += sizeof(struct dirent);

    de = (struct dirent*)(USERDATA_RAW(nino) + USERDATA_SIZE(nino));
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, ".");
    de->d_ino = nino;
    USERDATA_SIZE(nino) += sizeof(struct dirent);

    de = (struct dirent*)(USERDATA_RAW(nino) + USERDATA_SIZE(nino));
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, "..");
    de->d_ino = ino;
    USERDATA_SIZE(nino) += sizeof(struct dirent);

    free(ppath);

    return 0;
}

int mytmpfs_rmdir(const char *path)
{
    const char *lst = strrchr(path, '/');
    const char *name = lst + 1;
    char *ppath = (char*)malloc(lst - path + 1);
    memcpy(ppath, path, lst - path);
    ppath[lst - path] = '\0';

    ino_t ino;
    if (mytmpfs_resolve_path(ppath, &ino) != 0) {
        free(ppath);
        return -ENOENT;
    }

    struct dirent *de;
    unsigned long res = ULONG_MAX;
    for (unsigned long i = 0; i < USERDATA_SIZE(ino); i += sizeof(struct dirent)) {
        de = (struct dirent*)(USERDATA_RAW(ino) + i);
        if (strcmp(name, de->d_name) == 0) {
            res = i;
            break;
        }
    }
    if (res == ULONG_MAX) {
        return -ENOENT;
    }

    unsigned long cnt = (USERDATA_SIZE(de->d_ino)) / sizeof(struct dirent);
    if (cnt != 2) {
        return -ENOTEMPTY;
    }

    if (USERDATA_ACNT(de->d_ino) != 0) {
        return -EBUSY;
    }

    free(DATA->userdata[de->d_ino - 1]);
    mytmpfs_delete_stat(de->d_ino, DATA);

    struct stat stbuf;
    mytmpfs_get_stat(ino, &stbuf, DATA);
    time(&stbuf.st_atime);
    stbuf.st_mtime = stbuf.st_atime;
    stbuf.st_nlink--;

    memcpy(USERDATA_RAW(ino) + res, USERDATA_RAW(ino) + res + sizeof(struct dirent), USERDATA_SIZE(ino) - res);
    
    stbuf.st_size -= sizeof(struct dirent);
    stbuf.st_blocks = (USERDATA_SHIFT + stbuf.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    USERDATA_SIZE(ino) -= sizeof(struct dirent);
    DATA->userdata[ino - 1] = realloc(DATA->userdata[ino - 1], stbuf.st_blocks * BLOCK_SIZE);

    mytmpfs_set_stat(ino, &stbuf, DATA);
    return 0;
}

int mytmpfs_open(const char *path, struct fuse_file_info *fi)
{
    ino_t ino;
    if (mytmpfs_resolve_path(path, &ino) != 0) {
        return -ENOENT;
    }
    fi->fh = ino;
    USERDATA_ACNT(fi->fh)++;
    return 0;
}

int mytmpfs_read(const char *path, char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
{
    size_t eof = USERDATA_SIZE(fi->fh);

    struct stat stbuf;
    mytmpfs_get_stat(fi->fh, &stbuf, DATA);
    time(&stbuf.st_atime);
    mytmpfs_set_stat(fi->fh, &stbuf, DATA);

    if (offset < 0) {
        return -EFAULT;
    }
    if ((unsigned long)offset >= eof) {
        return -EFAULT;
    }

    size_t canread = eof - offset;
    size_t cread = canread < len ? canread : len;

    memcpy(buf, USERDATA_RAW(fi->fh), cread);

    return cread;
}

int mytmpfs_write(const char *path, const char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
{
    if ((unsigned long)offset > USERDATA_SIZE(fi->fh)) {
        return -EFBIG;
    }

    struct stat stbuf;
    mytmpfs_get_stat(fi->fh, &stbuf, DATA);
    stbuf.st_size = offset + len;
    stbuf.st_blocks = (USERDATA_SHIFT + stbuf.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    stbuf.st_mtime = time(&stbuf.st_atime);

    void* tmp = (void*)realloc(DATA->userdata[fi->fh - 1], stbuf.st_blocks * BLOCK_SIZE);
    if (tmp == NULL) {
        return -ENOMEM;
    }
    DATA->userdata[fi->fh - 1] = tmp;

    memcpy(USERDATA_RAW(fi->fh) + offset, buf, len);
    USERDATA_SIZE(fi->fh) = offset + len;
    mytmpfs_set_stat(fi->fh, &stbuf, DATA);
    return len;
}

int mytmpfs_release(const char *path, struct fuse_file_info *fi)
{
    if (fi != NULL) {
        USERDATA_ACNT(fi->fh)--;
        fi->fh = (unsigned long)NULL;
    }
    return 0;
}

int mytmpfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    const char *lst = strrchr(path, '/');
    const char *name = lst + 1;
    char *ppath = (char*)malloc(lst - path + 1);
    memcpy(ppath, path, lst - path);
    ppath[lst - path] = '\0';

    ino_t ino;
    if (mytmpfs_resolve_path(ppath, &ino) != 0) {
        free(ppath);
        return -ENOENT;
    }

    struct stat ostat, nstat;
    mytmpfs_get_stat(ino, &ostat, DATA);

    memcpy(&nstat, &ostat, sizeof(struct stat));

    ostat.st_nlink++;
    ostat.st_size += sizeof(struct dirent);
    ostat.st_blocks = (USERDATA_SHIFT + ostat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    time(&ostat.st_atime);
    ostat.st_mtime = ostat.st_atime;

    nstat.st_dev = dev;
    nstat.st_mode = mode | S_IFREG;
    nstat.st_nlink = 1;
    nstat.st_size = 0;
    nstat.st_blocks = (USERDATA_SHIFT + nstat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    nstat.st_ctime = ostat.st_atime;
    nstat.st_atime = ostat.st_atime;
    nstat.st_mtime = ostat.st_atime;

    ino_t nino;
    if (mytmpfs_create_stat(&nstat, &nino, DATA) != 0) {
        free(ppath);
        return -ENOMEM;
    }

    if (nino >= DATA->userdata_count + 1) {
        void **tmp = (void**)realloc(DATA->userdata, 2 * DATA->userdata_count * sizeof(void*));
        if (tmp == NULL) {
            mytmpfs_delete_stat(nino, DATA);
            free(ppath);
            return -ENOMEM;
        }
        DATA->userdata = tmp;
        DATA->userdata_count *= 2;
    }

    void *tmp = (void*)realloc(DATA->userdata[ino - 1], ostat.st_blocks * BLOCK_SIZE);
    if (tmp == NULL) {
        mytmpfs_delete_stat(nino, DATA);
        free(ppath);
        return -ENOMEM;
    }
    DATA->userdata[ino - 1] = tmp;
    
    DATA->userdata[nino - 1] = (void*)malloc(nstat.st_blocks * BLOCK_SIZE);
    if (DATA->userdata[nino - 1] == NULL) {
        mytmpfs_delete_stat(nino, DATA);
        free(ppath);
        ostat.st_size -= sizeof(struct dirent);
        ostat.st_blocks = (USERDATA_SHIFT + ostat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        DATA->userdata[ino - 1] = (void*)realloc(DATA->userdata[ino - 1], ostat.st_blocks * BLOCK_SIZE);
        return -ENOMEM;
    }
    USERDATA_SIZE(nino) = 0;
    USERDATA_ACNT(nino) = 0;

    mytmpfs_set_stat(ino, &ostat, DATA);

    struct dirent *de = (struct dirent*)(USERDATA_RAW(ino) + USERDATA_SIZE(ino));
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, name);
    de->d_ino = nino;
    USERDATA_SIZE(ino) += sizeof(struct dirent);

    free(ppath);

    return 0;
}

int mytmpfs_unlink(const char *path)
{
    const char *lst = strrchr(path, '/');
    const char *name = lst + 1;
    char *ppath = (char*)malloc(lst - path + 1);
    memcpy(ppath, path, lst - path);
    ppath[lst - path] = '\0';

    ino_t ino;
    if (mytmpfs_resolve_path(ppath, &ino) != 0) {
        free(ppath);
        return -ENOENT;
    }

    struct dirent *de;
    unsigned long res = ULONG_MAX;
    for (unsigned long i = 0; i < USERDATA_SIZE(ino); i += sizeof(struct dirent)) {
        de = (struct dirent*)(USERDATA_RAW(ino) + i);
        if (strcmp(name, de->d_name) == 0) {
            res = i;
            break;
        }
    }
    if (res == ULONG_MAX) {
        return -ENOENT;
    }

    if (USERDATA_ACNT(de->d_ino) != 0) {
        return -EBUSY;
    }

    free(DATA->userdata[de->d_ino - 1]);
    mytmpfs_delete_stat(de->d_ino, DATA);

    struct stat stbuf;
    mytmpfs_get_stat(ino, &stbuf, DATA);
    time(&stbuf.st_atime);
    stbuf.st_mtime = stbuf.st_atime;
    stbuf.st_nlink--;

    memcpy(USERDATA_RAW(ino) + res, USERDATA_RAW(ino) + res + sizeof(struct dirent), USERDATA_SIZE(ino) - res);
    
    stbuf.st_size -= sizeof(struct dirent);
    stbuf.st_blocks = (USERDATA_SHIFT + stbuf.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    USERDATA_SIZE(ino) -= sizeof(struct dirent);
    DATA->userdata[ino - 1] = realloc(DATA->userdata[ino - 1], stbuf.st_blocks * BLOCK_SIZE);

    mytmpfs_set_stat(ino, &stbuf, DATA);
    return 0;
}

int main(int argc, char *argv[])
{
    struct mytmpfs_data *mytmpfs_dt = (struct mytmpfs_data*)malloc(sizeof(struct mytmpfs_data));

    if (mytmpfs_dt == NULL) {
        return 1;
    }

    #ifdef DEBUG
    mytmpfs_dt->dbg = fopen("dbglog.txt", "w");
    #endif

    if (stat(argv[argc - 1], &mytmpfs_dt->root_premount_stat) != 0) {
        printf("Unable to get stat of %s\n", argv[argc - 1]);
        free(mytmpfs_dt);
        return 1;
    }
    if ((mytmpfs_dt->root_premount_stat.st_mode & S_IFMT) != S_IFDIR) {
        printf("%s is not a directory\n", argv[argc - 1]);
        free(mytmpfs_dt);
        return 1;
    }

    if (mytmpfs_init_stat(mytmpfs_dt) != 0) {
        printf("Unable to allocate the minimal memory required\n");
        free(mytmpfs_dt);
        return 1;
    }
    if ((mytmpfs_dt->userdata = (void**)malloc(sizeof(void*))) == NULL) {
        printf("Unable to allocate the minimal memory required\n");
        mytmpfs_free_stat_pages(mytmpfs_dt);
        free(mytmpfs_dt);
        return 1;
    }
    if ((mytmpfs_dt->userdata[0] = (void*)malloc((USERDATA_SHIFT + 2 * sizeof(struct dirent) + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE)) == NULL) {
        printf("Unable to allocate the minimal memory required\n");
        free(mytmpfs_dt->userdata);
        mytmpfs_free_stat_pages(mytmpfs_dt);
        free(mytmpfs_dt);
        return 1;
    }
    mytmpfs_dt->userdata_blocks_allocated = (USERDATA_SHIFT + 2 * sizeof(struct dirent) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    mytmpfs_dt->userdata_count = 1;

    return fuse_main(argc, argv, &mytmpfs_op, (void*)mytmpfs_dt);
}
