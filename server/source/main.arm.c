#include <feos.h>
#include <multifeos.h>
#include <dswifi9.h>
#include <errno.h>
#include <md5.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>
#include "message.h"

typedef int socklen_t;
#define perror(x) fprintf(stderr, x ": %s\n", strerror(errno))

static const int yes = 1;
static const int no  = 0;

static unsigned char buf[1024];

static int  process(int s);
static void getHash(message_t *msg);
static int  update(int s, message_t *msg);

static volatile thread_t daemon = NULL;
static volatile bool     quit   = false;
static volatile int      status = -1;

int feosync(void *param);

int main(int argc, char *argv[]) {
  if(argc == 1 || (argv[1] && stricmp(argv[1], "start") == 0)) {
    if(daemon != NULL) { // daemon is already running
      printf("FeOSync Daemon is already running\n");
      return 0;
    }

    // start the daemon
    LdrBeginResidency();
    printf("FeOSync Daemon starting\n");
    daemon = FeOS_CreateThread(DEFAULT_STACK_SIZE, feosync, NULL);
    if(daemon == NULL) {
      fprintf(stderr, "Failed to start FeOSync Daemon\n");
      LdrEndResidency();
      return 1;
    }

    // wait for initialization
    while(status == -1)
      FeOS_Yield();
    if(status) {
      LdrEndResidency();
      FeOS_ThreadJoin(daemon);
    }
    return status;
  }
  else if(argv[1] && stricmp(argv[1], "stop") == 0) {
    if(daemon == NULL) { // daemon is already not running
      printf("FeOSync Daemon is already stopped\n");
      return 0;
    }

    // signal the daemon to stop
    quit = true;
    FeOS_ThreadJoin(daemon);
    printf("FeOSync Daemon is stopping\n");
    LdrEndResidency();
    daemon = NULL;
  }

  return 0;
}

int feosync(void *param) {
  int  rc, s, listener, broadcaster;
  struct sockaddr_in addr;
  socklen_t          addrlen;
  struct in_addr     ip, netmask;

  // initialize wifi
  if(!Wifi_Startup()) {
    fprintf(stderr, "Wifi Failed to initialize\n");
    return (status = 1);
  }

  // get ip address and netmask
  ip = Wifi_GetIPInfo(NULL, &netmask, NULL, NULL);

  // create a listener socket
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if(listener == -1) {
    perror("socket");
    Wifi_Cleanup();
    return (status = 1);
  }

  // create a broadcasting socket
  broadcaster = socket(AF_INET, SOCK_DGRAM, 0);
  if(broadcaster == -1) {
    perror("socket");
    closesocket(listener);
    Wifi_Cleanup();
    return (status = 1);
  }

  // set the socket options
  if(setsockopt(listener,    SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))
  || setsockopt(broadcaster, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes))
  || setsockopt(broadcaster, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
  {
    perror("setsockopt");
    closesocket(listener);
    closesocket(broadcaster);
    Wifi_Cleanup();
    return (status = 1);
  }

  // bind the listener socket to an address
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(0xFE05);
  addr.sin_addr.s_addr = INADDR_ANY;
  rc = bind(listener, (struct sockaddr*)&addr, sizeof(addr));
  if(rc == -1) {
    perror("bind");
    closesocket(listener);
    closesocket(broadcaster);
    Wifi_Cleanup();
    return (status = 1);
  }

  // set the listener and broadcaster sockets to non-blocking
  if(ioctl(listener,    FIONBIO, (char*)&yes)
  || ioctl(broadcaster, FIONBIO, (char*)&yes))
  {
    perror("ioctl");
    closesocket(listener);
    closesocket(broadcaster);
    Wifi_Cleanup();
    return (status = 1);
  }

  // listen for connections
  rc = listen(listener, 5);
  if(rc == -1) {
    perror("listen");
    closesocket(listener);
    closesocket(broadcaster);
    Wifi_Cleanup();
    return (status = 1);
  }

  status = 0;

  while(!quit) {
    // broadcast yourself
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(0xFE05);
    addr.sin_addr.s_addr = ip.s_addr | ~netmask.s_addr;
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    rc = sendto(broadcaster, &ip, sizeof(ip), (int)NULL, (struct sockaddr*)&addr, sizeof(addr));
    if(rc == -1 && errno != EWOULDBLOCK)
      perror("sendto");

    // wait at least one second
    for(rc = 0; rc < 60; rc++)
      swiWaitForVBlank();

    // accept a connection
    s = accept(listener, (struct sockaddr*)&addr, &addrlen);
    if(s == -1 && errno != EWOULDBLOCK) {
      perror("accept");
      closesocket(listener);
      closesocket(broadcaster);
      Wifi_Cleanup();
      return 1;
    }

    if(s != -1) {
      // set connection to blocking
      rc = ioctl(s, FIONBIO, (char*)&no);
      if(rc == -1) {
        perror("ioctl");
        closesocket(listener);
        closesocket(broadcaster);
        closesocket(s);
        Wifi_Cleanup();
        return 1;
      }

      rc = process(s);
      if(rc == -1) {
        closesocket(listener);
        closesocket(broadcaster);
        closesocket(s);
        Wifi_Cleanup();
        return 1;
      }
      closesocket(s);
    }
  }

  closesocket(listener);
  closesocket(broadcaster);
  Wifi_Cleanup();
  return 0;
}

int process(int s) {
  int rc;
  static message_t msg;

  while(1) {
    rc = recvMessage(s, &msg);
    if(rc <= 0)
      return rc;

    switch(msg.header.type) {
      case MD5SUM:
        printf("hash %s\n", msg.data);
        getHash(&msg);
        rc = sendMessage(s, &msg);
        if(rc <= 0)
          return rc;
        break;
      case UPDATE:
        printf("update %s\n", msg.data);
        rc = update(s, &msg);
        if(rc <= 0)
          return rc;
        break;
      case MKDIR:
        printf("mkdir %s\n", msg.data);
        rc = mkdir((char*)msg.data, 0755);
        if(rc == -1 && errno != EEXIST) {
          fprintf(stderr, "mkdir('%s'): %s\n", msg.data, strerror(errno));
          msg.header.rc = -1;
          msg.header.size = 0;
          rc = sendMessage(s, &msg);
          if(rc <= 0)
            return rc;
        }
        else {
          msg.header.rc = 0;
          msg.header.size = 0;
          rc = sendMessage(s, &msg);
          if(rc <= 0)
            return rc;
        }
        break;
      default:
        fprintf(stderr, "Invalid message type (%d)\n", msg.header.type);
        return -1;
        break;
    }
  }
}

void getHash(message_t *msg) {
  MD5_CTX ctx;
  FILE    *fp;
  int     rc;

  if((fp = fopen((char*)msg->data, "rb")) == NULL) {
    if(errno == ENOENT)
      msg->header.rc = 0;
    else {
      fprintf(stderr, "fopen: '%s': %s\n", msg->data, strerror(errno));
      msg->header.rc = -1;
    }
    msg->header.size = 0;
    return;
  }

  if(!MD5_Init(&ctx)) {
    fprintf(stderr, "MD5_Init: '%s': Failed to initialize\n", msg->data);
    fclose(fp);
    msg->header.rc = -1;
    msg->header.size = 0;
    return;
  }
  while((rc = fread(buf, 1, sizeof(buf), fp)) > 0) {
    MD5_Update(&ctx, buf, rc);
    FeOS_Yield();
  }
  if(!MD5_Final(msg->hash, &ctx)) {
    fprintf(stderr, "MD5_Update: '%s': Failed to finalize\n", msg->data);
    fclose(fp);
    msg->header.rc = -1;
    msg->header.size = 0;
    return;
  }

  if(fclose(fp)) {
    fprintf(stderr, "fclose: '%s': %s\n", msg->data, strerror(errno));
    msg->header.rc = -1;
    msg->header.size = 0;
    return;
  }

  msg->header.rc = 0;
  msg->header.size = sizeof(msg->hash);
}

int update(int s, message_t *msg) {
  FILE *fp;
  int  rc;
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  if((fp = fopen((char*)msg->data, "wb")) == NULL) {
    fprintf(stderr, "fopen: '%s': %s\n", msg->data, strerror(errno));
    msg->header.rc = -1;
    msg->header.size = 0;
    return -1;
  }

  inflateInit(&strm);

  while(1) {
    rc = recvMessage(s, msg);
    if(rc <= 0) {
      if(rc == 0)
        fprintf(stderr, "Disconnected during transfer\n");
      else
        fprintf(stderr, "Error receiving data: %s\n", strerror(errno));
      fclose(fp);
      inflateEnd(&strm);
      return rc;
    }
    if(msg->header.size == 0) {
      printf("Compression ratio: %lu.%02lu\n",
        strm.total_in/strm.total_out,
        (strm.total_in * 100 / strm.total_out) % 100);
      fclose(fp);
      inflateEnd(&strm);
      return 1;
    }

    strm.avail_in = msg->header.size;
    strm.next_in  = msg->data;

    do {
      strm.avail_out = sizeof(buf);
      strm.next_out  = buf;
      inflate(&strm, Z_NO_FLUSH);
      rc = fwrite(buf, 1, strm.next_out - buf, fp);
      if(rc != strm.next_out - buf) {
        fprintf(stderr, "Error writing to file: %s\n", strerror(errno));
        fclose(fp);
        inflateEnd(&strm);
        return -1;
      }
      if(strm.avail_in > 0)
        FeOS_Yield();
    } while(strm.avail_in > 0);
  }
}
