#include <stdbool.h>
#include <stddef.h>

struct DList {
  struct DList *prev;
  struct DList *next;
};
void dlist_init(struct DList *node);
bool dlist_empty(struct DList *node);
void dlist_detatch(struct DList *node);
void dlist_insert(struct DList *target, struct DList *newNode);
