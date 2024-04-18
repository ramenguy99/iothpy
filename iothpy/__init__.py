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
"""
Internet of threads library

iothpy is a library for developing Internet of Threads applications.
It's built upon libioth and allows to access all of its functionality in python.


Here is an example of a basic creation and configuration of a networking 
stack connected to a vdeurl:


import iothpy

# Create a new stack using vdestack connected to vde:///tmp/mysw
stack = iothpy.Stack("vdestack", "vde:///tmp/mysw")

# Get the index of the default interface vde0
if_index = stack.if_nametoindex("vde0")


# Enable the interface
stack.linksetupdown(if_index, 1)

# Set MAC address of the interface
stack.linksetaddr(if_index, "80:00:42:0e:e7:3a")

# Add IPv4 address and default gateway
stack.ipaddr_add(iothpy.AF_INET, "10.0.0.1", 24, if_index)
stack.iproute_add(iothpy.AF_INET, "10.0.0.254")

# Create a new socket on the stack
s = stack.socket(iothpy.AF_INET, iothpy.SOCK_STREAM)


For more information on stack configuration see help("iothpy.stack")

"""

#
# Import symbols to expose them at package scope
# so they can be accessed with iothpy.name
#

# Import the Stack type
from iothpy.stack import Stack

# Import constants for ioth_linkadd
from ._const_linkadd import *

# Import functions from the c module
from ._iothpy import getdefaulttimeout, setdefaulttimeout, CMSG_LEN, CMSG_SPACE, close, timeout


# Import the function to override the built-in socket module
from iothpy.override import override_socket_module

# Import functions and constants from the builtin socket module 
from socket import (
    # Convertion utils
    inet_aton, inet_ntoa, inet_ntop, inet_pton, ntohl, ntohs, htonl, htons,

    # Constants
    AF_INET, AF_INET6, SOCK_STREAM, SOCK_DGRAM, INADDR_ANY,

    # Get host functions
    gethostbyname, gethostbyname_ex, gethostbyaddr, gethostname,

    # Other functions
    getfqdn, getaddrinfo, getnameinfo, getprotobyname, getservbyname, getservbyport,
    sethostname,
)

