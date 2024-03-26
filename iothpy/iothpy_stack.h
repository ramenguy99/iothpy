#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <ioth.h>
#include <iothconf.h>
#include <iothdns.h>

typedef struct stack_object {
    PyObject_HEAD
    struct ioth* stack;
    struct iothdns* stack_dns;
} stack_object;

extern PyTypeObject stack_type;
