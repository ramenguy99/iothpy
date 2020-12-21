#define PY_SSIZE_T_CLEAN
#include <Python.h>

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

    _PyTime_t sock_timeout;     /* Operation timeout in seconds */
    
} socket_object;

extern PyTypeObject socket_type;
extern PyObject *socket_timeout;
