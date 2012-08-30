#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <openssl/md5.h>
#include <zlib.h>
#include "message.h"

#ifdef WIN32
#include <winsock.h>
typedef int socklen_t;
#define SHUT_RDWR SD_BOTH
void PrintSocketError(const char *name);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#else
#include <sys/socket.h>
#define closesocket close
#define PrintSocketError perror
#endif

static unsigned char buf[1024];
static const int on = 1;

static int update(int s, const char *filename);
static int md5sum(unsigned char *digest, const char *filename);

int main(int argc, char *argv[]) {
  FILE   *find;
  char   *line = NULL;
  size_t linesz = 0;
  int    rc;
  int    s, b;
  char   *cmd;
  const char *host = NULL, *directory;
  message_t msg;
  unsigned char digest[16];
  struct addrinfo hints, *res;
  struct sockaddr_in addr;
  socklen_t          addr_len = sizeof(addr);

  if(argc != 2 && argc != 3) {
    fprintf(stderr, "Usage: %s <directory> [host]\n", argv[0]);
    return 1;
  }

  directory = argv[1];
  if(argc == 3)
    host = argv[2];

  if(chdir(directory)) {
    fprintf(stderr, "chdir('%s'):  %s\n", directory, strerror(errno));
    return 1;
  }

#ifdef WIN32
  WSADATA wsaData;
  if(WSAStartup(MAKEWORD(2,0), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed.\n");
    return 1;
  }
  atexit((void(*)(void))WSACleanup);
#endif

  if(host == NULL) { // host not provided; attempt to auto-discover
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if(b == -1) {
      PrintSocketError("socket");
      return 1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(0xFE05);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(setsockopt(b, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on))
    || setsockopt(b, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)))
    {
      PrintSocketError("setsockopt");
      shutdown(b, SHUT_RDWR);
      closesocket(b);
      return 1;
    }
    if(bind(b, (struct sockaddr*)&addr, sizeof(addr)))
    {
      PrintSocketError("bind");
      shutdown(b, SHUT_RDWR);
      closesocket(b);
      return 1;
    }

    rc = recvfrom(b, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addr_len);
    shutdown(b, SHUT_RDWR);
    closesocket(b);
    if(rc <= 0) {
      if(rc == -1)
        PrintSocketError("recvfrom");
      return 1;
    }
    addr.sin_port = htons(0xFE05);
  }
  else { // host was provided
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if((rc = getaddrinfo(host, "65029", &hints, &res))) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
      return 1;
    }
    memcpy(&addr, res->ai_addr, res->ai_addrlen);
    addr_len = res->ai_addrlen;
    freeaddrinfo(res);
  }

  printf("Connecting to %s\n", inet_ntoa(addr.sin_addr));
  printf("port = %d\n", ntohs(addr.sin_port));

  s = socket(AF_INET, SOCK_STREAM, 0);
  if(s == -1) {
    PrintSocketError("socket");
    return 1;
  }

  if(connect(s, (struct sockaddr*)&addr, addr_len) == -1) {
    PrintSocketError("connect");
    shutdown(s, SHUT_RDWR);
    closesocket(s);
    return 1;
  }

  cmd = "find * -type d | sort";
  find = popen(cmd, "r");
  if(find == NULL) {
    fprintf(stderr, "popen('%s'): %s\n", cmd, strerror(errno));
    shutdown(s, SHUT_RDWR);
    closesocket(s);
    return 1;
  }

  while((rc = getline(&line, &linesz, find)) != -1) {
    if(line[rc-1] == '\n')
      line[rc-1] = 0;
    memset(&msg, 0, sizeof(msg));
    msg.header.size = rc+1;
    msg.header.type = MKDIR;
    msg.data[0] = '/';
    memcpy(msg.data+1, line, rc);
    printf("mkdir %s\n", msg.data);

    rc = sendMessage(s, &msg);
    if(rc <= 0) {
      shutdown(s, SHUT_RDWR);
      closesocket(s);
      pclose(find);
      free(line);
      return 1;
    }

    rc = recvMessage(s, &msg);
    if(rc <= 0 || msg.header.rc == -1) {
      shutdown(s, SHUT_RDWR);
      closesocket(s);
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
    shutdown(s, SHUT_RDWR);
    closesocket(s);
    free(line);
    return 1;
  }

  while((rc = getline(&line, &linesz, find)) != -1) {
    if(line[rc-1] == '\n')
      line[rc-1] = 0;
    memset(&msg, 0, sizeof(msg));
    msg.header.size = rc+1;
    msg.header.type = MD5SUM;
    msg.data[0] = '/';
    memcpy(msg.data+1, line, rc);
    printf("md5sum %s\n", msg.data);

    rc = sendMessage(s, &msg);
    if(rc <= 0 || msg.header.rc == -1) {
      shutdown(s, SHUT_RDWR);
      closesocket(s);
      pclose(find);
      free(line);
      return 1;
    }

    rc = md5sum(digest, line);
    if(rc == -1) {
      shutdown(s, SHUT_RDWR);
      closesocket(s);
      pclose(find);
      free(line);
      return 1;
    }

    rc = recvMessage(s, &msg);
    if(rc <= -1 || msg.header.rc == -1) {
      shutdown(s, SHUT_RDWR);
      closesocket(s);
      pclose(find);
      free(line);
      return 1;
    }

    if(memcmp(digest, msg.data, sizeof(digest))) {
      fprintf(stderr, "update /%s\n", line);
      rc = update(s, line);
      if(rc <= 0) {
        shutdown(s, SHUT_RDWR);
        closesocket(s);
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
  int rc, rc2, flush;
  z_stream strm;
  message_t msg;

  memset(&msg, 0, sizeof(msg));
  memset(&strm, 0, sizeof(strm));

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

  deflateInit(&strm, Z_BEST_COMPRESSION);

  msg.header.size = 0;
  strm.avail_in   = 0;
  strm.avail_out = sizeof(msg.data);
  strm.next_out  = msg.data;

  do {
    // need to grab more input
    if(strm.avail_in == 0) {
      rc = fread(buf, 1, sizeof(buf), fp);
      if(ferror(fp)) {
        deflateEnd(&strm);
        fclose(fp);
        return -1;
      }
      flush = feof(fp) ? Z_FINISH : Z_NO_FLUSH;
      strm.avail_in  = rc;
      strm.next_in   = buf;
    }

    rc = deflate(&strm, flush);

    // filled up the output buffer or finished compressing
    if(strm.avail_out == 0 || rc == Z_STREAM_END) {
      msg.header.size = strm.next_out - msg.data;
      rc2 = sendMessage(s, &msg);
      if(rc2 <= 0) {
        fclose(fp);
        deflateEnd(&strm);
        return rc2;
      }
      strm.avail_out = sizeof(msg.data);
      strm.next_out  = msg.data;
    }
  } while(rc == Z_OK);

  printf("Compression ratio: %lu.%02lu\n",
    strm.total_out/strm.total_in,
    (strm.total_out * 100 / strm.total_in) % 100);

  fclose(fp);
  deflateEnd(&strm);

  msg.header.size = 0;
  rc = sendMessage(s, &msg);
  if(rc <= 0)
    return rc;

  return 1;
}

static int md5sum(unsigned char *digest, const char *filename) {
  FILE *fp;
  MD5_CTX ctx;
  int rc;

  if(digest == NULL || filename == NULL) {
    errno = EINVAL;
    return -1;
  }

  fp = fopen(filename, "rb");
  if(fp == NULL) {
    return -1;
  }

  MD5_Init(&ctx);
  do {
    rc = fread(buf, 1, sizeof(buf), fp);
    if(rc > 0)
      MD5_Update(&ctx, buf, rc);
  } while(rc == sizeof(buf));

  MD5_Final(digest, &ctx);

  rc = 0;
  if(ferror(fp))
    rc = -1;

  fclose(fp);
  return rc;
}

#ifdef WIN32
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
  static char  line[1024];
  char         *ptr;
  size_t       len;

  if(lineptr == NULL || n == NULL) {
    errno = EINVAL;
    return -1;
  }
  if(ferror(stream) || feof(stream))
    return -1;

  if((ptr = fgets(line, sizeof(line), stream)) == NULL)
    return -1;

  len = strlen(line);
  if(*lineptr == NULL)
    *n = 0;
  if(*n < len) {
    ptr = realloc(*lineptr, len);
    if(ptr == NULL)
      return -1;
    *lineptr = ptr;
    *n = len;
  }

  memcpy(*lineptr, line, len+1);
  return len;
}

void PrintSocketError(const char *name) {
  switch(WSAGetLastError()) {
#define ERR(x) case x: fprintf(stderr, "%s:" #x "\n", name); break;
    ERR(WSAEACCES)
    ERR(WSAEADDRINUSE)
    ERR(WSAEADDRNOTAVAIL)
    ERR(WSAEAFNOSUPPORT)
    ERR(WSAEALREADY)
    ERR(WSAECONNABORTED)
    ERR(WSAECONNREFUSED)
    ERR(WSAECONNRESET)
    ERR(WSAEFAULT)
    ERR(WSAEHOSTUNREACH)
    ERR(WSAEINPROGRESS)
    ERR(WSAEINTR)
    ERR(WSAEINVAL)
    ERR(WSAEISCONN)
    ERR(WSAEMFILE)
    ERR(WSAEMSGSIZE)
    ERR(WSAENETDOWN)
    ERR(WSAENETRESET)
    ERR(WSAENETUNREACH)
    ERR(WSAENOBUFS)
    ERR(WSAENOPROTOOPT)
    ERR(WSAENOTCONN)
    ERR(WSAENOTSOCK)
    ERR(WSAEOPNOTSUPP)
    ERR(WSAEPROTONOSUPPORT)
    ERR(WSAEPROTOTYPE)
    ERR(WSAEPROVIDERFAILEDINIT)
    ERR(WSAESHUTDOWN)
    ERR(WSAESOCKTNOSUPPORT)
    ERR(WSAETIMEDOUT)
    ERR(WSAEWOULDBLOCK)
    ERR(WSAEINVALIDPROCTABLE)
    ERR(WSAEINVALIDPROVIDER)
    ERR(WSANOTINITIALISED)
  }
}
#endif
