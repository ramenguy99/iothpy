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
extern _PyTime_t defaulttimeout;

int socket_parse_timeout(_PyTime_t *timeout, PyObject *timeout_obj);
int get_CMSG_LEN(size_t length, size_t *result);
int get_CMSG_SPACE(size_t length, size_t *result);

#if INT_MAX > 0x7fffffff
#define SOCKLEN_T_LIMIT 0x7fffffff
#else
#define SOCKLEN_T_LIMIT INT_MAX
#endif

