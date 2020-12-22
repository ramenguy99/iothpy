from .socket_impl import socket_py
from ._pycoxnet import stack_base, getdefaulttimeout, setdefaulttimeout

# TODO: These utilities should probably be reimplemented in the pycoxnet c module and constants
from socket import inet_aton, inet_ntoa, inet_ntop, inet_pton, ntohl, ntohs, htonl, htons, INADDR_ANY
from socket import AF_INET, AF_INET6, SOCK_STREAM, SOCK_DGRAM, _intenum_converter, AddressFamily, SocketKind

class stack(stack_base):
    def __init__(self, *arg, **kwarg):
       stack_base.__init__(self, *arg, **kwarg)

    def socket(self, family=-1, type=-1, proto=-1, fileno=None):
        return socket_py(self, family, type, proto, fileno)
        

def override_socket_module(stack):
    import socket
    class my_socket(socket_py):
        def __init__(self, family=-1, type=-1, proto=-1, fileno=None):
           socket_py.__init__(self, stack, family, type, proto, fileno)

    socket.__dict__["socket"] = my_socket

