#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void error(const char *msg) {
  perror(msg);
  exit(0);
}
int main(int argc, char *argv[]) {
  int fd, n;
  socklen_t clilen;
  struct sockaddr_in server, client;
  bzero(&server, sizeof(server));
  if (argc < 2)
    perror("port no.");
  fd = socket(AF_INET, SOCK_STREAM, 0); // socket syscall
  if (fd < 0)
    error("socket() error");

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(atoi(argv[1]));

  if (bind(fd, (const struct sockaddr *)&server, sizeof(server)) < 0)
    error("bind() error");

  listen(fd, SOMAXCONN);

  clilen = sizeof(client);
  while (1) {
    int newsock = accept(fd, (struct sockaddr *)&client, &clilen);
    if (newsock < 0)
      error("listen()");
    char rbuf[1024];
    n = read(newsock, rbuf, sizeof(rbuf));
    if (n < 0)
      error("read()");
    if (n < 0)
      error("read()");
    printf("Recv messaged: %s", rbuf);
    char wbuf[1024] = "Thank you client";
    n = write(newsock, wbuf, sizeof(wbuf));
    if (n < 0)
      error("write()");
    close(newsock);
    close(fd);
    return 0;
  }

  return EXIT_SUCCESS;
}
