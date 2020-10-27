#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define _GNU_SOURCE
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

static PyObject *
pycox_test(PyObject *self, PyObject *args)
{
    return PyUnicode_FromString("Hello from pycox!");
}

static PyMethodDef pycox_methods[] = {
    {"test",  pycox_test, METH_VARARGS, "Returns a test string"},
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
    struct picox *mystack = picox_newstack(NULL);

    return PyModule_Create(&pycox_module);
}
