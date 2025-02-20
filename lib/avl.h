#include_next <stddef.h>
#include_next <stdint.h>

// avl tree intrinsic structure
struct AVLNode {
  struct AVLNode *parent;
  struct AVLNode *left;
  struct AVLNode *right;
  uint32_t height;
  uint32_t cnt;
};

inline void avl_init(struct AVLNode *node) {
  node->left = node->right = node->parent = NULL;
  node->height = 1;
  node->cnt = 1;
};

inline uint32_t get_height(struct AVLNode *node) {
  return node ? node->height : 0;
};
inline uint32_t get_count(struct AVLNode *node) {

  return node ? node->cnt : 0;
};

struct AVLNode *avl_fix(struct AVLNode *node);
struct AVLNode *avl_del(struct AVLNode *node);
struct AVLNode *avl_offset(struct AVLNode *node, int64_t offset);
