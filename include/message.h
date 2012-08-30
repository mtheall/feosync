#pragma once

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#ifndef FEOS
#define swiWaitForVBlank() ((void)0)
#endif

#ifdef WIN32
#include <winsock.h>
#define ECONNRESET WSAECONNRESET
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

typedef enum {
  MD5SUM = 0,
  UPDATE = 1,
  MKDIR  = 2,
} message_type_t;

typedef struct {
  struct {
    uint16_t size;
    uint8_t  type;
    int8_t   rc;
  } header;
  union {
    uint8_t data[1024];
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
      if(errno == ECONNRESET)
        return 0;
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
      if(errno == ECONNRESET)
        return 0;
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

  msg->header.size = ntohs(msg->header.size);
  rc = RECV(s, (char*)msg->data, msg->header.size);
  if(rc == -1)
    return rc;

  return sizeof(msg->header) + rc;
}

static inline int sendMessage(int s, message_t *msg) {
  msg->header.size = htons(msg->header.size);
  return SEND(s, (char*)&msg->header, sizeof(msg->header) + ntohs(msg->header.size));
}
