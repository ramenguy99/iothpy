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

#define IS_PATH(str) (strchr(str, '/') != NULL)


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

static int
stack_init_(PyObject* self, PyObject* args, PyObject* kwds)
{
    stack_object* s = (stack_object*)self;

    char* stack_name = NULL;
    char* config = NULL;

    const char** urls = NULL;
    const char* single_url_buf[2];
    const char** multi_url_buf = NULL;
    PyObject* list = NULL;

    if(PyArg_ParseTuple(args, "s", &stack_name) && (strchr(stack_name, ',')) != NULL)
        s->stack = ioth_newstackc(stack_name);
    else{
        PyErr_Clear();          //clear error raised by PyArg_ParseTuple

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
    }

    if(!s->stack) {
        PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }

    s->stack_dns = iothdns_init(s->stack, NULL);

    if(!s->stack_dns){
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
Configure the stack using the config string. The options supported\n\
are listed here: https://github.com/virtualsquare/iothconf/tree/master .\n\
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
It returns NULL and errno = 0 if nothing changed since the previous call.\n\
In case of error it returns NULL and errno != 0.\n\
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
It returns a tuple (addrinfo_list, code, mem_address), where code is 0 on success,\n\
nonzero values on error. Check getaddrinfo(3) for more details.\n\
'hints' and addrinfo_list are based on struct addrinfo\n\
mem_address is the address in memory of the struct addrinfo.");

static PyObject* dns_getaddrinfo(stack_object* self, PyObject* args, PyObject* kwargs){
    static char* kwnames[] = {"host", "port", "family", "type", "proto", "flags", 0};
    struct addrinfo hints, *res, *res0;
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

    if(!PyArg_ParseTupleAndKeywords(args,kwargs, "zO|iiii:getaddrinfo", kwnames, &hoststr, &portObj,  
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

    Py_BEGIN_ALLOW_THREADS
    error = iothdns_getaddrinfo(self->stack_dns, hoststr, portstr, &hints, &res0);
    Py_END_ALLOW_THREADS

    if(error){
        res0 = NULL;
        //set_gaierror(get_module_state(self), error);
        return NULL;
    }

    all = PyList_New(0);
    if(all == NULL) return NULL;
    for(res = res0; res; res= res -> ai_next){
        PyObject* single;
        PyObject* addr = make_sockaddr(res->ai_addr, res->ai_addrlen);
        if(addr == NULL) return NULL;
        single = Py_BuildValue("iiisO", res->ai_family,
            res->ai_socktype, res->ai_protocol,
            res->ai_canonname ? res->ai_canonname : "",
            addr);
        Py_DECREF(addr);
        if(single == NULL) return NULL;
        if(PyList_Append(all, single)){
            Py_DECREF(single);
            return NULL;
        }
        Py_DECREF(single);
    }
    if(res0) iothdns_freeaddrinfo(res0);
    return all;
}


static PyMethodDef stack_methods[] = {
    /* Listing network interfaces */
    {"if_nameindex", (PyCFunction)stack_if_nameindex, METH_NOARGS, if_nameindex_doc},
    {"if_nametoindex", (PyCFunction)stack_if_nametoindex, METH_VARARGS, if_nametoindex_doc},
    {"if_indextoname", (PyCFunction)stack_if_indextoname, METH_O, if_indextoname_doc},

    /* Network interface configuration */
    {"linksetupdown", (PyCFunction)stack_linksetupdown, METH_VARARGS, linksetupdown_doc},
    {"iplink_add", (PyCFunction)stack_iplink_add, METH_VARARGS, iplink_add_doc},
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
    {"dns_update", (PyCFunction)stack_dns_upgrade, METH_VARARGS, stack_dns_upgrade_doc},

    /* queries */
    {"getaddrinfo", (PyCFunctionWithKeywords)dns_getaddrinfo, METH_VARARGS | METH_KEYWORDS, dns_getaddrinfo_doc},

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
    stack_init_,                                /* tp_init */
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

