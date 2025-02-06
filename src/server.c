#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
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
  errno = 0;
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
  int *incoming;
  int incoming_size;
  int incoming_capacity;
  int *outgoing;
  int outgoing_size;
  int outgoing_capacity;
};

void append_buf(int **incoming, int *data, size_t len, int *incoming_size,
                int *incoming_capacity) {
  // dynamically allocate memory
  int req = (*incoming_size) + len;
  if (req > (*incoming_capacity)) {
    int new_cap =
        ((*incoming_capacity) * 2 > req ? (*incoming_capacity) * 2 : req);
    int *tmp = (int *)realloc(*incoming, new_cap * sizeof(int));
    if (tmp == NULL) {
      perror("realloc()");
      return;
    }
    *incoming = tmp;
    (*incoming_capacity) = new_cap;
  }
  memcpy(&(*incoming)[*incoming_size], data, len * sizeof(int));
  (*incoming_size) += len;
}

void consume_buf(int **incoming, size_t len, int *incoming_size,
                 int *incoming_capacity) {
  // dynamically de-allocate memory
  if (len >= (size_t)*incoming_size) {
    *incoming_size = 0;
    return;
  }
  memmove(*incoming, *incoming + len, (*incoming_size - len) * sizeof(int));
  *incoming_size -= len;
  // we can shrink if needed
  if (*incoming_size < *incoming_capacity / 4) {
    int new_cap = (*incoming_capacity) / 2;
    if (new_cap < 4)
      new_cap = 4;
    int *tmp = (int *)realloc(*incoming, new_cap * sizeof(int));
    if (tmp) {
      *incoming = tmp;
      *incoming_capacity = new_cap;
    }
  }
}

int read_full(int sockfd, char *buf, size_t n) {
  ssize_t rv;
  while (n > 0) {
    rv = read(sockfd, buf, n);
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n); // can read less than n bytes due to SIGINT
    n -= (size_t)rv;
    buf += rv; // advancing the pointer for next read cycle
  }
  return 0;
}

int write_all(int sockfd, const char *buf, size_t n) {
  ssize_t rv;
  while (n > 0) {
    rv = write(sockfd, buf, n);
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

/* poll(struct pollfd *fds, nfds_t nfds, int timeout) */
int one_req(int sockfd) {

  char rbuf[4 + MAX_PAYLOAD_SIZE];
  int err = read_full(sockfd, rbuf, 4);
  if (err) {
    error("read()");
    return err;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  len = ntohl(len); // change endianess
  if (len > MAX_PAYLOAD_SIZE) {
    error("msg too long");
    return -1;
  }

  err = read_full(sockfd, &rbuf[4], len);
  if (err) {
    error("read()");
    return err;
  }
  /* fprintf(stderr, "client says: %.*s\n", len, &rbuf[4]); */
  /* bzero(rbuf, sizeof(rbuf)); */
  /* printf("Client says: %s", &rbuf[4]); */

  /* len = sizeof(msg); */
  /* if (len > MAX_PAYLOAD_SIZE) { */
  /*   error("msg too long"); */
  /*   return -1; */
  /* } */
  const char reply[] = "PONG";
  char wbuf[4 + sizeof(reply)];
  len = (uint32_t)strlen(reply);
  uint32_t len_net = htonl(len);
  memcpy(wbuf, &len_net, 4);
  memcpy(&wbuf[4], reply, len);
  err = write_all(sockfd, wbuf, 4 + len);
  /* bzero(wbuf, sizeof(wbuf)); */
  if (err) {
    error("write_all()");
    return -1;
  }
  return 0;
  /* char wbuf[4 + MAX_PAYLOAD_SIZE]; */
  /* memcpy(wbuf, &len, 4); */
  /* memcpy(wbuf, msg, len); */
  /* err = write_all(sockfd, wbuf, 4 + len); */
  /* if (err) { */
  /*   return err; */
  /* } */
  /* return 0; */
}
int main(int argc, char *argv[]) {
  int fd;
  socklen_t clilen;
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

  if (bind(fd, (const struct sockaddr *)&server, sizeof(server)) < 0) {
    error("bind() error");
    exit(0);
  }

  if (listen(fd, SOMAXCONN) < 0) {
    error("listen()");
    exit(0);
  }

  while (1) {
    struct sockaddr_in client;
    clilen = sizeof(client);
    int newsock = accept(fd, (struct sockaddr *)&client, &clilen);
    if (newsock < 0)
      error("accept()");
    while (1) {
      /* printf("Enter your message for the client: "); */
      /* char msg[MAX_PAYLOAD_SIZE]; */
      /* scanf("%s", msg); */
      int err = one_req(newsock);
      if (err) {
        break;
      }
    }
    close(newsock);
  }
  close(fd);
  return 0;
}
