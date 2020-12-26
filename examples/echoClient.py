#!/usr/bin/python

import sys
import pycoxnet
import time
import select

if(len(sys.argv) != 2):
    name = sys.argv[0]
    print("Usage: {0} vdeurl\ne,g: {1} vxvde://234.0.0.1\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.Stack("picox", sys.argv[1])
ifindex = stack.if_nametoindex("vde0")

stack.linksetupdown(ifindex, 1)
stack.ipaddr_add(pycoxnet.AF_INET, "10.0.0.2", 24, ifindex)
stack.iproute_add(pycoxnet.AF_INET, "10.0.0.254")

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM)

sock.connect(("10.0.0.1", 5000))

print("Connected to server at", sock.getpeername())

poll_obj = select.poll()
poll_obj.register(sock, select.POLLIN);
poll_obj.register(sys.stdin, select.POLLIN)

while(True):
    events = poll_obj.poll();
    for fd, event in events:
        if(fd == sock.fileno()):
            message = sock.recv(1024)
            if(message):
                print(message.decode())
            else:
                break
        if(fd == sys.stdin.fileno()):
            message = input().rstrip()
            if(message):
                sock.send(message.encode())
            else:
                break

