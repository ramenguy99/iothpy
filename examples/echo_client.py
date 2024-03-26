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

# To configure the stack, you can use the single function:
# stack.ioth_config("ip=10.0.0.2/24, eth")

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