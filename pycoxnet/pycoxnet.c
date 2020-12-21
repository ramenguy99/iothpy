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
