#include <zephyr/ztest.h>
#include <zephyr/sys/list_gen.h>

/* Custom list and node definitions using list_gen macros */

struct sys_mynode {
    struct sys_mynode *next;
};

typedef struct sys_mynode sys_mynode_t;

struct sys_mylist {
    sys_mynode_t *head;
    sys_mynode_t *tail;
};

typedef struct sys_mylist sys_mylist_t;

static inline void sys_mylist_init(sys_mylist_t *list)
{
    list->head = NULL;
    list->tail = NULL;
}

#define SYS_MYLIST_STATIC_INIT(ptr) { NULL, NULL }

static inline sys_mynode_t *z_mynode_next_peek(const sys_mynode_t *node)
{
    return node->next;
}

static inline void z_mynode_next_set(sys_mynode_t *parent, sys_mynode_t *child)
{
    parent->next = child;
}

static inline void z_mylist_head_set(sys_mylist_t *list, sys_mynode_t *node)
{
    list->head = node;
}

static inline void z_mylist_tail_set(sys_mylist_t *list, sys_mynode_t *node)
{
    list->tail = node;
}

static inline sys_mynode_t *sys_mylist_peek_head(const sys_mylist_t *list)
{
    return list->head;
}

static inline sys_mynode_t *sys_mylist_peek_tail(const sys_mylist_t *list)
{
    return list->tail;
}

Z_GENLIST_IS_EMPTY(mylist)
Z_GENLIST_PEEK_NEXT_NO_CHECK(mylist, mynode)
Z_GENLIST_PEEK_NEXT(mylist, mynode)
Z_GENLIST_PREPEND(mylist, mynode)
Z_GENLIST_APPEND(mylist, mynode)
Z_GENLIST_APPEND_LIST(mylist, mynode)
Z_GENLIST_MERGE_LIST(mylist, mynode)
Z_GENLIST_INSERT(mylist, mynode)
Z_GENLIST_GET_NOT_EMPTY(mylist, mynode)
Z_GENLIST_GET(mylist, mynode)
Z_GENLIST_REMOVE(mylist, mynode)
Z_GENLIST_FIND_AND_REMOVE(mylist, mynode)
Z_GENLIST_FIND(mylist, mynode)
Z_GENLIST_LEN(mylist, mynode)

struct container {
    sys_mynode_t node;
    int data;
};

static sys_mylist_t clist;
static struct container nodes[4];

ZTEST(dlist_api, test_list_gen_basic)
{
    sys_mylist_init(&clist);
    zassert_true(sys_mylist_is_empty(&clist), "list not empty");

    sys_mylist_append(&clist, &nodes[0].node);
    sys_mylist_prepend(&clist, &nodes[1].node);

    zassert_equal(sys_mylist_peek_head(&clist), &nodes[1].node, "wrong head");
    zassert_equal(sys_mylist_peek_tail(&clist), &nodes[0].node, "wrong tail");
    zassert_equal(sys_mylist_len(&clist), 2, "wrong len");

    sys_mylist_insert(&clist, &nodes[1].node, &nodes[2].node);
    zassert_equal(sys_mylist_len(&clist), 3);

    sys_mynode_t *prev = NULL;
    zassert_true(sys_mylist_find(&clist, &nodes[2].node, &prev));
    zassert_equal(prev, &nodes[1].node, "find prev wrong");

    sys_mylist_remove(&clist, prev, &nodes[2].node);
    zassert_equal(sys_mylist_len(&clist), 2);

    zassert_true(sys_mylist_find_and_remove(&clist, &nodes[0].node));
    zassert_equal(sys_mylist_len(&clist), 1);

    sys_mynode_t *n = sys_mylist_get_not_empty(&clist);
    zassert_equal(n, &nodes[1].node);
    zassert_true(sys_mylist_is_empty(&clist));
}

ZTEST(dlist_api, test_list_gen_merge)
{
    sys_mylist_t list1, list2;
    sys_mylist_init(&list1);
    sys_mylist_init(&list2);

    sys_mylist_append(&list1, &nodes[0].node);
    sys_mylist_append(&list1, &nodes[1].node);
    sys_mylist_append(&list2, &nodes[2].node);
    sys_mylist_append(&list2, &nodes[3].node);

    sys_mylist_merge_mylist(&list1, &list2);
    zassert_true(sys_mylist_is_empty(&list2));
    zassert_equal(sys_mylist_len(&list1), 4);
    zassert_equal(sys_mylist_peek_tail(&list1), &nodes[3].node);
}
