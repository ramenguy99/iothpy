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