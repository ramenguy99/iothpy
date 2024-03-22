# iothpy

## Python module for ioth, iothconf and iothdns support

This module provides the creation and configuration of Internet of Threads stack and socket, with an additional support for clients who need to query DNS servers, using iothdns API.

### classes

#### Stack

##### Creation
To create a stack object, the constructor is:
```py
iothpy.Stack(stack_name, vdeurl = None, config_dns = None)
```
where:
* stack_name (required): it is the name of ioth stack to be used (e.g. vdestack, picox, ...), or a string following newstackc configuration syntax.
* vdeurl(optional): can be a string or a list of string representing the vde urls. The stack will have an interface for each vde.
* config_dns(optional): path or string for the configuration of iothdns descriptor, following /etc/resolv.conf syntax. Default value is"/etc/resolv.conf".

Note: if stack_name isn't a configuration and vdeurl is None, then the stack will be initializated with no interface.

##### Configuration
To configure the stack you can use these functions:
    `linksetupdown`, `linkgetaddr`, `linksetaddr`, `iplink_add`, `iplink_del`, `linksetmtu`, `ipaddr_add`, `ipaddr_del`,`iproute_add`, `iproute_del`.

Or you can use a single method: `iothconfig(config)`

To update the dns, you can use `iothdns_update(config)`

##### Other methods
```py
Stack.getaddrinfo(host, port, family=0, type=0, proto=0)
```
It translate the *host/port* argument in a sequence of 5-tuple containing all the info for create a socket connected to service.
This method have the same signature of builtin `socket.getaddrinfo()` but uses iothdns.


```py
Stack.socket(self, family=-1, type=-1, proto=-1, fileno=None)
```
Create and return a new socket on this stack
This method take the same parameters as the builtin `socket.socket()` function

#### Socket
This class have the main methods of the builtin socket module, like `bind`, `close`, `connect` , `send` and more.

### Installation

### Example
Let's make a server-client communication:
```py
#file server.py:

#!/usr/bin/python

import sys
import iothpy
import time
import threading

# Check arguments
if(len(sys.argv) != 2):
    name = sys.argv[0]
    print("Usage: {0} vdeurl\ne,g: {1} vxvde://234.0.0.1\n\n".format(name, name))
    exit(1)

# Create and configure stack
stack  = iothpy.Stack("vdestack", sys.argv[1])
ifindex = stack.if_nametoindex("vde0")
stack.linksetupdown(ifindex, 1)
stack.ipaddr_add(iothpy.AF_INET, "10.0.0.1", 24, ifindex)
stack.iproute_add(iothpy.AF_INET, "10.0.0.254")

# To create and configure stack, you can use also:
# stack = iothpy.Stack("stack=vdestack,vnl={0},eth, ip=10.0.0.1/24".format(sys.argv[1]))

# Create a tcp listening socket from
sock = stack.socket(iothpy.AF_INET, iothpy.SOCK_STREAM)

# From here on we can use the ioth socket as if we were using a python socket
sock.bind(('', 5000))
sock.listen(1)

# Handle incoming connection
def handle(conn, addr):
    while True:
        data = conn.recv(1024)
        if not data:
            print("Connection closed by", addr)
            break
        print("Got:", data.decode(), "from", addr)
        conn.send(data)

# Listen on the socket for a new connection
while True:
    conn, addr = sock.accept()
    print("New connection by {}".format((conn, addr)))
    # Create a new thread to handle multiple concurrent connections
    t = threading.Thread(target = handle, args=(conn, addr), daemon=True)
    t.start()
```

```py
# file client.py:

#!/usr/bin/python

import iothpy

import sys
import time
import select

# Check arguments
if(len(sys.argv) != 2):
    name = sys.argv[0]
    print("Usage: {0} vdeurl\ne,g: {1} vxvde://234.0.0.1\n\n".format(name, name))
    exit(1)

# Create and configure stack
stack  = iothpy.Stack("vdestack", sys.argv[1])
ifindex = stack.if_nametoindex("vde0")
stack.linksetupdown(ifindex, 1)
stack.ipaddr_add(iothpy.AF_INET, "10.0.0.2", 24, ifindex)
stack.iproute_add(iothpy.AF_INET, "10.0.0.254")

# Create a tcp socket and connect to server
sock = stack.socket(iothpy.AF_INET, iothpy.SOCK_STREAM)

# From here on we can use the ioth socket as if we were using a python socket
sock.connect(("10.0.0.1", 5000))

print("Connected to server at", sock.getpeername())

# Create a poll object to wait for messsages from the server and stdin
poll_obj = select.poll()
poll_obj.register(sock, select.POLLIN)
poll_obj.register(sys.stdin, select.POLLIN)

while(True):
    events = poll_obj.poll()
    for fd, event in events:
        # Read messages from the server
        if(fd == sock.fileno()):
            message = sock.recv(1024)
            if(message):
                print(message.decode())
            else:
                break
        # Read from stdin
        if(fd == sys.stdin.fileno()):
            message = input().rstrip()
            if(message):
                sock.send(message.encode())
            else:
                break
```

You can test