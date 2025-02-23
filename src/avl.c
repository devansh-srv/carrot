#include "../lib/avl.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
void avl_init(struct AVLNode *node) {
  node->left = node->right = node->parent = NULL;
  node->height = 1;
  node->cnt = 1;
};

uint32_t get_height(struct AVLNode *node) { return node ? node->height : 0; };
uint32_t get_count(struct AVLNode *node) { return node ? node->cnt : 0; };
uint32_t max(uint32_t a, uint32_t b) { return a < b ? a : b; }

// helper function to update auxiliary data
void avl_update(struct AVLNode *node) {
  node->height = 1 + max(get_height(node->left), get_height(node->right));
  node->cnt = 1 + get_count(node->left) + get_count(node->right);
}
/* makes the right child of node(nodeToBe) the parent of it and the left subtree
 * of nodeToBe  becomes the right subtree of node */
struct AVLNode *rot_left(struct AVLNode *node) {
  struct AVLNode *parent = node->parent;
  struct AVLNode *nodeToBe = node->right;
  struct AVLNode *inNode = nodeToBe->left;
  node->right = inNode;
  if (inNode)
    inNode->parent = node;
  nodeToBe->parent = parent;
  nodeToBe->left = node;
  node->parent = nodeToBe;
  avl_update(node);
  avl_update(nodeToBe);
  return nodeToBe;
}

/* makes the left  child of node(nodeToBe) the parent of it and the right
 * subtree of nodeToBe  becomes the left  subtree of node */
struct AVLNode *rot_right(struct AVLNode *node) {
  struct AVLNode *parent = node->parent;
  struct AVLNode *nodeToBe = node->left;
  struct AVLNode *inNode = nodeToBe->right;
  node->left = inNode;
  if (inNode)
    inNode->parent = node;
  nodeToBe->parent = parent;
  nodeToBe->right = node;
  node->parent = nodeToBe;
  avl_update(node);
  avl_update(nodeToBe);
  return nodeToBe;
}

// handles the left left condition if required
struct AVLNode *fix_left(struct AVLNode *node) {
  if (get_height(node->left->left) < get_height(node->left->right)) {
    node->left = rot_left(node->left);
  }
  return rot_right(node);
}

// handles the right right condition if required
struct AVLNode *fix_right(struct AVLNode *node) {
  if (get_height(node->right->right) < get_height(node->right->left)) {
    node->right = rot_right(node->right);
  }
  return rot_left(node);
}

struct AVLNode *avl_fix(struct AVLNode *node) {
  while (true) {
    struct AVLNode **from = &node;
    struct AVLNode *parent = node->parent;
    if (parent) {
      from = parent->left == node ? &parent->left : &parent->right;
    }
    avl_update(node);
    uint32_t lh = get_height(node->left);
    uint32_t rh = get_height(node->right);
    if (lh == rh + 2) {
      *from = fix_left(node);
    } else if (lh + 2 == rh) {
      *from = fix_right(node);
    }
    // if root
    if (!parent)
      return *from; // returns root pointer
    node = parent;
  }
}
/* helper function to detatch the node */
struct AVLNode *avl_del_help(struct AVLNode *node) {
  assert(!node->left || !node->right);
  struct AVLNode *child = node->left ? node->left : node->right;
  struct AVLNode *parent = node->parent;
  if (child)
    child->parent = parent;
  if (!parent)
    return child;
  struct AVLNode **from = parent->left == node ? &parent->left : &parent->right;
  *from = child;
  return avl_fix(parent);
}

// deletes the said node and returns the root
struct AVLNode *avl_del(struct AVLNode *node) {
  if (!node->left || !node->right) {
    return avl_del_help(node);
  } else {
    struct AVLNode *victim = node->right;
    while (victim->left) {
      victim = victim->left;
    }
    struct AVLNode *root = avl_del_help(victim);
    *victim = *node;
    if (victim->left)
      victim->left->parent = victim;
    if (victim->right)
      victim->right->parent = victim;
    struct AVLNode **from = &root;
    struct AVLNode *parent = node->parent;
    if (victim->parent != node->parent) {
      printf("not eq\n");
    } else {
      printf("eq\n");
    }
    if (parent) {
      from = parent->left == node ? &parent->left : &parent->right;
    }
    *from = victim;
    return root;
  }
}

// function for rank queries
// similar to the operation select(i) but with rank diff
struct AVLNode *avl_offset(struct AVLNode *node, int64_t offset) {
  int64_t pos = 0;
  while (pos != offset) {
    if (pos < offset && pos + get_count(node->right) >= offset) {
      node = node->right;
      pos += 1 + get_count(node->left);
    } else if (pos > offset && pos - get_count(node->left) <= offset) {

      node = node->left;
      pos -= 1 + get_count(node->right);
    } else {
      struct AVLNode *parent = node->parent;
      if (!parent)
        return NULL;
      if (parent->right == node) {
        pos -= 1 + get_count(node->left);
      } else {
        pos += 1 + get_count(node->right);
      }
      node = parent;
    }
  }
  return node;
}
int64_t avl_rank(struct AVLNode *node) {
  int64_t rank = 1 + get_count(node->left);
  struct AVLNode *parent = node->parent;
  while (parent) {
    if (parent->right == node) {
      rank += 1 + get_count(parent->left);
    }
    node = parent;
    parent = node->parent;
  }
  return rank;
}
