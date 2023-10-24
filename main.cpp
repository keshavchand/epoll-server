#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <queue>
#include <unordered_map>
using namespace std;

#include "ioMultiplexer.cpp"
#include "server.cpp"

// using ClientArray = vector<ClientWorker*>;
using ClientMap = unordered_map<int, ClientWorker *>;

struct info {
  int processed;
};

int createServer(int port = 8080, int maxConn = SOMAXCONN) {
  int serverFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  if (serverFd == -1) {
    return serverFd;
  }

  int enable = 1;
  int err =
      setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  if (err < 0) {
    return err;
  }
  err = setsockopt(serverFd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
  if (err < 0) {
    return err;
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  err = bind(serverFd, (sockaddr *)&addr, sizeof(addr));
  if (err < 0) {
    return err;
  }

  return (listen(serverFd, maxConn) < 0) ? -1 : serverFd;
}

void AcceptClient(int serverFd, IoMux &ioMux, ClientMap &clientMap) {
  sockaddr_in client;
  unsigned int clientSockLen = sizeof(client);
  int clientSock =
      accept4(serverFd, (sockaddr *)&client, &clientSockLen, SOCK_NONBLOCK);
  if (clientSock == -1) {
    perror("Socket accept failed");
    return;
  }

  int err = AddFd(ioMux, clientSock, EPOLLIN);
  if (err == -1) {
    perror("Epoll ctl failed");
    // XXX: Should we??
    return;
  }
  clientMap[clientSock] = new ClientWorker(clientSock);
}

void IncommingData(IoMux &ioMux, ClientMap &clientMap, epoll_event event) {
  const int SIZE_DATA = 256;
  uint8_t data[SIZE_DATA] = {};
  ClientWorker *cw;
  int fd = event.data.fd;

  int i = read(fd, (void *)data, SIZE_DATA);
  // https://stackoverflow.com/questions/70905227/epoll-does-not-signal-an-event-when-socket-is-close
  // if read is zero meaning the socket is closed
  if (i == 0) {
    clientMap.erase(fd);
    RemoveFd(ioMux, fd);
    close(fd);
    return;
  }

  if (i < 0)
    return;

  cw = clientMap[fd];
  if (!cw)
    return;
  int offset = 0;
  while (i - offset > 0) {
    int shouldWrite = 0;
    offset +=
        ExtendMessage(&cw->inMessage, data + offset, i - offset, &shouldWrite);

    if (!shouldWrite)
      continue;

    // TODO: Check is the message is full
    for (auto c = clientMap.begin(); c != clientMap.end(); c++) {
      if (c->first == fd)
        continue;
      AddMessage(c->second, cw->inMessage);
    }

    cw->inMessage = Message(0, 0);
  }
}

void SendData(IoMux &ioMux, ClientMap &clientMap, epoll_event event) {
  int fd = event.data.fd;
  ClientWorker *cw = clientMap[fd];
  SendMessage(cw);
}

void *run(void *v) {
  int PORT = 8080;
  int MAX_CONN = 10;
  int MAX_EVENTS = 100;
  int TID = gettid();

  // XXX: cout isn't thread safe
  cout << "Running Thread " << TID << endl;
  int serverFd = createServer(PORT);
  if (serverFd < 0) {
    perror("Creating Server");
    return NULL;
  }

  IoMux ioMux = NewIoMultiplexer();
  int err = AddFd(ioMux, serverFd, EPOLLIN | EPOLLOUT);
  if (err == -1) {
    perror("ioMultiplexing adding Server");
    return NULL;
  }

  epoll_event events[MAX_EVENTS] = {};
  ClientMap clientMap;
  int acceptedCount = 0;

  for (;;) {
    int nfds = Wait(ioMux, events, MAX_EVENTS);
    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == serverFd) {
        cout << "Accepting" << acceptedCount++ << endl;
        AcceptClient(serverFd, ioMux, clientMap);
      } else if (events[i].events & EPOLLIN) {
        IncommingData(ioMux, clientMap, events[i]);
      } else if (events[i].events & EPOLLOUT) {
        SendData(ioMux, clientMap, events[i]);
      }
    }
  }
}

void *watch(void *v) {
  info *p = (info *)v;
  while (true) {
    sleep(3);
    for (int i = 0; i < 4; i++) {
      cout << p[i].processed << " ";
    }
    cout << endl;
  }
}

int main() {

  const int threadCount = 1;
  pthread_t threadId[threadCount] = {};
  info data[threadCount] = {};

  pthread_t watcherThreadId;
  // pthread_create(&watcherThreadId, NULL, watch, &data);
  for (int i = 0; i < threadCount; i++) {
    pthread_create(&threadId[i], NULL, run, &data[i]);
  }
  for (int i = 0; i < threadCount; i++) {
    pthread_join(threadId[i], NULL);
  }
}
