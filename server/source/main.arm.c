#include <feos.h>
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
#include "message.h"

typedef int socklen_t;
#define perror(x) fprintf(stderr, x ": %s\n", strerror(errno))

static int yes = 1;
static int no  = 0;

static int  process(int s);
static void getHash(message_t *msg);
static int  update(int s, message_t *msg);

int main(int argc, char *argv[]) {
  int  rc, s, listener;
  int  down;
  struct sockaddr_in addr;
  socklen_t          addrlen;
  struct in_addr     ip;

  if(!Wifi_Startup()) {
    fprintf(stderr, "Wifi Failed to initialize\n");
    return 1;
  }

  ip = Wifi_GetIPInfo(NULL, NULL, NULL, NULL);

  printf("IP: %s\n", inet_ntoa(ip));

  listener = socket(AF_INET, SOCK_STREAM, 0);
  if(listener == -1) {
    perror("socket");
    Wifi_Cleanup();
    return 1;
  }

  rc = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  if(rc == -1) {
    perror("setsockopt");
    closesocket(listener);
    Wifi_Cleanup();
    return 1;
  }

  addr.sin_family = AF_INET;
  addr.sin_port   = htons(5903);
  addr.sin_addr.s_addr = INADDR_ANY;

  rc = bind(listener, (struct sockaddr*)&addr, sizeof(addr));
  if(rc == -1) {
    perror("bind");
    closesocket(listener);
    Wifi_Cleanup();
    return 1;
  }

  rc = ioctl(listener, FIONBIO, (char*)&yes);
  if(rc == -1) {
    perror("ioctl");
    closesocket(listener);
    Wifi_Cleanup();
    return 1;
  }

  rc = listen(listener, 5);
  if(rc == -1) {
    perror("listen");
    closesocket(listener);
    Wifi_Cleanup();
    return 1;
  }

  printf("Waiting for connection\n");
  printf("Press B to quit\n");
  do {
    s = accept(listener, (struct sockaddr*)&addr, &addrlen);
    if(s == -1 && errno != EWOULDBLOCK) {
      perror("accept");
      closesocket(listener);
      Wifi_Cleanup();
      return 1;
    }

    if(s != -1) {
      rc = ioctl(s, FIONBIO, (char*)&no);
      if(rc == -1) {
        perror("ioctl");
        closesocket(listener);
        closesocket(s);
        Wifi_Cleanup();
        return 1;
      }

      rc = process(s);
      if(rc == -1) {
        closesocket(listener);
        closesocket(s);
        Wifi_Cleanup();
        return 1;
      }
      closesocket(s);
      printf("Waiting for connection\n");
      printf("Press B to quit\n");
    }

    swiWaitForVBlank();
    scanKeys();
    down = keysDown();
  } while(!(down & KEY_B));

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
  char    data[1024];

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
  while((rc = fread(data, 1, sizeof(data), fp)) > 0)
    MD5_Update(&ctx, data, rc);
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

  if((fp = fopen((char*)msg->data, "wb")) == NULL) {
    fprintf(stderr, "fopen: '%s': %s\n", msg->data, strerror(errno));
    msg->header.rc = -1;
    msg->header.size = 0;
    return -1;
  }

  while(1) {
    rc = recvMessage(s, msg);
    if(rc <= 0) {
      if(rc == 0)
        fprintf(stderr, "Disconnected during transfer\n");
      else
        fprintf(stderr, "Error receiving data: %s\n", strerror(errno));
      fclose(fp);
      return rc;
    }
    if(msg->header.size == 0) {
      printf("update completed\n");
      fclose(fp);
      return 1;
    }
    rc = fwrite(msg->data, 1, msg->header.size, fp);
    if(rc != msg->header.size) {
      fprintf(stderr, "fwrite(%p, 1, %u, %p) returns %d\n", msg->data, msg->header.size, fp, rc);
      fprintf(stderr, "Error writing to file: %s\n", strerror(errno));
      fclose(fp);
      return -1;
    }
  }
}
