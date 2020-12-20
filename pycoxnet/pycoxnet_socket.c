#include "pycoxnet_stack.h"
#include "pycoxnet_socket.h"

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
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <libioth.h>

/* Convert IPv4 sockaddr to a Python str. */
static PyObject *
make_ipv4_addr(struct sockaddr_in *addr)
{
    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) == NULL) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    return PyUnicode_FromString(buf);
}

/* Convert IPv6 sockaddr to a Python str. */
static PyObject *
make_ipv6_addr(struct sockaddr_in6 *addr)
{
    char buf[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf)) == NULL) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    return PyUnicode_FromString(buf);
}

/* Utility to create a tuple representing the given sockaddr suitable
   for passing it back to bind, connect etc.. */
static PyObject *
make_sockaddr(struct sockaddr *addr, size_t addrlen)
{
    if (addrlen == 0) {
        /* No address -- may be recvfrom() from known socket */
        Py_RETURN_NONE;
    }

    switch (addr->sa_family) {
        case AF_INET:
        {
            struct sockaddr_in *a = (struct sockaddr_in *)addr;
            PyObject *addrobj = make_ipv4_addr(a);
            PyObject *ret = NULL;
            if (addrobj) {
                ret = Py_BuildValue("Oi", addrobj, ntohs(a->sin_port));
                Py_DECREF(addrobj);
            }
            return ret; 
        } break;

        case AF_INET6:
        {
            struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
            PyObject *addrobj = make_ipv6_addr(a);
            PyObject *ret = NULL;
            if (addrobj) {
                ret = Py_BuildValue("OiII",
                                    addrobj,
                                    ntohs(a->sin6_port),
                                    ntohl(a->sin6_flowinfo),
                                    a->sin6_scope_id);
                Py_DECREF(addrobj);
            }
            return ret;
        } break;

        default:
        {
            Py_RETURN_NONE;
        } break;
    }
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

//Socket methods
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

static PyObject *
sock_accept(PyObject *self, PyObject* unused_args)
{
    socket_object* s = (socket_object*)self;


    struct sockaddr_storage addrbuf;
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    int connfd;
    Py_BEGIN_ALLOW_THREADS
    connfd = ioth_accept(s->fd, (struct sockaddr*)&addrbuf, &addrlen);
    Py_END_ALLOW_THREADS

    if(connfd == -1) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    PyObject* sock = new_socket_from_fd((stack_object*)s->stack, s->family, s->type, s->proto, connfd);
    if(!sock) {
        return NULL;
    }

    PyObject* addr = make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
    if(!addr) {
        Py_XDECREF(sock);
        return NULL;
    }

    PyObject* res = PyTuple_Pack(2, sock, addr);

    Py_XDECREF(sock);
    Py_XDECREF(addr);

    return res;
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

    Py_BEGIN_ALLOW_THREADS
    outlen = ioth_read(s->fd, PyBytes_AsString(buf), recvlen);
    Py_END_ALLOW_THREADS


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

static PyObject *
sock_send(PyObject *self, PyObject *args) 
{
    socket_object* s = (socket_object*)self;

    Py_buffer buf;
    int flags = 0;

    if(!PyArg_ParseTuple(args, "y*|i:send", &buf, &flags))
        return NULL;

    ssize_t res;
    Py_BEGIN_ALLOW_THREADS
    res = ioth_send(s->fd, buf.buf, buf.len, flags);
    Py_END_ALLOW_THREADS

    if(res == -1) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(sendto_doc,
"sendto(data[, flags], address) -> count\n\
\n\
Like send(data, flags) but allows specifying the destination address.\n\
For IP sockets, the address is a pair (hostaddr, port).");

static PyObject *
sock_sendto(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    Py_buffer pbuf;
    PyObject *addro;
    Py_ssize_t arglen;

    int flags;

    flags = 0;
    arglen = PyTuple_Size(args);

    switch(arglen) {
        case 2:
            if(!PyArg_ParseTuple(args, "y*O:sendto", &pbuf, &addro))
                return NULL;
            break;
        case 3:
            if(!PyArg_ParseTuple(args, "y*iO:sendto", &pbuf, &flags, &addro))
                return NULL;
            break;
        default:
            PyErr_Format(PyExc_TypeError, "sendto() takes 2 or 3 arguments (%zd given)", arglen);
            return NULL;

    }


    struct sockaddr_storage addrbuf;
    socklen_t addrlen;
    if(!get_sockaddr_from_tuple("sendto", s, addro, (struct sockaddr*)&addrbuf, &addrlen))
        return NULL;
    
    ssize_t res;
    Py_BEGIN_ALLOW_THREADS
    res = ioth_sendto(s->fd, pbuf.buf, pbuf.len, flags, (struct sockaddr*)&addrbuf, addrlen);
    Py_END_ALLOW_THREADS

    if(res == -1){
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(recvfrom_doc,
"recvfrom(buffersize[, flags]) -> (data, address info)\n\
\n\
Like recv(buffersize, flags) but also return the sender's address info.");

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

    struct sockaddr_storage addrbuf;
    socklen_t addrlen;

    // if(!getsockaddrlen(s, &addrlen)){
    //     PyErr_SetString(PyExc_Exception, "failed to get sockaddrlen");
    //     Py_XDECREF(buf);
    //     return NULL;
    // }

    addrlen = sizeof(struct sockaddr_storage);
    memset(&addrbuf, 0, sizeof(struct sockaddr_storage));

    Py_BEGIN_ALLOW_THREADS
    outlen = ioth_recvfrom(s->fd, PyBytes_AsString(buf), (int)recvlen, flags, (struct sockaddr*)&addrbuf, &addrlen);
    Py_END_ALLOW_THREADS

    if(outlen < 0) {
        PyErr_SetString(PyExc_Exception, "failed to read from socket");
        goto finally;
    }
    
    PyObject* addr = make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
    if(addr == NULL){
        PyErr_SetString(PyExc_Exception, "failed to read from socket");
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

static PyObject *
sock_close(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    if(s->fd != -1)
    {
        int res = ioth_close(s->fd);
        s->fd = -1;
        if(res < 0 && errno != ECONNRESET) {
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }
    }

    //Return none if no errors
    Py_RETURN_NONE;
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

    int res;
    Py_BEGIN_ALLOW_THREADS
    res = ioth_connect(s->fd, (struct sockaddr*)&addrbuf, addrlen);
    Py_END_ALLOW_THREADS

    if(res != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return 0;
    }

    Py_RETURN_NONE;
}

static PyObject *
sock_fileno(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    return PyLong_FromLong(s->fd);
}

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
static PyMethodDef socket_methods[] = 
{
    {"bind",    sock_bind,    METH_O,       "bind addr"},
    {"close",   sock_close,   METH_NOARGS,  "close socket identified by fd"},
    {"connect", sock_connect, METH_O,       "connect socket identified by fd sin_addr"},
    {"listen",  sock_listen,  METH_VARARGS, "start listen on socket identified by fd"},
    {"accept",  sock_accept,  METH_NOARGS,  "accept connection on socket identified by fd"},
    {"recv",    sock_recv,    METH_VARARGS, "recv size bytes as string from socket indentified by fd"},
    {"recvfrom", sock_recvfrom, METH_VARARGS, recvfrom_doc},
    {"send",    sock_send,    METH_VARARGS, "send string to socket indentified by fd"},  
    {"sendto", sock_sendto, METH_VARARGS, sendto_doc},
    {"fileno",  sock_fileno,    METH_NOARGS, "returns the socket fd"}, 
    {"getsockopt", sock_getsockopt, METH_VARARGS, "get socket option"},
    {"setsockopt", sock_setsockopt, METH_VARARGS, "set socket option"},
    {"shutdown", sock_shutdown, METH_O, "shutdown the socket"},
    {"getsockname", sock_getsockname, METH_NOARGS, "get socket name"},
    {"getpeername", sock_getpeername, METH_NOARGS, "get peer name"},
    {"setblocking", sock_setblocking, METH_O, setblocking_doc},
    {"getblocking", sock_getblocking, METH_NOARGS, getblocking_doc},




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
socket_initobj(PyObject* self, PyObject* args, PyObject* kwds)
{
    socket_object* s = (socket_object*)self;
    s->family = AF_INET;
    s->type = SOCK_STREAM;
    s->proto = 0;

    PyObject* fdobj = NULL;
    int fd = -1;

    if(!PyArg_ParseTuple(args, "Oiii|O", &s->stack, &s->family, &s->type, &s->proto, &fdobj))
        return -1;

    /* Create a new socket */
    if(fdobj == NULL || fdobj == Py_None)
    {
        s->fd = ioth_msocket(((struct stack_object*)s->stack)->stack, s->family, s->type, s->proto);
        if(s->fd == -1)
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

        s->fd = fd;
    }

    Py_INCREF(s->stack);
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
    }
    
    return new;
}

static void
socket_finalize(socket_object* s)
{
    PyObject *error_type, *error_value, *error_traceback;
    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    Py_DECREF(s->stack);
    if (s->fd != -1) {
        ioth_close(s->fd);
        s->fd = -1;
    }

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);
}

PyDoc_STRVAR(socket_doc, "Test documentation for socket type");
 
/* sock_object members */
static PyMemberDef socket_memberlist[] = {
       {"family", T_INT, offsetof(socket_object, family), READONLY, "the socket family"},
       {"type", T_INT, offsetof(socket_object, type), READONLY, "the socket type"},
       {"proto", T_INT, offsetof(socket_object, proto), READONLY, "the socket protocol"},
       {0},
};


PyTypeObject socket_type = {
    PyVarObject_HEAD_INIT(0, 0)         /* Must fill in type value later */
    "_pycoxnet.socket",                         /* tp_name */
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
    0,                          /* tp_getset */
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

