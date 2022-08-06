/*@file psn-epoll.h
 *
 * MIT License
 *
 * Copyright (c) 2022 phit666
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <winsock2.h>
#include <stdint.h>

#define EPOLLIN      SOCK_NOTIFY_REGISTER_EVENT_IN
#define EPOLLPRI     NULL
#define EPOLLOUT     SOCK_NOTIFY_REGISTER_EVENT_OUT
#define EPOLLERR     SOCK_NOTIFY_EVENT_ERR
#define EPOLLHUP     SOCK_NOTIFY_REGISTER_EVENT_HANGUP
#define EPOLLRDNORM  SOCK_NOTIFY_REGISTER_EVENT_IN
#define EPOLLRDBAND  NULL
#define EPOLLWRNORM  SOCK_NOTIFY_REGISTER_EVENT_IN | SOCK_NOTIFY_REGISTER_EVENT_OUT
#define EPOLLWRBAND  NULL
#define EPOLLRDHUP   SOCK_NOTIFY_REGISTER_EVENT_IN | SOCK_NOTIFY_REGISTER_EVENT_HANGUP
#define EPOLLONESHOT 0x08
#define EPOLLET 0x10

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_MOD 2
#define EPOLL_CTL_DEL 3

#define socket_t SOCKET

typedef union epoll_data {
	void* ptr;
	int      fd;
	uint32_t u32;
	uint64_t u64;
} epoll_data_t;

struct epoll_event {
	uint32_t     events;    /* Epoll events */
	epoll_data_t data;      /* User data variable */
};

int epoll_create(int size);
int epoll_create1(int flags); 
int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event);
int epoll_wait(int epfd, struct epoll_event* events,
	int maxevents, int timeout);
/*epoll cleanup*/
void close(int epfd);
#else
#define socket_t int
#endif

/*portable helper functions*/
int epoll_sock2fd(socket_t s);
socket_t epoll_fd2sock(int fd);
void epoll_postqueued(int epfd);
