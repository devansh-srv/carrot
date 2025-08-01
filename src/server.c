/* #ifdef DEBUG */
/* #define DEBUG_PRINT(fmt, ...) fprintf(stderr, "DEBUG: " fmt "\n",
 * ##__VA_ARGS__) */
/* #else */
/* #define DEBUG_PRINT(fmt, ...) */
/* #endif */
/**/
/* // Add debug prints in key locations */
/* DEBUG_PRINT("Connection received from %s:%d", inet_ntoa(client.sin_addr), */
/*             ntohs(client.sin_port)); */
/* #include "../lib/hashtable.h" */
#include "../lib/heap.h"
#include "../lib/list.h"
#include "../lib/utils.h"
#include "../lib/zest.h"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define PORT 12345
// macro for finding the base pointer to struct

/* const size_t MAX_PAYLOAD_SIZE = 4096; // scale this shit up */
const uint64_t IDLE_TIMEOUT_MS = 5 * 1000;
const size_t MAX_PAYLOAD_SIZE = 32 << 20;
const size_t MAX_ARGS = 200 * 1000;
const size_t MAX_WORKS = 2000;
void error(const char *msg) { perror(msg); }
void errno_msg(const char *msg) {
  fprintf(stderr, "errno: %d, error: %s", errno, msg);
}

// monotonic clock
uint64_t get_monotime() {
  struct timespec res = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &res);
  return (uint64_t)(res.tv_sec) * 1000 + res.tv_nsec / 1000 / 1000;
}
// we need non blocking sockets
void nonB(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    errno_msg("fcntl()");
    exit(0);
  }
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
  if (errno) {
    errno_msg("fnctl() set flag");
    return;
  }
}

// for managing the state after every event loop
struct Conn {
  int fd;
  bool want_read;
  bool want_write;
  bool want_close;
  uint8_t *incoming;
  int incoming_size;
  int incoming_capacity;
  uint8_t *outgoing;
  int outgoing_size;
  int outgoing_capacity;
  uint64_t last_active_ms;
  // dummy is the node to be put in linked list to keep track of active and
  // stale conn
  struct DList dummy;
};

void append_buf(uint8_t **incoming, uint8_t *data, size_t len,
                int *incoming_size, int *incoming_capacity) {
  // dynamically allocate memory
  int req = (*incoming_size) + len;
  if (req > (*incoming_capacity)) {
    int new_cap =
        ((*incoming_capacity) * 2 > req ? (*incoming_capacity) * 2 : req);
    uint8_t *tmp = (uint8_t *)realloc(*incoming, new_cap * sizeof(uint8_t));
    if (tmp == NULL) {
      perror("realloc()");
      return;
    }
    *incoming = tmp;
    (*incoming_capacity) = new_cap;
  }
  memcpy(*incoming + *incoming_size, data, len * sizeof(uint8_t));
  (*incoming_size) += len;
}

void consume_buf(uint8_t **incoming, size_t len, int *incoming_size,
                 int *incoming_capacity) {
  // dynamically de-allocate memory
  if (len >= (size_t)*incoming_size) {
    *incoming_size = 0;
    return;
  }
  memmove(*incoming, *incoming + len, (*incoming_size - len) * sizeof(uint8_t));
  *incoming_size -= len;
  // we can shrink if needed
  if (*incoming_size < *incoming_capacity / 4) {
    int new_cap = (*incoming_capacity) / 2;
    if (new_cap < 4)
      new_cap = 4;
    uint8_t *tmp = (uint8_t *)realloc(*incoming, new_cap * sizeof(uint8_t));
    if (tmp) {
      *incoming = tmp;
      *incoming_capacity = new_cap;
    }
  }
}

// for reading uint32_t type
bool read_u32(uint8_t **curr, uint8_t *end, uint32_t *out) {

  if (*curr + 4 > end) {
    return false;
  }
  uint32_t net_val;
  memcpy(&net_val, *curr, 4);

  *out = ntohl(net_val);
  *curr += 4;
  return true;
}
// for reading string typ
bool read_str(uint8_t **curr, uint8_t *end, size_t len, char **out) {

  if (*curr + len > end) {
    return false;
  }
  *out = (char *)malloc(len + 1);
  if (*out == NULL) {
    error("malloc failed @ read_str");
    return false;
  }
  memcpy(*out, *curr, len);
  (*out)[len] = '\0';
  *curr += len;
  return true;
}

// parser
int parse_req(uint8_t *data, size_t len, char **out, int *out_size,
              int *out_cap) {
  uint8_t *end = data + len;
  uint32_t num_str = 0;
  // reads the  number of strings in the redis command 4 bytes
  if (!read_u32(&data, end, &num_str)) {
    error("parse_req 1 failed");
    exit(EXIT_FAILURE);
  }
  /* printf("num_str: %d\n", num_str); */
  if (num_str > MAX_ARGS) {

    error("args too long");
    return -1;
  }
  // loops untill reads every (len:string) in the redis command
  while ((uint32_t)*out_size < num_str) {
    // reads the strlen of redis command string
    if (*out_size >= *out_cap) {
      int new_cap = (*out_cap) * 2;
      char *tmp = (char *)realloc(*out, new_cap);
      if (!tmp) {
        error("cmd tmp realloc failed");
        exit(EXIT_FAILURE);
      }
      *out_cap = new_cap;
      *out = tmp;
    }
    uint32_t len = 0;
    /* reads the command string */
    if (!read_u32(&data, end, &len)) {
      error("failed to read len in parse_req");
      exit(EXIT_FAILURE);
    }
    char *s = NULL;
    if (!read_str(&data, end, len, &s)) {
      error("failed to read str in parse_req");
      exit(EXIT_FAILURE);
    }
    (out)[*out_size] = s;
    (*out_size)++;
    printf("out_size: %d\n", *out_size);
  }
  // trailing garbage
  if (data != end) {
    error("garbage parse_req");
    exit(EXIT_FAILURE);
  }
  return 0;
}
// err code for TAG_ERR
enum {
  ERR_UNKNOWN = 1,
  ERR_TOO_BIG = 2,
  ERR_BAD_TYPE = 3,
  ERR_BAD_ARG = 4,
};
// tags for data types
enum {
  TAG_NIL = 0,
  TAG_ERR = 1,
  TAG_STR = 2,
  TAG_INT = 3,
  TAG_DBL = 4,
  TAG_ARR = 5,
};
// val types
enum {
  T_INIT = 0,
  T_STR = 1,  // string
  T_ZSET = 2, // sorted set
};
// append_buf functions for different data types
void append_buf_u8(uint8_t **incoming, uint8_t data, int *incoming_size,
                   int *incoming_capacity) {
  append_buf(incoming, (uint8_t *)&data, 1, incoming_size, incoming_capacity);
}

void append_buf_u32(uint8_t **incoming, uint32_t data, int *incoming_size,
                    int *incoming_capacity) {
  append_buf(incoming, (uint8_t *)&data, 4, incoming_size, incoming_capacity);
}

void append_buf_i64(uint8_t **incoming, int64_t data, int *incoming_size,
                    int *incoming_capacity) {
  append_buf(incoming, (uint8_t *)&data, 8, incoming_size, incoming_capacity);
}

void append_buf_dbl(uint8_t **incoming, double data, int *incoming_size,
                    int *incoming_capacity) {
  append_buf(incoming, (uint8_t *)&data, 8, incoming_size, incoming_capacity);
}

void out_nil(uint8_t **incoming, int *incoming_size, int *incoming_capacity) {
  append_buf_u8(incoming, TAG_NIL, incoming_size, incoming_capacity);
}

void out_str(uint8_t **incoming, char *data, size_t size, int *incoming_size,
             int *incoming_capacity) {
  // add tag
  append_buf_u8(incoming, TAG_STR, incoming_size, incoming_capacity);
  // add len
  append_buf_u32(incoming, (uint32_t)size, incoming_size, incoming_capacity);
  // append str
  append_buf(incoming, (uint8_t *)data, size, incoming_size, incoming_capacity);
}

void out_int(uint8_t **incoming, int64_t data, int *incoming_size,
             int *incoming_capacity) {
  // append tag
  append_buf_u8(incoming, TAG_INT, incoming_size, incoming_capacity);
  // append val
  append_buf_i64(incoming, data, incoming_size, incoming_capacity);
}

void out_dbl(uint8_t **incoming, double data, int *incoming_size,
             int *incoming_capacity) {
  // append tag
  append_buf_u8(incoming, TAG_DBL, incoming_size, incoming_capacity);
  // append val
  append_buf_dbl(incoming, data, incoming_size, incoming_capacity);
}

void out_err(uint8_t **incoming, int32_t code, char *msg, int *incoming_size,
             int *incoming_capacity) {
  // add tag
  append_buf_u8(incoming, TAG_ERR, incoming_size, incoming_capacity);
  // add code
  append_buf_u32(incoming, code, incoming_size, incoming_capacity);
  // add msg len
  size_t len = strlen(msg);
  append_buf_u32(incoming, len, incoming_size, incoming_capacity);
  // add msg
  append_buf(incoming, (uint8_t *)msg, len, incoming_size, incoming_capacity);
}

void out_arr(uint8_t **incoming, uint32_t n, int *incoming_size,
             int *incoming_capacity) {
  // add tag
  append_buf_u8(incoming, TAG_ARR, incoming_size, incoming_capacity);
  /* add len */
  append_buf_u32(incoming, n, incoming_size, incoming_capacity);
}

size_t out_arr_begin(uint8_t **incoming, int *incoming_size,
                     int *incoming_capacity) {
  append_buf_u8(incoming, TAG_ARR, incoming_size, incoming_capacity);
  append_buf_u32(incoming, 0, incoming_size, incoming_capacity);
  return (*incoming_size) - 4;
}
void out_arr_end(uint8_t **incoming, int *incoming_size, int *incoming_capacity,
                 size_t cnt, uint32_t n) {
  assert((*incoming)[cnt - 1] == TAG_ARR);
  memcpy(*incoming + cnt, &n, 4);
}

// discard enum and Response
//  handling response status as an enum
enum {
  RES_OK = 0,
  RES_ERR = 1,
  RES_NX = 2,
};

// response struct
struct Response {
  uint32_t status;
  uint8_t *data;
  size_t data_size;
  size_t data_cap;
};

// global declaration and init of the hashMap db
struct {
  struct HashMap db;
  // mapping connections fd(s)
  struct Conn **fdconn;
  size_t fdconn_cap;
  size_t fdconn_size;
  // timers for idle connections linked list head
  struct DList dummy;
  /* heap array */
  // for ttl
  struct HeapItem *heap;
  size_t heap_size;
  size_t heap_cap;

} g_data;

struct Entry {
  struct HashNode node;
  char *key;
  size_t heap_idx;
  uint32_t type;
  char *val;
  struct Zset zset;
};

struct Entry *ent_init(uint32_t type) {
  struct Entry *ent = (struct Entry *)malloc(sizeof(struct Entry));
  ent->type = type;
  ent->heap_idx = (size_t)-1;
  return ent;
}
void entry_set_ttl(struct Entry *ent, int64_t ttl_ms);
void ent_del(struct Entry *ent) {
  if (ent->type == T_ZSET)
    zset_clear(&ent->zset);
  entry_set_ttl(ent, -1);
  free(ent);
}
struct LookupKey {
  struct HashNode node;
  char *key;
};
// to check the equality of the keys using the container_of macro
// the same function whose pointer is passed in the hashMap functions for
// checking the inequality of nodes bool(*eq)(struct HashNode*a,struct HashNode*
// b)
bool entry_eq(struct HashNode *a, struct HashNode *b) {
  struct Entry *aa = container_of(a, struct Entry, node);
  struct Entry *bb = container_of(b, struct Entry, node);
  // checks the equality of the key strings
  // uses strcmp so be we need to return true when evaluates to 0
  int res = strcmp(aa->key, bb->key);
  if (res == 0)
    return true;
  return false;
}

// get command handler for redis command `get`
// reads the key in struct Entry* and looks up in the hashMap with key hashCode
void do_get(char **cmd, uint8_t **incoming, int *incoming_size,
            int *incoming_capacity) {
  struct Entry key;
  if (cmd[1] != NULL) {
    key.key = cmd[1];
  } else {
    error("cmd[1] is null");
    return;
  }
  /* printf(cmd[1]); */
  /* printf("\n"); */
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    printf("hey there");
    /* did not find the key  */
    return out_nil(incoming, incoming_size, incoming_capacity);
  }
  struct Entry *x = container_of(node, struct Entry, node);
  if (x->type != T_STR)
    return out_err(incoming, ERR_BAD_TYPE, "not a string type", incoming_size,
                   incoming_capacity);
  // found the key
  // length of the response aka the val
  size_t length = strlen(x->val);
  assert(length <= MAX_PAYLOAD_SIZE);

  // prepares the response (str)
  return out_str(incoming, x->val, length, incoming_size, incoming_capacity);
}

// set handler for redis command `set`
void do_set(char **cmd, uint8_t **incoming, int *incoming_size,
            int *incoming_capacity) {
  struct LookupKey key;
  if (cmd[1] != NULL) {
    key.key = cmd[1];
  } else {
    error("cmd[1] is null");
    return;
  }
  /* printf(cmd[1]); */
  /* printf("\n"); */
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    // successfully found the node
    // uses base pointer to assign the val
    struct Entry *x = container_of(node, struct Entry, node);
    if (x->type != T_STR) {
      return out_err(incoming, ERR_BAD_TYPE, "not a string type", incoming_size,
                     incoming_capacity);
    }
    x->val = strdup(cmd[2]);
  } else {
    struct Entry *ent = ent_init(T_STR);
    if (!ent) {
      error("ent malloc failed");
      exit(EXIT_FAILURE);
    }
    // prepares the node for hm_insert
    ent->key = strdup(key.key);
    ent->node.hashCode = key.node.hashCode;
    ent->val = strdup(cmd[2]);
    hm_insert(&g_data.db, &ent->node);
  }
  char *msg = "OK";
  size_t len = strlen(msg);
  out_str(incoming, msg, len, incoming_size, incoming_capacity);
}

// del handler for redis command `del`
void do_del(char **cmd, uint8_t **incoming, int *incoming_size,
            int *incoming_capacity) {
  struct LookupKey key;
  if (cmd[1] != NULL) {
    key.key = cmd[1];
  } else {
    error("cmd[1] is null");
    return;
  }
  /* printf(cmd[1]); */
  /* printf("\n"); */
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_del(&g_data.db, &key.node, &entry_eq);
  if (node) {
    /* successfully deleted the node from hashMap */
    struct Entry *entry = container_of(node, struct Entry, node);
    /* prepares the response */
    ent_del(entry);
  }
  return out_int(incoming, node ? 1 : 0, incoming_size, incoming_capacity);
}

void heap_delete(struct HeapItem *a, size_t pos, size_t *heap_size,
                 size_t *heap_cap) {
  *(a + pos) = *(a + (*heap_size - 1));
  *heap_size -= 1;
  if (pos < *heap_size) {
    heap_update(a, pos, *heap_size);
  }
}
// add the HeapItem to the heap array
void heap_upsert(struct HeapItem **a, size_t pos, struct HeapItem t,
                 size_t *heap_size, size_t *heap_cap) {
  if (pos < *heap_size) {
    // update to the array
    (*a)[pos] = t;
  } else {
    // dynamically realloc to accomodate
    size_t new_cap = ((*heap_cap) * 2);
    struct HeapItem *tmp =
        (struct HeapItem *)realloc(*a, new_cap * sizeof(struct HeapItem));
    if (!tmp) {
      error("heap realloc failed at 500 something");
      exit(EXIT_FAILURE);
    }
    *a = tmp;
    *heap_cap = new_cap;
    pos = *heap_size;
    (*a)[*heap_size] = t;
    /* if (t.ref) */
    /*   *(size_t *)(t.ref) = *heap_size; */
    *heap_size = *heap_size + (size_t)1;
  }
  heap_update(*a, pos, *heap_size);
}
// function for setting the ttl val
void entry_set_ttl(struct Entry *entry, int64_t ttl_ms) {
  // if negative ttl_ms delete from Heap right now
  if (ttl_ms < 0 && entry->heap_idx != (size_t)-1) {
    heap_delete(g_data.heap, entry->heap_idx, &g_data.heap_size,
                &g_data.heap_cap);
    entry->heap_idx = -1;
  } else if (ttl_ms >= 0) {
    // sets the time when it has to be deleted
    // monotime (now) +  time to live
    uint64_t expire_at = get_monotime() + (uint64_t)ttl_ms;
    struct HeapItem item = {expire_at, &entry->heap_idx};
    heap_upsert(&g_data.heap, entry->heap_idx, item, &g_data.heap_size,
                &g_data.heap_cap);
  }
}
/* writes array of keys  */
bool fn(struct HashNode *node, uint8_t **incoming, int *incoming_size,
        int *incoming_capacity) {
  struct Entry *entry = container_of(node, struct Entry, node);
  char *key = entry->key;
  size_t len = strlen(key);
  out_str(incoming, key, len, incoming_size, incoming_capacity);
  return true;
}
void do_keys(uint8_t **incoming, int *incoming_size, int *incoming_capacity) {
  out_arr(incoming, hm_size(&g_data.db), incoming_size, incoming_capacity);
  hm_each(&g_data.db, incoming, incoming_size, incoming_capacity, &fn);
}
// typecasting functions
bool str2dbl(char *msg, double *out) {
  char *end = NULL;
  *out = strtod(msg, &end);
  return end == msg + strlen(msg) && !isnan(*out);
}

bool str2int(char *msg, int64_t *out) {
  char *end = NULL;
  *out = strtoll(msg, &end, 10);
  return end == msg + strlen(msg);
}
// pexpire handler
void do_expire(char **cmd, uint8_t **incoming, int *incoming_size,
               int *incoming_capacity) {
  int64_t ttl_ms = 0;
  if (!str2int(cmd[2], &ttl_ms)) {
    return out_err(incoming, ERR_BAD_ARG, "expected int", incoming_size,
                   incoming_capacity);
  }
  struct LookupKey key;
  key.key = cmd[1];
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    struct Entry *entry = container_of(node, struct Entry, node);
    entry_set_ttl(entry, ttl_ms);
  }
  return out_int(incoming, node ? 1 : 0, incoming_size, incoming_capacity);
}

// pttl handeler
void do_ttl(char **cmd, uint8_t **incoming, int *incoming_size,
            int *incoming_capacity) {
  struct LookupKey key;
  key.key = cmd[1];
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_int(incoming, -2, incoming_size, incoming_capacity);
  }

  struct Entry *entry = container_of(node, struct Entry, node);
  if (entry->heap_idx == (size_t)-1) {
    return out_int(incoming, -1, incoming_size, incoming_capacity);
  }
  struct HeapItem *expire_st = g_data.heap + entry->heap_idx;
  uint64_t expire = expire_st->val;
  uint64_t now = get_monotime();
  return out_int(incoming, expire > now ? (expire - now) : 0, incoming_size,
                 incoming_capacity);
}
// zadd handler function to add (score,name)
void do_zadd(char **cmd, uint8_t **incoming, int *incoming_size,
             int *incoming_capacity) {
  double score = 0;
  if (!str2dbl(cmd[2], &score))
    return out_err(incoming, ERR_BAD_ARG, "expect float", incoming_size,
                   incoming_capacity);
  struct LookupKey key;
  if (cmd[1] != NULL) {
    key.key = cmd[1];
  } else {
    error("cmd[1] is NULL");
    return;
  }
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  struct Entry *entry = NULL;
  if (!node) {
    entry = ent_init(T_ZSET);
    entry->key = strdup(key.key);
    entry->node.hashCode = key.node.hashCode;
    hm_insert(&g_data.db, &entry->node);
  } else {
    entry = container_of(node, struct Entry, node);
    if (entry->type != T_ZSET)
      return out_err(incoming, ERR_BAD_TYPE, "expect zset", incoming_size,
                     incoming_capacity);
  }
  char *name = cmd[3];
  bool rv = zset_insert(&entry->zset, name, strlen(name), score);
  return out_int(incoming, (int64_t)rv, incoming_size, incoming_capacity);
}

struct Zset empty_zset;
struct Zset *expect_zset(char *msg) {
  struct LookupKey key;
  key.key = msg;
  key.node.hashCode = str_hash((uint8_t *)key.key, strlen(key.key));
  struct HashNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return (struct Zset *)&empty_zset;
  }
  struct Entry *ent = container_of(node, struct Entry, node);
  return ent->type == T_ZSET ? &ent->zset : NULL;
}
// removing the sorted set type
void do_zrem(char **cmd, uint8_t **incoming, int *incoming_size,
             int *incoming_capacity) {
  struct Zset *zset = expect_zset(cmd[1]);
  if (!zset)
    return out_err(incoming, ERR_BAD_TYPE, "expected Zset", incoming_size,
                   incoming_capacity);
  char *name = cmd[2];
  struct Znode *znode = zset_lookup(zset, name, strlen(name));
  if (znode) {
    zset_del(zset, znode);
  }
  return out_int(incoming, znode ? 1 : 0, incoming_size, incoming_capacity);
}

//
// function for handling zscore
void do_zscore(char **cmd, uint8_t **incoming, int *incoming_size,
               int *incoming_capacity) {
  struct Zset *zset = expect_zset(cmd[1]);
  if (!zset)
    return out_err(incoming, ERR_BAD_TYPE, "expect_zset", incoming_size,
                   incoming_capacity);
  char *name = cmd[2];
  struct Znode *znode = zset_lookup(zset, name, strlen(name));
  return znode
             ? out_dbl(incoming, znode->score, incoming_size, incoming_capacity)
             : out_nil(incoming, incoming_size, incoming_capacity);
}

// function to handle zquery
void do_zquery(char **cmd, uint8_t **incoming, int *incoming_size,
               int *incoming_capacity) {
  double score = 0;
  if (!str2dbl(cmd[2], &score)) {
    return out_err(incoming, ERR_BAD_ARG, "expected a double", incoming_size,
                   incoming_capacity);
  }
  char *name = cmd[3];
  int64_t offset = 0, limit = 0;
  if (!str2int(cmd[4], &offset) || !str2int(cmd[5], &limit)) {
    return out_err(incoming, ERR_BAD_ARG, "expected int", incoming_size,
                   incoming_capacity);
  }
  struct Zset *zset = expect_zset(cmd[1]);
  if (!zset) {
    return out_err(incoming, ERR_BAD_TYPE, "expect_zset", incoming_size,
                   incoming_capacity);
  }
  if (limit <= 0)
    return out_arr(incoming, 0, incoming_size, incoming_capacity);
  struct Znode *znode = zset_seek(zset, name, strlen(name), score);
  /* if (!znode) { */
  /*   error("haha idiot\n"); */
  /*   exit(EXIT_FAILURE); */
  /* } */
  znode = znode_offset(znode, offset);
  size_t cnt = out_arr_begin(incoming, incoming_size, incoming_capacity);
  int64_t n = 0;
  while (znode && n < limit) {
    out_str(incoming, znode->name, znode->len, incoming_size,
            incoming_capacity);
    out_dbl(incoming, znode->score, incoming_size, incoming_capacity);
    znode = znode_offset(znode, offset + 1);
    n += 2;
  }
  out_arr_end(incoming, incoming_size, incoming_capacity, cnt, (uint32_t)n);
}
// function for handling zscore
void do_zrank(char **cmd, uint8_t **incoming, int *incoming_size,
              int *incoming_capacity) {
  struct Zset *zset = expect_zset(cmd[1]);
  if (!zset)
    return out_err(incoming, ERR_BAD_TYPE, "expect_zset", incoming_size,
                   incoming_capacity);
  char *name = cmd[2];
  struct Znode *znode = zset_lookup(zset, name, strlen(name));
  int64_t rank = znode_rank(znode);
  return rank != 0 ? out_int(incoming, rank, incoming_size, incoming_capacity)
                   : out_nil(incoming, incoming_size, incoming_capacity);
}
// req handler
void do_req(char **cmd, int *cmd_size, uint8_t **incoming, int *incoming_size,
            int *incoming_capacity) {
  if (*cmd_size == 2 && strcmp(cmd[0], "get") == 0) {
    return do_get(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 3 && strcmp(cmd[0], "set") == 0) {
    return do_set(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 2 && strcmp(cmd[0], "del") == 0) {
    return do_del(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 3 && strcmp(cmd[0], "pexpire") == 0) {
    return do_expire(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 2 && strcmp(cmd[0], "pttl") == 0) {
    return do_ttl(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 1 && strcmp(cmd[0], "keys") == 0) {
    return do_keys(incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 4 && strcmp(cmd[0], "zadd") == 0) {
    return do_zadd(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 3 && strcmp(cmd[0], "zrem") == 0) {
    return do_zrem(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 3 && strcmp(cmd[0], "zscore") == 0) {
    return do_zscore(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 6 && strcmp(cmd[0], "zquery") == 0) {
    return do_zquery(cmd, incoming, incoming_size, incoming_capacity);
  } else if (*cmd_size == 3 && strcmp(cmd[0], "zrank") == 0) {
    return do_zrank(cmd, incoming, incoming_size, incoming_capacity);
  } else {
    return out_err(incoming, ERR_UNKNOWN, "unknown command", incoming_size,
                   incoming_capacity);
  }
}

// reserves 4 bytes for pricipal header
void response_begin(uint8_t **incoming, int *incoming_size,
                    int *incoming_capacity, size_t *header) {
  *header = *incoming_size;
  append_buf_u32(incoming, 0, incoming_size, incoming_capacity);
}
// computes the size of the resp msg
size_t response_size(int *incoming_size, size_t header) {
  return (*incoming_size) - header - 4;
}
// checks whether the response is > MAX_PAYLOAD_SIZE
// if it is so the rewrites the response as ERR_TOO_BIG
void response_end(uint8_t **incoming, int *incoming_size,
                  int *incoming_capacity, size_t header) {
  size_t res_sz = response_size(incoming_size, header);
  if (res_sz > MAX_PAYLOAD_SIZE) {
    *incoming_size = header + 4;
    out_err(incoming, ERR_TOO_BIG, "response too big", incoming_size,
            incoming_capacity);
    res_sz = response_size(incoming_size, header);
  }
  uint32_t sz = (uint32_t)res_sz;
  memcpy(*incoming + header, &sz, 4);
}
// callback for accept
// accept client connection
// initializes a struct Conn* instance conn
// returns client conn state

// sends one req at a time
// parses req and gets the prepared response to read req and write response
bool one_req(struct Conn *conn) {
  if (conn->incoming_size < 4) {
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming, 4);
  uint32_t host_len = ntohl(len);
  printf("Network order len: %u (0x%08x)\n", len, len);
  printf("Host order len: %u (0x%08x)\n", host_len, host_len);
  printf("len: %u\n", len);
  printf("host_len: %u\n", host_len);
  if ((size_t)host_len > MAX_PAYLOAD_SIZE) {
    error("msg too long");
    conn->want_close = true;
    return false;
  }
  if (host_len + 4 > conn->incoming_size) {
    return false;
  }
  uint8_t *req = &(conn->incoming[4]);
  char **cmd = (char **)calloc(100, sizeof(char *));
  if (!cmd) {
    error("cmd calloc failed");
    exit(EXIT_FAILURE);
  }
  int cmd_size = 0;
  int cmd_cap = 100;
  int p = parse_req(req, host_len, cmd, &cmd_size, &cmd_cap);
  if (p < 0) {
    error("bad_req");
    conn->want_close = true;
    return false;
  }
  size_t header_begin = 0;
  response_begin(&conn->outgoing, &conn->outgoing_size,
                 &conn->outgoing_capacity, &header_begin);
  do_req(cmd, &cmd_size, &conn->outgoing, &conn->outgoing_size,
         &conn->outgoing_capacity);
  response_end(&conn->outgoing, &conn->outgoing_size, &conn->outgoing_capacity,
               header_begin);
  for (size_t i = 0; i < cmd_size; i++) {
    free(cmd[i]);
  }
  free(cmd);
  consume_buf(&conn->incoming, 4 + host_len, &conn->incoming_size,
              &conn->incoming_capacity);
  return true;
}

// handle for accepting connections
int32_t handle_accept(int fd) {
  struct sockaddr_in client;
  socklen_t clilen = sizeof(client);
  errno = 0;
  int sockfd = accept(fd, (struct sockaddr *)&client, &clilen);
  /* do { */
  /*   sockfd = accept(fd, (struct sockaddr *)&client, &clilen); */
  /* } while (sockfd <= 0 && errno == EAGAIN); */

  uint32_t ip = client.sin_addr.s_addr;
  fprintf(stderr, "client_addr: %d.%d.%d.%d:%d\n", ip & 255, (ip >> 8) & 255,
          (ip >> 16) & 255, (ip >> 24), ntohs(client.sin_port));
  nonB(sockfd);

  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  conn->fd = sockfd;
  conn->want_read = true;
  conn->want_write = false;
  conn->want_close = false;
  conn->incoming = (uint8_t *)calloc(8, sizeof(uint8_t));
  conn->outgoing = (uint8_t *)calloc(8, sizeof(uint8_t));
  conn->incoming_size = 0;
  conn->outgoing_size = 0;
  conn->incoming_capacity = 8;
  conn->outgoing_capacity = 8;
  conn->last_active_ms = get_monotime();
  dlist_insert(&g_data.dummy, &conn->dummy);

  /* struct Conn **fdconn = (struct Conn **)calloc(8, sizeof(struct Conn *)); */
  /* if (fdconn == NULL) { */
  /*   error("fdconn error"); */
  /*   exit(0); */
  /* } */
  /* size_t fdconn_cap = 8; */

  /* struct Conn *conn = handle_accept(fd); */
  if (conn && conn->fd >= 0) {
    if (g_data.fdconn_cap < (size_t)conn->fd + 1) {
      int new_cap = (size_t)conn->fd + 1;
      struct Conn **tmp = (struct Conn **)realloc(
          g_data.fdconn, (size_t)new_cap * sizeof(struct Conn *));
      if (tmp == NULL) {
        error("fdconn realloc()");
        exit(0);
      }
      g_data.fdconn = tmp;
      g_data.fdconn_cap = new_cap;
    }
    assert(!g_data.fdconn[conn->fd]);
    g_data.fdconn[conn->fd] = conn;
  }
  // need to check malloc idiot
  if (conn->incoming == NULL || conn->outgoing == NULL) {
    error("memory not available for incoming and outgoing");
    free(conn->incoming);
    free(conn->outgoing);
    free(conn);
    close(sockfd);

    return -1;
  }
  return 0;
}

void conn_destroy(struct Conn *conn) {
  close(conn->fd);
  g_data.fdconn[conn->fd] = NULL;
  dlist_detatch(&conn->dummy);
  free(conn->incoming);
  free(conn->outgoing);
  free(conn);
}
// callback for write
// writes data from connection state conn outgoing to kernel buffer
// // and consumes conn->outgoing
void handle_write(struct Conn *conn) {
  assert(conn->outgoing_size > 0);
  /* printf("outgoing_size: %d\n", conn->outgoing_size); */
  /* printf("outgoing: %s\n", &conn->outgoing[0]); */
  ssize_t err = write(conn->fd, &conn->outgoing[0], conn->outgoing_size);
  if (err <= 0 && errno == EAGAIN) {
    return;
  }
  if (err < 0) {
    error("write() error");
    conn->want_close = true;
    return;
  }
  consume_buf(&conn->outgoing, (size_t)err, &conn->outgoing_size,
              &conn->outgoing_capacity);
  if (conn->outgoing_size == 0) {
    conn->want_write = false;
    conn->want_read = true;
  }
}
// callback for read
// makes read() syscall to dump data into buf
// and append_buf() 's data to conn->incoming
void handle_read(struct Conn *conn) {
  uint8_t buf[64 * 1024];
  int err = read(conn->fd, buf, sizeof(buf));
  if (err < 0 && errno == EAGAIN) {
    return;
  }
  if (err < 0) {
    error("read() error");
    conn->want_close = true;
    return;
  }
  if (err == 0) {
    if (conn->incoming_size == 0) {
      error("client disconnected");
    } else {
      error("EOF");
    }
    conn->want_close = true;
    return;
  }
  /* printf("Received %d bytes: ", err); */
  /* for (int i = 0; i < err; i++) { */
  /*   printf("%02x ", buf[i]); */
  /* } */
  /* printf("\n"); */
  append_buf(&conn->incoming, buf, (size_t)err, &conn->incoming_size,
             &conn->incoming_capacity);

  // keeps reading requests and writing responses
  while (one_req(conn)) {
  }

  // wants to write
  if (conn->outgoing_size > 0) {
    conn->want_read = false;
    conn->want_write = true;
    return handle_write(conn);
  }
}
// function to get the next_timer
int32_t next_timer() {
  uint64_t now = get_monotime();
  uint64_t next = (uint64_t)-1;
  if (!dlist_empty(&g_data.dummy)) {
    struct Conn *conn = container_of(g_data.dummy.next, struct Conn, dummy);
    next = conn->last_active_ms + IDLE_TIMEOUT_MS;
  }
  if (g_data.heap_size != 0 && g_data.heap[0].val < next) {
    next = g_data.heap[0].val;
  }
  if (next == (uint64_t)-1) {
    return -1;
  }
  if (next <= now) {
    return 0;
  }
  return (int32_t)(next - now);
}

// cmp function for process_timers()
bool hnode_same(struct HashNode *a, struct HashNode *b) { return a == b; }

// eradicating stale timers from the heap/linked list
void process_timers() {
  uint64_t now = get_monotime();
  while (!dlist_empty(&g_data.dummy)) {
    struct Conn *conn = container_of(g_data.dummy.next, struct Conn, dummy);
    uint64_t next = conn->last_active_ms + IDLE_TIMEOUT_MS;
    if (next >= now)
      break;
    fprintf(stderr, "removing idle connection%d\n", conn->fd);
    conn_destroy(conn);
  }
  size_t nworks = 0;
  while (g_data.heap_size != 0 && g_data.heap[0].val < now) {
    struct Entry *entry =
        container_of(g_data.heap[0].ref, struct Entry, heap_idx);
    struct HashNode *node = hm_del(&g_data.db, &entry->node, &hnode_same);
    assert(node == &entry->node);
    ent_del(entry);
    if (nworks++ >= MAX_WORKS)
      break;
  }
}
void print_banner() { system("figlet CARROT"); }

int main(int argc, char *argv[]) {
  print_banner();
  dlist_init(&g_data.dummy);
  g_data.fdconn = (struct Conn **)calloc(8, sizeof(struct Conn *));
  if (g_data.fdconn == NULL) {
    error("fdconn error");
    exit(0);
  }
  g_data.fdconn_cap = 8;
  g_data.fdconn_size = 0;
  g_data.heap = (struct HeapItem *)calloc(8, sizeof(struct HeapItem));
  if (g_data.heap == NULL) {
    error("HeapItem error");
    exit(EXIT_FAILURE);
  }
  g_data.heap_size = 0;
  g_data.heap_cap = 8;
  int fd;
  struct sockaddr_in server;
  bzero(&server, sizeof(server));
  if (argc < 1)
    perror("command not found");
  fd = socket(PF_INET, SOCK_STREAM, 0); // socket syscall
  if (fd < 0)
    error("socket() error");
  int val = -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); // to counter rtt

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(PORT);
  nonB(fd);
  if (bind(fd, (const struct sockaddr *)&server, sizeof(server)) < 0) {
    error("bind() error");
    exit(0);
  }

  if (listen(fd, SOMAXCONN) < 0) {
    error("listen()");
    exit(0);
  }

  /* struct Conn **fdconn = (struct Conn **)calloc(8, sizeof(struct Conn *)); */
  /* if (fdconn == NULL) { */
  /*   error("fdconn error"); */
  /*   exit(0); */
  /* } */
  /* size_t fdconn_cap = 8; */
  struct pollfd *poll_args = (struct pollfd *)calloc(8, sizeof(struct pollfd));
  if (poll_args == NULL) {
    error("pollfd error");
    exit(0);
  }
  int poll_args_size = 0;
  int poll_args_cap = 8;
  while (1) {
    poll_args_size = 0;
    // 1 element for listening socket
    struct pollfd pfd = {fd, POLL_IN, 0};
    memcpy(&poll_args[poll_args_size], &pfd, sizeof(pfd));
    poll_args_size += 1;
    for (int i = 0; i < (int)g_data.fdconn_cap; i++) {
      struct Conn *conn = g_data.fdconn[i];
      if (!conn) {
        continue;
      }
      struct pollfd pfd = {conn->fd, POLLERR, 0};
      if (conn->want_read) {
        pfd.events |= POLLIN;
      }
      if (conn->want_write) {
        pfd.events |= POLLOUT;
      }
      int req = poll_args_size + sizeof(pfd);
      if (req >= poll_args_cap) {
        int new_cap = (2 * poll_args_cap) > req ? (2 * poll_args_cap) : req;
        struct pollfd *tmp = (struct pollfd *)realloc(
            poll_args, new_cap * sizeof(struct pollfd));
        if (tmp == NULL) {
          error("struct pollfd tmp 2 fail");
          exit(0);
        }
        poll_args = tmp;
        poll_args_cap = new_cap;
      }
      memcpy(&poll_args[poll_args_size], &pfd, sizeof(pfd));
      poll_args_size += 1;
    }
    int32_t timeout = next_timer();
    int err = poll(poll_args, (nfds_t)poll_args_size, timeout);
    process_timers();
    if (err < 0 && errno == EAGAIN) {
      continue;
    }
    if (err < 0) {
      error("poll()");
      exit(0);
    }
    if (poll_args[0].revents) {
      handle_accept(fd);
    }
    for (int i = 1; i < poll_args_size; i++) {
      short ready = poll_args[i].revents;
      if (ready == 0) {
        continue;
      }
      int fd = poll_args[i].fd;
      if (fd <= 0) {
        errno_msg("poll args[i].fd: ");
        fprintf(stderr, "%d", fd);
      }
      if ((size_t)fd >= g_data.fdconn_cap) {
        error("mapping issue");
        continue;
      }
      struct Conn *conn = g_data.fdconn[fd];
      if (conn == NULL) {
        error("conn is NULL");
        exit(EXIT_FAILURE);
      }
      if (ready & POLLIN) {
        assert(conn != NULL);
        assert(conn->want_read);
        handle_read(conn);
      }
      if (ready & POLLOUT) {
        assert(conn->want_write);
        handle_write(conn);
      }
      if ((ready & POLLERR) || conn->want_close) {

        conn_destroy(conn);
      }
    }
  }
  return 0;
}
