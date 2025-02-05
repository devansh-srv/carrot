#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
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
const size_t MAX_PAYLOAD_SIZE = 4096;

void error(const char *msg) { perror(msg); }

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
  printf("length: %d", len);
  if (len > MAX_PAYLOAD_SIZE) {
    error("msg too long");
    return -1;
  }

  err = read_full(sockfd, &rbuf[4], len);
  if (err) {
    error("read()");
    return err;
  }
  fprintf(stderr, "client says: %.*s\n", len, &rbuf[4]);
  /* bzero(rbuf, sizeof(rbuf)); */
  /* printf("Client says: %s", &rbuf[4]); */

  /* len = sizeof(msg); */
  /* if (len > MAX_PAYLOAD_SIZE) { */
  /*   error("msg too long"); */
  /*   return -1; */
  /* } */
  const char reply[] = "world";
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
  fd = socket(AF_INET, SOCK_STREAM, 0); // socket syscall
  if (fd < 0)
    error("socket() error");

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
  return EXIT_SUCCESS;
}
