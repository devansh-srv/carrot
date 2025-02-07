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
const size_t MAX_PAYLOAD_SIZE = 4096; // scale this shit up

void error(const char *msg) { perror(msg); }
void errno_msg(const char *msg) {
  fprintf(stderr, "errno: %d, error: %s", errno, msg);
}

void nonB(int fd) { // we need non blocking sockets
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    errno_msg("fcntl()");
    exit(0);
  }
  flags |= O_NONBLOCK;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    errno_msg("fnctl() set flag");
    exit(0);
  }
}

// for managing the state
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
  printf("client says: len:%d data:%.*s\n", host_len,
         host_len < 100 ? host_len : 100, req);

  append_buf(&conn->outgoing, (uint8_t *)&len, 4, &conn->outgoing_size,
             &conn->outgoing_capacity);
  append_buf(&conn->outgoing, req, host_len, &conn->outgoing_size,
             &conn->outgoing_capacity);
  consume_buf(&conn->incoming, 4 + host_len, &conn->incoming_size,
              &conn->incoming_capacity);
  return true;
}

// callback for accept
struct Conn *handle_accpet(int fd) {
  struct sockaddr_in client;
  socklen_t clilen = sizeof(client);
  errno = 0;

  int sockfd = accept(fd, (struct sockaddr *)&client, &clilen);
  if (sockfd < 0) {
    errno_msg("accept() fail 104");
    return NULL;
  }
  uint32_t ip = client.sin_addr.s_addr;
  fprintf(stderr, "client_addr: %d.%d.%d.%d:%d\n", ip & 255, (ip >> 8) & 255,
          (ip >> 16) & 255, (ip >> 24), ntohs(client.sin_port));
  nonB(sockfd);

  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  conn->fd = sockfd;
  conn->want_read = true;
  conn->want_write = false;
  conn->want_close = false;
  conn->incoming = (uint8_t *)malloc(8 * sizeof(uint8_t));
  conn->outgoing = (uint8_t *)malloc(8 * sizeof(uint8_t));
  conn->incoming_size = 0;
  conn->outgoing_size = 0;
  conn->incoming_capacity = 8;
  conn->outgoing_capacity = 8;

  // need to check malloc idiot
  if (conn->incoming == NULL || conn->outgoing == NULL) {
    error("memory not available for incoming and outgoing");
    free(conn->incoming);
    free(conn->outgoing);
    close(sockfd);

    return NULL;
  }
  return conn;
}
// callback for write
void handle_write(struct Conn *conn) {
  assert(conn->outgoing_size > 0);
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

  struct Conn **fdconn = (struct Conn **)malloc(8 * sizeof(struct Conn *));
  if (fdconn == NULL) {
    error("fdconn error");
    exit(0);
  }
  size_t fdconn_cap = 8;
  struct pollfd *poll_args = (struct pollfd *)malloc(8 * sizeof(struct pollfd));
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
      struct pollfd pfd = {conn->fd, POLL_ERR, 0};
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
      struct Conn *conn = handle_accpet(fd);
      if (conn) {
        if (fdconn_cap < (size_t)conn->fd + 1) {
          int new_cap = (size_t)conn->fd;
          struct Conn **tmp = (struct Conn **)realloc(
              fdconn, (size_t)new_cap * sizeof(**fdconn));
          if (tmp == NULL) {
            error("fdconn realloc()");
            exit(0);
          }
          fdconn = tmp;
          fdconn_cap = new_cap;
        }
        assert(!fdconn[conn->fd]);
        fdconn[conn->fd] = conn;
      }
    }
    for (int i = 0; i < poll_args_cap; i++) {
      short ready = poll_args[i].revents;
      if (ready == 0) {
        continue;
      }
      struct Conn *conn = fdconn[poll_args[i].fd];
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
