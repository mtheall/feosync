#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "message.h"

static int update(int s, const char *filename);

int main(int argc, char *argv[]) {
  FILE   *find;
  char   *line = NULL;
  size_t linesz = 0;
  int    rc;
  int    s;
  char   *cmd;
  const char *ip, *directory;
  message_t msg;
  unsigned char digest[16];
  struct addrinfo hints, *res;

  if(argc != 3) {
    fprintf(stderr, "Usage: %s <ip> <directory>\n", argv[0]);
    return 1;
  }

  ip = argv[1];
  directory = argv[2];

  if(chdir(directory)) {
    fprintf(stderr, "chdir('%s'):  %s\n", directory, strerror(errno));
    return 1;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if((rc = getaddrinfo(ip, "5903", &hints, &res))) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
    return 1;
  }

  s = socket(AF_INET, SOCK_STREAM, 0);
  if(s == -1) {
    fprintf(stderr, "socket:  %s\n", strerror(errno));
    freeaddrinfo(res);
    return 1;
  }

  if(connect(s, res->ai_addr, res->ai_addrlen) == -1) {
    fprintf(stderr, "connect('%s'): %s\n", ip, strerror(errno));
    freeaddrinfo(res);
    close(s);
    return 1;
  }
  freeaddrinfo(res);

  cmd = "find * -type d | sort";
  find = popen(cmd, "r");
  if(find == NULL) {
    fprintf(stderr, "popen('%s'): %s\n", cmd, strerror(errno));
    close(s);
    return 1;
  }

  while((rc = getline(&line, &linesz, find)) != -1) {
    line[rc-1] = 0;
    memset(&msg, 0, sizeof(msg));
    msg.header.size = rc+1;
    msg.header.type = MKDIR;
    msg.data[0] = '/';
    memcpy(msg.data+1, line, rc);
    printf("mkdir %s\n", msg.data);

    rc = sendMessage(s, &msg);
    if(rc <= 0) {
      close(s);
      pclose(find);
      free(line);
      return 1;
    }

    rc = recvMessage(s, &msg);
    if(rc <= 0 || msg.header.rc == -1) {
      close(s);
      pclose(find);
      free(line);
      return 1;
    }
  }
  pclose(find);

  cmd = "find * -type f | sort";
  find = popen(cmd, "r");
  if(find == NULL) {
    fprintf(stderr, "popen('%s'): %s\n", cmd, strerror(errno));
    close(s);
    free(line);
    return 1;
  }

  while((rc = getline(&line, &linesz, find)) != -1) {
    line[rc-1] = 0;
    memset(&msg, 0, sizeof(msg));
    msg.header.size = rc+1;
    msg.header.type = MD5SUM;
    msg.data[0] = '/';
    memcpy(msg.data+1, line, rc);
    printf("md5sum %s\n", msg.data);

    rc = sendMessage(s, &msg);
    if(rc <= 0 || msg.header.rc == -1) {
      close(s);
      pclose(find);
      free(line);
      return 1;
    }

    rc = md5sum(digest, line);
    if(rc == -1) {
      close(s);
      pclose(find);
      free(line);
      return 1;
    }

    rc = recvMessage(s, &msg);
    if(rc <= -1 || msg.header.rc == -1) {
      close(s);
      pclose(find);
      free(line);
      return 1;
    }

    if(memcmp(digest, msg.data, sizeof(digest))) {
      fprintf(stderr, "update /%s\n", line);
      rc = update(s, line);
      if(rc <= 0) {
        close(s);
        pclose(find);
        free(line);
        return 1;
      }
    }
  }

  pclose(find);
  free(line);

  return 0;
}

static int update(int s, const char *filename) {
  FILE *fp;
  int rc, rc2;
  message_t msg;

  memset(&msg, 0, sizeof(msg));

  fp = fopen(filename, "rb");
  if(fp == NULL) {
    fprintf(stderr, "fopen('%s'): %s\n", filename, strerror(errno));
    return -1;
  }

  msg.header.type = UPDATE;
  msg.header.size = strlen(filename)+2;
  msg.data[0] = '/';
  memcpy(msg.data+1, filename, strlen(filename)+1);

  rc = sendMessage(s, &msg);
  if(rc <= 0) {
    fclose(fp);
    return rc;
  }

  do {
    rc = fread(msg.data, 1, sizeof(msg.data), fp);
    if(rc > 0) {
      msg.header.size = rc;
      rc2 = sendMessage(s, &msg);
      if(rc2 <= 0) {
        fclose(fp);
        return rc2;
      }
    }
  } while(rc > 0);

  fclose(fp);

  msg.header.size = 0;
  rc = sendMessage(s, &msg);
  if(rc <= 0)
    return rc;

  return 1;
}
