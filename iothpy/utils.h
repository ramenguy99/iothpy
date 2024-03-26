#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h>

/* Utility to create a tuple representing the given sockaddr suitable
   for passing it back to bind, connect etc.. */
PyObject* make_sockaddr(struct sockaddr *addr, size_t addrlen);

/* Convert IPv4 sockaddr to a Python str. */
PyObject* make_ipv4_addr(struct sockaddr_in *addr);

/* Convert IPv6 sockaddr to a Python str. */
PyObject* make_ipv6_addr(struct sockaddr_in6 *addr);