#include "../lib/utils.h"
#include "../lib/zest.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void msg_error(const char *msg) { perror(msg); }

// function to create a new Znode
struct Znode *znode_new(char *name, size_t len, double score) {
  struct Znode *znode = (struct Znode *)malloc(sizeof(struct Znode));
  if (!znode) {
    msg_error("znode malloc failed in src/Zset.c");
    return NULL;
  }
  avl_init(&znode->tree);
  znode->hmap.next = NULL;
  znode->hmap.hashCode = str_hash((uint8_t *)name, len);
  znode->score = score;
  znode->len = len;
  memcpy(&znode->name[0], name, len);
  return znode;
}

// function for deleting znode
void znode_del(struct Znode *znode) { free(znode); }

size_t min(size_t a, size_t b) { return a < b ? a : b; }

// compares the node val with given val in order to simplify the insetion proces
bool zless_help(struct AVLNode *node, char *name, size_t len, double score) {
  struct Znode *znode = container_of(node, struct Znode, tree);
  if (znode->score != score)
    return znode->score < score;
  int r = memcmp(znode->name, name, min(znode->len, len));
  return r != 0 ? r < 0 : znode->len < len;
}
// cmp for (score,name) tuple
bool zless(struct AVLNode *a, struct AVLNode *b) {
  struct Znode *node = container_of(b, struct Znode, tree);
  return zless_help(a, node->name, node->len, node->score);
}

// avl tree insertion
void avl_insert(struct Zset *zset, struct Znode *node) {
  struct AVLNode *parent = NULL;
  struct AVLNode **from = &zset->root;
  while (*from) {
    parent = *from;
    from = zless(&node->tree, parent) ? &parent->left : &parent->right;
  }
  *from = &node->tree;
  node->tree.parent = parent;
  /* returns the new root */
  zset->root = avl_fix(&node->tree);
}

// function to handle score changes
// we need to first detach the existing node and then attach the edited node
void zset_update(struct Zset *zset, struct Znode *znode, double score) {
  if (znode->score == score)
    return;
  /* avl_del returns the new root  */
  zset->root = avl_del(&znode->tree);
  avl_init(&znode->tree);
  znode->score = score;
  avl_insert(zset, znode);
}

// function to handle if the node exists already
bool zset_insert(struct Zset *zset, char *name, size_t len, double score) {
  struct Znode *node = zset_lookup(zset, name, len);
  /* if the node already exists it should just change the score in it */
  if (node) {
    zset_update(zset, node, score);
    /* returns false when updated */
    return false;
  }
  node = znode_new(name, len, score);
  hm_insert(&zset->hm, &node->hmap);
  avl_insert(zset, node);
  /* returns true when inserted */
  return true;
}
/* helper struct */
struct HashKey {
  struct HashNode node;
  const char *name;
  size_t len;
};

// comparator func for comparing the name field while looking for hm_lookup()
bool hcmp(struct HashNode *a, struct HashNode *b) {
  struct Znode *aa = container_of(a, struct Znode, hmap);
  struct HashKey *bb = container_of(b, struct HashKey, node);
  if (aa->len != bb->len)
    return false;
  return 0 == memcmp(aa->name, bb->name, aa->len);
}
// zset lookup
struct Znode *zset_lookup(struct Zset *zset, char *name, size_t len) {
  if (!zset->root)
    return NULL;
  struct HashKey key;
  key.node.hashCode = str_hash((uint8_t *)name, len);
  key.len = len;
  key.name = name;
  struct HashNode *lookup = hm_lookup(&zset->hm, &key.node, &hcmp);
  return lookup ? container_of(lookup, struct Znode, hmap) : NULL;
}
// deletes(from hashMap and avl tree) and frees the data held by znode
void zset_del(struct Zset *zset, struct Znode *znode) {
  struct HashKey key;
  key.node.hashCode = znode->hmap.hashCode;
  key.name = znode->name;
  key.len = znode->len;
  struct HashNode *del = hm_del(&zset->hm, &key.node, &hcmp);
  assert(del);
  zset->root = avl_del(&znode->tree);
  znode_del(znode);
}
// searches and stores potential candidate for node>= key
struct Znode *zset_seek(struct Zset *zset, char *name, size_t len,
                        double score) {
  struct AVLNode *node = NULL;
  struct AVLNode *found = NULL;
  for (node = zset->root; node;) {
    if (zless_help(node, name, len, score)) {
      node = node->right;
    } else {
      found = node;
      node = node->left;
    }
  }
  return found ? container_of(found, struct Znode, tree) : NULL;
}
// offset node
struct Znode *znode_offset(struct Znode *znode, int64_t offset) {
  struct AVLNode *found = znode ? avl_offset(&znode->tree, offset) : NULL;
  return found ? container_of(found, struct Znode, tree) : NULL;
}
// return the rank of the node
int64_t znode_rank(struct Znode *znode) {
  return znode ? avl_rank(&znode->tree) : 0;
  /* return found ? container_of(found, struct Znode, tree) : NULL; */
}

// helps in cleaning the tree memory by disposing the left and right subtrees
// and freeing the znode
void tree_dispose(struct AVLNode *node) {
  if (!node)
    return;
  tree_dispose(node->left);
  tree_dispose(node->right);
  znode_del(container_of(node, struct Znode, tree));
}
// clears from the hashMap and tree
void zset_clear(struct Zset *zset) {
  hm_free(&zset->hm);
  tree_dispose(zset->root);
  zset->root = NULL;
}
