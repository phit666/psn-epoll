# PSN-EPOLL
 EPOLL for Windows with Process Socket Notification api coded in C++, it closely resembles Linux EPOLL (funcations/variables) and even supported Edge Trigger notification. epoll_wait is thread safe so you can call it in multiple threads at the same time as worker threads.
 
# Notes
Process socket notification is a new Windows socket api so to compile it you will need to install the latest Windows SDK.
Linux fd is int type so to get an int fd value from socket use the portable function epoll_sock2fd and to get the socket from fd use epoll_fd2sock.
