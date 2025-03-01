#include "../lib/list.h"
// the dummy node
void dlist_init(struct DList *node) { node->prev = node->next = node; };
// returns true if the list has only the dummy node
bool dlist_empty(struct DList *node) { return node->next == node; };
// detatch the prev and next from the given struct DList* node
void dlist_detatch(struct DList *node) {
  struct DList *prev = node->prev;
  struct DList *next = node->next;
  prev->next = next;
  next->prev = prev;
};
// insert the newNode between the target and the its prev
void dlist_insert(struct DList *target, struct DList *newNode) {
  struct DList *prev = target->prev;
  prev->next = newNode;
  newNode->prev = prev;
  newNode->next = target;
  target->prev = newNode;
};
