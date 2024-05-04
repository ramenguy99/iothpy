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
#include "utils.h"
#include "iothpy_stack.h"
#include "iothpy_socket.h"


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
#include <errno.h>

#include <ioth.h>


#define IS_PATH(str) (strchr(str, '/') != NULL)
#define NI_MAXHOST 1025
#define NI_MAXSERV 32

static char* stackName = NULL;

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
        /* This is currently commented because freeing the stack causes a segfault when using picox */
        /* ioth_delstack(self->stack); */
        self->stack = NULL;
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
        self->stack_dns = NULL;
    }

   return new;
}

static int stack_dns_init(stack_object* self, char* config){
    if (config == NULL || IS_PATH(config)){
        self -> stack_dns = iothdns_init(self->stack, config);
    } else{
        self -> stack_dns = iothdns_init_strcfg(self->stack, config);
    }

    if(self->stack_dns == NULL){
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }
    return 0;
}

static int
stack_initobj(PyObject* self, PyObject* args, PyObject* kwargs)
{
    stack_object* s = (stack_object*)self;

    PyObject* vdeurl = NULL;

    char* config_dns = NULL;
    char* stack_name = NULL;

    const char** urls = NULL;
    const char* single_url_buf[2];
    const char** multi_url_buf = NULL;

    if(!PyArg_ParseTuple(args, "s|Oz", &stack_name, &vdeurl, &config_dns)){
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    

    if(stack_dns_init(s, config_dns) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    if(vdeurl == Py_None){
        /*stack interface in configuration string */
        s->stack = ioth_newstackc(stack_name);

        if(!s->stack) {
            PyErr_SetFromErrno(PyExc_OSError);
            return -1;
        }

        //get stack name
        char* stackString = strstr(stack_name, "stack=\0");
        if(stackString){
            strtok(stackString, "=");
            stackName = strtok(NULL, "=");
        }
    }
    else{
        /* check if vde url is a string or a list of strings */
        if(PyUnicode_Check(vdeurl)){
            single_url_buf[0] = PyUnicode_AsUTF8(vdeurl);
            single_url_buf[1] = 0;
            urls = single_url_buf;
        } else if(PyBytes_Check(vdeurl)){
            single_url_buf[0] = PyBytes_AS_STRING(vdeurl);
            single_url_buf[1] = 0;
            urls = single_url_buf;
        }else {
            char* argument_error = "vdeurl argument must be a string or a list of strings";
            if(!PyList_Check(vdeurl)) {
                PyErr_SetString(PyExc_ValueError, argument_error);
                return -1;
            }

            /* Allocate enough space for each string plus the null sentinel */
            Py_ssize_t len = PyList_Size(vdeurl);
            multi_url_buf = malloc(sizeof(char*) * (len + 1));
            for(Py_ssize_t i = 0; i < len; i++)
            {
                PyObject* string = PyList_GetItem(vdeurl, i);
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
        stackName = stack_name;
    }

    if(!s->stack) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    

    return 0;
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
parse_iproute_args(PyObject* args, PyObject* kwargs, int* out_family, char* out_gw_buf,
                   char** out_dst_bufp, int* out_dst_prefix, int* out_if_index) 
{
    int family = -1;
    char* gw_str = NULL;
    char* dst_str = NULL;
    int dst_prefix = 0;
    int if_index = 0;

    static char* kwnames[] = { 
        "", "", /* 2 required positional arguments */
        "dst_addr", "dst_prefix", "ifindex"
    };


    /* Parse arguments */
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "is|sii", kwnames, &family, &gw_str, 
                                    &dst_str,  &dst_prefix, &if_index)) {
        return 0;
    }

    /* Check family */
    if(family != AF_INET && family != AF_INET6) {
        PyErr_Format(PyExc_ValueError, "unknown address family %d", family);
        return 0;
    }

    /* Convert string address to bytes */
    if(inet_pton(family, gw_str, out_gw_buf) < 0) {
        PyErr_SetString(PyExc_ValueError, "invalid gw_addr address string");
        return 0;
    }

    if(dst_str) {
        /* Convert string address to bytes */
        if(inet_pton(family, gw_str, *out_dst_bufp) < 0) {
            PyErr_SetString(PyExc_ValueError, "invalid dst_addr address string");
            return 0;
        }
    } else {
        *out_dst_bufp = NULL;
    }

    *out_family = family;
    *out_dst_prefix = dst_prefix;
    *out_if_index = if_index;

    return 1;
}

PyDoc_STRVAR(iproute_add_doc, "iproute_add(family, gw_addr, dst_addr = None, dst_prefix = 0, ifindex = 0)\n\
\n\
Add a static route to dst_addr/dst_prefixlen network through the gateway gw_addr.\n\
All addresses must be valid IPv4 or IPv6 strings.\n\
If dst_addr == None it adds a default route.\n\
If gw_addr is an IPv6 link local address, ifindex must be specified");

static PyObject*
stack_iproute_add(stack_object* self, PyObject* args, PyObject* kwargs)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    int family;
    char gw_buf[sizeof(struct in6_addr)];
    char dst_buf[sizeof(struct in6_addr)];
    char* dst_bufp = dst_buf;
    int dst_prefix;
    int if_index;

    if(!parse_iproute_args(args, kwargs, &family, gw_buf, &dst_bufp, &dst_prefix, &if_index))
        return NULL;

    if(ioth_iproute_add(self->stack, family, dst_buf, dst_prefix, gw_buf, if_index) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add ip route");
        return NULL;       
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(iproute_del_doc, "iproute_del(family, gw_addr, dst_addr = None, dst_prefix = 0, ifindex = 0)\n\
\n\
Delete the static route to dst_addr/dst_prefixlen network through the gateway gw_addr.\n\
If dst_addr == None it deletes the default route.\n\
If gw_addr is an IPv6 link local address, ifindex must be specified");

static PyObject*
stack_iproute_del(stack_object* self, PyObject* args, PyObject* kwargs)
{
    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }


    int family;
    char gw_buf[sizeof(struct in6_addr)];
    char dst_buf[sizeof(struct in6_addr)];
    char* dst_bufp = dst_buf;
    int dst_prefix;
    int if_index;

    if(!parse_iproute_args(args, kwargs, &family, gw_buf, &dst_bufp, &dst_prefix, &if_index))
        return NULL;

    if(ioth_iproute_del(self->stack, family, dst_buf, dst_prefix, gw_buf, if_index) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to del ip route");
        return NULL;       
    }

    Py_RETURN_NONE;
}


/* 
    Parse arguments to ip_addr_add and ip_addr_del
    Returns 0 and raises an exception if the arguments are invalid
*/
static int
parse_ipaddr_args(PyObject* args, int* out_af, char* out_addr, int* out_prefix_len, int* out_if_index) 
{
    char* addr_str = NULL;

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "isii", out_af, &addr_str, out_prefix_len, out_if_index)) {
        return 0;
    }

    int af = *out_af;
   
    /* Check family */
    if(af != AF_INET && af != AF_INET6) {
        PyErr_Format(PyExc_ValueError, "invalid address family %d", af);
        return 0;
    }

    /* Convert string address to bytes */
    if(inet_pton(af, addr_str, out_addr) < 0) {
        PyErr_SetString(PyExc_ValueError, "invalid address string");
        return 0;
    }

    return 1;
}

PyDoc_STRVAR(ipaddr_add_doc, "ipaddr_add(family, addr, prefix_len, if_index)\n\
\n\
Add an IP address to the interface if_index.\n\
Supports IPv4 (family == AF_INET) and IPv6 (family == AF_INET6),\n\
addr must be a string representing a valid ipv4 or ipv6 address respectively");

static PyObject*
stack_ipaddr_add(stack_object* self, PyObject* args)
{
    int af;
    char* addr_str = NULL;
    int prefix_len;
    int if_index;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    /* Parse arguments */
    char buf[sizeof(struct in6_addr)];
    if(!parse_ipaddr_args(args, &af, buf, &prefix_len, &if_index)) {
        return NULL;
    }

    if(ioth_ipaddr_add(self->stack, af, buf, prefix_len, if_index) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add ip address to interface");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(ipaddr_del_doc, "ipaddr_del(family, addr, prefix_len, if_index)\n\
\n\
Delete an IP address from the interface if_index.\n\
Supports IPv4 (family == AF_INET) and IPv6 (family == AF_INET6),\n\
addr must be a string representing a valid ipv4 or ipv6 address respectively");

static PyObject*
stack_ipaddr_del(stack_object *self, PyObject *args){
    int af;
    char* addr_str = NULL;
    int prefix_len;
    int if_index;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    /* Parse arguments */
    char buf[sizeof(struct in6_addr)];
    if(!parse_ipaddr_args(args, &af, buf, &prefix_len, &if_index)) {
        return NULL;
    }

    if(ioth_ipaddr_del(self->stack, af, buf, prefix_len, if_index) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to delete ip address from interface");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(iplink_add_doc, "iplink_add(ifindex, type, data, ifname)\n\
\n\
This function adds a new link of type type,  named  ifname.  The\n\
value of data depends on the type of link and can be optional. \n\
Data is a list of tuple (tag, tag_data). \n\
A default interface name is assigned if ifname is missing.  The  link  is\n\
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
    PyObject* data = NULL;
    int nifd = - 1;
    int newifindex;
    struct nl_iplink_data* ifd = NULL;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    if(strcmp(stackName, "vdestack") == 0){
        PyErr_SetString(PyExc_Exception, "Operation not supported by vdestack");
        return NULL;
    }

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "is|Os:iplink_add", &ifindex, &type, &data, &ifname)) {
        return NULL;
    }

    if(PyList_Check(data)){
        nifd = (int)PyList_Size(data);
        ifd = (struct nl_iplink_data*) malloc(sizeof(struct nl_iplink_data) * nifd);

        for(int i = 0; i < nifd; i++) {
            PyObject* cur_tuple = PyList_GetItem(data, i);
            if(!PyTuple_Check(cur_tuple)){
                PyErr_SetString(PyExc_ValueError, "Data in list must be tuples");
                return NULL;
            }

            PyObject* tag = PyTuple_GetItem(cur_tuple, 0);
            PyObject* dataptr = PyTuple_GetItem(cur_tuple, 1);

            ifd[i] = (struct nl_iplink_data) {(int) PyLong_AsLong(tag), sizeof(PyLong_AsVoidPtr(dataptr)) + 1, (PyLong_AsVoidPtr(dataptr))};
        }

    } else if(PyTuple_Check(data)){
        nifd = 1;
        ifd = (struct nl_iplink_data*) malloc(sizeof(struct nl_iplink_data) * nifd);

        PyObject* tag = PyTuple_GetItem(data, 0);
        PyObject* dataptr = PyTuple_GetItem(data, 1);

        ifd[0] = (struct nl_iplink_data) {(int) PyLong_AsLong(tag), sizeof(PyLong_AsVoidPtr(dataptr)) + 1, (PyLong_AsVoidPtr(dataptr))};

    } else {
        PyErr_SetString(PyExc_ValueError, "Data must be list or tuple");
        return NULL;
    }

    if((newifindex = ioth_iplink_add(self->stack, ifname, ifindex, type, ifd,  nifd)) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add link");
        return NULL;
    }

    return PyLong_FromLong(newifindex);
}

PyDoc_STRVAR(iplink_add_vde_doc, "iplink_add_vde(ifindex, vnl, ifname)\n\
\n\
This function adds a new link of type vde,  named  ifname.  The\n\
vnl is the name of virtual network locator. \n\
A default interface name is assigned if ifname is missing.  The  link  is\n\
created with a given index when ifindex is positive.\n\
\n\
iplink_add can return the (positive)  ifindex  of  the  newly\n\
created  link  when  the  argument ifindex is -1 and the stack supports\n\
this feature.\n\
It's a simplified version of iplink_add to use for vde vnl.");

static PyObject*
stack_iplink_add_vde(stack_object *self, PyObject *args){
    char* ifname = NULL;
    int ifindex;
    char* vnl = NULL;
    int newifindex;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    if(strcmp(stackName, "vdestack") == 0){
        PyErr_SetString(PyExc_Exception, "Operation not supported by vdestack");
        return NULL;
    }

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "is|s:iplink_add_vde", &ifindex, &vnl, &ifname)) {
        return NULL;
    }

    if((newifindex = ioth_iplink_add(self->stack, ifname, ifindex, "vde", nl_iplink_strdata(IFLA_VDE_VNL, vnl))) < 0) {
        PyErr_SetString(PyExc_Exception, "failed to add link");
        return NULL;
    }

    return PyLong_FromLong(newifindex);
}

PyDoc_STRVAR(iplink_del_doc," iplink_del(ifname = "", ifindex = 0)\n\
This  function  removes  a  link.  The link to be deleted can be\n\
identified by the named paramenter ifname or by the named parameter (ifindex).\n\
Either  ifindex  can be zero or ifname can be empty. It is possible\n\
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

PyDoc_STRVAR(linkgetaddr_doc," linkgetaddr(ifindex)\n\
Returns the MAC address of the interface ifindex as a bytearray of length 6.");

static PyObject*
stack_linkgetaddr(stack_object *self, PyObject *args) {
    int ifindex;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }


    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "i", &ifindex)) {
        return NULL;
    }

    PyObject* buf = PyBytes_FromStringAndSize((char *)NULL, 6);
    if (buf == NULL)
        return NULL;

    int ret = 0;
    if((ret = ioth_linkgetaddr(self->stack, ifindex, (void *)PyBytes_AS_STRING(buf))) < 0){
        PyErr_SetString(PyExc_Exception, "failed to get MAC address");
        return NULL;
    }

    return buf;
}


PyDoc_STRVAR(linksetaddr_doc," _linksetaddr(ifindex, macaddr)\n\
Set the MAC address of the interface ifindex, macaddr must be a bytearray of length 6.");

static PyObject*
stack_linksetaddr(stack_object *self, PyObject *args) {
    int ifindex;
    Py_buffer addr;


    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }


    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "iy*", &ifindex, &addr)) {
        return NULL;
    }

    if(addr.len != 6) {
        PyErr_SetString(PyExc_ValueError, "MAC address must be of 6 bytes");
    }

    int ret = 0;
    if((ret = ioth_linksetaddr(self->stack, ifindex, addr.buf) < 0)) {
        PyErr_SetString(PyExc_Exception, "failed to set MAC address");
        return NULL;
    }

    Py_RETURN_NONE;
}


PyDoc_STRVAR(linksetmtu_doc," linksetmtu(ifindex, mtu)\n\
Set the MTU of the interface ifindex, mtu must be a positive integer");

static PyObject*
stack_linksetmtu(stack_object *self, PyObject *args) {
    int ifindex;
    int mtu;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "uninitialized stack");
        return NULL;
    }


    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "ii", &ifindex, &mtu)) {
        return NULL;
    }

    int ret = 0;
    if((ret = ioth_linksetmtu(self->stack, ifindex, mtu) < 0)) {
        PyErr_SetString(PyExc_Exception, "failed to set MAC address");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(ioth_config_doc, "ioth_config(config)\n\
Configure the stack using the config string. The options supported are:\n\
\n\
    stack=...: (ioth_newstackc only) define the ip stack implementation\n\
    vnl=...: (ioth_newstackc only) define the vde network to join\n\
    iface=... : select the interface e.g. iface=eth0 (default value vde0)\n\
    ifindex=... : id of the interface (it can be used instead of iface)\n\
    fqdn=.... : set the fully qualified domain name for dhcp, dhcpv6 slaac-hash-autoconf\n\
    mac=... : (or macaddr) define the macaddr for eth here below. (e.g. eth,mac=10:a1:b2:c3:d4:e5)\n\
    eth : turn on the interface (and set the MAC address if requested or a hash based MAC address if fqdn is defined)\n\
    dhcp : (or dhcp4 or dhcpv4) use dhcp (IPv4)\n\
    dhcp6 : (or dhcpv6) use dhcpv6 (for IPv6)\n\
    rd : (or rd6) use the router discovery protocol (IPv6)\n\
    slaac : use stateless auto-configuration (IPv6) (requires rd)\n\
    auto : shortcut for eth+dhcp+dhcp6+rd\n\
    auto4 : (or autov4) shortcut for eth+dhcp\n\
    auto6 : (or autov6) shortcut for eth+dhcp6+rd\n\
    ip=..../.. : set a static address IPv4 or IPv6 and its prefix length example: ip=10.0.0.100/24 or ip=2001:760:1:2::100/64\n\
    gw=..... : set a static default route IPv4 or IPv6\n\
    dns=.... : set a static address for a DNS server\n\
    domain=.... : set a static domain for the dns search\n\
    debug : show the status of the current configuration parameters\n\
    -static, -eth, -dhcp, -dhcp6, -rd, -auto, -auto4, -auto6 (and all the synonyms + a heading minus) clean (undo) the configuration\n\
\n\
An error may occur if the parameters are inconsistent.");

static PyObject*
stack_ioth_config(stack_object *self, PyObject *args)
{
    char* config;

    if(!self->stack) 
    {
        PyErr_SetString(PyExc_Exception, "uninitialized stack");
        return NULL;
    }

    /* Parse arguments */
    if(!PyArg_ParseTuple(args, "s", &config)){
        return NULL;
    }

    int res = 0;
    if(res = ioth_config(self->stack, config) < 0){
        PyErr_SetString(PyExc_Exception, "error in configuration. Check config options");
        return NULL;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(ioth_resolvconf_doc, "ioth_resolvconf(config)\n\
Return a configuration string for the domain name resolution library.\n\
The syntax of the configuration file is consistent with resolve.conf\n\
config variable are iface and ifindex. Man iothconf for more info.");

static PyObject*
stack_ioth_resolvconf(stack_object *self, PyObject *args)
{
    char* config = NULL;
    char* resolvConf = NULL;

    /* parse config or set to NULL */
    if(!PyArg_ParseTuple(args, "z", &config)){
        PyErr_SetString(PyExc_Exception, "failed to parse config string");
        return NULL;
    }

    resolvConf = ioth_resolvconf(self->stack, config);

    if (resolvConf == NULL){
        /* check for an error */
        if(errno != 0){
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }
        else{
            PyErr_Clear();
            Py_RETURN_NONE;
        }
    }
    return Py_BuildValue("s", resolvConf);
}

PyDoc_STRVAR(stack_dns_upgrade_doc, "dns_update(config)\n\
config can be path to resolv.conf syntax file or a string\n\
written in same syntax of resolv.conf.");

static PyObject*
stack_dns_upgrade(stack_object* self, PyObject* args){
    char* config = NULL;

    if(self->stack_dns == NULL){
        PyErr_SetString(PyExc_Exception, "Uninitialized dns");
        return NULL;
    }

    if(!PyArg_ParseTuple(args, "s", &config))
        return NULL;
    
    if(IS_PATH(config)){
        if(iothdns_update(self->stack_dns, config) < 0){
            PyErr_SetFromErrno(PyExc_SyntaxError);
            return NULL;
        }
    } else {
        if(iothdns_update_strcfg(self->stack_dns, config) < 0){
            PyErr_SetFromErrno(PyExc_SyntaxError);
            return NULL;
        }
    }
    Py_RETURN_NONE;
}


PyDoc_STRVAR(dns_getaddrinfo_doc,"getaddrinfo(host, port, family=0, type=0, proto=0, flags=0)\n\
host is a domain name, a string representation of an IPv4/v6 address or None.\n\
port is a string service name such as 'http', a numeric port number or None.\n\
The family, type and proto arguments can be optionally specified in order to\n\
narrow the list of addresses returned.\n\
The function returns a list of 5-tuples with the following structure:\n\
\n\
(family, type, proto, canonname, sockaddr)");


static PyObject* dns_getaddrinfo(stack_object* self, PyObject* args, PyObject* kwargs){
    static char* kwnames[] = {"host", "port", "family", "type", "proto", "flags", 0};
    struct addrinfo hints, *res;
    struct addrinfo *resList = NULL;
    char *hoststr, *portstr;
    PyObject* portObj;
    PyObject* portObjStr = NULL;
    PyObject* all = NULL;

    int family, socktype, protocol, flags;
    int error;

    if(self->stack == NULL){
        PyErr_SetString(PyExc_Exception, "Uninitialized stack");
        return NULL;
    }

    socktype = protocol = flags = 0;
    family = AF_UNSPEC;

    if(!PyArg_ParseTupleAndKeywords(args,kwargs, "zO|iiii", kwnames, &hoststr, &portObj,  
        &family, &socktype, &protocol, &flags))
        return NULL;

    if(PyLong_CheckExact(portObj)){
        portObjStr = PyObject_Str(portObj);
        if(portObjStr == NULL) return NULL;
        portstr = PyUnicode_AsUTF8(portObjStr);
    } else if(PyUnicode_Check(portObj)) {
        portstr = PyUnicode_AsUTF8(portObj);
    } else if(PyBytes_Check(portObj)){
        portstr = PyBytes_AS_STRING(portObj);
    } else if (portObj == Py_None){
        portstr = NULL;
    } else {
        PyErr_SetString(PyExc_OSError, "Int or String expected");
        return NULL;
    }

    

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;
    hints.ai_flags = flags;
    
    error = iothdns_getaddrinfo(self->stack_dns, hoststr, portstr, &hints, &resList);

    if(error){
        resList = NULL;
        return Py_BuildValue("is", error, iothdns_gai_strerror(error));
    }

    all = PyList_New(0);
    if(all == NULL) return NULL;
    for(res = resList; res; res= res -> ai_next){
        PyObject* single;
        PyObject* addr = make_sockaddr(res->ai_addr, res->ai_addrlen);
        if(addr == NULL) return NULL;
        single = Py_BuildValue("iiisO", res->ai_family,
            res->ai_socktype, res->ai_protocol,
            res->ai_canonname ? res->ai_canonname : "",
            addr);
        Py_XDECREF(addr);
        if(single == NULL) return NULL;
        if(PyList_Append(all, single)){
            Py_XDECREF(single);
            return NULL;
        }
        Py_XDECREF(single);
    }
    if(resList) iothdns_freeaddrinfo(resList);
    return all;
}

PyDoc_STRVAR(dns_getnameinfo_doc, "getnameinfo(sockaddr, flags) --> (host, port)\n\
\n\
Get host and port for a sockaddr.);");

static PyObject* dns_getnameinfo(PyObject* self, PyObject *args){

    stack_object* s = (socket_object*) self;

    PyObject * sockaddr = (PyObject *)NULL;
    int flags;
    const char *hostptr;
    int port;
    unsigned int flowinfo, scope_id;
    int error;
    char hbuf[NI_MAXHOST], pbuf[NI_MAXSERV];
    struct addrinfo hints, *res = NULL;
    PyObject *name, *ret;

    flags = flowinfo = scope_id = 0;
    if(!PyArg_ParseTuple(args,"Oi:getnameinfo", &sockaddr, &flags))
        return NULL;
    
    if(!PyTuple_Check(sockaddr)){
        PyErr_SetString(PyExc_TypeError, "getnameinfo() argument 1 must be a tuple");
        return NULL;
    }

    if (!PyArg_ParseTuple(sockaddr, "si|II;getnameinfo(): illegal sockaddr argument",
                          &hostptr, &port, &flowinfo, &scope_id))
        return NULL;

    if (flowinfo > 0xfffff) {
        PyErr_SetString(PyExc_OverflowError, "getnameinfo(): flowinfo must be 0-1048575.");
        return NULL;
    }

    PyOS_snprintf(pbuf, sizeof(pbuf), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM; 
    hints.ai_flags = AI_NUMERICHOST;

    error = iothdns_getaddrinfo(s->stack_dns, hostptr, pbuf, &hints, &res);

    if(error){
        res = NULL;
        return Py_BuildValue("is", error, iothdns_gai_strerror(error));
    }

    if(res->ai_next){
        PyErr_SetString(PyExc_OSError, "sockaddr resolved to multiple addresses");
        iothdns_freeaddrinfo(res);
        return (PyObject*) NULL;
    }

    switch (res->ai_family){
        case AF_INET:{
            if (PyTuple_GET_SIZE(sockaddr) != 2) {
                PyErr_SetString(PyExc_OSError, "IPv4 sockaddr must be 2 tuple");
                iothdns_freeaddrinfo(res);
                return (PyObject*) NULL;
            }
            break;
        }

        case AF_INET6: {
            struct sockaddr_in6 *sin6;
            sin6 = (struct sockaddr_in6 *)res->ai_addr;
            sin6->sin6_flowinfo = htonl(flowinfo);
            sin6->sin6_scope_id = scope_id;
            break;
        }
    }

    error = iothdns_getnameinfo(s->stack_dns, res->ai_addr, (socklen_t) res->ai_addrlen,
                        hbuf, sizeof(hbuf), pbuf, sizeof(pbuf), flags );

    if(error){
        iothdns_freeaddrinfo(res);
        return Py_BuildValue("is", error, iothdns_gai_strerror(error));
    }

    name = PyUnicode_FromString(hbuf);
    if(name == NULL){
        iothdns_freeaddrinfo(res);
        return NULL;
    }

    ret = Py_BuildValue("Ns", name, pbuf);
    iothdns_freeaddrinfo(res);
    return ret;
}


static PyMethodDef stack_methods[] = {
    /* Listing network interfaces */
    {"if_nameindex", (PyCFunction)stack_if_nameindex, METH_NOARGS, if_nameindex_doc},
    {"if_nametoindex", (PyCFunction)stack_if_nametoindex, METH_VARARGS, if_nametoindex_doc},
    {"if_indextoname", (PyCFunction)stack_if_indextoname, METH_O, if_indextoname_doc},

    /* Network interface configuration */
    {"linksetupdown", (PyCFunction)stack_linksetupdown, METH_VARARGS, linksetupdown_doc},
    {"iplink_add", (PyCFunction)stack_iplink_add, METH_VARARGS, iplink_add_doc},
    {"iplink_add_vde", (PyCFunction)stack_iplink_add_vde, METH_VARARGS, iplink_add_vde_doc},
    {"iplink_del", (PyCFunction)stack_iplink_del, METH_VARARGS | METH_KEYWORDS, iplink_del_doc},

    {"linkgetaddr", (PyCFunction)stack_linkgetaddr, METH_VARARGS, linkgetaddr_doc},
    {"_linksetaddr", (PyCFunction)stack_linksetaddr, METH_VARARGS, linksetaddr_doc},
    {"linksetmtu",  (PyCFunction)stack_linksetmtu,  METH_VARARGS, linksetmtu_doc},

    {"ipaddr_add", (PyCFunction)stack_ipaddr_add, METH_VARARGS, ipaddr_add_doc},
    {"ipaddr_del", (PyCFunction)stack_ipaddr_del, METH_VARARGS, ipaddr_del_doc},
    {"iproute_add", (PyCFunction)stack_iproute_add, METH_VARARGS | METH_KEYWORDS, iproute_add_doc},
    {"iproute_del", (PyCFunction)stack_iproute_del, METH_VARARGS | METH_KEYWORDS, iproute_del_doc},

    /* Iothconf */
    {"ioth_config", (PyCFunction)stack_ioth_config, METH_VARARGS, ioth_config_doc},
    {"ioth_resolvconf", (PyCFunction)stack_ioth_resolvconf, METH_VARARGS, ioth_resolvconf_doc},

    /* Iothdns */

    /* configuration */
    {"iothdns_update", (PyCFunction)stack_dns_upgrade, METH_VARARGS, stack_dns_upgrade_doc},

    /* queries */
    {"getaddrinfo", (PyCFunctionWithKeywords)dns_getaddrinfo, METH_VARARGS | METH_KEYWORDS, dns_getaddrinfo_doc},
    {"getnameinfo", (PyCFunction)dns_getnameinfo, METH_VARARGS, dns_getnameinfo_doc},

    {NULL, NULL} /* sentinel */
};

PyDoc_STRVAR(stack_doc,
"StackBase(stack, vdeurl)\n\
\n\
This class is used internally as a base type for the Stack class\n\
");

PyTypeObject stack_type = {
  PyVarObject_HEAD_INIT(0, 0)                 /* Must fill in type value later */
    "_iothpy.StackBase",                             /* tp_name */
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

