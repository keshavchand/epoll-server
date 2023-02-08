#pragma once

struct IoMux {
	int epfd;
	int eventSize;

};

IoMux NewIoMultiplexer() {
	IoMux m = {};
	m.epfd = epoll_create1(0);
	return m;
}

int RemoveFd(IoMux& mux, int fd) {
	return epoll_ctl(mux.epfd, EPOLL_CTL_DEL, fd, NULL);
}

int AddFd(IoMux& mux, int fd, int events) {
	epoll_event ev = {};
	ev.events = events;
	ev.data.fd = fd;
	return epoll_ctl(mux.epfd, EPOLL_CTL_ADD, fd, &ev);
}

int Wait(IoMux& mux, epoll_event *e, int size, int timeout = -1) {
	return epoll_wait(mux.epfd, e, size, timeout);
}
