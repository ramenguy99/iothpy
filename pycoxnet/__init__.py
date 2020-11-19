from .pycoxnet2 import test2
from ._pycoxnet import stack

# TODO: These utilities should probably be reimplemented in the pycoxnet c module and constants
from socket import inet_aton, inet_ntoa, inet_ntop, inet_pton, ntohl, ntohs, htonl, htons, INADDR_ANY
from socket import AF_INET, AF_INET6, SOCK_STREAM
