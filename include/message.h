#pragma once

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef FEOS
#include <md5.h>
#else
#define swiWaitForVBlank() ((void)0)
#include <openssl/md5.h>
#endif

typedef enum {
  MD5SUM,
  UPDATE,
  MKDIR,
} message_type_t;

typedef struct {
  struct {
    uint32_t size;
    uint8_t  type;
    int8_t   rc;
  } header;
  union {
    int8_t  data[1024];
    uint8_t hash[16];
  };
} message_t;

static inline int RECV(int s, char *buf, size_t size) {
  int rc;
  int toRecv;
  int recvd = 0;

  while(recvd < size) {
    toRecv = size - recvd;
    if(toRecv > 1024)
      toRecv = 1024;

    rc = recv(s, &buf[recvd], toRecv, 0);
    if(rc == -1) {
      fprintf(stderr, "recv: %s\n", strerror(errno));
      return -1;
    }
    else if(rc == 0)
      return 0;
    else
      recvd += rc;
    if(recvd != size)
      swiWaitForVBlank();
  }

  return recvd;
}

static inline int SEND(int s, char *buf, size_t size) {
  int rc;
  int toSend;
  int sent = 0;

  while(sent < size) {
    toSend = size - sent;
    if(toSend > 1024)
      toSend = 1024;

    rc = send(s, &buf[sent], toSend, 0);
    if(rc == -1) {
      fprintf(stderr, "send: %s\n", strerror(errno));
      return -1;
    }
    else if(rc == 0)
      return 0;
    else
      sent += rc;
    if(sent != size)
      swiWaitForVBlank();
  }

  return sent;
}

static inline int recvMessage(int s, message_t *msg) {
  int rc;

  rc = RECV(s, (char*)&msg->header, sizeof(msg->header));
  if(rc != sizeof(msg->header))
    return rc;

  rc = RECV(s, (char*)msg->data, msg->header.size);
  if(rc == -1)
    return rc;

  return sizeof(msg->header) + rc;
}

static inline int sendMessage(int s, message_t *msg) {
  return SEND(s, (char*)&msg->header, sizeof(msg->header) + msg->header.size);
}

static inline int md5sum(unsigned char *digest, const char *filename) {
  FILE *fp;
  MD5_CTX ctx;
  int rc;
  static unsigned char data[1024];

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
    rc = fread(data, 1, sizeof(data), fp);
    if(rc > 0)
      MD5_Update(&ctx, data, rc);
  } while(rc == sizeof(data));

  MD5_Final(digest, &ctx);

  rc = 0;
  if(ferror(fp))
    rc = -1;

  fclose(fp);
  return rc;
}
