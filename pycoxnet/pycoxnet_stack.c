#include "pycoxnet_stack.h"
#include "pycoxnet_socket.h"

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
    return PyUnicode_FromFormat("ioth stack: %p", self->stack);
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
    
    const char** urls = NULL;
    const char* single_url_buf[2];
    const char** multi_url_buf = NULL;
    PyObject* list = NULL;

    /* Parse an optional string */
    if(PyArg_ParseTuple(args, "s|s", &stack_name, single_url_buf)) 
    {
        single_url_buf[1] = 0;
        urls = single_url_buf;
    } 
    else 
    {
        /* If not a string we expect a list of strings */
        PyErr_Clear();
        if(!PyArg_ParseTuple(args, "s|O;first argument must be a string and second an optional string or list of strings", &stack_name, &list))
            return -1;

        char* argument_error = "Second argument must be a list of strings";
        if(!PyList_Check(list)) {
            PyErr_SetString(PyExc_ValueError, argument_error);
            return -1;
        }

        /* Allocate enough space for each string plus the null sentinel */
        Py_ssize_t len = PyList_Size(list);
        multi_url_buf = malloc(sizeof(char*) * (len + 1));
        for(Py_ssize_t i = 0; i < len; i++)
        {
            PyObject* string = PyList_GetItem(list, i);
            if(!PyUnicode_Check(string)) {
                PyErr_SetString(PyExc_ValueError, argument_error);
                return -1;
            }
            const char* url = PyUnicode_AsUTF8(string);
            multi_url_buf[i] = url;
        }
        multi_url_buf[len] = 0;
        urls = multi_url_buf;
    }

    s->stack = ioth_newstackv(stack_name, urls);
    free(multi_url_buf);
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

/* 
    Parse arguments for iproute functions, arguments must not be null.
    Returns 1 on success and 0 on failure raising an error. 
    On success dst_addr and gw_addr must be freed using PyBuffer_Release, 
    dst_addr.buf may be null if None was passed as dst_addr in that case 
    it should not be freed.
*/
static int
parse_iproute_args(PyObject* args, int* out_family, Py_buffer* out_dst_addr, int* out_prefix_len, 
                   Py_buffer* out_gw_addr)
{
    int family;
    PyObject* dst_addr_obj;
    Py_buffer dst_addr, gw_addr;
    int prefix_len;

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "iOiy*", &family, &dst_addr_obj, &prefix_len, &gw_addr)) {
        return 0;
    }

    /* Check family */
    if(family != AF_INET && family != AF_INET6) {
        PyErr_Format(PyExc_ValueError, "unknown address family %d", family);
        PyBuffer_Release(&gw_addr);
        return 0;
    }

    /* Check gw address length */
    if(!check_ip_buffer(family, gw_addr)) {
       PyErr_SetString(PyExc_ValueError, "invalid length of dst_addr");
       PyBuffer_Release(&gw_addr);
       return 0;
    }

    /* Fill dst_addr if not None */
    dst_addr.buf = 0;
    if(dst_addr_obj != Py_None) {
        if (PyObject_GetBuffer(dst_addr_obj, &dst_addr, PyBUF_SIMPLE) != 0) {
            PyErr_SetString(PyExc_Exception, "dst_addr must be bytes-like object or None");
            PyBuffer_Release(&gw_addr);
            return 0;
        }
        if (!PyBuffer_IsContiguous(&dst_addr, 'C')) {
            PyErr_SetString(PyExc_Exception, "dst_addr must be a contiguous buffer");
            PyBuffer_Release(&dst_addr);
            PyBuffer_Release(&gw_addr);
            return 0;
        }

        /* Check dest address length */
        if(!check_ip_buffer(family, dst_addr)) {
            PyErr_SetString(PyExc_ValueError, "invalid length of dst_addr");
            PyBuffer_Release(&dst_addr);
            PyBuffer_Release(&gw_addr);
            return 0;
       }
    }

    *out_family = family;
    *out_prefix_len = prefix_len;
    *out_dst_addr = dst_addr;
    *out_gw_addr = gw_addr;

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
    Py_buffer dst_addr, gw_addr;
    int prefix_len;

    if(!parse_iproute_args(args, &family, &dst_addr, &prefix_len, &gw_addr))
        return NULL;

    if(ioth_iproute_add(self->stack, family, dst_addr.buf, prefix_len, gw_addr.buf) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add ip route");
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

PyDoc_STRVAR(iproute_del_doc, "iproute_del(family, dst_addr, dst_prefixlen, gw_addr)\n\
\n\
Remove the static route to dst_addr/dst_prefixlen network through the gateway gw_addr.");

static PyObject*
stack_iproute_del(stack_object* self, PyObject* args)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    int family;
    Py_buffer dst_addr, gw_addr;
    int prefix_len;

    if(!parse_iproute_args(args, &family, &dst_addr, &prefix_len, &gw_addr))
        return NULL;

    if(ioth_iproute_del(self->stack, family, dst_addr.buf, prefix_len, gw_addr.buf) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to remove ip route");
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

PyDoc_STRVAR(ipaddr_del_doc, "ipaddr_del(family, addr, prefix_len, if_index)\n\
\n\
delete an IP address to the interface if_index.\n\
Supports IPv4 (family == AF_INET) and IPv6 (family == AF_INET6)");

static PyObject*
stack_ipaddr_del(stack_object *self, PyObject *args){
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
    if(!PyArg_ParseTuple(args, "iy*ii:ipaddr_del", &af, &packed_ip, &prefix_len, &if_index)) {
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

    if(ioth_ipaddr_del(self->stack, af, packed_ip.buf, prefix_len, if_index) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to delete ip address to interface");
        PyBuffer_Release(&packed_ip);
        return NULL;
    }

    PyBuffer_Release(&packed_ip);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(iplink_add_doc, "iplink_add(ifindex, type, data, ifname)\n\
\n\
This function adds a new link of type type,  named  ifname.  The\n\
value of data depends on the type of link and can be optional. A de‐\n\
fault interface name is assigned if ifname is missing.  The  link  is\n\
created with a given index when ifindex is positive.\n\
\n\
iplink_add can return the (positive)  ifindex  of  the  newly\n\
created  link  when  the  argument ifindex is -1 and the stack supports\n\
this feature.");

static PyObject*
stack_iplink_add(stack_object *self, PyObject *args){
    char* ifname = NULL;
    unsigned int ifindex;
    char* type;
    char* data = NULL;

    int newifindex;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "is|ss:iplink_add", &ifindex, &type, &data, &ifname)) {
        return NULL;
    }

     if((newifindex = ioth_iplink_add(self->stack, ifname, ifindex, type, data)) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add link");
        return NULL;
    }

    return PyLong_FromLong(newifindex);
}

PyDoc_STRVAR(iplink_del_doc," iplink_del(ifname = "", ifindex = 0)\n\
This  function  removes  a  link.  The link to be deleted can be\n\
identified by the named paramenter ifname or by the named parameter (ifindex).  Ei‐\n\
ther  ifindex  can be zero or ifname can be empty. It is possible\n\
to use both ifindex and ifname to identify the  link.  An  error\n\
may occur if the parameters are inconsistent.");

static PyObject*
stack_iplink_del(stack_object *self, PyObject *args, PyObject *kwargs){
    char* ifname = NULL;
    unsigned int ifindex = 0;
    PyObject *retvalue = NULL;

    static char *kwlist[] = {"ifname", "ifindex", NULL};

    PyObject *empty = PyTuple_New(0);

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        goto out;
    }    

    if(empty == NULL){
        PyErr_SetString(PyExc_Exception, "failed to remove link");
        goto out;
    }

    if(!PyArg_ParseTupleAndKeywords(empty, kwargs, "|si", kwlist, &ifname, &ifindex)){
        goto out;
    }
    
    if(ifname == NULL && ifindex == 0){
        PyErr_SetString(PyExc_Exception, "failed to remove link empty parameters");
        goto out;
    }

    int ret = 0;
    if((ret = ioth_iplink_del(self->stack, ifname, ifindex))<0){
        PyErr_SetString(PyExc_Exception, "failed to remove link");
        goto out;
    }

    retvalue = PyLong_FromLong(ret);

    out:
        Py_XDECREF(empty);
        return retvalue;
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
    {"ipaddr_del", (PyCFunction)stack_ipaddr_del, METH_VARARGS, ipaddr_del_doc},
    {"iplink_add", (PyCFunction)stack_iplink_add, METH_VARARGS, iplink_add_doc},
    {"iplink_del", (PyCFunction)stack_iplink_del, METH_VARARGS | METH_KEYWORDS, iplink_del_doc},
    {"iproute_add", (PyCFunction)stack_iproute_add, METH_VARARGS, iproute_add_doc},
    {"iproute_del", (PyCFunction)stack_iproute_del, METH_VARARGS, iproute_del_doc},
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

PyTypeObject stack_type = {
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

