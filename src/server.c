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
#include "../lib/hashtable.h"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
#include <unistd.h>

// macro for finding the base pointer to struct
#define container_of(ptr, type, member)                                        \
  (type *)((char *)ptr - offsetof(type, member))

/* const size_t MAX_PAYLOAD_SIZE = 4096; // scale this shit up */
const size_t MAX_PAYLOAD_SIZE = 32 << 20;
const size_t MAX_ARGS = 200 * 1000;

void error(const char *msg) { perror(msg); }
void errno_msg(const char *msg) {
  fprintf(stderr, "errno: %d, error: %s", errno, msg);
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

// handling response status as an enum
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
} g_data;

struct Entry {
  struct HashNode node;
  char *key;
  char *val;
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

// FNV hash because it is fast
uint64_t str_hash(uint8_t *data, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) {
    h = (h + data[i]) * 0x01000193;
  }
  return h;
}

// get command handler for redis command `get`
// reads the key in struct Entry* and looks up in the hashMap with key hashCode
void do_get(char **cmd, struct Response *res) {
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
    res->status = RES_NX;
    return;
  }
  struct Entry *x = container_of(node, struct Entry, node);
  // found the key
  // length of the response aka the val
  size_t length = strlen(x->val);
  assert(length <= MAX_PAYLOAD_SIZE);

  // prepares the response
  memcpy(res->data, x->val, length);
  res->data_size = length;
}

// set handler for redis command `set`
void do_set(char **cmd) {
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
  if (node) {
    // successfully found the node
    // uses base pointer to assign the val
    struct Entry *x = container_of(node, struct Entry, node);
    x->val = strdup(cmd[2]);
  } else {
    struct Entry *ent = (struct Entry *)malloc(sizeof(struct Entry));
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
}

// del handler for redis command `del`
void do_del(char **cmd, struct Response *res) {
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
  struct HashNode *node = hm_del(&g_data.db, &key.node, &entry_eq);
  if (node) {
    /* successfully deleted the node from hashMap */
    struct Entry *entry = container_of(node, struct Entry, node);
    /* prepares the response */
    free(entry->key);
    free(entry->val);
    free(entry);
  } else {
    res->status = RES_NX;
  }
}

// req handler
void do_req(char **cmd, int *cmd_size, struct Response *res) {
  if (*cmd_size == 2 && strcmp(cmd[0], "get") == 0) {
    return do_get(cmd, res);
  } else if (*cmd_size == 3 && strcmp(cmd[0], "set") == 0) {
    return do_set(cmd);
  } else if (*cmd_size == 2 && strcmp(cmd[0], "del") == 0) {
    return do_del(cmd, res);
  } else {
    res->status = RES_NX;
  }
}

// function to prepare the response message
void make_res(struct Response *resp, uint8_t **out, int *out_size,
              int *out_cap) {
  uint32_t res_len = 4 + (uint32_t)resp->data_size;
  // length of the response
  append_buf(out, (uint8_t *)&res_len, 4, out_size, out_cap);
  // resp.status
  append_buf(out, (uint8_t *)&(resp->status), 4, out_size, out_cap);
  /* printf("%u\n", (*out)[4]); */
  /* printf("%u\n", (*out)[5]); */
  /* printf("%u\n", (*out)[6]); */
  /* printf("%u\n", (*out)[7]); */
  // resp.data (if any)
  append_buf(out, resp->data, resp->data_size, out_size, out_cap);
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
  // Response init
  struct Response resp = {0,
                          (uint8_t *)malloc(MAX_PAYLOAD_SIZE * sizeof(uint8_t)),
                          0, MAX_PAYLOAD_SIZE};
  if (!resp.data) {
    error("resp.data malloc failed");
    free(cmd);
    return false;
  }
  do_req(cmd, &cmd_size, &resp);
  make_res(&resp, &conn->outgoing, &conn->outgoing_size,
           &conn->outgoing_capacity);
  free(resp.data);
  for (size_t i = 0; i < cmd_size; i++) {
    free(cmd[i]);
  }
  free(cmd);
  consume_buf(&conn->incoming, 4 + host_len, &conn->incoming_size,
              &conn->incoming_capacity);
  return true;
}

// handle for accepting connections
struct Conn *handle_accept(int fd) {
  struct sockaddr_in client;
  socklen_t clilen = sizeof(client);
  errno = 0;
  int sockfd = -1;
  do {
    sockfd = accept(fd, (struct sockaddr *)&client, &clilen);
  } while (sockfd <= 0 && errno == EAGAIN);

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

  // need to check malloc idiot
  if (conn->incoming == NULL || conn->outgoing == NULL) {
    error("memory not available for incoming and outgoing");
    free(conn->incoming);
    free(conn->outgoing);
    free(conn);
    close(sockfd);

    return NULL;
  }
  return conn;
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

int main(int argc, char *argv[]) {
  int fd;
  struct sockaddr_in server;
  bzero(&server, sizeof(server));
  if (argc < 2)
    perror("port no.");
  fd = socket(PF_INET, SOCK_STREAM, 0); // socket syscall
  if (fd < 0)
    error("socket() error");
  int val = -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); // to counter rtt

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(atoi(argv[1]));
  nonB(fd);
  if (bind(fd, (const struct sockaddr *)&server, sizeof(server)) < 0) {
    error("bind() error");
    exit(0);
  }

  if (listen(fd, SOMAXCONN) < 0) {
    error("listen()");
    exit(0);
  }

  struct Conn **fdconn = (struct Conn **)calloc(8, sizeof(struct Conn *));
  if (fdconn == NULL) {
    error("fdconn error");
    exit(0);
  }
  size_t fdconn_cap = 8;
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
    for (int i = 0; i < (int)fdconn_cap; i++) {
      struct Conn *conn = fdconn[i];
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
    int err = poll(poll_args, (nfds_t)poll_args_size, -1);
    if (err < 0 && errno == EAGAIN) {
      continue;
    }
    if (err < 0) {
      error("poll()");
      exit(0);
    }
    if (poll_args[0].revents) {
      struct Conn *conn = handle_accept(fd);
      if (conn && conn->fd >= 0) {
        if (fdconn_cap < (size_t)conn->fd + 1) {
          int new_cap = (size_t)conn->fd + 1;
          struct Conn **tmp = (struct Conn **)realloc(
              fdconn, (size_t)new_cap * sizeof(struct Conn *));
          if (tmp == NULL) {
            error("fdconn realloc()");
            exit(0);
          }
          /* for (size_t i = fdconn_cap; i < (size_t)new_cap; i++) { */
          /*   tmp[i] = NULL; */
          /* } */
          fdconn = tmp;
          fdconn_cap = new_cap;
        }
        assert(!fdconn[conn->fd]);
        fdconn[conn->fd] = conn;
      }
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
      if ((size_t)fd >= fdconn_cap) {
        error("mapping issue");
        continue;
      }
      struct Conn *conn = fdconn[fd];
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

        close(conn->fd);
        free(conn->incoming);
        free(conn->outgoing);
        fdconn[conn->fd] = NULL;
        free(conn);
      }
    }
  }
  return 0;
}
