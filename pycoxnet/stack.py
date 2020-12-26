""" Stack module

This module defines the Stack class used to create a networking stack.

TODO: examples

"""

#Import pycoxnet c module
from . import _pycoxnet

#Import msocket for the MSocket class
from . import msocket

class Stack(_pycoxnet.StackBase):
    """Stack class that represents a ioth networking stack
    
    Parameters
    ----------
    stack : str
        Name of the ioth stack to be used (e.g. vdestack, picox, ...)
    
    vdeurl: str or list of strings
        One or more vde urls, the stack will be initialized with one 
        interface connected to each of the vde urls specified
    """
    def __init__(self, *arg, **kwarg):
        # Pass all arguments to the base class constructor
       _pycoxnet.StackBase.__init__(self, *arg, **kwarg)

    def socket(self, family=-1, type=-1, proto=-1, fileno=None):
        """Create and return a new socket on this stack

        This method takes the same parameters as the builtin socket.socket() function.
        Stack.socket(family=AF_INET, type=SOCK_STREAM, proto=0, fileno=None)
        """
        return msocket.MSocket(self, family, type, proto, fileno)

