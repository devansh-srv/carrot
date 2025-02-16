#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// hashtable linked node def
struct HashNode {
  struct HashNode *next;
  uint64_t hashCode;
};

// hashtable struct
struct HashTable {
  struct HashNode **tab;
  size_t size;
  size_t mask;
};

// redis hashmap implementation with 2 hashtables
// to support resizing when high load factor
struct HashMap {
  struct HashTable newer;
  struct HashTable older;
  size_t migrate_pos;
};

struct HashNode *hm_lookup(struct HashMap *hm, struct HashNode *key,
                           bool (*eq)(struct HashNode *, struct HashNode *));
void hm_insert(struct HashMap *hm, struct HashNode *node);
struct HashNode *hm_del(struct HashMap *hm, struct HashNode *key,
                        bool (*eq)(struct HashNode *, struct HashNode *));
void hm_free(struct HashMap *hm);
size_t hm_size(struct HashMap *hm);
void hm_each(struct HashMap *hm, uint8_t **incoming, int *incoming_size,
             int *incoming_cap,
             bool (*f)(struct HashNode *, uint8_t **, int *, int *));
