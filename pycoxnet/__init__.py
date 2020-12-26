"""
Internet of threads library

TODO: Full description with examples here
"""

#
# Import symbols to expose them at package scope
# so they can be accessed with pycoxnet.name
#

# Import the Stack type
from pycoxnet.stack import Stack

# Import functions from the c module
from pycoxnet._pycoxnet import getdefaulttimeout, setdefaulttimeout, CMSG_LEN, CMSG_SPACE

# Import the function to override the built-in socket module
from pycoxnet.override import override_socket_module

# Import functions and constants from the builtin socket module 
from socket import (
    # Convertion utils
    inet_aton, inet_ntoa, inet_ntop, inet_pton, ntohl, ntohs, htonl, htons,

    # Constants
    AF_INET, AF_INET6, SOCK_STREAM, SOCK_DGRAM, INADDR_ANY
)

