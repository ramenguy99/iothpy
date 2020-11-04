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

typedef struct stack_object {
    PyObject_HEAD
    struct picox* stack;
} stack_object;

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
        /* TODO: delstack gives a segfault, probably a bug in picoxnet  */
        /* picox_delstack(self->stack); */
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

PyDoc_STRVAR(pycox_msocket_doc, "pycox_msocket(int domain, int type, int protocol)\n\
\n\
create a pycox socket\n\
return a file descriptor");


static int
stack_pycox_msocket(stack_object* self, PyObject *args, PyObject *kwds)
{
    int domain;
    int type;
    int protocol;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    // static char *kwlist = {"domain", "type", "protocol"};

    // if(!PyArg_ParseTupleAndKeywords(args, kwds, "iii", kwlist, &domain, &type, &protocol))
    //     return 0;
    
    if(!PyArg_ParseTuple(args, "iii", &domain, &type, &protocol))
        return -1;

    int fd = 0;

    printf("%d, %d, %d", domain, type, protocol);
    fd = picox_msocket(self->stack, domain, type, protocol);

    return fd;

}


static PyMethodDef stack_methods[] = {
    {"getstack", (PyCFunction)stack_getstack, METH_NOARGS, getstack_doc},
    
    {"if_nameindex", (PyCFunction)stack_if_nameindex, METH_NOARGS, if_nameindex_doc},
    {"if_nametoindex", (PyCFunction)stack_if_nametoindex, METH_VARARGS, if_nametoindex_doc},
    {"if_indextoname", (PyCFunction)stack_if_indextoname, METH_O, if_indextoname_doc},

    {"ipaddr_add", (PyCFunction)stack_ipaddr_add, METH_VARARGS, ipaddr_add_doc},

    {"pycox_msocket", (PyCFunction)stack_pycox_msocket, METH_VARARGS | METH_KEYWORDS, pycox_msocket_doc},


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
    .tp_name = "_pycoxnet.stack",                             /* tp_name */
    .tp_basicsize = sizeof(stack_object),                       /* tp_basicsize */                                          /* tp_itemsize */
    .tp_dealloc = (destructor)stack_dealloc,                  /* tp_dealloc */
    .tp_repr = (reprfunc)stack_repr,                       /* tp_repr */
    .tp_str = (reprfunc)stack_str,                        /* tp_str */
    .tp_setattro =PyObject_GenericGetAttr,                    /* tp_getattro */
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    .tp_doc = stack_doc,                                  /* tp_doc */
    .tp_methods = stack_methods,                              /* tp_methods */
    .tp_init = stack_initobj,                              /* tp_init */
    .tp_alloc = PyType_GenericAlloc,                        /* tp_alloc */
    .tp_new = stack_new,                                  /* tp_new */
    .tp_free = PyObject_Del,                               /* tp_free */
    .tp_finalize = (destructor)stack_finalize,                 /* tp_finalize */
};

PyMODINIT_FUNC
PyInit__pycoxnet(void)
{
    Py_TYPE(&stack_type) = &PyType_Type;

    PyObject* module = PyModule_Create(&pycox_module);

    /* Add a symbol for the stack type */
    Py_INCREF((PyObject *)&stack_type);
    if(PyModule_AddObject(module, "stack", (PyObject*)&stack_type) != 0) {
        return NULL;
    }

    return module;
}
