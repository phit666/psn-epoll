/*@file psn-epoll.cpp
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
#include <map>
#include <mutex>
#include "psn-epoll.h"
#include <errno.h>

#define EPOLL_MAX_FD 2000000

static std::recursive_mutex m1;

#ifdef _WIN32
static std::map<int, HANDLE> mfd2hwnd;
static std::map<int, socket_t> mfd2sock;
static std::map<socket_t, int> msock2fd;
static std::map<int, epoll_event*> mevents;
static int epfdctr = 0;
static int fdctr = 0;
#endif

static int closed = 0;

int epoll_sock2fd(socket_t s) {
#ifdef _WIN32
    std::map<socket_t, int>::iterator iter;
    std::map<int, socket_t>::iterator iter2;

    std::lock_guard<std::recursive_mutex> lock1(m1);
    
    iter = msock2fd.find(s);
    if (iter != msock2fd.end())
        return iter->second;
   
    for (;;) {
        ++fdctr;
        if (fdctr > EPOLL_MAX_FD)
            fdctr = 1;
        iter2 = mfd2sock.find(fdctr);
        if (iter2 == mfd2sock.end())
            break;
    }

    msock2fd.insert(std::pair<socket_t, int>(s, fdctr));
    mfd2sock.insert(std::pair<int, socket_t>(fdctr, s));

    return fdctr;
#else
    return s;
#endif
}

socket_t epoll_fd2sock(int fd) {
#ifdef _WIN32
    std::map<int, socket_t>::iterator iter;
    std::lock_guard<std::recursive_mutex> lock1(m1);
    iter = mfd2sock.find(fd);
    if (iter != mfd2sock.end())
        return iter->second;
    return INVALID_SOCKET;
#else
    return fd;
#endif
}

void epoll_postqueued(int epfd) {
#ifdef _WIN32
    std::lock_guard<std::recursive_mutex> lock1(m1);
    closed = 1;
    PostQueuedCompletionStatus(mfd2hwnd[epfd], 0, 0, NULL);
#endif
}

#ifdef _WIN32

static void _delefd(int fd) {
    std::map<int, epoll_event*>::iterator iter;
    iter = mevents.find(fd);
    if (iter != mevents.end()) {
        free(iter->second);
        mevents.erase(iter);
    }
}

static int _existefd(int fd) {
    std::map<int, epoll_event*>::iterator iter;
    iter = mevents.find(fd);
    if (iter != mevents.end())
        return 1;
    return 0;
}

static int _existepfd(int epfd) {
    std::map<int, HANDLE>::iterator iter;
    iter = mfd2hwnd.find(epfd);
    if (iter != mfd2hwnd.end())
        return 1;
    return 0;
}

void close(int epfd) {
    std::lock_guard<std::recursive_mutex> lock1(m1);
    if (mfd2hwnd[epfd] == NULL)
        return;
    CloseHandle(mfd2hwnd[epfd]);
    mfd2hwnd.clear();
    mfd2sock.clear();
    msock2fd.clear();
    closed = 1;
    std::map<int, epoll_event*>::iterator iter;
    for (iter = mevents.begin(); iter != mevents.end(); iter++) {
        free(iter->second);
    }
    mevents.clear();
}


int epoll_create(int size) {
	if (!size)
		return -1;

	HANDLE phwnd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    std::lock_guard<std::recursive_mutex> lock1(m1);
    mfd2hwnd.insert(std::pair<int, HANDLE>(++epfdctr, phwnd));
    return epfdctr;
}

int epoll_create1(int flags) {
	return epoll_create(1);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) {

    DWORD errorCode;
    u_long nonBlocking = 1;
    SOCK_NOTIFY_REGISTRATION registration = {};
    SOCKET s = epoll_fd2sock(fd);
    epoll_event* pevent = NULL;
    intptr_t _fd = static_cast<intptr_t>(fd);

    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }

    if (!_existepfd(epfd) || (event == NULL && EPOLL_CTL_DEL != op)) {
        errno = EINVAL;
        return -1;
    }

    switch (op) {

    case EPOLL_CTL_DEL:
        if (!_existefd(fd)) {
            errno = ENOENT;
            return -1;
        }
        _delefd(fd);
        op = SOCK_NOTIFY_OP_REMOVE;
        break;

    case EPOLL_CTL_MOD:
        if (!_existefd(fd)) {
            errno = ENOENT;
            return -1;
        }
        pevent = mevents[fd];
        if (pevent == NULL) {
            errno = EINVAL;
            return -1;
        }
        if (event != NULL) {
            ::memcpy(pevent, event, sizeof(epoll_event));
        }
        op = SOCK_NOTIFY_OP_ENABLE;
        break;

    case EPOLL_CTL_ADD:
        if (_existefd(fd)) {
            errno = EEXIST;
            return -1;
        }
        pevent = (epoll_event*)calloc(1, sizeof(epoll_event));
        if (pevent == NULL) {
            errno = ENOMEM;
            return -1;
        }
        if (event != NULL) {
            ::memcpy(pevent, event, sizeof(epoll_event));
        }
        mevents.insert(std::pair<int, epoll_event*>(fd, pevent));
        op = SOCK_NOTIFY_OP_ENABLE;
        break;

    default:
        errno = EINVAL;
        return -1;
    }

    if (op != SOCK_NOTIFY_OP_REMOVE) {
        registration.triggerFlags = SOCK_NOTIFY_TRIGGER_PERSISTENT;
        if (pevent->events & EPOLLET) {
            registration.triggerFlags |= SOCK_NOTIFY_TRIGGER_EDGE;
            pevent->events ^= EPOLLET;
        }
        else {
            registration.triggerFlags |= SOCK_NOTIFY_TRIGGER_LEVEL;
        }
        if (pevent->events & EPOLLONESHOT) {
            registration.triggerFlags |= SOCK_NOTIFY_TRIGGER_ONESHOT;
            registration.triggerFlags ^= SOCK_NOTIFY_TRIGGER_PERSISTENT;
            pevent->events ^= EPOLLONESHOT;
        }
        registration.eventFilter = pevent->events;
    }

    registration.completionKey = (void*)_fd;
    registration.operation = op;
    registration.socket = s;

    errorCode = ProcessSocketNotifications(mfd2hwnd[epfd], 1, &registration, 0, 0, NULL, NULL);

    if (errorCode != ERROR_SUCCESS) {
        errno = EINVAL;
        return -1;
    }

    if (registration.registrationResult != ERROR_SUCCESS) {
        errorCode = registration.registrationResult;
        errno = EINVAL;
        return -1;
    }

    /*this is a work around with the lack of notification for send/write when set to trigger_edge*/
    if (op == SOCK_NOTIFY_OP_ENABLE && registration.triggerFlags & SOCK_NOTIFY_TRIGGER_EDGE) {
        if (pevent->events & EPOLLOUT) {
            pevent->data.u32 = EPOLLOUT | EPOLLET;
            PostQueuedCompletionStatus(mfd2hwnd[epfd], 0, _fd, NULL);
        }
    }

    return 0;
}

int epoll_wait(int epfd, struct epoll_event* events,
	int maxevents, int timeout) {

    intptr_t pfd;
    int fd;
    uint32_t notificationCount;
    uint32_t psnevents;
    uint32_t errorCode;
    OVERLAPPED_ENTRY* notification = NULL;
    epoll_event* pevent = NULL;

    if (!_existepfd(epfd)) {
        errno = EINVAL;
        return -1;
    }

    if (closed != 0) {
        return 0;
    }

    if (events == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (maxevents < 1) {
        errno = EINVAL;
        return -1;
    }

    notification = (OVERLAPPED_ENTRY*)calloc(maxevents, sizeof(OVERLAPPED_ENTRY));

    if (notification == NULL) {
        errno = ENOMEM;
        return -1;
    }

    errorCode = ProcessSocketNotifications(mfd2hwnd[epfd], 0, NULL, timeout, maxevents, notification, &notificationCount);

    if (errorCode != ERROR_SUCCESS) {
        if (GetLastError() == WAIT_TIMEOUT)
            return 0;
        errno = EINVAL;
        free(notification);
        return -1;
    }

    std::lock_guard<std::recursive_mutex> lock1(m1);

    if (closed != 0) {
        free(notification);
        return 0;
    }

    int i = 0;

    for (int n = 0; n < notificationCount; n++) {

        psnevents = SocketNotificationRetrieveEvents(&notification[n]);

        if (psnevents & SOCK_NOTIFY_EVENT_REMOVE) {
            continue;
        }

        pfd = (intptr_t)notification[n].lpCompletionKey;

        if (pfd == 0) {
            continue;
        }

        fd = static_cast<int>(pfd);

        if (!_existefd(fd)) {
            continue;
        }

        pevent = mevents[fd];

        if (pevent == NULL)
            continue;

        events[i].events = (psnevents == 0) ? pevent->data.u32 : psnevents;
        events[i].data.u32 = pevent->data.u32;
        events[i].data.u64 = pevent->data.u64;
        events[i].data.ptr = pevent->data.ptr;
        events[i++].data.fd = fd;
    }

    free(notification);
    return i;
}

#endif

