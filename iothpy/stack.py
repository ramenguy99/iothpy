# 
# This file is part of the iothpy library: python support for ioth.
# 
# Copyright (c) 2020-2024   Dario Mylonopoulos
#                           Lorenzo Liso
#                           Francesco Testa
# Virtualsquare team.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU General Public License 
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
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

To configure dns, you can use:
    iothdns_update

Other methods:
    getaddrinfo
    getnameinfo
    socket
"""

#Import iothpy c module
from . import _iothpy

#Import msocket for the MSocket class
from . import msocket

#Import function and classes to get getaddrinfo like built-in
from socket import _intenum_converter, AddressFamily, SocketKind, gaierror

class Stack(_iothpy.StackBase):
    """Stack class that represents a ioth networking stack
    
    Parameters
    ----------
    stack : str
        Name of the ioth stack to be used (e.g. vdestack, picox, ...)
        Can contain the configuration. vdeurl in this case is not used.

    vdeurl: str or list of strings
        Optional if stack string contain configuration.
        One or more vde urls, the stack will be initialized with one 
        interface connected to each of the vde urls specified

    config_dns : str
        Path or string of the configuration for iothdns. Default is "/etc/resolv.conf"
    """
    def __init__(self, stack, vdeurl = None, config_dns = None ):
        # Pass all arguments to the base class constructor
       _iothpy.StackBase.__init__(self, stack, vdeurl, config_dns)

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

    def getaddrinfo(self, *args, **kwargs):
        """Returns all the addresses info of host and port take as parameters.
        
        This method takes the same parameters as the builtin socket.getaddrinfo().
        """

        res = _iothpy.StackBase.getaddrinfo(self, *args,**kwargs)

        if(not isinstance(res, list)):
            raise gaierror(res[1])
        
        else:
            addrlist = []

            for res in _iothpy.StackBase.getaddrinfo(self, *args,**kwargs):
                af, socktype, proto, canonname, sa = res
                addrlist.append((_intenum_converter(af, AddressFamily),
                                _intenum_converter(socktype, SocketKind),
                                proto, canonname, sa))
            return addrlist

    def getnameinfo(self, *args):
        """Returns the host and port of sockaddr.

        This method takes the same parameters as the builtin socket.getnameinfo().
        """

        res = _iothpy.StackBase.getnameinfo(self, *args)

        if(isinstance(res[0], int)):
            raise gaierror(res[1])

        return res