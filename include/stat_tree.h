#ifndef STAT_TREE_H_
#define STAT_TREE_H_

#include "mytmpfs.h"

struct stat_tree_leaf
{
    unsigned long type;
    unsigned long empty_stats;
    unsigned long empty_stats_loc;
    struct stat stats[STATS_PER_PAGE];
};

struct stat_tree_node
{
    unsigned long type;
    unsigned long empty_stats;
    void* left_child;
    void* right_child;
};

#define STAT_TREE_NODE_LEAF(nd) ((nd).type == 1)

struct stat_tree_roots
{
    unsigned long roots_sz;
    void *lptr, *ptr;
    void *roots[STATS_ROOTS];
};

void* mytmpfs_allocate_stats_page(struct mytmpfs_data *data);
int mytmpfs_init_stat(struct mytmpfs_data *data);
int mytmpfs_create_stat(const struct stat *statbuf, __ino_t *ino, struct mytmpfs_data *data);
int mytmpfs_find_stat_internal(__ino_t ino, struct stat **statbuf, const int is_delete, struct mytmpfs_data *data);
int mytmpfs_get_stat(__ino_t ino, struct stat *statbuf, struct mytmpfs_data *data);
int mytmpfs_set_stat(__ino_t ino, struct stat *statbuf, struct mytmpfs_data *data);
void mytmpfs_delete_stat(__ino_t ino, struct mytmpfs_data *data);
void mytmpfs_free_stat_pages(struct mytmpfs_data *data);

#endif
