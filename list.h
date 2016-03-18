
#ifndef _INIT_LIST_H_
#define _INIT_LIST_H_
#include <stddef.h>
struct listnode
{
    struct listnode *next;
    struct listnode *prev;
};

#define node_to_item(node, container, member) \
    (container *) (((char*) (node)) - offsetof(container, member))

#define list_declare(name) \
    struct listnode name = { \
        .next = &name, \
        .prev = &name, \
    }

#define list_for_each(node, list) \
    for (node = (list)->next; node != (list); node = node->next)

#define list_for_each_reverse(node, list) \
    for (node = (list)->prev; node != (list); node = node->prev)

static void list_init(struct listnode *list);
static void list_add_tail(struct listnode *list, struct listnode *item);
static void list_remove(struct listnode *item);

#define list_empty(list) ((list) == (list)->next)
#define list_head(list) ((list)->next)
#define list_tail(list) ((list)->prev)

#endif
