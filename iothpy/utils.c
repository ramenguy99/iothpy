#include "utils.h"

/* Utility to create a tuple representing the given sockaddr suitable
   for passing it back to bind, connect etc.. */
PyObject * make_sockaddr(struct sockaddr *addr, size_t addrlen)
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


/* Convert IPv4 sockaddr to a Python str. */
PyObject * make_ipv4_addr(struct sockaddr_in *addr)
{
    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) == NULL) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    return PyUnicode_FromString(buf);
}

/* Convert IPv6 sockaddr to a Python str. */
PyObject * make_ipv6_addr(struct sockaddr_in6 *addr)
{
    char buf[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf)) == NULL) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    return PyUnicode_FromString(buf);
}