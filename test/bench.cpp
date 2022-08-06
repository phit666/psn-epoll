/*@file bench.cpp
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
#include "../psn-epoll.h"
#include "third_party/socketpair.h"
#include "third_party/select.h"

#include <iostream>
#include <csignal>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <ctime>
#include <cmath>
#include <map>

#pragma comment(lib, "ws2_32.lib")

static void runbench();
static void epolldispatch();

static size_t con = 0;
static size_t writes = 0;
static size_t twrites = 0;
static size_t treads = 0;
static size_t dispatchcounts = 0;
static int ncount = 0;
static int errcount = 0;
std::chrono::time_point<std::chrono::high_resolution_clock> startick;
std::chrono::time_point<std::chrono::high_resolution_clock> endtick;
static intptr_t difftick = 0;
static char method[10] = { 0 };
static int m = 0;

struct socketpair
{
    SOCKET s1;
    SOCKET s2;
};
std::map<int, socketpair> ms;

static struct timeval ts, te;
static int epfd;

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cout << std::endl;
        std::cout << "Usage:" << std::endl;
        std::cout << "bench.exe <connections> <writes> <methods: select or psn-epoll>" << std::endl;
        std::cout << std::endl;
        system("pause");
        return -1;
    }

    sprintf_s(method, 10, "%s", argv[3]);
    con = atoi(argv[1]);
    writes = atoi(argv[2]);

    if (strcmp(method, "select") != 0 && strcmp(method, "psn-epoll") != 0) {
        std::cout << "Invalid " << method << " entered, available methods are select or psn-epoll." << std::endl;
        system("pause");
        return -1;
    }

    if (!con || !writes) {
        std::cout << "connections and writes should be positive values." << std::endl;
        system("pause");
        return -1;
    }

    if (strcmp(method, "select") == 0)
        m = 0;
    else if (strcmp(method, "psn-epoll") == 0)
        m = 1;

    std::cout << "<<<" << method << " method benchmark >>>" << std::endl;

#ifdef _WIN32
    WSADATA WSAData;
    WSAStartup(0x0202, &WSAData);
#endif

    if (m == 1) {
        epfd = epoll_create1(0);
    }
    else {
        initselect();
    }

    for (int n = 0; n < con; n++) {

        SOCKET s[2];
        if (int err = dumb_socketpair(s, 0) != 0) {
            printf("socketpair failed, connections:%d err:%d %d.\n", n + 1, err, WSAGetLastError());
            break;
        }
        socketpair spair;
        spair.s1 = s[0];
        spair.s2 = s[1];

        ms.insert(std::pair<int, socketpair>(n, spair));

        if (m == 1) {
            epoll_event _event = {};
            _event.events = EPOLLIN;
            _event.data.fd = epoll_sock2fd(s[0]);

            if (epoll_ctl(epfd, EPOLL_CTL_ADD, _event.data.fd, &_event) == -1) {
                printf("epoll_ctl, failed to add fd %d errno:%d", _event.data.fd, errno);
                break;
            }
        }
        else {
            addfd(s[0]);
        }
    }

    size_t average = 0;

    for (int n = 0; n < 10; n++) {
        runbench();
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(endtick - startick).count();
        average += dur;
        printf("Writes/Read:%lld/%lld Dispatch:%lld Error:%d Result:%lld usec.\n", twrites, treads, dispatchcounts, errcount, dur);
    }

    printf("Average Result:%lld usec.\n", average / 10);

    std::map <int, socketpair>::iterator iter;
    for (iter = ms.begin(); iter != ms.end(); iter++) {
        closesocket(iter->second.s1);
        closesocket(iter->second.s2);
    }

    if (m == 1) {
        close(epfd);
    }
    ms.clear();
#ifdef _WIN32
    WSACleanup();
#endif
}

static void epolldispatch() {
    epoll_event _event[10];
    int fds = epoll_wait(epfd, _event, 10, 0);
    if (fds > 0) {
        for (int n = 0; n < fds; n++) {
            if (_event[n].events & EPOLLIN) {
                SOCKET s = epoll_fd2sock(_event[n].data.fd);
                readcb(s);
            }
        }
    }
}

static void runbench() {

    twrites = treads = dispatchcounts = errcount = ncount = 0;

    if (m == 1) {
        epolldispatch();
    }
    else
        selectdispatch();

    if (send(ms[ncount].s2, ".", 1, 0) > 0) {
        twrites += 1;
    }
    else
        return;

    startick = std::chrono::high_resolution_clock::now();

    for(; treads != twrites; ++dispatchcounts){
        if (m == 1) {
            epolldispatch();
        }
        else {
            if (!selectdispatch())
                break;
        }
    }

    endtick = std::chrono::high_resolution_clock::now();
}


void readcb(SOCKET s)
{
    char rbuf[1];

    int len = recv(s, rbuf, 1, 0);

    if (!len)
        return;

    treads += len;

    if (twrites >= writes)
        return;

    ncount++;

    if (ncount >= con) {
        ncount = 0;
    }

    if (send(ms[ncount].s2, ".", 1, 0) == SOCKET_ERROR) {
        errcount++;
    }

    twrites += 1;
}

