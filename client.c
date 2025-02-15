#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <linux/limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
// payload size
const size_t MAX_PAYLOAD_SIZE = 32 << 20;
/* const size_t MAX_PAYLOAD_SIZE = 4096; */

void error(const char *msg) { perror(msg); }

void errno_msg(const char *msg) {
  fprintf(stderr, "errno: %d, error: %s", errno, msg);
}

int read_full(int sockfd, uint8_t *buf, size_t n) {

  /* 33554431 */
  ssize_t rv;
  while (n > 0) {
    rv = read(sockfd, buf, n);
    if (rv < 0 && errno == EINTR)
      continue;
    if (rv <= 0) {

      return -1;
    }
    assert((size_t)rv <= n); // can read less than n bytes due to SIGINT
    n -= (size_t)rv;
    buf += rv; // advancing the pointer for next read cycle
  }
  return 0;
}

// works similarly to read_full()
int write_all(int sockfd, uint8_t *buf, size_t n) {
  ssize_t rv;
  while (n > 0) {
    rv = write(sockfd, buf, n);
    if (rv < 0 && errno == EINTR)
      continue;
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

// copies msg into wbuf and handles endianess of the val len
// finally uses append_buf() to write into wbuf and write_all() calls write()
// syscall
int send_query(int sockfd, char **cmd, int *cmd_size, int *cmd_cap) {
  // redis pkt --------------------------------  header -> cmd_len -> str_len
  // ->str ->str_len ->str ......... principal header length is already 4
  uint32_t len = 4;
  for (int i = 0; i < *cmd_size; i++) {
    len += (4 + strlen(cmd[i]));
  }
  if (len > MAX_PAYLOAD_SIZE) {
    return -1;
  }
  uint8_t *wbuf = (uint8_t *)calloc(4 + MAX_PAYLOAD_SIZE, sizeof(uint8_t));
  if (!wbuf) {
    error("calloc failed");
    return -1;
  }
  printf("len: %u\n", len);
  uint32_t n_len = htonl(len);
  printf("n_len: %u\n", n_len);
  memcpy(wbuf, &n_len, 4);
  int cmd_size_net = htonl(*cmd_size);
  memcpy(wbuf + 4, &cmd_size_net, 4);
  size_t curr = 8;
  for (int i = 0; i < *cmd_size; i++) {
    uint32_t str_len = strlen(cmd[i]);
    uint32_t str_len_net = htonl(str_len);
    memcpy(wbuf + curr, &str_len_net, 4);
    curr += 4;
    memcpy(wbuf + curr, cmd[i], str_len);
    curr += str_len;
  }

  int ret = write_all(sockfd, wbuf, 4 + len);
  free(wbuf);
  return ret;
}

// reades into rbuf using read_full()
// and handles endianess of len
int read_res(int sockfd) {
  uint8_t *rbuf = (uint8_t *)calloc(4 + MAX_PAYLOAD_SIZE + 1, sizeof(uint8_t));
  if (!rbuf) {
    error("calloc failed");
    return -1;
  }
  errno = 0;
  int err = read_full(sockfd, rbuf, (size_t)4);
  if (err && errno == 0) {
    error("EOF");
    return err;
  }
  if (err) {
    error("read_all() failed");
    return err;
  }
  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  printf("len: %d\n", len);
  if ((size_t)len > MAX_PAYLOAD_SIZE) {
    error("msg too long");
    return -1;
  }
  err = read_full(sockfd, rbuf + 4, len);
  if (err) {
    error("read() error");
    return err;
  }
  uint32_t rescode = 0;
  if (len < 4) {
    error("bad response");
    return -1;
  }
  memcpy(&rescode, rbuf + 4, (size_t)4);
  printf("server says: [%u], %.*s\n", rescode, len - 4, rbuf + 8);
  free(rbuf);
  return 0;
}

int main(int argc, char *argv[]) {
  int sock, n;
  if (argc < 3)
    error("hostname and port no.");
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    error("socket()");

  struct sockaddr_in server;
  bzero(&server, sizeof(server));

  struct hostent *s = gethostbyname(argv[1]); // searches for host in /etc/host
  if (s == NULL) {
    error("No such host");
  }
  server.sin_family = AF_INET;
  server.sin_port = htons(atoi(argv[2]));
  bcopy((const void *)s->h_addr, (void *)&server.sin_addr.s_addr,
        s->h_length); // we need to copy the addr

  if (connect(sock, (const struct sockaddr *)&server, sizeof(server)) < 0) {
    error("connect()");
    exit(EXIT_FAILURE);
  }
  size_t query_sz = argc - 3;
  char **query =
      (char **)calloc(query_sz, sizeof(char *)); // structure queries for server

  if (query == NULL) {
    error("query mem fails");
    exit(0);
  }
  int query_size = 0;
  for (int i = 0; i < query_sz; i++) {
    query[i] = strdup(argv[i + 3]);
    query_size++;
  }

  int err = send_query(sock, query, &query_size, (int *)&query_sz);
  if (err) {

    error("send_query_main fail");
    close(sock);
    return EXIT_FAILURE;
  }
  for (int i = 0; i < query_size; i++) {

    if (query[i] != NULL) {
      free(query[i]);
      query[i] = NULL;
    }
  }
  free(query);
  query = NULL;
  err = read_res(sock);
  if (err) {
    error("read_res_main fail");
    close(sock);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
