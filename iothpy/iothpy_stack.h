#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <ioth.h>

typedef struct stack_object {
    PyObject_HEAD
    struct ioth* stack;
} stack_object;

extern PyTypeObject stack_type;
