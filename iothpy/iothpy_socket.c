/* 
 * This file is part of the iothpy library: python support for ioth.
 * 
 * Copyright (c) 2020-2024   Dario Mylonopoulos
 *                           Lorenzo Liso
 *                           Francesco Testa
 * Virtualsquare team.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
/*
    Part of the contents of this file comes from the cpython codebase 
    and its use is limited by the PYTHON SOFTWARE FOUNDATION LICENSE.
    For more information see https://github.com/python/cpython/blob/master/LICENSE
*/

#include "utils.h"
#include "iothpy_stack.h"
#include "iothpy_socket.h"

//PyMemberDef
#include <structmember.h>

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

#include <ioth.h>

_PyTime_t defaulttimeout = _PYTIME_FROMSECONDS(-1);

/* 
   Parse a timeout object into a _PyTime_t, raise an exception and return -1 if
   not a valid timeout object
*/
int socket_parse_timeout(_PyTime_t *timeout, PyObject *timeout_obj)
{

    _PyTime_t ms;
    int overflow = 0;

    if (timeout_obj == Py_None) {
        *timeout = _PyTime_FromSeconds(-1);
        return 0;
    }

    if (_PyTime_FromSecondsObject(timeout, timeout_obj, _PyTime_ROUND_TIMEOUT) < 0)
        return -1;

    if (*timeout < 0) {
        PyErr_SetString(PyExc_ValueError, "Timeout value out of range");
        return -1;
    }

    ms = _PyTime_AsMilliseconds(*timeout, _PyTime_ROUND_TIMEOUT);
    overflow |= (ms > INT_MAX);
    if (overflow) {
        PyErr_SetString(PyExc_OverflowError, "timeout doesn't fit into C timeval");
        return -1;
    }

    return 0;
}


/* Utility to get a sockaddr from a tuple argument passed to a python function.
   addr must be a pointer to an allocated sockaddr struct of the proper size for the 
   family of the socket. Returns 0 on invalid arguments */
static int
get_sockaddr_from_tuple(char* func_name, socket_object* s, PyObject* args, struct sockaddr* sockaddr, socklen_t* len)
{
    char* ip_addr_string;
    int port;

    if (!PyTuple_Check(args)) 
    {
        PyErr_Format(PyExc_TypeError, "%s(): argument must be tuple (host, port) not %.500s", func_name, Py_TYPE(args)->tp_name);
        return 0;
    }

    if (!PyArg_ParseTuple(args, "si;AF_INET address must be a pair (host, port)",
                          &ip_addr_string, &port))
    {
        if (PyErr_ExceptionMatches(PyExc_OverflowError)) 
        {
            PyErr_Format(PyExc_OverflowError, "%s(): port must be 0-65535", func_name);
        }
        return 0;
    }

    if (port < 0 || port > 0xffff) {
        PyErr_Format(PyExc_OverflowError, "%s(): port must be 0-65535", func_name);
        return 0;
    }

    // const char* address;
    switch (s->family) {
        case AF_INET:
        {
            struct sockaddr_in* addr = (struct sockaddr_in*)sockaddr;
            if(len)
                *len = sizeof(*addr);

            addr->sin_family = AF_INET;
            addr->sin_port = htons(port);

            /* Special case empty string to INADDR_ANY */
            if(ip_addr_string[0] == '\0') 
            {
                addr->sin_addr.s_addr = htonl(INADDR_ANY);
            }
            /* Special case <broadcast> string to INADDR_BROADCAST */
            else if(strcmp(ip_addr_string, "<broadcast>") == 0)
            {
                addr->sin_addr.s_addr = htonl(INADDR_BROADCAST);
            }
            else 
            {
                if(inet_pton(AF_INET, ip_addr_string, &addr->sin_addr) != 1) 
                {
                    PyErr_SetString(PyExc_ValueError, "invalid ip address");
                    return 0;
                }
            }
        } break;

        case AF_INET6:
        {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)sockaddr;
            if(len)
                *len = sizeof(*addr);

            addr->sin6_family = AF_INET6;
            addr->sin6_port = htons(port);

            /* Special case empty string to INADDR_ANY */
            if(ip_addr_string[0] == '\0') 
            {
                addr->sin6_addr = in6addr_any;
            }
            else 
            {
                if(inet_pton(AF_INET6, ip_addr_string, &addr->sin6_addr) != 1) 
                {
                    PyErr_SetString(PyExc_ValueError, "invalid ip address");
                    return 0;
                }
            }
        } break;

        default:
        {
            PyErr_SetString(PyExc_ValueError, "invalid socket family");
            return 0;
        } break;
    }

    return 1;
}


/* 
    Return the length of an IPv4 or IPv6 address based on the socket family, 
    raise an exception if the socket family is not valid
*/
static int
getsockaddrlen(socket_object *s, socklen_t *len_ret)
{
    switch(s->family){
        case AF_INET:
        {
            *len_ret = sizeof (struct sockaddr_in);
            return 1;
        }
        case AF_INET6:
        {
            *len_ret = sizeof (struct sockaddr_in6);
            return 1;
        }
        default:
            PyErr_SetString(PyExc_OSError, "getsockaddrlen: bad family");
            return 0;
    }
}

//Berkley Socket methods
#define CHECK_ERRNO(expected) (errno == expected)
#define GET_SOCK_ERROR errno
#define SET_SOCK_ERROR(e) do { errno = e; } while(0)
#define SOCK_TIMEOUT_ERR EWOULDBLOCK
#define SOCK_INPROGRESS_ERR EINPROGRESS

PyObject *socket_timeout;

/* Poll on a socket object */
static int
internal_select(socket_object *s, int writing, _PyTime_t interval, int connect)
{
    int n;
    struct pollfd pollfd;
    _PyTime_t ms;

    /* must be called with the GIL held */
    assert(PyGILState_Check());

    /* Error condition is for output only */
    assert(!(connect && !writing));

    /* Guard against closed socket */
    if (s->fd == -1)
        return 0;

    /* Prefer poll, if available, since you can poll() any fd
     * which can't be done with select(). */
    pollfd.fd = s->fd;
    pollfd.events = writing ? POLLOUT : POLLIN;
    if (connect) {
        /* On Windows, the socket becomes writable on connection success,
           but a connection failure is notified as an error. On POSIX, the
           socket becomes writable on connection success or on connection
           failure. */
        pollfd.events |= POLLERR;
    }

    /* s->sock_timeout is in seconds, timeout in ms */
    ms = _PyTime_AsMilliseconds(interval, _PyTime_ROUND_CEILING);
    assert(ms <= INT_MAX);

    /* On some OSes, typically BSD-based ones, the timeout parameter of the
       poll() syscall, when negative, must be exactly INFTIM, where defined,
       or -1. See issue 37811. */
    if (ms < 0) {
#ifdef INFTIM
        ms = INFTIM;
#else
        ms = -1;
#endif
    }

    Py_BEGIN_ALLOW_THREADS;
    n = poll(&pollfd, 1, (int)ms);
    Py_END_ALLOW_THREADS;

    if (n < 0)
        return -1;
    if (n == 0)
        return 1;
    return 0;
}


/* Utility function to call blocking methods on a socket */
static int
sock_call(socket_object *s,
             int writing,
             int (*sock_func) (socket_object* s, void *data),
             void *data,
             int connect,
             int *err,
             _PyTime_t timeout)
{
    int has_timeout = (timeout > 0);
    _PyTime_t deadline = 0;
    int deadline_initialized = 0;
    int res;

    /* sock_call() must be called with the GIL held. */
    assert(PyGILState_Check());

    /* outer loop to retry select() when select() is interrupted by a signal
       or to retry select()+sock_func() on false positive (see above) */
    while (1) {
        /* For connect(), poll even for blocking socket. The connection
           runs asynchronously. */
        if (has_timeout || connect) {
            if (has_timeout) {
                _PyTime_t interval;

                if (deadline_initialized) {
                    /* recompute the timeout */
                    interval = deadline - _PyTime_GetMonotonicClock();
                }
                else {
                    deadline_initialized = 1;
                    deadline = _PyTime_GetMonotonicClock() + timeout;
                    interval = timeout;
                }

                if (interval >= 0)
                    res = internal_select(s, writing, interval, connect);
                else
                    res = 1;
            }
            else {
                res = internal_select(s, writing, timeout, connect);
            }

            if (res == -1) {
                if (err)
                    *err = GET_SOCK_ERROR;

                if (CHECK_ERRNO(EINTR)) {
                    /* select() was interrupted by a signal */
                    if (PyErr_CheckSignals()) {
                        if (err)
                            *err = -1;
                        return -1;
                    }

                    /* retry select() */
                    continue;
                }

                /* select() failed */
                PyErr_SetFromErrno(PyExc_OSError);
                return -1;
            }

            if (res == 1) {
                if (err)
                    *err = SOCK_TIMEOUT_ERR;
                else
                    PyErr_SetString(socket_timeout, "timed out");
                return -1;
            }

            /* the socket is ready */
        }

        /* inner loop to retry sock_func() when sock_func() is interrupted
           by a signal */
        while (1) {
            Py_BEGIN_ALLOW_THREADS
            res = sock_func(s, data);
            Py_END_ALLOW_THREADS

            if (res) {
                /* sock_func() succeeded */
                if (err)
                    *err = 0;
                return 0;
            }

            if (err)
                *err = GET_SOCK_ERROR;

            if (!CHECK_ERRNO(EINTR))
                break;

            /* sock_func() was interrupted by a signal */
            if (PyErr_CheckSignals()) {
                if (err)
                    *err = -1;
                return -1;
            }

            /* retry sock_func() */
        }

        if (s->sock_timeout > 0
            && (CHECK_ERRNO(EWOULDBLOCK) || CHECK_ERRNO(EAGAIN))) {
            /* False positive: sock_func() failed with EWOULDBLOCK or EAGAIN.
               For example, select() could indicate a socket is ready for
               reading, but the data then discarded by the OS because of a
               wrong checksum.
               Loop on select() to recheck for socket readyness. */
            continue;
        }

        /* sock_func() failed */
        if (!err)
            PyErr_SetFromErrno(PyExc_OSError);
        /* else: err was already set before */
        return -1;
    }
}


static PyObject *
sock_bind(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    struct sockaddr_storage addrbuf;
    socklen_t addrlen;
    if(!get_sockaddr_from_tuple("bind", s, args, (struct sockaddr*)&addrbuf, &addrlen))
    {
        return NULL;
    }

    int res;
    Py_BEGIN_ALLOW_THREADS
    res = ioth_bind(s->fd, (struct sockaddr*)&addrbuf, addrlen);
    Py_END_ALLOW_THREADS

    if(res != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return 0;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(bind_doc,
"bind(address)\n\
\n\
Bind the socket to a local address.  For IP sockets, the address is a\n\
pair (host, port); the host must refer to the local host. For raw packet\n\
sockets the address is a tuple (ifname, proto [,pkttype [,hatype [,addr]]])");

static PyObject *
sock_listen(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    int backlog = Py_MIN(SOMAXCONN, 128);
    int res;

    if (!PyArg_ParseTuple(args, "|i:listen", &backlog))
        return NULL;

    if (backlog < 0)
        backlog = 0;


    Py_BEGIN_ALLOW_THREADS
    res = ioth_listen(s->fd, backlog);
    Py_END_ALLOW_THREADS

    if(res != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return 0;
    }


        Py_RETURN_NONE;
    }

    PyDoc_STRVAR(listen_doc,
    "listen([backlog])\n\
    \n\
    Enable a server to accept connections.  If backlog is specified, it must be\n\
    at least 0 (if it is lower, it is set to 0); it specifies the number of\n\
    unaccepted connections that the system will allow before refusing new\n\
    connections. If not specified, a default reasonable value is chosen.");

    static PyObject* 
    new_socket_from_fd(stack_object* stack, int family, int type, int proto, int fd)
    {
        PyObject* socket_args = Py_BuildValue("Oiiii", (PyObject*)stack, family, type, proto, fd);
        if(!socket_args) {
            return NULL;
        }

        // Instantiate a socket by calling the constructor of the socket type
        PyObject* socket = PyObject_CallObject((PyObject*)&socket_type, socket_args);

        // Release arguments
        Py_DECREF(socket_args);

        return socket;
    }


    struct sock_accept_ctx {
        socklen_t* addrlen;
        struct sockaddr* addrbuf;
        int result;
    };

    static int
    sock_accept_impl(socket_object* s, void *data)
    {
        struct sock_accept_ctx* ctx = data;
        struct sockaddr* paddrbuf = ctx->addrbuf;
        socklen_t *paddrlen = ctx->addrlen;

        ctx->result = ioth_accept(s->fd, paddrbuf, paddrlen);
    }

    static PyObject*
    sock_accept(PyObject* self, PyObject* unused_args)
    {
        socket_object* s = (socket_object*)self;


        struct sockaddr_storage addrbuf;
        socklen_t addrlen = sizeof(struct sockaddr_storage);

        struct sock_accept_ctx ctx;
        ctx.addrlen = &addrlen;
        ctx.addrbuf = (struct sockaddr*)&addrbuf;

        if(sock_call(s, 0, sock_accept_impl, &ctx, 0, NULL, s->sock_timeout) < 0) {
            return NULL;
        }

        int connfd = ctx.result;

        if(connfd == -1) {
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }

        PyObject* sock = PyLong_FromLong(connfd);
        if (sock == NULL) {
            ioth_close(connfd);
            return NULL;
        }

        PyObject* addr = make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
        if(!addr) {
            ioth_close(connfd);
            Py_XDECREF(sock);
            return NULL;
        }

        PyObject* res = PyTuple_Pack(2, sock, addr);

        Py_XDECREF(sock);
        Py_XDECREF(addr);

        return res;
    }

    PyDoc_STRVAR(accept_doc,
    "_accept() -> (integer, address info)\n\
    \n\
    Wait for an incoming connection.  Return a new socket file descriptor\n\
    representing the connection, and the address of the client.\n\
    For IP sockets, the address info is a pair (hostaddr, port).");


    struct sock_recv {
        char *cbuf;
        Py_ssize_t len;
        int flags;
        Py_ssize_t result;
    };

    static int
    sock_recv_impl(socket_object* s, void *data)
    {
        struct sock_recv *ctx = data;

        ctx->result = ioth_recv(s->fd, ctx->cbuf, ctx->len, ctx->flags);
        return ctx->result >= 0;
    }


    /*
     * This is the guts of the recv() and recv_into() methods, which reads into a
     * char buffer.  If you have any inc/dec ref to do to the objects that contain
     * the buffer, do it in the caller.  This function returns the number of bytes
     * successfully read.  If there was an error, it returns -1.  Note that it is
     * also possible that we return a number of bytes smaller than the request
     * bytes.
     */

    static Py_ssize_t
    sock_recv_guts(socket_object* s, char* cbuf, Py_ssize_t len, int flags)
    {
        struct sock_recv ctx;

        if (len == 0) {
            /* If 0 bytes were requested, do nothing. */
            return 0;
        }

        ctx.cbuf = cbuf;
        ctx.len = len;
        ctx.flags = flags;
        if (sock_call(s, 0, sock_recv_impl, &ctx, 0, NULL, s->sock_timeout) < 0)
            return -1;

        return ctx.result;
    }

    static PyObject *
    sock_recv(PyObject *self, PyObject *args)
    {
        socket_object* s = (socket_object*)self;

        ssize_t recvlen = 0;
        ssize_t outlen = 0;
        int flags = 0;

        if(!PyArg_ParseTuple(args, "n|i", &recvlen, &flags))
            return NULL;

        PyObject *buf = PyBytes_FromStringAndSize(NULL, recvlen);
        if(buf == NULL) {
            return NULL;
        }

        /* Call the guts */
        outlen = sock_recv_guts(s, PyBytes_AS_STRING(buf), recvlen, flags);

        if(outlen < 0) {
            PyErr_SetString(PyExc_Exception, "failed to read from socket");
            return NULL;
        }

        if(recvlen != outlen) {
            //Resize the buffer since we read less bytes than expected
            _PyBytes_Resize(&buf, outlen);
        }

        return buf;
    }

    PyDoc_STRVAR(recv_doc,
    "recv(buffersize[, flags]) -> data\n\
    \n\
    Receive up to buffersize bytes from the socket.  For the optional flags\n\
    argument, see the Unix manual.  When no data is available, block until\n\
    at least one byte is available or until the remote end is closed.  When\n\
    the remote end is closed and all data is read, return the empty string.");



    static PyObject*
    sock_recv_into(PyObject* self, PyObject *args, PyObject *kwds)
    {
        socket_object* s = (socket_object*)self;

        static char *kwlist[] = {"buffer", "nbytes", "flags", 0};

        int flags = 0;
        Py_buffer pbuf;
        char *buf;
        Py_ssize_t buflen, readlen, recvlen = 0;

        /* Get the buffer's memory */
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "w*|ni:recv_into", kwlist,
                                         &pbuf, &recvlen, &flags))
            return NULL;
        buf = pbuf.buf;
        buflen = pbuf.len;

        if (recvlen < 0) {
            PyBuffer_Release(&pbuf);
            PyErr_SetString(PyExc_ValueError, "negative buffersize in recv_into");
            return NULL;
        }
        if (recvlen == 0) {
            /* If nbytes was not specified, use the buffer's length */
            recvlen = buflen;
        }

        /* Check if the buffer is large enough */
        if (buflen < recvlen) {
            PyBuffer_Release(&pbuf);
            PyErr_SetString(PyExc_ValueError, "buffer too small for requested bytes");
            return NULL;
        }

        /* Call the guts */
        readlen = sock_recv_guts(s, buf, recvlen, flags);
        if (readlen < 0) {
            /* Return an error. */
            PyBuffer_Release(&pbuf);
            return NULL;
        }

        PyBuffer_Release(&pbuf);
        /* Return the number of bytes read.  Note that we do not do anything
           special here in the case that readlen < recvlen. */
        return PyLong_FromSsize_t(readlen);
    }

    PyDoc_STRVAR(recv_into_doc,
    "recv_into(buffer, [nbytes[, flags]]) -> nbytes_read\n\
    \n\
    A version of recv() that stores its data into a buffer rather than creating\n\
    a new string.  Receive up to buffersize bytes from the socket.  If buffersize\n\
    is not specified (or 0), receive up to the size available in the given buffer.\n\
    \n\
    See recv() for documentation about the flags.");



    struct sock_recvfrom_ctx {
        char* cbuf;
        Py_ssize_t len;
        int flags;
        socklen_t *addrlen;
        struct sockaddr* addrbuf;
        Py_ssize_t result;
    };

    static int
    sock_recvfrom_impl(socket_object* s, void *data)
    {
        struct sock_recvfrom_ctx *ctx = data;

        memset(ctx->addrbuf, 0, *ctx->addrlen);

        ctx->result = ioth_recvfrom(s->fd, ctx->cbuf, ctx->len, ctx->flags, ctx->addrbuf, ctx->addrlen);
        return ctx->result >= 0;
    }

    /*
     * This is the guts of the recvfrom() and recvfrom_into() methods, which reads
     * into a char buffer.  If you have any inc/def ref to do to the objects that
     * contain the buffer, do it in the caller.  This function returns the number
     * of bytes successfully read.  If there was an error, it returns -1.  Note
     * that it is also possible that we return a number of bytes smaller than the
     * request bytes.
     *
     * 'addr' is a return value for the address object.  Note that you must decref
     * it yourself.
     */
    static Py_ssize_t
    sock_recvfrom_guts(socket_object* s, char* cbuf, Py_ssize_t len, int flags,
                       PyObject** addr)
    {
        struct sockaddr_storage addrbuf;
        socklen_t addrlen;
        struct sock_recvfrom_ctx ctx;

        *addr = NULL;

        if (!getsockaddrlen(s, &addrlen))
            return -1;

        ctx.cbuf = cbuf;
        ctx.len = len;
        ctx.flags = flags;
        ctx.addrbuf = (struct sockaddr*)&addrbuf;
        ctx.addrlen = &addrlen;
        if (sock_call(s, 0, sock_recvfrom_impl, &ctx, 0, NULL, s->sock_timeout) < 0)
            return -1;

        *addr = make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
        if (*addr == NULL)
            return -1;

        return ctx.result;
    }

    static PyObject*
    sock_recvfrom(PyObject *self, PyObject *args){
        socket_object* s = (socket_object*)self;

        PyObject *ret = NULL;
        int flags = 0;
        ssize_t recvlen, outlen;

        if (!PyArg_ParseTuple(args, "n|i:recvfrom", &recvlen, &flags)){
            return NULL;
        }

        if (recvlen < 0) {
            PyErr_SetString(PyExc_ValueError,
                            "negative buffersize in recvfrom");
            return NULL;
        }
        
        PyObject *buf = PyBytes_FromStringAndSize(NULL, recvlen);
        if (buf == NULL){
            return NULL;
        }

        PyObject* addr = NULL;
        outlen = sock_recvfrom_guts(s, PyBytes_AS_STRING(buf), recvlen, flags, &addr);

        if(outlen < 0) {
            goto finally;
        }
        
        if (outlen != recvlen) {
            /* We did not read as many bytes as we anticipated, resize the
               string if possible and be successful. */
            if (_PyBytes_Resize(&buf, outlen) < 0)
                /* Oopsy, not so successful after all. */
                goto finally;
        }


        ret = PyTuple_Pack(2, buf, addr);

    finally:
        Py_XDECREF(buf);
        Py_XDECREF(addr);
        return ret;
    }

    PyDoc_STRVAR(recvfrom_doc,
    "recvfrom(buffersize[, flags]) -> (data, address info)\n\
    \n\
    Like recv(buffersize, flags) but also return the sender's address info.");


    /* s.recvfrom_into(buffer[, nbytes [,flags]]) method */

    static PyObject *
    sock_recvfrom_into(PyObject* self, PyObject *args, PyObject* kwds)
    {
        socket_object* s = (socket_object*)self;

        static char *kwlist[] = {"buffer", "nbytes", "flags", 0};

        int flags = 0;
        Py_buffer pbuf;
        char *buf;
        Py_ssize_t readlen, buflen, recvlen = 0;

        PyObject *addr = NULL;

        if (!PyArg_ParseTupleAndKeywords(args, kwds, "w*|ni:recvfrom_into",
                                         kwlist, &pbuf,
                                         &recvlen, &flags))
            return NULL;
        buf = pbuf.buf;
        buflen = pbuf.len;

        if (recvlen < 0) {
            PyBuffer_Release(&pbuf);
            PyErr_SetString(PyExc_ValueError,
                            "negative buffersize in recvfrom_into");
            return NULL;
        }
        if (recvlen == 0) {
            /* If nbytes was not specified, use the buffer's length */
            recvlen = buflen;
        } else if (recvlen > buflen) {
            PyBuffer_Release(&pbuf);
            PyErr_SetString(PyExc_ValueError,
                            "nbytes is greater than the length of the buffer");
            return NULL;
        }

        readlen = sock_recvfrom_guts(s, buf, recvlen, flags, &addr);
        if (readlen < 0) {
            PyBuffer_Release(&pbuf);
            /* Return an error */
            Py_XDECREF(addr);
            return NULL;
        }

        PyBuffer_Release(&pbuf);
        /* Return the number of bytes read and the address.  Note that we do
           not do anything special here in the case that readlen < recvlen. */
        return Py_BuildValue("nN", readlen, addr);
    }

    PyDoc_STRVAR(recvfrom_into_doc,
    "recvfrom_into(buffer[, nbytes[, flags]]) -> (nbytes, address info)\n\
    \n\
    Like recv_into(buffer[, nbytes[, flags]]) but also return the sender's address info.");

    /* The sendmsg() and recvmsg[_into]() methods require a working
       CMSG_LEN().  See the comment near get_CMSG_LEN(). */
    #ifdef CMSG_LEN
    /* Return true iff msg->msg_controllen is valid, cmsgh is a valid
       pointer in msg->msg_control with at least "space" bytes after it,
       and its cmsg_len member inside the buffer. */
    static int
    cmsg_min_space(struct msghdr *msg, struct cmsghdr *cmsgh, size_t space)
    {
        size_t cmsg_offset;
        static const size_t cmsg_len_end = (offsetof(struct cmsghdr, cmsg_len) +
                                            sizeof(cmsgh->cmsg_len));

        /* Note that POSIX allows msg_controllen to be of signed type. */
        if (cmsgh == NULL || msg->msg_control == NULL)
            return 0;
        /* Note that POSIX allows msg_controllen to be of a signed type. This is
           annoying under OS X as it's unsigned there and so it triggers a
           tautological comparison warning under Clang when compared against 0.
           Since the check is valid on other platforms, silence the warning under
           Clang. */
        if (msg->msg_controllen < 0)
            return 0;
        if (space < cmsg_len_end)
            space = cmsg_len_end;
        cmsg_offset = (char *)cmsgh - (char *)msg->msg_control;
        return (cmsg_offset <= (size_t)-1 - space &&
                cmsg_offset + space <= msg->msg_controllen);
    }


    /* If pointer CMSG_DATA(cmsgh) is in buffer msg->msg_control, set
       *space to number of bytes following it in the buffer and return
       true; otherwise, return false.  Assumes cmsgh, msg->msg_control and
       msg->msg_controllen are valid. */
    static int
    get_cmsg_data_space(struct msghdr *msg, struct cmsghdr *cmsgh, size_t *space)
    {
        size_t data_offset;
        char *data_ptr;

        if ((data_ptr = (char *)CMSG_DATA(cmsgh)) == NULL)
            return 0;
        data_offset = data_ptr - (char *)msg->msg_control;
        if (data_offset > msg->msg_controllen)
            return 0;
        *space = msg->msg_controllen - data_offset;
        return 1;
    }

    /* If cmsgh is invalid or not contained in the buffer pointed to by
       msg->msg_control, return -1.  If cmsgh is valid and its associated
       data is entirely contained in the buffer, set *data_len to the
       length of the associated data and return 0.  If only part of the
       associated data is contained in the buffer but cmsgh is otherwise
       valid, set *data_len to the length contained in the buffer and
       return 1. */
    static int
    get_cmsg_data_len(struct msghdr *msg, struct cmsghdr *cmsgh, size_t *data_len)
    {
        size_t space, cmsg_data_len;

        if (!cmsg_min_space(msg, cmsgh, CMSG_LEN(0)) ||
            cmsgh->cmsg_len < CMSG_LEN(0))
            return -1;
        cmsg_data_len = cmsgh->cmsg_len - CMSG_LEN(0);
        if (!get_cmsg_data_space(msg, cmsgh, &space))
            return -1;
        if (space >= cmsg_data_len) {
            *data_len = cmsg_data_len;
            return 0;
        }
        *data_len = space;
        return 1;
    }

    struct sock_recvmsg_ctx {
        struct msghdr *msg;
        int flags;
        ssize_t result;
    };

    static int
    sock_recvmsg_impl(socket_object* s, void *data)
    {
        struct sock_recvmsg_ctx *ctx = data;

        ctx->result = ioth_recvmsg(s->fd, ctx->msg, ctx->flags);
        return  (ctx->result >= 0);
    }

    /*
     * Call recvmsg() with the supplied iovec structures, flags, and
     * ancillary data buffer size (controllen).  Returns the tuple return
     * value for recvmsg() or recvmsg_into(), with the first item provided
     * by the supplied makeval() function.  makeval() will be called with
     * the length read and makeval_data as arguments, and must return a
     * new reference (which will be decrefed if there is a subsequent
     * error).  On error, closes any file descriptors received via
     * SCM_RIGHTS.
     */

    static PyObject *
    sock_recvmsg_guts(socket_object *s, struct iovec *iov, int iovlen,
                      int flags, Py_ssize_t controllen,
                      PyObject *(*makeval)(ssize_t, void *), void *makeval_data)
    {
        struct sockaddr_storage addrbuf;
        socklen_t addrbuflen;
        struct msghdr msg = {0};
        PyObject *cmsg_list = NULL, *retval = NULL;
        void *controlbuf = NULL;
        struct cmsghdr *cmsgh;
        size_t cmsgdatalen = 0;
        int cmsg_status;
        struct sock_recvmsg_ctx ctx;

        /* XXX: POSIX says that msg_name and msg_namelen "shall be
           ignored" when the socket is connected (Linux fills them in
           anyway for AF_UNIX sockets at least).  Normally msg_namelen
           seems to be set to 0 if there's no address, but try to
           initialize msg_name to something that won't be mistaken for a
           real address if that doesn't happen. */
        if (!getsockaddrlen(s, &addrbuflen))
            return NULL;
        memset(&addrbuf, 0, addrbuflen);
        ((struct sockaddr*)&addrbuf)->sa_family = AF_UNSPEC;

        if (controllen < 0 || controllen > SOCKLEN_T_LIMIT) {
            PyErr_SetString(PyExc_ValueError, "invalid ancillary data buffer length");
            return NULL;
        }
        if (controllen > 0 && (controlbuf = PyMem_Malloc(controllen)) == NULL)
            return PyErr_NoMemory();

        /* Make the system call. */
        msg.msg_name = (struct sockaddr*)&addrbuf;
        msg.msg_namelen = addrbuflen;
        msg.msg_iov = iov;
        msg.msg_iovlen = iovlen;
        msg.msg_control = controlbuf;
        msg.msg_controllen = controllen;

        ctx.msg = &msg;
        ctx.flags = flags;
        if (sock_call(s, 0, sock_recvmsg_impl, &ctx, 0, NULL, s->sock_timeout) < 0)
            goto finally;

        /* Make list of (level, type, data) tuples from control messages. */
        if ((cmsg_list = PyList_New(0)) == NULL)
            goto err_closefds;
        /* Check for empty ancillary data as old CMSG_FIRSTHDR()
           implementations didn't do so. */
        for (cmsgh = ((msg.msg_controllen > 0) ? CMSG_FIRSTHDR(&msg) : NULL);
             cmsgh != NULL; cmsgh = CMSG_NXTHDR(&msg, cmsgh)) {
            PyObject *bytes, *tuple;
            int tmp;

            cmsg_status = get_cmsg_data_len(&msg, cmsgh, &cmsgdatalen);
            if (cmsg_status != 0) {
                if (PyErr_WarnEx(PyExc_RuntimeWarning,
                                 "received malformed or improperly-truncated "
                                 "ancillary data", 1) == -1)
                    goto err_closefds;
            }
            if (cmsg_status < 0)
                break;
            if (cmsgdatalen > PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_OSError, "control message too long");
                goto err_closefds;
            }

            bytes = PyBytes_FromStringAndSize((char *)CMSG_DATA(cmsgh),
                                              cmsgdatalen);
            tuple = Py_BuildValue("iiN", (int)cmsgh->cmsg_level,
                                  (int)cmsgh->cmsg_type, bytes);
            if (tuple == NULL)
                goto err_closefds;
            tmp = PyList_Append(cmsg_list, tuple);
            Py_DECREF(tuple);
            if (tmp != 0)
                goto err_closefds;

            if (cmsg_status != 0)
                break;
        }

        retval = Py_BuildValue("NOiN",
                               (*makeval)(ctx.result, makeval_data),
                               cmsg_list,
                               (int)msg.msg_flags,
                               make_sockaddr((struct sockaddr*)(&addrbuf), 
                                   ((msg.msg_namelen > addrbuflen) ?  addrbuflen : msg.msg_namelen)));
        if (retval == NULL)
            goto err_closefds;

    finally:
        Py_XDECREF(cmsg_list);
        PyMem_Free(controlbuf);
        return retval;

    err_closefds:
    #ifdef SCM_RIGHTS
        /* Close all descriptors coming from SCM_RIGHTS, so they don't leak. */
        for (cmsgh = ((msg.msg_controllen > 0) ? CMSG_FIRSTHDR(&msg) : NULL);
             cmsgh != NULL; cmsgh = CMSG_NXTHDR(&msg, cmsgh)) {
            cmsg_status = get_cmsg_data_len(&msg, cmsgh, &cmsgdatalen);
            if (cmsg_status < 0)
                break;
            if (cmsgh->cmsg_level == SOL_SOCKET &&
                cmsgh->cmsg_type == SCM_RIGHTS) {
                size_t numfds;
                int *fdp;

                numfds = cmsgdatalen / sizeof(int);
                fdp = (int *)CMSG_DATA(cmsgh);
                while (numfds-- > 0)
                    close(*fdp++);
            }
            if (cmsg_status != 0)
                break;
        }
    #endif /* SCM_RIGHTS */
        goto finally;
    }


    static PyObject *
    makeval_recvmsg(ssize_t received, void *data)
    {
        PyObject **buf = data;

        if (received < PyBytes_GET_SIZE(*buf))
            _PyBytes_Resize(buf, received);
        Py_XINCREF(*buf);
        return *buf;
    }

    /* s.recvmsg(bufsize[, ancbufsize[, flags]]) method */

    static PyObject *
    sock_recvmsg(PyObject* self, PyObject *args)
    {
        socket_object* s = (socket_object*)self;
        Py_ssize_t bufsize, ancbufsize = 0;
        int flags = 0;
        struct iovec iov;
        PyObject *buf = NULL, *retval = NULL;

        if (!PyArg_ParseTuple(args, "n|ni:recvmsg", &bufsize, &ancbufsize, &flags))
            return NULL;

        if (bufsize < 0) {
            PyErr_SetString(PyExc_ValueError, "negative buffer size in recvmsg()");
        return NULL;
    }
    if ((buf = PyBytes_FromStringAndSize(NULL, bufsize)) == NULL)
        return NULL;
    iov.iov_base = PyBytes_AS_STRING(buf);
    iov.iov_len = bufsize;

    /* Note that we're passing a pointer to *our pointer* to the bytes
       object here (&buf); makeval_recvmsg() may incref the object, or
       deallocate it and set our pointer to NULL. */
    retval = sock_recvmsg_guts(s, &iov, 1, flags, ancbufsize,
                               &makeval_recvmsg, &buf);
    Py_XDECREF(buf);
    return retval;
}

PyDoc_STRVAR(recvmsg_doc,
"recvmsg(bufsize[, ancbufsize[, flags]]) -> (data, ancdata, msg_flags, address)\n\
\n\
Receive normal data (up to bufsize bytes) and ancillary data from the\n\
socket.  The ancbufsize argument sets the size in bytes of the\n\
internal buffer used to receive the ancillary data; it defaults to 0,\n\
meaning that no ancillary data will be received.  Appropriate buffer\n\
sizes for ancillary data can be calculated using CMSG_SPACE() or\n\
CMSG_LEN(), and items which do not fit into the buffer might be\n\
truncated or discarded.  The flags argument defaults to 0 and has the\n\
same meaning as for recv().\n\
\n\
The return value is a 4-tuple: (data, ancdata, msg_flags, address).\n\
The data item is a bytes object holding the non-ancillary data\n\
received.  The ancdata item is a list of zero or more tuples\n\
(cmsg_level, cmsg_type, cmsg_data) representing the ancillary data\n\
(control messages) received: cmsg_level and cmsg_type are integers\n\
specifying the protocol level and protocol-specific type respectively,\n\
and cmsg_data is a bytes object holding the associated data.  The\n\
msg_flags item is the bitwise OR of various flags indicating\n\
conditions on the received message; see your system documentation for\n\
details.  If the receiving socket is unconnected, address is the\n\
address of the sending socket, if available; otherwise, its value is\n\
unspecified.\n\
\n\
If recvmsg() raises an exception after the system call returns, it\n\
will first attempt to close any file descriptors received via the\n\
SCM_RIGHTS mechanism.");


static PyObject *
makeval_recvmsg_into(ssize_t received, void *data)
{
    return PyLong_FromSsize_t(received);
}

/* s.recvmsg_into(buffers[, ancbufsize[, flags]]) method */

static PyObject *
sock_recvmsg_into(PyObject* self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    Py_ssize_t ancbufsize = 0;
    int flags = 0;
    struct iovec *iovs = NULL;
    Py_ssize_t i, nitems, nbufs = 0;
    Py_buffer *bufs = NULL;
    PyObject *buffers_arg, *fast, *retval = NULL;

    if (!PyArg_ParseTuple(args, "O|ni:recvmsg_into",
                          &buffers_arg, &ancbufsize, &flags))
        return NULL;

    if ((fast = PySequence_Fast(buffers_arg,
                                "recvmsg_into() argument 1 must be an "
                                "iterable")) == NULL)
        return NULL;
    nitems = PySequence_Fast_GET_SIZE(fast);
    if (nitems > INT_MAX) {
        PyErr_SetString(PyExc_OSError, "recvmsg_into() argument 1 is too long");
        goto finally;
    }

    /* Fill in an iovec for each item, and save the Py_buffer
       structs to release afterwards. */
    if (nitems > 0 && ((iovs = PyMem_New(struct iovec, nitems)) == NULL ||
                       (bufs = PyMem_New(Py_buffer, nitems)) == NULL)) {
        PyErr_NoMemory();
        goto finally;
    }
    for (; nbufs < nitems; nbufs++) {
        if (!PyArg_Parse(PySequence_Fast_GET_ITEM(fast, nbufs),
                         "w*;recvmsg_into() argument 1 must be an iterable "
                         "of single-segment read-write buffers",
                         &bufs[nbufs]))
            goto finally;
        iovs[nbufs].iov_base = bufs[nbufs].buf;
        iovs[nbufs].iov_len = bufs[nbufs].len;
    }

    retval = sock_recvmsg_guts(s, iovs, nitems, flags, ancbufsize,
                               &makeval_recvmsg_into, NULL);
finally:
    for (i = 0; i < nbufs; i++)
        PyBuffer_Release(&bufs[i]);
    PyMem_Free(bufs);
    PyMem_Free(iovs);
    Py_DECREF(fast);
    return retval;
}

PyDoc_STRVAR(recvmsg_into_doc,
"recvmsg_into(buffers[, ancbufsize[, flags]]) -> (nbytes, ancdata, msg_flags, address)\n\
\n\
Receive normal data and ancillary data from the socket, scattering the\n\
non-ancillary data into a series of buffers.  The buffers argument\n\
must be an iterable of objects that export writable buffers\n\
(e.g. bytearray objects); these will be filled with successive chunks\n\
of the non-ancillary data until it has all been written or there are\n\
no more buffers.  The ancbufsize argument sets the size in bytes of\n\
the internal buffer used to receive the ancillary data; it defaults to\n\
0, meaning that no ancillary data will be received.  Appropriate\n\
buffer sizes for ancillary data can be calculated using CMSG_SPACE()\n\
or CMSG_LEN(), and items which do not fit into the buffer might be\n\
truncated or discarded.  The flags argument defaults to 0 and has the\n\
same meaning as for recv().\n\
\n\
The return value is a 4-tuple: (nbytes, ancdata, msg_flags, address).\n\
The nbytes item is the total number of bytes of non-ancillary data\n\
written into the buffers.  The ancdata item is a list of zero or more\n\
tuples (cmsg_level, cmsg_type, cmsg_data) representing the ancillary\n\
data (control messages) received: cmsg_level and cmsg_type are\n\
integers specifying the protocol level and protocol-specific type\n\
respectively, and cmsg_data is a bytes object holding the associated\n\
data.  The msg_flags item is the bitwise OR of various flags\n\
indicating conditions on the received message; see your system\n\
documentation for details.  If the receiving socket is unconnected,\n\
address is the address of the sending socket, if available; otherwise,\n\
its value is unspecified.\n\
\n\
If recvmsg_into() raises an exception after the system call returns,\n\
it will first attempt to close any file descriptors received via the\n\
SCM_RIGHTS mechanism.");
#endif    /* CMSG_LEN */


struct sock_send_ctx {
    char *buf;
    Py_ssize_t len;
    int flags;
    Py_ssize_t result;
};

static int
sock_send_impl(socket_object *s, void *data)
{
    struct sock_send_ctx *ctx = data;

    ctx->result = ioth_send(s->fd, ctx->buf, ctx->len, ctx->flags);
    return ctx->result >= 0;
}

static PyObject *
sock_send(PyObject *self, PyObject *args) 
{
    socket_object* s = (socket_object*)self;

    int flags = 0;
    Py_buffer pbuf;
    struct sock_send_ctx ctx;

    if (!PyArg_ParseTuple(args, "y*|i:send", &pbuf, &flags))
        return NULL;

    ctx.buf = pbuf.buf;
    ctx.len = pbuf.len;
    ctx.flags = flags;

    if (sock_call(s, 1, sock_send_impl, &ctx, 0, NULL, s->sock_timeout) < 0) {
        PyBuffer_Release(&pbuf);
        return NULL;
    }

    PyBuffer_Release(&pbuf);
    return PyLong_FromSsize_t(ctx.result);
}

PyDoc_STRVAR(send_doc,
"send(data[, flags]) -> count\n\
\n\
Send a data string to the socket.  For the optional flags\n\
argument, see the Unix manual.  Return the number of bytes\n\
sent; this may be less than len(data) if the network is busy.");



static PyObject *
sock_sendall(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    char *buf;
    Py_ssize_t len, n;
    int flags = 0;
    Py_buffer pbuf;
    struct sock_send_ctx ctx;
    int has_timeout = (s->sock_timeout > 0);
    _PyTime_t interval = s->sock_timeout;
    _PyTime_t deadline = 0;
    int deadline_initialized = 0;
    PyObject *res = NULL;

    if (!PyArg_ParseTuple(args, "y*|i:sendall", &pbuf, &flags))
        return NULL;
    buf = pbuf.buf;
    len = pbuf.len;

    do {
        if (has_timeout) {
            if (deadline_initialized) {
                /* recompute the timeout */
                interval = deadline - _PyTime_GetMonotonicClock();
            }
            else {
                deadline_initialized = 1;
                deadline = _PyTime_GetMonotonicClock() + s->sock_timeout;
            }

            if (interval <= 0) {
                PyErr_SetString(socket_timeout, "timed out");
                goto done;
            }
        }

        ctx.buf = buf;
        ctx.len = len;
        ctx.flags = flags;
        if (sock_call(s, 1, sock_send_impl, &ctx, 0, NULL, interval) < 0)
            goto done;
        n = ctx.result;
        assert(n >= 0);

        buf += n;
        len -= n;

        /* We must run our signal handlers before looping again.
           send() can return a successful partial write when it is
           interrupted, so we can't restrict ourselves to EINTR. */
        if (PyErr_CheckSignals())
            goto done;
    } while (len > 0);
    PyBuffer_Release(&pbuf);

    Py_INCREF(Py_None);
    res = Py_None;

done:
    PyBuffer_Release(&pbuf);
    return res;
}

PyDoc_STRVAR(sendall_doc,
"sendall(data[, flags])\n\
\n\
Send a data string to the socket.  For the optional flags\n\
argument, see the Unix manual.  This calls send() repeatedly\n\
until all data is sent.  If an error occurs, it's impossible\n\
to tell how much data has been sent.");


#ifdef CMSG_LEN
/* If length is in range, set *result to CMSG_LEN(length) and return
   true; otherwise, return false. */
int
get_CMSG_LEN(size_t length, size_t *result)
{
    size_t tmp;

    if (length > (SOCKLEN_T_LIMIT - CMSG_LEN(0)))
        return 0;
    tmp = CMSG_LEN(length);
    if (tmp > SOCKLEN_T_LIMIT || tmp < length)
        return 0;
    *result = tmp;
    return 1;
}

#ifdef CMSG_SPACE
/* If length is in range, set *result to CMSG_SPACE(length) and return
   true; otherwise, return false. */
int
get_CMSG_SPACE(size_t length, size_t *result)
{
    size_t tmp;

    /* Use CMSG_SPACE(1) here in order to take account of the padding
       necessary before *and* after the data. */
    if (length > (SOCKLEN_T_LIMIT - CMSG_SPACE(1)))
        return 0;
    tmp = CMSG_SPACE(length);
    if (tmp > SOCKLEN_T_LIMIT || tmp < length)
        return 0;
    *result = tmp;
    return 1;
}
#endif
#endif

struct sock_sendto_ctx {
    char *buf;
    Py_ssize_t len;
    int flags;
    int addrlen;
    struct sockaddr* addrbuf;
    Py_ssize_t result;
};

static int
sock_sendto_impl(socket_object *s, void *data)
{
    struct sock_sendto_ctx *ctx = data;

    ctx->result = ioth_sendto(s->fd, ctx->buf, ctx->len, ctx->flags, ctx->addrbuf, ctx->addrlen);
    return ctx->result >= 0;
}

/* s.sendto(data, [flags,] sockaddr) method */

static PyObject *
sock_sendto(PyObject* self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    
    Py_buffer pbuf;
    PyObject *addro;
    Py_ssize_t arglen;
    struct sockaddr_storage addrbuf;
    int addrlen, flags;
    struct sock_sendto_ctx ctx;

    flags = 0;
    arglen = PyTuple_Size(args);
    switch (arglen) {
        case 2:
            if (!PyArg_ParseTuple(args, "y*O:sendto", &pbuf, &addro)) {
                return NULL;
            }
            break;
        case 3:
            if (!PyArg_ParseTuple(args, "y*iO:sendto", &pbuf, &flags, &addro)) {
                return NULL;
            }
            break;
        default:
            PyErr_Format(PyExc_TypeError, "sendto() takes 2 or 3 arguments (%zd given)", arglen);
            return NULL;
    }

    if(!get_sockaddr_from_tuple("sendto", s, addro, (struct sockaddr*)&addrbuf, &addrlen)) {
        PyBuffer_Release(&pbuf);
        return NULL;
    }

    ctx.buf = pbuf.buf;
    ctx.len = pbuf.len;
    ctx.flags = flags;
    ctx.addrlen = addrlen;
    ctx.addrbuf = (struct sockaddr*)&addrbuf;
    if (sock_call(s, 1, sock_sendto_impl, &ctx, 0, NULL, s->sock_timeout) < 0) {
        PyBuffer_Release(&pbuf);
        return NULL;
    }
    PyBuffer_Release(&pbuf);

    return PyLong_FromSsize_t(ctx.result);
}

PyDoc_STRVAR(sendto_doc,
"sendto(data[, flags], address) -> count\n\
\n\
Like send(data, flags) but allows specifying the destination address.\n\
For IP sockets, the address is a pair (hostaddr, port).");


/* The sendmsg() and recvmsg[_into]() methods require a working
   CMSG_LEN().  See the comment near get_CMSG_LEN(). */
#ifdef CMSG_LEN
struct sock_sendmsg_ctx {
    struct msghdr *msg;
    int flags;
    ssize_t result;
};

static int
sock_sendmsg_iovec(socket_object *s, PyObject *data_arg,
                   struct msghdr *msg,
                   Py_buffer **databufsout, Py_ssize_t *ndatabufsout) {
    Py_ssize_t ndataparts, ndatabufs = 0;
    int result = -1;
    struct iovec *iovs = NULL;
    PyObject *data_fast = NULL;
    Py_buffer *databufs = NULL;

    /* Fill in an iovec for each message part, and save the Py_buffer
       structs to release afterwards. */
    data_fast = PySequence_Fast(data_arg,
                                "sendmsg() argument 1 must be an "
                                "iterable");
    if (data_fast == NULL) {
        goto finally;
    }

    ndataparts = PySequence_Fast_GET_SIZE(data_fast);
    if (ndataparts > INT_MAX) {
        PyErr_SetString(PyExc_OSError, "sendmsg() argument 1 is too long");
        goto finally;
    }

    msg->msg_iovlen = ndataparts;
    if (ndataparts > 0) {
        iovs = PyMem_New(struct iovec, ndataparts);
        if (iovs == NULL) {
            PyErr_NoMemory();
            goto finally;
        }
        msg->msg_iov = iovs;

        databufs = PyMem_New(Py_buffer, ndataparts);
        if (databufs == NULL) {
            PyErr_NoMemory();
            goto finally;
        }
    }
    for (; ndatabufs < ndataparts; ndatabufs++) {
        if (!PyArg_Parse(PySequence_Fast_GET_ITEM(data_fast, ndatabufs),
                         "y*;sendmsg() argument 1 must be an iterable of "
                         "bytes-like objects",
                         &databufs[ndatabufs]))
            goto finally;
        iovs[ndatabufs].iov_base = databufs[ndatabufs].buf;
        iovs[ndatabufs].iov_len = databufs[ndatabufs].len;
    }
    result = 0;
  finally:
    *databufsout = databufs;
    *ndatabufsout = ndatabufs;
    Py_XDECREF(data_fast);
    return result;
}

static int
sock_sendmsg_impl(socket_object *s, void *data)
{
    struct sock_sendmsg_ctx *ctx = data;

    ctx->result = ioth_sendmsg(s->fd, ctx->msg, ctx->flags);
    return (ctx->result >= 0);
}

/* s.sendmsg(buffers[, ancdata[, flags[, address]]]) method */

static PyObject *
sock_sendmsg(PyObject* self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    Py_ssize_t i, ndatabufs = 0, ncmsgs, ncmsgbufs = 0;
    Py_buffer *databufs = NULL;
    struct sockaddr_storage addrbuf;
    struct msghdr msg;
    struct cmsginfo {
        int level;
        int type;
        Py_buffer data;
    } *cmsgs = NULL;
    void *controlbuf = NULL;
    size_t controllen, controllen_last;
    int addrlen, flags = 0;
    PyObject *data_arg, *cmsg_arg = NULL, *addr_arg = NULL,
        *cmsg_fast = NULL, *retval = NULL;
    struct sock_sendmsg_ctx ctx;

    if (!PyArg_ParseTuple(args, "O|OiO:sendmsg",
                          &data_arg, &cmsg_arg, &flags, &addr_arg)) {
        return NULL;
    }

    memset(&msg, 0, sizeof(msg));

    /* Parse destination address. */
    if (addr_arg != NULL && addr_arg != Py_None) {

        if(!get_sockaddr_from_tuple("sendmsg", s, addr_arg, (struct sockaddr*)&addrbuf, &addrlen))
        {
            goto finally;
        }
        msg.msg_name = &addrbuf;
        msg.msg_namelen = addrlen;
    }

    /* Fill in an iovec for each message part, and save the Py_buffer
       structs to release afterwards. */
    if (sock_sendmsg_iovec(s, data_arg, &msg, &databufs, &ndatabufs) == -1) {
        goto finally;
    }

    if (cmsg_arg == NULL)
        ncmsgs = 0;
    else {
        if ((cmsg_fast = PySequence_Fast(cmsg_arg,
                                         "sendmsg() argument 2 must be an "
                                         "iterable")) == NULL)
            goto finally;
        ncmsgs = PySequence_Fast_GET_SIZE(cmsg_fast);
    }

#ifndef CMSG_SPACE
    if (ncmsgs > 1) {
        PyErr_SetString(PyExc_OSError,
                        "sending multiple control messages is not supported "
                        "on this system");
        goto finally;
    }
#endif
    /* Save level, type and Py_buffer for each control message,
       and calculate total size. */
    if (ncmsgs > 0 && (cmsgs = PyMem_New(struct cmsginfo, ncmsgs)) == NULL) {
        PyErr_NoMemory();
        goto finally;
    }
    controllen = controllen_last = 0;
    while (ncmsgbufs < ncmsgs) {
        size_t bufsize, space;

        if (!PyArg_Parse(PySequence_Fast_GET_ITEM(cmsg_fast, ncmsgbufs),
                         "(iiy*):[sendmsg() ancillary data items]",
                         &cmsgs[ncmsgbufs].level,
                         &cmsgs[ncmsgbufs].type,
                         &cmsgs[ncmsgbufs].data))
            goto finally;
        bufsize = cmsgs[ncmsgbufs++].data.len;

#ifdef CMSG_SPACE
        if (!get_CMSG_SPACE(bufsize, &space)) {
#else
        if (!get_CMSG_LEN(bufsize, &space)) {
#endif
            PyErr_SetString(PyExc_OSError, "ancillary data item too large");
            goto finally;
        }
        controllen += space;
        if (controllen > SOCKLEN_T_LIMIT || controllen < controllen_last) {
            PyErr_SetString(PyExc_OSError, "too much ancillary data");
            goto finally;
        }
        controllen_last = controllen;
    }

    /* Construct ancillary data block from control message info. */
    if (ncmsgbufs > 0) {
        struct cmsghdr *cmsgh = NULL;

        controlbuf = PyMem_Malloc(controllen);
        if (controlbuf == NULL) {
            PyErr_NoMemory();
            goto finally;
        }
        msg.msg_control = controlbuf;

        msg.msg_controllen = controllen;

        /* Need to zero out the buffer as a workaround for glibc's
           CMSG_NXTHDR() implementation.  After getting the pointer to
           the next header, it checks its (uninitialized) cmsg_len
           member to see if the "message" fits in the buffer, and
           returns NULL if it doesn't.  Zero-filling the buffer
           ensures that this doesn't happen. */
        memset(controlbuf, 0, controllen);

        for (i = 0; i < ncmsgbufs; i++) {
            size_t msg_len, data_len = cmsgs[i].data.len;
            int enough_space = 0;

            cmsgh = (i == 0) ? CMSG_FIRSTHDR(&msg) : CMSG_NXTHDR(&msg, cmsgh);
            if (cmsgh == NULL) {
                PyErr_Format(PyExc_RuntimeError,
                             "unexpected NULL result from %s()",
                             (i == 0) ? "CMSG_FIRSTHDR" : "CMSG_NXTHDR");
                goto finally;
            }
            if (!get_CMSG_LEN(data_len, &msg_len)) {
                PyErr_SetString(PyExc_RuntimeError,
                                "item size out of range for CMSG_LEN()");
                goto finally;
            }
            if (cmsg_min_space(&msg, cmsgh, msg_len)) {
                size_t space;

                cmsgh->cmsg_len = msg_len;
                if (get_cmsg_data_space(&msg, cmsgh, &space))
                    enough_space = (space >= data_len);
            }
            if (!enough_space) {
                PyErr_SetString(PyExc_RuntimeError,
                                "ancillary data does not fit in calculated "
                                "space");
                goto finally;
            }
            cmsgh->cmsg_level = cmsgs[i].level;
            cmsgh->cmsg_type = cmsgs[i].type;
            memcpy(CMSG_DATA(cmsgh), cmsgs[i].data.buf, data_len);
        }
    }

    ctx.msg = &msg;
    ctx.flags = flags;
    if (sock_call(s, 1, sock_sendmsg_impl, &ctx, 0, NULL, s->sock_timeout) < 0)
        goto finally;

    retval = PyLong_FromSsize_t(ctx.result);

finally:
    PyMem_Free(controlbuf);
    for (i = 0; i < ncmsgbufs; i++)
        PyBuffer_Release(&cmsgs[i].data);
    PyMem_Free(cmsgs);
    Py_XDECREF(cmsg_fast);
    PyMem_Free(msg.msg_iov);
    for (i = 0; i < ndatabufs; i++) {
        PyBuffer_Release(&databufs[i]);
    }
    PyMem_Free(databufs);
    return retval;
}

PyDoc_STRVAR(sendmsg_doc,
"sendmsg(buffers[, ancdata[, flags[, address]]]) -> count\n\
\n\
Send normal and ancillary data to the socket, gathering the\n\
non-ancillary data from a series of buffers and concatenating it into\n\
a single message.  The buffers argument specifies the non-ancillary\n\
data as an iterable of bytes-like objects (e.g. bytes objects).\n\
The ancdata argument specifies the ancillary data (control messages)\n\
as an iterable of zero or more tuples (cmsg_level, cmsg_type,\n\
cmsg_data), where cmsg_level and cmsg_type are integers specifying the\n\
protocol level and protocol-specific type respectively, and cmsg_data\n\
is a bytes-like object holding the associated data.  The flags\n\
argument defaults to 0 and has the same meaning as for send().  If\n\
address is supplied and not None, it sets a destination address for\n\
the message.  The return value is the number of bytes of non-ancillary\n\
data sent.");
#endif    /* CMSG_LEN */


static PyObject *
sock_close(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    if(s->fd != -1)
    {
        int res;

        Py_BEGIN_ALLOW_THREADS
        res = ioth_close(s->fd);
        Py_END_ALLOW_THREADS

        s->fd = -1;
        if(res < 0 && errno != ECONNRESET) {
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }
    }

    //Return none if no errors
    Py_RETURN_NONE;
}

PyDoc_STRVAR(close_doc,
"close()\n\
\n\
Close the socket.  It cannot be used after this call.");



static int
sock_connect_impl(socket_object* s, void* Py_UNUSED(data))
{
    int err;
    socklen_t size = sizeof err;

    if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void *)&err, &size)) {
        /* getsockopt() failed */
        return 0;
    }

    if (err == EISCONN)
        return 1;
    if (err != 0) {
        /* sock_call_ex() uses GET_SOCK_ERROR() to get the error code */
        SET_SOCK_ERROR(err);
        return 0;
    }
    return 1;
}

static int
internal_connect(socket_object* s, struct sockaddr *addr, int addrlen,
                 int raise)
{
    int res, err, wait_connect;

    Py_BEGIN_ALLOW_THREADS
    res = ioth_connect(s->fd, addr, addrlen);
    Py_END_ALLOW_THREADS

    if (!res) {
        /* connect() succeeded, the socket is connected */
        return 0;
    }

    /* connect() failed */

    /* save error, PyErr_CheckSignals() can replace it */
    err = GET_SOCK_ERROR;
    if (CHECK_ERRNO(EINTR)) {
        if (PyErr_CheckSignals())
            return -1;

        wait_connect = (s->sock_timeout != 0);
    }
    else {
        wait_connect = (s->sock_timeout > 0 && err == SOCK_INPROGRESS_ERR);
    }

    if (!wait_connect) {
        if (raise) {
            /* restore error, maybe replaced by PyErr_CheckSignals() */
            SET_SOCK_ERROR(err);
            PyErr_SetFromErrno(PyExc_OSError);
            return -1;
        }
        else
            return err;
    }

    if (raise) {
        /* socket.connect() raises an exception on error */
        if (sock_call(s, 1, sock_connect_impl, NULL,
                         1, NULL, s->sock_timeout) < 0)
            return -1;
    }
    else {
        /* socket.connect_ex() returns the error code on error */
        if (sock_call(s, 1, sock_connect_impl, NULL,
                         1, &err, s->sock_timeout) < 0)
            return err;
    }
    return 0;
}


static PyObject *
sock_connect(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    struct sockaddr_storage addrbuf;
    int addrlen;
    if(!get_sockaddr_from_tuple("connect", s, args, (struct sockaddr*)&addrbuf, &addrlen))
    {
        return NULL;
    }

    int res = internal_connect(s, (struct sockaddr*)&addrbuf, addrlen, 1);
    if(res < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(connect_doc,
"connect(address)\n\
\n\
Connect the socket to a remote address.  For IP sockets, the address\n\
is a pair (host, port).");


static PyObject *
sock_connect_ex(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    struct sockaddr_storage addrbuf;
    int addrlen;
    if(!get_sockaddr_from_tuple("connect_ex", s, args, (struct sockaddr*)&addrbuf, &addrlen))
    {
        return NULL;
    }

    int res = internal_connect(s, (struct sockaddr*)&addrbuf, addrlen, 0);
    if(res < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(connect_ex_doc,
"connect_ex(address) -> errno\n\
\n\
This is like connect(address), but returns an error code (the errno value)\n\
instead of raising an exception when an error occurs.");



static PyObject *
sock_fileno(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    return PyLong_FromLong(s->fd);
}

PyDoc_STRVAR(fileno_doc,
"fileno() -> integer\n\
\n\
Return the integer file descriptor of the socket.");



static PyObject *
sock_getsockopt(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    int level;
    int optname;
    int res;

    PyObject *buf;
    socklen_t buflen = 0;

    if (!PyArg_ParseTuple(args, "ii|i:getsockopt",
                          &level, &optname, &buflen))
        return NULL;

    if (buflen <= 0 || buflen > 1024) {
        PyErr_SetString(PyExc_OSError, "getsockopt buflen out of range");
        return NULL;
    }

    if (buflen == 0) {
        int flag = 0;
        socklen_t flagsize = sizeof(flag);

        res = ioth_getsockopt(s->fd, level, optname, (void *)&flag, &flagsize);
        if (res < 0) {
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }
        
        return PyLong_FromLong(flag);
    }

    buf = PyBytes_FromStringAndSize((char *)NULL, buflen);
    if (buf == NULL)
        return NULL;

    res = ioth_getsockopt(s->fd, level, optname, (void *)PyBytes_AS_STRING(buf), &buflen);
    if (res < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    _PyBytes_Resize(&buf, buflen);
    return buf;
}

PyDoc_STRVAR(getsockopt_doc,
"getsockopt(level, option[, buffersize]) -> value\n\
\n\
Get a socket option.  See the Unix manual for level and option.\n\
If a nonzero buffersize argument is given, the return value is a\n\
string of that length; otherwise it is an integer.");



static PyObject *
sock_setsockopt(PyObject* self, PyObject *args)
{
    socket_object* s = (socket_object*)self;

    int level;
    int optname;
    int res;
    Py_buffer optval;
    int flag;
    unsigned int optlen;
    PyObject *none;

   /* setsockopt(level, opt, flag) */
    if (PyArg_ParseTuple(args, "iii:setsockopt", &level, &optname, &flag)) {
        res = ioth_setsockopt(s->fd, level, optname, (char*)&flag, sizeof flag);
        goto done;
    }

    PyErr_Clear();

    /* setsockopt(level, opt, None, flag) */
    if (PyArg_ParseTuple(args, "iiO!I:setsockopt",
                         &level, &optname, Py_TYPE(Py_None), &none, &optlen)) {
        assert(sizeof(socklen_t) >= sizeof(unsigned int));
        res = ioth_setsockopt(s->fd, level, optname, NULL, (socklen_t)optlen);
        goto done;
    }

    PyErr_Clear();
    /* setsockopt(level, opt, buffer) */
    if (!PyArg_ParseTuple(args, "iiy*:setsockopt", &level, &optname, &optval))
        return NULL;

    res = ioth_setsockopt(s->fd, level, optname, optval.buf, optval.len);
    PyBuffer_Release(&optval);

done:
    if (res < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(setsockopt_doc,
"setsockopt(level, option, value: int)\n\
setsockopt(level, option, value: buffer)\n\
setsockopt(level, option, None, optlen: int)\n\
\n\
Set a socket option.  See the Unix manual for level and option.\n\
The value argument can either be an integer, a string buffer, or\n\
None, optlen.");



static PyObject *
sock_detach(PyObject* self, PyObject *Py_UNUSED(ignored))
{
    socket_object* s = (socket_object*)self;
    int fd = s->fd;
    s->fd = -1;
    return PyLong_FromLong(fd);
}

PyDoc_STRVAR(detach_doc,
"detach()\n\
\n\
Close the socket object without closing the underlying file descriptor.\n\
The object cannot be used after this call, but the file descriptor\n\
can be reused for other purposes.  The file descriptor is returned.");

static PyObject *
sock_shutdown(PyObject *self, PyObject *arg)
{
    socket_object* s = (socket_object*)self;

    int how;
    int res;

    how = _PyLong_AsInt(arg);
    if (how == -1 && PyErr_Occurred())
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    res = ioth_shutdown(s->fd, how);
    Py_END_ALLOW_THREADS

    if (res < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(shutdown_doc,
"shutdown(flag)\n\
\n\
Shut down the reading side of the socket (flag == SHUT_RD), the writing side\n\
of the socket (flag == SHUT_WR), or both ends (flag == SHUT_RDWR).");


static PyObject*
sock_getsockname(PyObject* self, PyObject* args)
{
    socket_object* s = (socket_object*)self;
    
    struct sockaddr_storage addrbuf;
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    int res;
    Py_BEGIN_ALLOW_THREADS
    res = ioth_getsockname(s->fd, (struct sockaddr*)&addrbuf, &addrlen);
    Py_END_ALLOW_THREADS

    if(res < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
}

PyDoc_STRVAR(getsockname_doc,
"getsockname() -> address info\n\
\n\
Return the address of the local endpoint.  For IP sockets, the address\n\
info is a pair (hostaddr, port).");



static PyObject*
sock_getpeername(PyObject* self, PyObject* args)
{
    socket_object* s = (socket_object*)self;
    
    struct sockaddr_storage addrbuf;
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    int res;
    Py_BEGIN_ALLOW_THREADS
    res = ioth_getpeername(s->fd, (struct sockaddr*)&addrbuf, &addrlen);
    Py_END_ALLOW_THREADS

    if(res < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
}

PyDoc_STRVAR(getpeername_doc,
"getpeername() -> address info\n\
\n\
Return the address of the remote endpoint.  For IP sockets, the address\n\
info is a pair (hostaddr, port).");



/* Function to perform the setting of socket blocking mode
   internally. block = (1 | 0). */
static int
internal_setblocking(socket_object* s, int block)
{
    int result = -1;
    int delay_flag, new_delay_flag;

    /* Use fcntl instead of ioctl because it's supported by picoxnet */
    Py_BEGIN_ALLOW_THREADS
    delay_flag = ioth_fcntl(s->fd, F_GETFL, 0);
    if (delay_flag == -1)
        goto done;
    if (block)
        new_delay_flag = delay_flag & (~O_NONBLOCK);
    else
        new_delay_flag = delay_flag | O_NONBLOCK;
    if (new_delay_flag != delay_flag)
        if (ioth_fcntl(s->fd, F_SETFL, new_delay_flag) == -1)
            goto done;

    result = 0;
done:
    Py_END_ALLOW_THREADS

    if (result) {
        PyErr_SetFromErrno(PyExc_OSError);
    }

    return result;
}


/* s.setblocking(flag) method.  Argument:
   False -- non-blocking mode; same as settimeout(0)
   True -- blocking mode; same as settimeout(None)
*/

static PyObject *
sock_setblocking(PyObject *self, PyObject *arg)
{
    socket_object* s = (socket_object*)self;

    long block;

    block = PyLong_AsLong(arg);
    if (block == -1 && PyErr_Occurred())
        return NULL;

    s->sock_timeout = _PyTime_FromSeconds(block ? -1 : 0);
    if (internal_setblocking(s, block) == -1) {
        return NULL;
    }
    Py_RETURN_NONE;
}


PyDoc_STRVAR(setblocking_doc,
"setblocking(flag)\n\
\n\
Set the socket to blocking (flag is true) or non-blocking (false).\n\
setblocking(True) is equivalent to settimeout(None);\n\
setblocking(False) is equivalent to settimeout(0.0).");

/* s.getblocking() method.
   Returns True if socket is in blocking mode,
   False if it is in non-blocking mode.
*/
static PyObject *
sock_getblocking(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    socket_object* s = (socket_object*)self;

    if (s->sock_timeout) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

PyDoc_STRVAR(getblocking_doc,
"getblocking()\n\
\n\
Returns True if socket is in blocking mode, or False if it\n\
is in non-blocking mode.");

/* s.settimeout(timeout) method.  Argument:
   None -- no timeout, blocking mode; same as setblocking(True)
   0.0  -- non-blocking mode; same as setblocking(False)
   > 0  -- timeout mode; operations time out after timeout seconds
   < 0  -- illegal; raises an exception
*/
static PyObject *
sock_settimeout(PyObject *self, PyObject *arg)
{
    socket_object* s = (socket_object*)self;

    _PyTime_t timeout;

    if (socket_parse_timeout(&timeout, arg) < 0)
        return NULL;

    s->sock_timeout = timeout;

    int block = timeout < 0;
    /* Blocking mode for a Python socket object means that operations
       like :meth:`recv` or :meth:`sendall` will block the execution of
       the current thread until they are complete or aborted with a
       `socket.timeout` or `socket.error` errors.  When timeout is `None`,
       the underlying FD is in a blocking mode.  When timeout is a positive
       number, the FD is in a non-blocking mode, and socket ops are
       implemented with a `select()` call.
       When timeout is 0.0, the FD is in a non-blocking mode.
       This table summarizes all states in which the socket object and
       its underlying FD can be:
       ==================== ===================== ==============
        `gettimeout()`       `getblocking()`       FD
       ==================== ===================== ==============
        ``None``             ``True``              blocking
        ``0.0``              ``False``             non-blocking
        ``> 0``              ``True``              non-blocking
    */

    if (internal_setblocking(s, block) == -1) {
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(settimeout_doc,
"settimeout(timeout)\n\
\n\
Set a timeout on socket operations.  'timeout' can be a float,\n\
giving in seconds, or None.  Setting a timeout of None disables\n\
the timeout feature and is equivalent to setblocking(1).\n\
Setting a timeout of zero is the same as setblocking(0).");

/* s.gettimeout() method.
   Returns the timeout associated with a socket. */
static PyObject *
sock_gettimeout(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    socket_object* s = (socket_object*)self;

    if (s->sock_timeout < 0) {
        Py_RETURN_NONE;
    }
    else {
        double seconds = _PyTime_AsSecondsDouble(s->sock_timeout);
        return PyFloat_FromDouble(seconds);
    }
}

PyDoc_STRVAR(gettimeout_doc,
"gettimeout() -> timeout\n\
\n\
Returns the timeout in seconds (float) associated with socket\n\
operations. A timeout of None indicates that timeouts on socket\n\
operations are disabled.");


static PyMethodDef socket_methods[] = 
{
    {"bind",    sock_bind,    METH_O,       bind_doc},
    {"close",   sock_close,   METH_NOARGS,  close_doc},
    {"connect", sock_connect, METH_O,       connect_doc},
    {"connect_ex", sock_connect_ex, METH_O, connect_ex_doc},
    {"listen",  sock_listen,  METH_VARARGS, listen_doc},
    {"_accept",  sock_accept,  METH_NOARGS, accept_doc},
    {"recv",    sock_recv,    METH_VARARGS, recv_doc},
    {"recv_into", (PyCFunction)sock_recv_into, METH_VARARGS | METH_KEYWORDS, recv_into_doc},
    {"recvfrom", sock_recvfrom, METH_VARARGS, recvfrom_doc},
    {"recvfrom_into", (PyCFunction)sock_recvfrom_into, METH_VARARGS | METH_KEYWORDS, recvfrom_into_doc},
    {"send",    sock_send,    METH_VARARGS, send_doc},  
    {"sendall",    sock_sendall,    METH_VARARGS, sendall_doc},  
    {"sendto", sock_sendto, METH_VARARGS, sendto_doc},

#ifdef CMSG_LEN
    {"recvmsg",      sock_recvmsg, METH_VARARGS, recvmsg_doc},
    {"recvmsg_into", sock_recvmsg_into, METH_VARARGS, recvmsg_into_doc,},
    //{"sendmsg",      sock_sendmsg, METH_VARARGS, sendmsg_doc},
#endif

    {"detach",  sock_detach, METH_NOARGS, detach_doc},
    {"fileno",  sock_fileno,    METH_NOARGS, fileno_doc}, 
    {"getsockopt", sock_getsockopt, METH_VARARGS, getsockopt_doc},
    {"setsockopt", sock_setsockopt, METH_VARARGS, setsockopt_doc},
    {"shutdown", sock_shutdown, METH_O, shutdown_doc},
    {"getsockname", sock_getsockname, METH_NOARGS, getsockname_doc},
    {"getpeername", sock_getpeername, METH_NOARGS, getpeername_doc},
    {"setblocking", sock_setblocking, METH_O, setblocking_doc},
    {"getblocking", sock_getblocking, METH_NOARGS, getblocking_doc},
    {"settimeout",  sock_settimeout, METH_O, settimeout_doc},
    {"gettimeout",  sock_gettimeout, METH_NOARGS, gettimeout_doc},


    {NULL, NULL} /* sentinel */
};

// Socket type functions

static void
socket_dealloc(socket_object* self)
{
    if(PyObject_CallFinalizerFromDealloc((PyObject*)self) < 0)
        return;
    
    PyTypeObject* tp = Py_TYPE(self);
    tp->tp_free(self);
}

static PyObject*
socket_repr(socket_object* self)
{
    return PyUnicode_FromFormat( "<socket object, fd=%ld, family=%d, type=%d, proto=%d>",
        self->fd, self->family, self->type, self->proto);
}

static int
init_sockobject(socket_object *s, PyObject* stack, int fd, int family, int type, int proto)
{
    s->fd = fd;
    s->family = family;
    s->type = type;
    s->proto = proto;

    /* It's possible to pass SOCK_NONBLOCK and SOCK_CLOEXEC bit flags
       on some OSes as part of socket.type.  We want to reset them here,
       to make socket.type be set to the same value on all platforms.
       Otherwise, simple code like 'if sock.type == SOCK_STREAM' is
       not portable.
    */
#ifdef SOCK_NONBLOCK
    s->type = s->type & ~SOCK_NONBLOCK;
#endif
#ifdef SOCK_CLOEXEC
    s->type = s->type & ~SOCK_CLOEXEC;
#endif

#ifdef SOCK_NONBLOCK
    if (type & SOCK_NONBLOCK)
        s->sock_timeout = 0;
    else
#endif
    {
        s->sock_timeout = defaulttimeout;
        if (defaulttimeout >= 0) {
            if (internal_setblocking(s, 0) == -1) {
                return -1;
            }
        }
    }

    s->stack = stack;
    Py_INCREF(s->stack);

    return 0;
}

static int
socket_initobj(PyObject* self, PyObject* args, PyObject* kwds)
{
    socket_object* s = (socket_object*)self;

    PyObject* stack;
    int family = AF_INET;
    int type = SOCK_STREAM;
    int proto = 0;

    PyObject* fdobj = NULL;
    int fd = -1;

    if(!PyArg_ParseTuple(args, "Oiii|O", &stack, &family, &type, &proto, &fdobj))
        return -1;

    /* Create a new socket */
    if(fdobj == NULL || fdobj == Py_None)
    {
        fd = ioth_msocket(((struct stack_object*)stack)->stack, family, type, proto);
        if(fd == -1)
        {
            PyErr_SetFromErrno(PyExc_OSError);
            return -1;
        }
    }
    /* Create a socket from an existing file descriptor */
    else 
    {
        if (PyFloat_Check(fdobj)) {
            PyErr_SetString(PyExc_TypeError, "integer argument expected, got float");
            return -1;
        }

        fd = PyLong_AsLong(fdobj);
        if (PyErr_Occurred())
            return -1;
        if (fd == -1) {
            PyErr_SetString(PyExc_ValueError, "invalid file descriptor");
            return -1;
        }
    }
    
    if (init_sockobject(s, stack, fd, family, type, proto) == -1) {
        ioth_close(fd);
        return -1;
    }

    return 0;
}

static PyObject*
socket_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *new;
    new = type->tp_alloc(type, 0);

    if (new != NULL) {
        socket_object* s = (socket_object*)new;
        s->fd = -1;
        s->sock_timeout = _PyTime_FromSeconds(-1);
        s->stack = NULL;
    }
    
    return new;
}

static void
socket_finalize(socket_object* s)
{
    PyObject *error_type, *error_value, *error_traceback;
    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    Py_XDECREF(s->stack);
    if (s->fd != -1) {
        ioth_close(s->fd);
        s->fd = -1;
    }

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);
}

 
/* sock_object members */
static PyMemberDef socket_memberlist[] = {
       {"family", T_INT, offsetof(socket_object, family), READONLY, "the socket family"},
       {"type", T_INT, offsetof(socket_object, type), READONLY, "the socket type"},
       {"proto", T_INT, offsetof(socket_object, proto), READONLY, "the socket protocol"},
       {"stack", T_OBJECT_EX, offsetof(socket_object, stack), READONLY, "the stack of the socket"},
       {0},
};


PyDoc_STRVAR(socket_doc, "Test documentation for MSocketBase type");

PyTypeObject socket_type = {
    PyVarObject_HEAD_INIT(0, 0)    /* Must fill in type value later */
    "_iothpy.MSocketBase",                    /* tp_name */
    sizeof(socket_object),                      /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)socket_dealloc,                 /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)socket_repr,                      /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    socket_doc,                                 /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    socket_methods,                             /* tp_methods */
    socket_memberlist,                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    socket_initobj,                             /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    socket_new,                                 /* tp_new */
    PyObject_Del,                               /* tp_free */
    0,                                          /* tp_is_gc */
    0,                                          /* tp_bases */
    0,                                          /* tp_mro */
    0,                                          /* tp_cache */
    0,                                          /* tp_subclasses */
    0,                                          /* tp_weaklist */
    0,                                          /* tp_del */
    0,                                          /* tp_version_tag */
    (destructor)socket_finalize,                /* tp_finalize */
};

