#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pycoxnet_stack.h"
#include "pycoxnet_socket.h"

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


/* Python API to getting and setting the default timeout value. */
static PyObject *
socket_getdefaulttimeout(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    if (defaulttimeout < 0) {
        Py_RETURN_NONE;
    }
    else {
        double seconds = _PyTime_AsSecondsDouble(defaulttimeout);
        return PyFloat_FromDouble(seconds);
    }
}

PyDoc_STRVAR(getdefaulttimeout_doc,
"getdefaulttimeout() -> timeout\n\
\n\
Returns the default timeout in seconds (float) for new socket objects.\n\
A value of None indicates that new socket objects have no timeout.\n\
When the socket module is first imported, the default is None.");

static PyObject *
socket_setdefaulttimeout(PyObject *self, PyObject *arg)
{
    _PyTime_t timeout;

    if (socket_parse_timeout(&timeout, arg) < 0)
        return NULL;

    defaulttimeout = timeout;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(setdefaulttimeout_doc,
"setdefaulttimeout(timeout)\n\
\n\
Set the default timeout in seconds (float) for new socket objects.\n\
A value of None indicates that new socket objects have no timeout.\n\
When the socket module is first imported, the default is None.");


static PyMethodDef pycox_methods[] = {
    {"getdefaulttimeout",  socket_getdefaulttimeout, METH_NOARGS, getdefaulttimeout_doc},
    {"setdefaulttimeout",  socket_setdefaulttimeout, METH_O, setdefaulttimeout_doc},    
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

    socket_timeout = PyErr_NewException("socket.timeout",
                                        PyExc_OSError, NULL);
    if (socket_timeout == NULL)
        return NULL;
    Py_INCREF(socket_timeout);
    PyModule_AddObject(module, "timeout", socket_timeout);

    /* Add a symbol for the stack type */
    Py_INCREF((PyObject *)&stack_type);
    if(PyModule_AddObject(module, "stack_base", (PyObject*)&stack_type) != 0) {
        return NULL;
    }

    /* Add a symbol for the socket type */
    Py_INCREF((PyObject *)&socket_type);
    if (PyModule_AddObject(module, "SocketType",
                           (PyObject *)&socket_type) != 0)
        return NULL;

    /* Add a symbol for the socket type */
    Py_INCREF((PyObject *)&socket_type);
    if (PyModule_AddObject(module, "socket_base",
                           (PyObject *)&socket_type) != 0)
        return NULL;

 
    return module;
}
