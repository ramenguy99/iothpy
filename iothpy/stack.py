""" Stack module

This module defines the Stack class used to create a networking stack.

To configure the stack you can use the following functions

Get interface index:
    if_nametoindex

Link configuration:
    linksetupdown
    linkgetaddr
    linksetaddr
    iplink_add
    iplink_del
    linksetmtu

IP configuration:
    ipaddr_add
    ipaddr_del
    iproute_add
    iproute_del

Or you can use a single method:
    iothconfig
"""

#Import iothpy c module
from . import _iothpy

#Import msocket for the MSocket class
from . import msocket

class Stack(_iothpy.StackBase):
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
       _iothpy.StackBase.__init__(self, *arg, **kwarg)

    def socket(self, family=-1, type=-1, proto=-1, fileno=None):
        """Create and return a new socket on this stack

        This method takes the same parameters as the builtin socket.socket() function.
        Stack.socket(family=AF_INET, type=SOCK_STREAM, proto=0, fileno=None)
        """
        return msocket.MSocket(self, family, type, proto, fileno)


    def linksetaddr(self, ifindex, addr):
        """Set the MAC address of the interface ifindex");

        ifindex must be an integer index, addr must be valid macaddr as a string or bytes object.
        E.g. "80:00:42:0e:e7:3a" or b'\\x80\\x00\\x42\\x0e\\xe7\\x3a'
        """
        if isinstance(addr, str):
            addr = addr.replace(":", "")
            addr = bytearray.fromhex(addr)

        self._linksetaddr(ifindex, addr)
