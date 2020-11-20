#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdint.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <picoxnet.h>
#include <nlinline+.h>
NLINLINE_LIBMULTI(picox_)

typedef struct stack_object {
    PyObject_HEAD
    struct picox* stack;
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

//Socket methods
static PyObject *
sock_bind(PyObject *self, PyObject *args)//funziona solo con "" come indirizzo di bind
{
    struct sockaddr_in servaddr;
    int fd;
    int type;
    const char *addr;
    unsigned int port;
    int address;

    if(!PyArg_ParseTuple(args, "iiis", &fd, &type, &port, &addr))
        return NULL;
    
    servaddr.sin_family = type;
    servaddr.sin_port = htons(port);

    if(strcmp(addr, "") == 0)
        address = INADDR_ANY;

    printf("address %d", address);
    servaddr.sin_addr.s_addr = htonl(address);

    int bind = picox_bind(fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    return PyLong_FromUnsignedLong(bind);
}

static PyObject *
sock_listen(PyObject *self, PyObject *args)
{
    int fd;
    int backlog;

    if(!PyArg_ParseTuple(args, "ii", &fd, &backlog))
        return NULL;
    
    return PyLong_FromUnsignedLong(picox_listen(fd, backlog));
}

static PyObject *
sock_accept(PyObject *self, PyObject *args)
{
    int fd;
    int port;
    int address;

    if(!PyArg_ParseTuple(args, "iii", &fd, &port, &address))
        return NULL;

    int n = picox_accept(fd, NULL, NULL);

    return PyLong_FromUnsignedLong(n); //wired
    
}

static PyObject *
sock_recv(PyObject *self, PyObject *args)
{
    int fd;
    int size;

    if(!PyArg_ParseTuple(args, "ii", &fd, &size))
        return NULL;

    PyObject *buf = PyBytes_FromStringAndSize(NULL, size);
    int n;

    if((n = picox_read(fd, PyBytes_AsString(buf), size)) <= 0) {
        PyErr_SetString(PyExc_Exception, "failed to read from socket");
        return NULL;
    }

    if(n != size) {
        //Resize the buffer since we read less bytes than expected
        _PyBytes_Resize(&buf, n);
    }

    return buf;
}

static PyObject *
sock_send(PyObject *self, PyObject *args) 
{
    int fd;
    const char *message;
    Py_buffer buf;

    if(!PyArg_ParseTuple(args, "iy*", &fd, &buf))
        return NULL;
    
    return PyLong_FromUnsignedLong(picox_write(fd, buf.buf, buf.len));
}



static PyObject *
sock_close(PyObject *self, PyObject *args)
{
    socket_object* s = (socket_object*)self;
    if(s->fd != -1)
    {
        int res = picox_close(s->fd);
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

    char* ip_addr_string;
    int port;

    if (!PyTuple_Check(args)) 
    {
        PyErr_Format(PyExc_TypeError, "connect(): argument must be tuple (host, port) not %.500s", Py_TYPE(args)->tp_name);
        return 0;
    }

    if (!PyArg_ParseTuple(args, "si;AF_INET address must be a pair (host, port)",
                          &ip_addr_string, &port))
    {
        if (PyErr_ExceptionMatches(PyExc_OverflowError)) 
        {
            PyErr_Format(PyExc_OverflowError, "connect(): port must be 0-65535");
        }
        return 0;
    }

    if (port < 0 || port > 0xffff) {
        PyErr_Format(PyExc_OverflowError, "connect(): port must be 0-65535");
        return 0;
    }

   // const char* address;
    switch (s->family) {
        case AF_INET:
        {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            if(inet_pton(AF_INET, ip_addr_string, &addr.sin_addr) != 1) 
            {
                PyErr_SetString(PyExc_ValueError, "invalid ip address");
                return 0;
            }

            int res;
            Py_BEGIN_ALLOW_THREADS
            res = picox_connect(s->fd, &addr, sizeof(addr));
            Py_END_ALLOW_THREADS

            if(res != 0) {
                PyErr_SetFromErrno(PyExc_OSError);
                return 0;
            }
        } break;

        case AF_INET6:
        {
            struct sockaddr_in6 addr;
            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(port);

            if(inet_pton(AF_INET6, ip_addr_string, &addr.sin6_addr) != 1) 
            {
                PyErr_SetString(PyExc_ValueError, "invalid ip address");
                return 0;
            }

            int res;
            Py_BEGIN_ALLOW_THREADS
            res = picox_connect(s->fd, &addr, sizeof(addr));
            Py_END_ALLOW_THREADS

            if(res != 0) {
                PyErr_SetFromErrno(PyExc_OSError);
                return 0;
            }
        } break;

        default:
        {
        } break;
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
    {"send",    sock_send,    METH_VARARGS, "send string to socket indentified by fd"}, 

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

    if(!PyArg_ParseTuple(args, "Oiii", &s->stack, &s->family, &s->type, &s->proto))
        return -1;

    Py_INCREF(s->stack);

    s->fd = picox_msocket(((struct stack_object*)s->stack)->stack, s->family, s->type, s->proto);
    if(s->fd == -1)
    {
        PyErr_SetFromErrno(PyExc_OSError);
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
        picox_close(s->fd);
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

    /* Delete the picox network stack */
    if(self->stack) {
        /*picox_delstack(self->stack);*/
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
    
    char* vdeurl = NULL;
    
    /* Parse an optional string */
    if(!PyArg_ParseTuple(args, "|s", &vdeurl)) {
        return -1;
    }

    s->stack = picox_newstack(vdeurl);

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


    struct picox_if_nameindex *ni = picox_if_nameindex(self->stack);
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
            picox_if_freenameindex(self->stack, ni);
            return NULL;
        }
        Py_DECREF(ni_tuple);
    }

    picox_if_freenameindex(self->stack, ni);

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

    unsigned long index = picox_if_nametoindex(self->stack, PyBytes_AS_STRING(oname));
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
    if(picox_indextoname(self->stack, index, name) == NULL)
    {
        PyErr_SetString(PyExc_Exception, "no interface with this index");
        return NULL;
    }

    return PyUnicode_DecodeFSDefault(name);
#endif
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

    /* Check that the length of the address matches the family */
    if (af == AF_INET) {
        if (packed_ip.len != sizeof(struct in_addr)) {
            PyErr_SetString(PyExc_ValueError, "invalid length of packed IP address string");
            PyBuffer_Release(&packed_ip);
            return NULL;
        }
    } else if (af == AF_INET6) {
        if (packed_ip.len != sizeof(struct in6_addr)) {
            PyErr_SetString(PyExc_ValueError, "invalid length of packed IP address string");
            PyBuffer_Release(&packed_ip);
            return NULL;
        }
    } else {
        PyErr_Format(PyExc_ValueError, "unknown address family %d", af);
        PyBuffer_Release(&packed_ip);
        return NULL;
    }

    if(picox_ipaddr_add(self->stack, af, packed_ip.buf, prefix_len, if_index) < 0) {
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

    {"ipaddr_add", (PyCFunction)stack_ipaddr_add, METH_VARARGS, ipaddr_add_doc},

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
