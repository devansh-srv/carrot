#include "../lib/heap.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }
size_t heap_left(size_t i) { return 2 * i + 1; }
size_t heap_right(size_t i) { return 2 * i + 2; }
void heap_up(struct HeapItem *a, size_t pos) {
  struct HeapItem t = a[pos];
  while (pos > 0 && a[heap_parent(pos)].val > t.val) {
    a[pos] = a[heap_parent(pos)];
    *a[pos].ref = pos;
    pos = heap_parent(pos);
  }
  a[pos] = t;
  *a[pos].ref = pos;
}
void heap_down(struct HeapItem *a, size_t pos, size_t len) {
  struct HeapItem t = a[pos];
  while (true) {
    size_t l = heap_left(pos);
    size_t r = heap_right(pos);
    size_t min_pos = pos;
    size_t min_val = t.val;
    if (l < len && min_val > a[l].val) {
      min_pos = l;
      min_val = a[l].val;
    }
    if (r < len && min_val > a[r].val) {
      min_pos = r;
      // check for this
      min_val = a[r].val;
    }
    if (min_pos == pos) {
      break;
    }
    a[pos] = a[min_pos];
    *a[pos].ref = pos;
    pos = min_pos;
  }
  a[pos] = t;
  *a[pos].ref = pos;
}
void heap_update(struct HeapItem *a, size_t pos, size_t len) {
  if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
    heap_up(a, pos);
  } else {
    heap_down(a, pos, len);
  }
}
