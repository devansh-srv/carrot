#include "../lib/avl.h"
#include "../lib/hashtable.h"

// data can be indexed by name or by (score,name)
struct Zset {
  struct AVLNode *root;
  struct HashMap hm;
};

struct Znode {
  struct AVLNode tree;
  struct HashNode hmap;
  double score;
  size_t len;
  char name[0];
};

// apis
bool zset_insert(struct Zset *zset, char *name, size_t len, double score);
struct Znode *zset_lookup(struct Zset *zset, char *name, size_t len);
void zset_del(struct Zset *zset, struct Znode *znode);
struct Znode *zset_seek(struct Zset *zset, char *name, size_t len,
                        double score);
void zset_clear(struct Zset *zset);
struct Znode *znode_offset(struct Znode *znode, int64_t offset);
int64_t znode_rank(struct Znode *znode);
