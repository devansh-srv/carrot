#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
// def for base pointer
#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  })

// FNV hash because it is fast
uint64_t str_hash(uint8_t *data, size_t len);
