#include <stddef.h>
#include <stdint.h>
struct HeapItem {
  uint64_t val;
  size_t *ref;
};
void heap_update(struct HeapItem *a, size_t pos, size_t len);
void heap_delete(struct HeapItem *a, size_t pos, size_t *heap_size,
                 size_t *heap_cap);
