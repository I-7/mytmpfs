#include "stat_tree.hpp"

// Binomial heap is used to store stats and find them by inode number.

void* mytmpfs_allocate_stats_page(mytmpfs_data *data)
{
    void* new_page = malloc(BLOCKS_PER_PAGE * BLOCK_SIZE);
    if (new_page == NULL) {
        return NULL;
    }

    void** new_pointer;
    if (data->stats_pages_allocated == 0) {
        new_pointer = (void**)malloc((data->stats_pages_allocated + 1) * sizeof(void*));
    } else {
        new_pointer = (void**)realloc(data->stats_pages, (data->stats_pages_allocated + 1) * sizeof(void*));
    }
    if (new_pointer == NULL) {
        free(new_page);
        return NULL;
    }

    data->stats_pages = new_pointer;
    data->stats_pages[data->stats_pages_allocated++] = new_page;
    return new_page;
}

int mytmpfs_init_stat(mytmpfs_data *data)
{
    data->stats_pages_allocated = 0;
    void* stats_page = mytmpfs_allocate_stats_page(data);
    if (stats_page == nullptr) {
        errno = ENOMEM;
        return -1;
    }

    stat_tree_roots *tree = (stat_tree_roots*)stats_page;
    tree->roots_sz = 0;
    tree->ptr = (char*)stats_page + sizeof(stat_tree_roots);
    for (unsigned long i = 0; i < STATS_ROOTS; i++) {
        tree->roots[i] = NULL;
    }

    return 0;
}

int mytmpfs_create_stat(const struct stat *statbuf, __ino_t *ino, mytmpfs_data *data)
{
    stat_tree_roots *tree = (stat_tree_roots*)(data->stats_pages[0]);
    unsigned long id_first_empty_root = tree->roots_sz;
    unsigned long id_place = tree->roots_sz;

    unsigned long res = 1;
    for (unsigned long i = 0; i < tree->roots_sz; i++) {
        if (tree->roots[i] != NULL) {
            if (((stat_tree_node*)(tree->roots[i]))->empty_stats > 0) {
                id_place = i;
            }
        } else {
            if (id_first_empty_root == tree->roots_sz) {
                id_first_empty_root = i;
            }
        }
    }

    if (id_place == tree->roots_sz) {
        for (unsigned long i = 0; i < tree->roots_sz; i++) {
            if (tree->roots[i] != NULL) {
                res += (1ll << i) * STATS_PER_PAGE;
            }
        }
        if (id_first_empty_root == tree->roots_sz) {
            tree->roots_sz++;
        }

        void *loc = mytmpfs_allocate_stats_page(data);
        if (loc == NULL) {
            errno = ENOMEM;
            return -1;
        }

        // We won't allocate more than 1 page for the path, so we can do it beforehands
        // So no changes would be made in case of unsucessful allocation
        void* nxtpage = nullptr;
        if ((char*)tree->ptr + sizeof(stat_tree_node) * id_first_empty_root > (char*)tree->lptr + BLOCKS_PER_PAGE * BLOCK_SIZE) {
            nxtpage = mytmpfs_allocate_stats_page(data);
            if (nxtpage == NULL) {
                free(loc);
                errno = ENOMEM;
                return -1;
            }
        }

        stat_tree_leaf *leaf = (stat_tree_leaf*)loc;
        leaf->type = 1;
        leaf->empty_stats = STATS_PER_PAGE;
        leaf->empty_stats_loc = UINT64_MAX;

        leaf->empty_stats--;
        leaf->empty_stats_loc ^= 1;
        memcpy((void*)(&leaf->stats[0]), statbuf, sizeof(struct stat));
        leaf->stats[0].st_ino = res;
        if (ino != nullptr) {
            *ino = res;
        }

        for (unsigned int i = 0; i < id_first_empty_root; i++) {
            if ((char*)tree->ptr + sizeof(stat_tree_node) > (char*)tree->lptr + BLOCKS_PER_PAGE * BLOCK_SIZE) {
                tree->ptr = nxtpage;
                tree->lptr = nxtpage;
            }
            stat_tree_node *new_node = (stat_tree_node*)tree->ptr;
            new_node->type = 0;
            new_node->left_child = tree->roots[i];
            new_node->right_child = loc;
            new_node->empty_stats = STATS_PER_PAGE - 1;
            tree->roots[i] = NULL;
            loc = tree->ptr;
            tree->ptr = (char*)tree->ptr + sizeof(stat_tree_node);
        }
        tree->roots[id_first_empty_root] = loc;

        return 0;
    }

    for (unsigned long i = id_place + 1; i < tree->roots_sz; i++) {
        if (tree->roots[i] != NULL) {
            res += (1ll << i) * STATS_PER_PAGE;
        }
    }

    stat_tree_node *node = (stat_tree_node*)(tree->roots[id_place]);
    while (!STAT_TREE_NODE_LEAF(*node)) {
        id_place--;
        node->empty_stats--;
        if (((stat_tree_node*)(node->left_child))->empty_stats > 0) {
            node = (stat_tree_node*)(node->left_child);
        } else {
            node = (stat_tree_node*)(node->right_child);
            res += (1ll << id_place) * STATS_PER_PAGE;
        }
    }

    stat_tree_leaf *leaf = (stat_tree_leaf*)node;
    unsigned long x = ffsl(leaf->empty_stats_loc) - 1;
    leaf->empty_stats_loc ^= 1ll << x;
    leaf->empty_stats--;
    memcpy(&leaf->stats[x], statbuf, sizeof(struct stat));
    res += x;
    leaf->stats[x].st_ino = res;
    if (ino != nullptr) {
        *ino = res;
    }
    return 0;
}

int mytmpfs_find_stat_internal(__ino_t ino, struct stat **statbuf, const int is_delete, mytmpfs_data *data)
{
    unsigned long x = 1;

    stat_tree_roots *tree = (stat_tree_roots*)(data->stats_pages[0]);
    unsigned long id = tree->roots_sz;

    while (id > 0) {
        if (tree->roots[id - 1] != 0) {
            if (x + (1ll << (id - 1)) * STATS_PER_PAGE <= ino) {
                x += (1ll << (id - 1)) * STATS_PER_PAGE;
            } else {
                break;
            }
        }
        id--;
    }

    if (id == 0) {
        return -1;
    }

    id--;

    stat_tree_node *node = (stat_tree_node*)(tree->roots[id]);
    while (!STAT_TREE_NODE_LEAF(*node)) {
        if (is_delete) {
            node->empty_stats++;
        }
        if (x + (1ll << (id - 1)) * STATS_PER_PAGE <= ino) {
            x += (1ll << (id - 1)) * STATS_PER_PAGE;
            node = (stat_tree_node*)(node->right_child);
        } else {
            node = (stat_tree_node*)(node->left_child);
        }
        id--;
    }

    stat_tree_leaf *leaf = (stat_tree_leaf*)(node);
    if (is_delete) {
        node->empty_stats++;
    }
    if (leaf->empty_stats_loc & (1ll << (ino - x))) {
        return -1;
    }
    if (is_delete) {
        leaf->empty_stats_loc |= (1ll << (ino - x));
    }
    if (statbuf != NULL) {
        *statbuf = &leaf->stats[(ino - x)];
    }
    return 0;
}

int mytmpfs_get_stat(__ino_t ino, struct stat *statbuf, mytmpfs_data *data)
{
    struct stat *stat_ptr;
    if (mytmpfs_find_stat_internal(ino, &stat_ptr, 0, data) == -1) {
        errno = ENOENT;
        return -1;
    }
    memcpy(statbuf, stat_ptr, sizeof(struct stat));
    return 0;
}

int mytmpfs_set_stat(__ino_t ino, struct stat *statbuf, mytmpfs_data *data)
{
    struct stat *stat_ptr;
    if (mytmpfs_find_stat_internal(ino, &stat_ptr, 0, data) == -1) {
        errno = ENOENT;
        return -1;
    }
    memcpy(stat_ptr, statbuf, sizeof(struct stat));
    return 0;
}

void mytmpfs_delete_stat(__ino_t ino, mytmpfs_data *data)
{
    if (mytmpfs_find_stat_internal(ino, NULL, 0, data) == -1) {
        return;
    }
    mytmpfs_find_stat_internal(ino, NULL, 1, data);
    return;
}

