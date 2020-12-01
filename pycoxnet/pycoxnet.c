#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


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

typedef struct stack_object {
    PyObject_HEAD
    struct ioth* stack;
} stack_object;

typedef struct socket_object 
{
    PyObject_HEAD
    /* 
        Python object representing the stack to which the socket belongs 
        The socket increses the reference count of the stack on creation
        and decreases it when closed to make sure the stack is not freed
        before the socket is closed.
    */
    PyObject* stack;

    /* File descriptor for the socket*/
    int fd;

    /* Socket properties */
    int family;
    int type;
    int proto;
} socket_object;


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

static PyObject* new_socket_from_fd(stack_object* stack, int family, int type, int proto, int fd);

static PyObject *
sock_accept(PyObject *self, PyObject* unused_args)
{
    socket_object* s = (socket_object*)self;


    struct sockaddr_storage addrbuf;
    socklen_t addrlen;

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

    // printf("after buf\n");
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

    struct sockaddr_in *sin = (struct sockaddr_in*)&addrbuf;
    unsigned char *ip = (unsigned char*)&sin->sin_addr.s_addr;
    printf("c ip : %d %d %d %d\n", ip[0], ip[1], ip[2], ip[3]);
    
    PyObject* addr = make_sockaddr((struct sockaddr*)&addrbuf, addrlen);
    if(addr == NULL){
        PyErr_SetString(PyExc_Exception, "failed to read from socket");
        goto finally;
    }
    
    if (outlen != recvlen) {
        /* We did not read as many bytes as we anticipated, resize the
           string if possible and be successful. */
           printf("before resize\n");
        if (_PyBytes_Resize(&buf, outlen) < 0)
            /* Oopsy, not so successful after all. */
            goto finally;
    }


    ret = PyTuple_Pack(2, buf, addr);
    printf("after pack\n");

finally:
    printf("before decref\n");
    Py_XDECREF(buf);
    Py_XDECREF(addr);
    printf("after decref\n");
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
 
static PyTypeObject socket_type = {
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
    0,                          /* tp_members */
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

static void 
stack_dealloc(stack_object* self)
{
    if(PyObject_CallFinalizerFromDealloc((PyObject*)self) < 0) {
        return;
    }

    PyTypeObject* tp = Py_TYPE(self);
    tp->tp_free(self);
}

static void 
stack_finalize(stack_object* self)
{
    PyObject *error_type, *error_value, *error_traceback;

    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    /* Delete the ioth network stack */
    if(self->stack) {
        /*ioth_delstack(self->stack);*/
    }

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);
    
}

static PyObject* 
stack_repr(stack_object* self)
{
    return PyUnicode_FromFormat("<stack ojbect, stack=%p>", self->stack);
}

static PyObject*
stack_str(stack_object* self)
{
    /* TODO: Would be cool to print network interfaces here */
    return PyUnicode_FromFormat("Picoxnet stack: %p", self->stack);
}

static PyObject*
stack_new(PyTypeObject* type, PyObject* args, PyObject *kwargs)
{
    PyObject* new = type->tp_alloc(type, 0);

    stack_object* self = (stack_object*)new;
    if(self != NULL) {
        self->stack = NULL;
    }

   return new;
}

static int
stack_initobj(PyObject* self, PyObject* args, PyObject* kwds)
{
    stack_object* s = (stack_object*)self;
    
    char* stack_name = NULL;
    char* vdeurl = NULL;
    
    /* Parse an optional string */
    if(!PyArg_ParseTuple(args, "s|s", &stack_name, &vdeurl)) {
        return -1;
    }

    //Transform the vde url in something like vde0=vde:///tmp/mysw
    //only if the stack is picox
    char buf[1024];
    if(vdeurl && strcmp(stack_name, "picox") == 0)
    {
        snprintf(buf, sizeof(buf), "vde0=%s", vdeurl);
        vdeurl = buf;
    }

    s->stack = ioth_newstacki(stack_name, vdeurl);

    return 0;
}


PyDoc_STRVAR(getstack_doc, "Test doc for getstack");

static PyObject* 
stack_getstack(stack_object* self)
{
    return PyLong_FromVoidPtr(self->stack);
}

PyDoc_STRVAR(if_nameindex_doc, "if_nameindex()\n\
\n\
Returns a list of network interface information (index, name) tuples.");

static PyObject*
stack_if_nameindex(stack_object* self)
{
    /* nlinline missing support for if_nameindex */
#if 1
    PyErr_SetNone(PyExc_NotImplementedError);
    return NULL;
#else
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    PyObject* list = PyList_New(0);
    if(!list) 
        return NULL;


    struct ioth_if_nameindex *ni = ioth_if_nameindex(self->stack);
    if(!ni) {
        Py_DECREF(list);
        PyErr_SetString(PyExc_Exception, "Unable to retrieve interfaces");
        return NULL;
    }

    for (int i = 0; ni[i].if_index != 0 && i < INT_MAX; i++)
    {
        PyObject *ni_tuple = Py_BuildValue("IO&", 
            ni[i].if_index, PyUnicode_DecodeFSDefault, ni[i].if_name);
        if(!ni_tuple || PyList_Append(list, ni_tuple) == -1) {
            Py_XDECREF(ni_tuple);
            Py_DECREF(list);
            ioth_if_freenameindex(self->stack, ni);
            return NULL;
        }
        Py_DECREF(ni_tuple);
    }

    ioth_if_freenameindex(self->stack, ni);

    return list;
#endif
}


PyDoc_STRVAR(if_nametoindex_doc, "if_nametoindex(if_name)\n\
\n\
Returns the interface index corresponding to the interface name if_name.");

static PyObject*
stack_if_nametoindex(stack_object* self, PyObject* args)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    PyObject* oname;
    if(!PyArg_ParseTuple(args, "O&:if_nametoindex", PyUnicode_FSConverter, &oname))
        return NULL;

    unsigned long index = ioth_if_nametoindex(self->stack, PyBytes_AS_STRING(oname));
    Py_DECREF(oname);

    // TODO: nlinline returns -1 on error instead of 0 (not in line with the man pages)
    if(index == -1) {
        PyErr_SetString(PyExc_Exception, "no interface with this name");
        return NULL;
    }

    return PyLong_FromUnsignedLong(index);
}


PyDoc_STRVAR(if_indextoname_doc, "if_indextoname(if_index)\n\
\n\
Returns the interface name corresponding to the interface index if_index.");

static PyObject*
stack_if_indextoname(stack_object* self, PyObject* arg)
{
    /* nlinline missing support for if_indextoname */
#if 1
    PyErr_SetNone(PyExc_NotImplementedError);
    return NULL;
#else
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    unsigned long index = PyLong_AsUnsignedLong(arg);
    if(PyErr_Occurred())
        return NULL;
    
    char name[IF_NAMESIZE + 1];
    if(ioth_indextoname(self->stack, index, name) == NULL)
    {
        PyErr_SetString(PyExc_Exception, "no interface with this index");
        return NULL;
    }

    return PyUnicode_DecodeFSDefault(name);
#endif
}


PyDoc_STRVAR(linksetupdown_doc, "linksetupdown(index, up_down)\n\
\n\
Turn the interface at the specified index up (updown == True) or down (updown == False)");

static PyObject*
stack_linksetupdown(stack_object* self, PyObject* args)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    int index, updown;
    if(!PyArg_ParseTuple(args, "ip", &index, &updown))
        return NULL;

    int res = ioth_linksetupdown(self->stack, index, updown);

    if(res == -1) {
        PyErr_SetString(PyExc_Exception, "no interface with this name");
        return NULL;
    }

    Py_RETURN_NONE;
}


static int
check_ip_buffer(int family, Py_buffer addr)
{
    /* Check that the length of the address matches the family */
    if (family == AF_INET) {
        if (addr.len != sizeof(struct in_addr)) {
            return 0;
        }
    } else if (family == AF_INET6) {
        if (addr.len != sizeof(struct in6_addr)) {
            return 0;
        }
    } else {
        return 0;
    }

    return 1;
}

PyDoc_STRVAR(iproute_add_doc, "iproute_add(family, dst_addr, dst_prefixlen, gw_addr)\n\
\n\
Add a static route to dst_addr/dst_prefixlen network through the gateway gw_addr. If dst_addr == None it adds a default route");


static PyObject*
stack_iproute_add(stack_object* self, PyObject* args)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    int family;
    PyObject* dst_addr_obj;
    Py_buffer dst_addr, gw_addr;
    int prefix_len;

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "iOiy*:ipaddr_add", &family, &dst_addr_obj, &prefix_len, &gw_addr)) {
        return NULL;
    }

    /* Check family */
    if(family != AF_INET && family != AF_INET6) {
        PyErr_Format(PyExc_ValueError, "unknown address family %d", family);
        PyBuffer_Release(&gw_addr);
        return NULL;
    }

    /* Check gw address length */
    if(!check_ip_buffer(family, gw_addr)) {
       PyErr_SetString(PyExc_ValueError, "invalid length of dst_addr");
       PyBuffer_Release(&gw_addr);
       return NULL;
    }

    /* Fill dst_addr if not None */
    dst_addr.buf = 0;
    if(dst_addr_obj != Py_None) {
        if (PyObject_GetBuffer(dst_addr_obj, &dst_addr, PyBUF_SIMPLE) != 0) {
            PyErr_SetString(PyExc_Exception, "dst_addr must be bytes-like object or None");
            PyBuffer_Release(&gw_addr);
            return NULL;
        }
        if (!PyBuffer_IsContiguous(&dst_addr, 'C')) {
            PyErr_SetString(PyExc_Exception, "dst_addr must be a contiguous buffer");
            PyBuffer_Release(&dst_addr);
            PyBuffer_Release(&gw_addr);
            return NULL;
        }

        /* Check dest address length */
        if(!check_ip_buffer(family, dst_addr)) {
           PyErr_SetString(PyExc_ValueError, "invalid length of dst_addr");
           PyBuffer_Release(&dst_addr);
           PyBuffer_Release(&gw_addr);
           return NULL;
        }
    }

    if(ioth_iproute_add(self->stack, family, dst_addr.buf, prefix_len, gw_addr.buf) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add ip route to interface");
        if(dst_addr.buf)
            PyBuffer_Release(&dst_addr);
        PyBuffer_Release(&gw_addr);
        return NULL;       
    }

    if(dst_addr.buf)
        PyBuffer_Release(&dst_addr);
    PyBuffer_Release(&gw_addr);

    Py_RETURN_NONE;
}

PyDoc_STRVAR(ipaddr_add_doc, "ipaddr_add(family, addr, prefix_len, if_index)\n\
\n\
Add an IP address to the interface if_index.\n\
Supports IPv4 (family == AF_INET) and IPv6 (family == AF_INET6)");

static PyObject*
stack_ipaddr_add(stack_object* self, PyObject* args)
{
    int af;
    Py_buffer packed_ip;
    int prefix_len;
    int if_index;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "iy*ii:ipaddr_add", &af, &packed_ip, &prefix_len, &if_index)) {
        return NULL;
    }
   
    /* Check family */
    if(af != AF_INET && af != AF_INET6) {
        PyErr_Format(PyExc_ValueError, "unknown address family %d", af);
        PyBuffer_Release(&packed_ip);
        return NULL;
    }

    /* Check address length */
    if(!check_ip_buffer(af, packed_ip)) {
       PyErr_SetString(PyExc_ValueError, "invalid length of ip address");
       PyBuffer_Release(&packed_ip);
       return NULL;
    }

    if(ioth_ipaddr_add(self->stack, af, packed_ip.buf, prefix_len, if_index) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add ip address to interface");
        PyBuffer_Release(&packed_ip);
        return NULL;
    }

    PyBuffer_Release(&packed_ip);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(stack_socket_doc, "create a new socket for the network stack");

static PyObject *
stack_socket(stack_object* self, PyObject *args, PyObject *kwds)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    // Parse keyword arguments the same way Python does it
    static char *keywords[] = {"family", "type", "proto", 0};
    int family = AF_INET;
    int type = SOCK_STREAM;
    int proto = 0;
    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|iii:socket", keywords, &family, &type, &proto))
        return NULL;

    // Prepare arguments for the socket constructor
    PyObject* socket_args = Py_BuildValue("Oiii", (PyObject*)self, family, type, proto);
    if(!socket_args) {
        return NULL;
    }

    // Instantiate a socket by calling the constructor of the socket type
    PyObject* socket = PyObject_CallObject((PyObject*)&socket_type, socket_args);

    // Release arguments
    Py_DECREF(socket_args);

    return socket;
}


static PyMethodDef stack_methods[] = {
    {"getstack", (PyCFunction)stack_getstack, METH_NOARGS, getstack_doc},
    
    {"if_nameindex", (PyCFunction)stack_if_nameindex, METH_NOARGS, if_nameindex_doc},
    {"if_nametoindex", (PyCFunction)stack_if_nametoindex, METH_VARARGS, if_nametoindex_doc},
    {"if_indextoname", (PyCFunction)stack_if_indextoname, METH_O, if_indextoname_doc},

    {"linksetupdown", (PyCFunction)stack_linksetupdown, METH_VARARGS, linksetupdown_doc},
    {"ipaddr_add", (PyCFunction)stack_ipaddr_add, METH_VARARGS, ipaddr_add_doc},
    {"iproute_add", (PyCFunction)stack_iproute_add, METH_VARARGS, iproute_add_doc},
    {"socket", (PyCFunction)stack_socket, METH_VARARGS | METH_KEYWORDS, stack_socket_doc},


    {NULL, NULL} /* sentinel */
};

PyDoc_STRVAR(stack_doc,
"stack(vdeurl=None) -> stack object\n\
\n\
Create a stack with no interfaces or with one interface named vde0 and connected to vdeurl if specified\n\
\n\
Methods of stack objects:\n\
getstack() -- return the pointer to the network stack\n\
");

static PyTypeObject stack_type = {
  PyVarObject_HEAD_INIT(0, 0)                 /* Must fill in type value later */
    "_pycoxnet.stack",                             /* tp_name */
    sizeof(stack_object),                       /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)stack_dealloc,                  /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)stack_repr,                       /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    (reprfunc)stack_str,                        /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    stack_doc,                                  /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    stack_methods,                              /* tp_methods */

    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    stack_initobj,                              /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    stack_new,                                  /* tp_new */
    PyObject_Del,                               /* tp_free */
    0,                                          /* tp_is_gc */
    0,                                          /* tp_bases */
    0,                                          /* tp_mro */
    0,                                          /* tp_cache */
    0,                                          /* tp_subclasses */
    0,                                          /* tp_weaklist */
    0,                                          /* tp_del */
    0,                                          /* tp_version_tag */
    (destructor)stack_finalize,                 /* tp_finalize */
};

static PyMethodDef pycox_methods[] = {
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef pycox_module = {
    PyModuleDef_HEAD_INIT,
    "_pycoxnet",   /* name of module */
    NULL,          /* module documentation, may be NULL */
    -1,            /* size of per-interpreter state of the module,
                      or -1 if the module keeps state in global variables. */
    pycox_methods
};

PyMODINIT_FUNC
PyInit__pycoxnet(void)
{
    Py_TYPE(&stack_type) = &PyType_Type;
    Py_TYPE(&socket_type) = &PyType_Type;

    PyObject* module = PyModule_Create(&pycox_module);

    /* Add a symbol for the stack type */
    Py_INCREF((PyObject *)&stack_type);
    if(PyModule_AddObject(module, "stack", (PyObject*)&stack_type) != 0) {
        return NULL;
    }

    return module;
}
