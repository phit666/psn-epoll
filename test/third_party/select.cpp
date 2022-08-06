/*@file select.cpp
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
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
# include <ws2tcpip.h>
# include <windows.h>
# include <io.h>
#define sock_t SOCKET
#else
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#define sock_t int
#endif
#include <errno.h>
#include "select.h"

struct fd_set read_set, master_set;
static int  max_sd = 0;

void initselect()
{
    FD_ZERO(&read_set);
}

void addfd(SOCKET s)
{
    FD_SET(s, &master_set);
}

int selectdispatch()
{
   timeval timeout;
   timeout.tv_sec = 0;
   timeout.tv_usec = 0;

   memcpy(&read_set, &master_set, sizeof(read_set));

   if (select(max_sd, &read_set, NULL, NULL, &timeout) == SOCKET_ERROR)
   {
       printf("select() failed, %d\n", WSAGetLastError());
       return 0;
   }

   for (int i = 0; i < (int)read_set.fd_count; ++i)
   {
       if (FD_ISSET(read_set.fd_array[i], &read_set))
       {
           readcb(read_set.fd_array[i]);
           break;
       }
   } 

   return 1;
}