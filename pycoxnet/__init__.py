from .pycoxnet2 import test2
from ._pycoxnet import stack, SocketType

# TODO: These utilities should probably be reimplemented in the pycoxnet c module and constants
import socket

from socket import inet_aton, inet_ntoa, inet_ntop, inet_pton, ntohl, ntohs, htonl, htons, INADDR_ANY
from socket import AF_INET, AF_INET6, SOCK_STREAM, SOCK_DGRAM

import io
from socket import SocketIO


def override_socket_module(stack):
    class my_socket(SocketType):
        def __init__(self, family=-1, type=-1, proto=-1, fileno=None):
            # For user code address family and type values are IntEnum members, but
            # for the underlying _socket.socket they're just integers. The
            # constructor of _socket.socket converts the given argument to an
            # integer automatically.
            if fileno is None:
                if family == -1:
                    family = AF_INET
                if type == -1:
                    type = SOCK_STREAM
                if proto == -1:
                    proto = 0
            SocketType.__init__(self, stack, family, type, proto, fileno)

            self._io_refs = 0
            self._closed = False


        def accept(self):
            s, addr = SocketType.accept(self)

            # hack to create a my_socket object instead
            sock = my_socket(self.family, self.type, self.proto, fileno=s.fileno())

            return sock, addr


        def makefile(self, mode="r", buffering=None, *, encoding=None, errors=None, newline=None):
            """makefile(...) -> an I/O stream connected to the socket
            The arguments are as for io.open() after the filename, except the only
            supported mode values are 'r' (default), 'w' and 'b'.
            """
            # XXX refactor to share code?
            if not set(mode) <= {"r", "w", "b"}:
                raise ValueError("invalid mode %r (only r, w, b allowed)" % (mode,))
            writing = "w" in mode
            reading = "r" in mode or not writing
            assert reading or writing
            binary = "b" in mode
            rawmode = ""
            if reading:
                rawmode += "r"
            if writing:
                rawmode += "w"
            raw = SocketIO(self, rawmode)
            self._io_refs += 1
            if buffering is None:
                buffering = -1
            if buffering < 0:
                buffering = io.DEFAULT_BUFFER_SIZE
            if buffering == 0:
                if not binary:
                    raise ValueError("unbuffered streams must be binary")
                return raw
            if reading and writing:
                buffer = io.BufferedRWPair(raw, raw, buffering)
            elif reading:
                buffer = io.BufferedReader(raw, buffering)
            else:
                assert writing
                buffer = io.BufferedWriter(raw, buffering)
            if binary:
                return buffer
            text = io.TextIOWrapper(buffer, encoding, errors, newline)
            text.mode = mode
            return text

        def _real_close(self, _ss=SocketType):
            # This function should not reference any globals. See issue #808164.
            _ss.close(self)

        def close(self):
            # This function should not reference any globals. See issue #808164.
            self._closed = True
            if self._io_refs <= 0:
                self._real_close()

        def _decref_socketios(self):
            if self._io_refs > 0:
                self._io_refs -= 1
            if self._closed:
                self.close()

    socket.__dict__["socket"] = my_socket;

