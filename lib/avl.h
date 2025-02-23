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

void avl_init(struct AVLNode *node);

uint32_t get_height(struct AVLNode *node);
uint32_t get_count(struct AVLNode *node);

struct AVLNode *avl_fix(struct AVLNode *node);
struct AVLNode *avl_del(struct AVLNode *node);
struct AVLNode *avl_offset(struct AVLNode *node, int64_t offset);
int64_t avl_rank(struct AVLNode *node);
