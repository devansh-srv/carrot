#include "../lib/hashtable.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
const size_t KEY_REHASHING_WORK = 128;
const size_t MAX_LOAD_FACTOR = 8;
// init the ht->tab array and provide the mask val n-1
/* ht->tab is a array of linked lists */
/* where each node is a HashNode  */
/* ht->tab[x] refs to the linked list @ index x */
/* out hashtable is going to be an array of slots where each slot stores the
 * (K,V) as a intrinsic linked list */
void ht_init(struct HashTable *ht, size_t n) {
  assert(n > 0 && (n & (n - 1)) == 0); // makes sure that n is a power of 2
  ht->tab = (struct HashNode **)calloc(n, sizeof(struct HashNode *));
  ht->size = 0;
  ht->mask = n - 1;
}

// node is added at the front of the linked list ht->tab
void ht_insert(struct HashTable *ht, struct HashNode *node) {
  size_t pos = node->hashCode & ht->mask;
  struct HashNode *next = ht->tab[pos];
  node->next = next;
  ht->tab[pos] = node;
  ht->size++;
}

// typical lookup function
// returns HashNode** from so that I always know what points to the pointer that
// points to the node that has to be deleted
// like *from is as if it is prev->next
// from points to a pointer that points to the node to be deleted
struct HashNode **ht_lookup(struct HashTable *ht, struct HashNode *key,
                            bool (*eq)(struct HashNode *, struct HashNode *)) {
  if (ht->tab == NULL)
    return NULL;
  size_t pos = key->hashCode & ht->mask;
  struct HashNode **from = &ht->tab[pos];
  for (struct HashNode *curr; (curr = *from) != NULL; from = &curr->next) {
    if (curr->hashCode == key->hashCode && eq(curr, key)) {
      return from;
    }
  }
  return NULL;
}
// detatch node (uses ht_lookup)
struct HashNode *ht_det(struct HashTable *ht, struct HashNode **from) {
  struct HashNode *toDel = *from;
  *from = toDel->next;
  ht->size--;
  return toDel;
}

void hm_help_rehashing(struct HashMap *hm) {
  size_t n = 0;
  while (n < KEY_REHASHING_WORK && hm->older.size > 0) {
    struct HashNode **from = &hm->older.tab[hm->migrate_pos];
    if (!*from) {
      hm->migrate_pos++;
      continue;
    }
    // removes the node **from from older and adds it to newer
    ht_insert(&hm->newer, ht_det(&hm->older, from));
    n++;
  }
  if (hm->older.size == 0 && hm->older.tab) {
    free(hm->older.tab);
    hm->older = (struct HashTable){};
  }
}
// rehashing
// use older and resize newer
void hm_rehashing(struct HashMap *hm) {
  assert(hm->older.tab == NULL);
  hm->older = hm->newer;
  ht_init(&hm->newer, (hm->newer.mask + 1) * 2);
  hm->migrate_pos = 0;
}

// hashmap lookup
struct HashNode *hm_lookup(struct HashMap *hm, struct HashNode *key,
                           bool (*eq)(struct HashNode *, struct HashNode *)) {
  // move some node to the newer everytime hm_lookup is called
  hm_help_rehashing(hm);
  struct HashNode **from = ht_lookup(&hm->newer, key, eq);
  if (!from)
    from = ht_lookup(&hm->older, key, eq);
  return from ? *from : NULL;
}

struct HashNode *hm_del(struct HashMap *hm, struct HashNode *key,
                        bool (*eq)(struct HashNode *, struct HashNode *)) {
  // move some node to the newer everytime hm_del is called
  hm_help_rehashing(hm);
  struct HashNode **from = ht_lookup(&hm->newer, key, eq);
  if (from) {
    return ht_det(&hm->newer, from);
  }
  from = ht_lookup(&hm->older, key, eq);
  if (from) {
    return ht_det(&hm->older, from);
  }
  return NULL;
}

void hm_insert(struct HashMap *hm, struct HashNode *node) {
  if (!hm->newer.tab) {
    ht_init(&hm->newer, 4);
  }
  ht_insert(&hm->newer, node);
  if (!hm->older.tab) {
    // we need to find the number of keys allowed
    // load_factor * num of slots
    size_t threshold = (hm->newer.mask + 1) * MAX_LOAD_FACTOR;
    // check whether do I need to trigger rehashing
    if (hm->newer.size >= threshold) {
      hm_rehashing(hm);
    }
  }
  hm_help_rehashing(hm);
}

void hm_free(struct HashMap *hm) {
  free(hm->older.tab);
  free(hm->newer.tab);
  hm = (struct HashMap *){};
}

size_t hm_size(struct HashMap *hm) { return hm->newer.size + hm->older.size; }
