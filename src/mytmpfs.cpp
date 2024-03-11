#include "mytmpfs.h"
#include "stat_tree.h"
#include <iostream>

static const struct fuse_operations mytmpfs_op = {
    .getattr = mytmpfs_getattr,
    .mkdir = mytmpfs_mkdir,
    .opendir = mytmpfs_opendir,
    .readdir = mytmpfs_readdir,
    .releasedir = mytmpfs_releasedir,
    .init = mytmpfs_init,
};

int resolve_path(const char *path, ino_t *ino)
{
    *ino = 1;
    dirent* de;
    size_t i = 1;
    while (i < strlen(path)) {
        size_t len = strchrnul(path + i, '/') - (path + i);
        int found = 0;
        for (unsigned int j = 0; j < ((unsigned long*)DATA->userdata[*ino - 1])[0]; j += sizeof(struct dirent)) {
            de = (dirent*)((unsigned char*)DATA->userdata[*ino - 1] + sizeof(unsigned long) + j);
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
    return (void*)DATA;
}

int mytmpfs_getattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    ino_t ino;
    if (resolve_path(path, &ino) != 0) {
        return -ENOENT;
    }
    get_stat(ino, statbuf, DATA);
    return 0;
}

int mytmpfs_opendir(const char *path, struct fuse_file_info *fi)
{
    ino_t ino;
    if (resolve_path(path, &ino) != 0) {
        return -ENOENT;
    }
    fi->fh = ino;
    return 0;
}

int mytmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    struct dirent *de;
    for (unsigned int j = 0; j < ((unsigned long*)DATA->userdata[fi->fh - 1])[0]; j += sizeof(struct dirent)) {
        de = (dirent*)((char*)DATA->userdata[fi->fh - 1] + sizeof(unsigned long) + j);
        if (filler(buf, de->d_name, NULL, 0, fuse_fill_dir_flags()) != 0) {
            return -ENOMEM;
        }
    }
    return 0;
}

int mytmpfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    if (fi != NULL) {
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
    if (resolve_path(ppath, &ino) != 0) {
        free(ppath);
        return -ENOENT;
    }

    struct stat ostat, nstat;
    get_stat(ino, &ostat, DATA);

    memcpy(&nstat, &ostat, sizeof(struct stat));

    ostat.st_nlink++;
    ostat.st_size += sizeof(dirent);
    ostat.st_blocks = (ostat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    time(&ostat.st_atime);
    ostat.st_mtime = ostat.st_atime;

    nstat.st_mode = mode | S_IFDIR;
    nstat.st_nlink = 2;
    nstat.st_size = sizeof(unsigned long) + 2 * sizeof(dirent);
    nstat.st_blocks = (nstat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    nstat.st_ctime = ostat.st_atime;
    nstat.st_atime = ostat.st_atime;
    nstat.st_mtime = ostat.st_atime;

    ino_t nino;
    if (create_stat(&nstat, &nino, DATA) != 0) {
        free(ppath);
        return -ENOMEM;
    }

    if (nino >= DATA->userdata_count + 1) {
        void **tmp = (void**)realloc(DATA->userdata, 2 * DATA->userdata_count * sizeof(void*));
        if (tmp == NULL) {
            delete_stat(nino, DATA);
            free(ppath);
            return -ENOMEM;
        }
        DATA->userdata = tmp;
        DATA->userdata_count *= 2;
    }

    void *tmp = (void*)realloc(DATA->userdata[ino - 1], ostat.st_blocks * BLOCK_SIZE);
    if (tmp == NULL) {
        delete_stat(nino, DATA);
        free(ppath);
        return -ENOMEM;
    }
    DATA->userdata[ino - 1] = tmp;
    
    DATA->userdata[nino - 1] = (void*)malloc(nstat.st_blocks * BLOCK_SIZE);
    if (DATA->userdata[nino - 1] == NULL) {
        delete_stat(nino, DATA);
        free(ppath);
        ostat.st_size -= sizeof(dirent);
        ostat.st_blocks = (ostat.st_blocks + BLOCK_SIZE - 1) / BLOCK_SIZE;
        DATA->userdata[ino - 1] = (void*)realloc(DATA->userdata[ino - 1], ostat.st_blocks * BLOCK_SIZE);
        return -ENOMEM;
    }
    ((unsigned long*)DATA->userdata[nino - 1])[0] = 0;

    set_stat(ino, &ostat, DATA);

    dirent *de = (dirent*)((char*)DATA->userdata[ino - 1] + sizeof(unsigned long) + ((unsigned long*)DATA->userdata[ino - 1])[0]);
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, name);
    de->d_ino = nino;
    ((unsigned long*)DATA->userdata[ino - 1])[0] += sizeof(dirent);

    de = (dirent*)((char*)DATA->userdata[nino - 1] + sizeof(unsigned long) + ((unsigned long*)DATA->userdata[nino - 1])[0]);
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, ".");
    de->d_ino = nino;
    ((unsigned long*)DATA->userdata[nino - 1])[0] += sizeof(dirent);

    de = (dirent*)((char*)DATA->userdata[nino - 1] + sizeof(unsigned long) + ((unsigned long*)DATA->userdata[nino - 1])[0]);
    memset(de->d_name, 0, sizeof(de->d_name));
    strcpy(de->d_name, "..");
    de->d_ino = ino;
    ((unsigned long*)DATA->userdata[nino - 1])[0] += sizeof(dirent);

    free(ppath);

    return 0;
}

int main(int argc, char *argv[])
{
    struct stat statbuf;
    DIR *d = NULL;
    struct dirent *de;

    mytmpfs_data *mytmpfs_dt = (mytmpfs_data*)malloc(sizeof(mytmpfs_data));

    if (mytmpfs_dt == NULL) {
        goto ex0;
    }

    #ifdef DEBUG
    new (&mytmpfs_dt->dbg) std::ofstream("dbglog.txt");
    #endif

    if (init_stat(mytmpfs_dt) != 0) {
        goto ex1;
    }

    if (stat(argv[argc - 1], &statbuf) != 0) {
        goto ex2;
    }
    statbuf.st_size = sizeof(unsigned long) + 2 * sizeof(dirent);
    statbuf.st_blocks = (sizeof(unsigned long) + 2 * sizeof(dirent) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    statbuf.st_mtime = time(&statbuf.st_atime);
    if (create_stat(&statbuf, NULL, mytmpfs_dt) != 0) {
        goto ex2;
    }

    mytmpfs_dt->userdata_count = 1;
    mytmpfs_dt->userdata_blocks_allocated = 0;
    mytmpfs_dt->userdata = (void**)malloc(sizeof(void*));
    if (mytmpfs_dt->userdata == NULL) {
        goto ex2;
    }
    mytmpfs_dt->userdata[0] = (void*)malloc(statbuf.st_blocks * BLOCK_SIZE);
    if (mytmpfs_dt->userdata[0] == NULL) {
        goto ex3;
    }
    ((unsigned long*)mytmpfs_dt->userdata[0])[0] = 0;

    d = opendir(argv[argc - 1]);
    if (d == NULL) {
        goto ex3;
    }

    for (int i = 0; i < 2; i++) {
        de = readdir(d);
        void *ptr = (char*)mytmpfs_dt->userdata[0] + sizeof(unsigned long) + ((unsigned long*)mytmpfs_dt->userdata[0])[0];
        memcpy(ptr, de, sizeof(dirent));
        if (strcmp(".", de->d_name) == 0) {
            ((struct dirent*)ptr)->d_ino = 1;
        }
        ((unsigned long*)mytmpfs_dt->userdata[0])[0] += sizeof(dirent);
    }
    de = readdir(d);
    if (de != NULL) {
        printf("Mount directory is not empty\n");
        goto ex4;
    }
    closedir(d);

    return fuse_main(argc, argv, &mytmpfs_op, (void*)mytmpfs_dt);

ex4:
    closedir(d);
ex3:
    free(mytmpfs_dt->userdata);
ex2:
    for (unsigned long i = 0; i < mytmpfs_dt->stats_pages_allocated; i++) {
        free(mytmpfs_dt->stats_pages[i]);
    }
    free(mytmpfs_dt->stats_pages);
ex1:
    free(mytmpfs_dt);
ex0:
    return 1;
}
