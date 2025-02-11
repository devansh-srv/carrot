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
    exit(0);
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
  memcpy(&(*incoming)[*incoming_size], data, len * sizeof(uint8_t));
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

// sends one req at a time (echoing right now)
// simply copies the incoming to outgoing to send to server
bool one_req(struct Conn *conn) {
  if (conn->incoming_size < 4) {
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming, 4);
  int host_len = ntohl(len);
  if ((size_t)host_len > MAX_PAYLOAD_SIZE) {
    error("msg too long");
    conn->want_close = true;
    return false;
  }
  if (host_len + 4 > conn->incoming_size) {
    return false;
  }
  uint8_t *req = &(conn->incoming[4]);
  /* printf("client says: len:%d data:%.*s\n", host_len, */
  /*        host_len < 100 ? host_len : 100, req); */
  printf("client says: len:%d data:%s\n", host_len, req);
  append_buf(&conn->outgoing, (uint8_t *)&len, 4, &conn->outgoing_size,
             &conn->outgoing_capacity);
  append_buf(&conn->outgoing, req, host_len, &conn->outgoing_size,
             &conn->outgoing_capacity);
  consume_buf(&conn->incoming, 4 + host_len, &conn->incoming_size,
              &conn->incoming_capacity);
  return true;
}

// for reading uint32_t type
bool read_u32(uint8_t **curr, uint8_t *end, uint32_t *out) {

  if (*curr + 4 > end) {
    return false;
  }
  memcpy(out, *curr, 4);
  *curr += 4;
  return true;
}
// for reading string type
bool read_str(uint8_t **curr, uint8_t *end, size_t len, char **out) {

  if (*curr + len > end) {
    return false;
  }
  *out = (char *)malloc(len + 1);
  if (out == NULL) {
    error("calloc failed @ read_str");
    exit(EXIT_FAILURE);
  }
  memcpy(*out, *curr, len);
  *curr += len;
  return true;
}

// parser
int parse_req(uint8_t *data, size_t len, char ***out, int *out_size,
              int *out_cap) {
  uint8_t *end = data + len;
  uint32_t num_str = 0;
  if (!read_u32(&data, end, &num_str)) {
    error("parse_req 1 failed");
    exit(EXIT_FAILURE);
  }
  if (num_str > MAX_ARGS) {
    error("args too long");
    exit(EXIT_FAILURE);
  }
  while (*out_size < num_str) {
    uint32_t len = 0;
    if (!read_u32(&data, end, &len)) {
      error("failed to read len in parse_req");
      exit(EXIT_FAILURE);
    }
    if (*out_size >= *out_cap) {
      int new_cap = (*out_cap) * 2;
      char **tmp = (char **)realloc(*out, new_cap);
      if (tmp == NULL) {
        exit(EXIT_FAILURE);
      }
      *out = tmp;
      *out_cap = new_cap;
    }
    char *s = NULL;
    if (!read_str(&data, end, len, &s)) {
      error("failed to read str in parse_req");
      exit(EXIT_FAILURE);
    }
    (*out)[*out_size] = s;
    (*out_size)++;
  }
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
  int status;
  uint8_t **data;
  int data_size;
  int data_cap;
};

void make_res(struct Response *resp, uint8_t ***out, int *out_size,
              int *out_cap) {
  int res_len = 4 + resp->data_size;
  append_buf(*out, (uint8_t *)&res_len, 4, out_size, out_cap);
  append_buf(*out, (uint8_t *)&resp->status, 4, out_size, out_cap);
  append_buf(*out, *resp->data, resp->data_size, out_size, out_cap);
}
// callback for accept
// accept client connection
// initializes a struct Conn* instance conn
// returns client conn state

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
  append_buf(&conn->incoming, buf, (size_t)err, &conn->incoming_size,
             &conn->incoming_capacity);

  // isn't very clear
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
      /* printf("%d bool: %d, %d, %d\n", conn->fd, conn->want_read, */
      /*        conn->want_write, conn->want_close); */
      /* printf("%d %d %d %d", conn->incoming_size, conn->incoming_capacity, */
      /*        conn->outgoing_size, conn->outgoing_capacity); */

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
