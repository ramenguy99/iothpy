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
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "iothpy_stack.h"
#include "iothpy_socket.h"

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

#include <ioth.h>


PyDoc_STRVAR(iothpy_doc,
"_iothpy c module\n\
\n\
This module defines the base classes MSocketBase and StackBase\n\
used to interface with the ioth c api. \n\
It also defines the functions needed to offer the same interface as\n\
the built-in socket module\n\
");


#ifdef CMSG_LEN

/* Python interface to CMSG_LEN(length). */

static PyObject *
socket_CMSG_LEN(PyObject *self, PyObject *args)
{
    Py_ssize_t length;
    size_t result;

    if (!PyArg_ParseTuple(args, "n:CMSG_LEN", &length))
        return NULL;
    if (length < 0 || !get_CMSG_LEN(length, &result)) {
        PyErr_Format(PyExc_OverflowError, "CMSG_LEN() argument out of range");
        return NULL;
    }
    return PyLong_FromSize_t(result);
}

PyDoc_STRVAR(CMSG_LEN_doc,
"CMSG_LEN(length) -> control message length\n\
\n\
Return the total length, without trailing padding, of an ancillary\n\
data item with associated data of the given length.  This value can\n\
often be used as the buffer size for recvmsg() to receive a single\n\
item of ancillary data, but RFC 3542 requires portable applications to\n\
use CMSG_SPACE() and thus include space for padding, even when the\n\
item will be the last in the buffer.  Raises OverflowError if length\n\
is outside the permissible range of values.");


#ifdef CMSG_SPACE

/* Python interface to CMSG_SPACE(length). */

static PyObject *
socket_CMSG_SPACE(PyObject *self, PyObject *args)
{
    Py_ssize_t length;
    size_t result;

    if (!PyArg_ParseTuple(args, "n:CMSG_SPACE", &length))
        return NULL;
    if (length < 0 || !get_CMSG_SPACE(length, &result)) {
        PyErr_SetString(PyExc_OverflowError,
                        "CMSG_SPACE() argument out of range");
        return NULL;
    }
    return PyLong_FromSize_t(result);
}

PyDoc_STRVAR(CMSG_SPACE_doc,
"CMSG_SPACE(length) -> buffer size\n\
\n\
Return the buffer size needed for recvmsg() to receive an ancillary\n\
data item with associated data of the given length, along with any\n\
trailing padding.  The buffer space needed to receive multiple items\n\
is the sum of the CMSG_SPACE() values for their associated data\n\
lengths.  Raises OverflowError if length is outside the permissible\n\
range of values.");
#endif    /* CMSG_SPACE */
#endif    /* CMSG_LEN */


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


static PyObject *
socket_close(PyObject *self, PyObject *fdobj)
{
    int fd;
    int res;

    fd = PyLong_AsLong(fdobj);
    if (fd == -1 && PyErr_Occurred())
        return NULL;
    
    Py_BEGIN_ALLOW_THREADS
    res = ioth_close(fd);
    Py_END_ALLOW_THREADS

    /* bpo-30319: The peer can already have closed the connection.
       Python ignores ECONNRESET on close(). */
    if (res < 0 && errno != ECONNRESET) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(close_doc,
"close(integer) -> None\n\
\n\
Close an integer socket file descriptor.  This is like os.close(), but for\n\
sockets; on some platforms os.close() won't work for socket file descriptors.");

static PyMethodDef iothpy_methods[] = {
#ifdef CMSG_LEN
    {"CMSG_LEN",   socket_CMSG_LEN, METH_VARARGS, CMSG_LEN_doc},
#ifdef CMSG_SPACE
    {"CMSG_SPACE", socket_CMSG_SPACE, METH_VARARGS, CMSG_SPACE_doc},
#endif
#endif
    {"getdefaulttimeout",  socket_getdefaulttimeout, METH_NOARGS, getdefaulttimeout_doc},
    {"setdefaulttimeout",  socket_setdefaulttimeout, METH_O, setdefaulttimeout_doc},    

    {"close",              socket_close, METH_O, close_doc},

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef iothpy_module = {
    PyModuleDef_HEAD_INIT,
    "_iothpy",   /* name of module */
    iothpy_doc,  /* module documentation, may be NULL */
    -1,            /* size of per-interpreter state of the module,
                      or -1 if the module keeps state in global variables. */
    iothpy_methods
};

PyMODINIT_FUNC
PyInit__iothpy(void)
{ 
#if PY_MINOR_VERSION > 9
    Py_SET_TYPE(&stack_type, &PyType_Type);
    Py_SET_TYPE(&socket_type, &PyType_Type);
#else
    Py_TYPE(&stack_type) = &PyType_Type;
    Py_TYPE(&socket_type) = &PyType_Type;
#endif
    PyObject* module = PyModule_Create(&iothpy_module);

    socket_timeout = PyErr_NewException("_iothpy.timeout",
                                        PyExc_OSError, NULL);
    if (socket_timeout == NULL)
        return NULL;
    Py_INCREF(socket_timeout);
    PyModule_AddObject(module, "timeout", socket_timeout);

    /* Add a symbol for the stack type */
    Py_INCREF((PyObject *)&stack_type);
    if(PyModule_AddObject(module, "StackBase", (PyObject*)&stack_type) != 0) {
        return NULL;
    }

    /* Add a symbol for the socket type */
    Py_INCREF((PyObject *)&socket_type);
    if (PyModule_AddObject(module, "MSocketBase",
                           (PyObject *)&socket_type) != 0)
        return NULL;
    return module;
}
