# iothpy

## Python library for internet of threads

iothpy is a library for developing Internet of Threads applications using python as a scripting language. It's built upon libioth and allows to access all of its functionality in python.

To get started with iothpy you need to have libioth installed and download iotphy with pip as follows:

```bash
pip install scikit-build
pip install --no-deps --index-url https://test.pypi.org/simple iothpy==1.2.6 --no-build-isolation
```

## Getting started with iothpy
Here is an example of a basic creation and configuration of a networking stack connected to a vdeurl:

```python
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

```
The Stack constructor takes the name of a ioth plugin (`vdestack`, `picox`, ...) and one or a list of vde urls. The new stack has a virtual interface connected to each vde network identified by the VNL.

The stack object has methods for all the configuration options defined by libioth, for more details you can run `help("iothpy.Stack")` inside a python interpreter.

After the configuration you can create sockets using the `Stack.socket` method. The parameters and the API of the returned socket is the same as the python built-in socket module.

## Example: simple TCP echo client-server

### `echo_server.py`
```python
import iothpy

import sys
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

# Create a tcp listening socket
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


### `echo_client.py`

```python
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

To run the example open two terminals and execute:
```bash
python echo_server.py vxvde://234.0.0.1
```

```
python echo_client.py vxvde://234.0.0.1
```

## Overriding the python built-in socket module

You can also bring already existing python modules to Internet of Threads by overriding the built-in socket module. In the following example we configure a new stack and use it run the simple http server from the python standard module http.server

### `vhttp.py`
```python
import iothpy

# Create and configure stack
stack = iothpy.Stack("vdestack", "vxvde://234.0.0.1")
if_index = stack.if_nametoindex("vde0")
stack.linksetupdown(if_index, 1)
stack.ipaddr_add(iothpy.AF_INET, "10.0.0.1", 24, if_index)

# Override the built-in module
iothpy.override_socket_module(stack)

# Import the standard modules
import http.server
import socketserver

# Create an http server on port 8000
PORT = 8000
Handler = http.server.SimpleHTTPRequestHandler
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print("serving at port", PORT)
    httpd.serve_forever()

```

After the call to `override_socket_module` all the sockets created importing the python socket module will instead be created on the  stack passed as a parameter to the function. To make sure all the sockets are created after overriding, you should import other modules only after the call to `override_socket_module`.

You can run the http server by executing:
```bash
python vhttp.py
```

You can connect to the server by using a vdens shell:
```
$ vdens vxvde://234.0.0.1
$# ip link set vde0 up
$# ip addr add 10.0.0.2/24 dev vde0
$# wget http://10.0.0.1:8000/ 
```
