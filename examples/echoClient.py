#!/usr/bin/python

import sys
import pycoxnet
import time
import select

if(len(sys.argv) != 6):
    name = sys.argv[0]
    print("Usage: {0} vdeurl client_ip prefix server_ip port\ne,g: {1} vxvde://234.0.0.1 10.0.0.2 24 10.0.0.1 5000\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.stack("picox", sys.argv[1])

prefix = int(sys.argv[3])
port  = int(sys.argv[5])
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])
gw_addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.254")
ifindex = stack.if_nametoindex("vde0")

stack.linksetupdown(ifindex, 1)
stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)
stack.iproute_add(pycoxnet.AF_INET, None, 0, gw_addr)

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM)

sock.connect((sys.argv[4], port))

print("Connected to server at", (sys.argv[4], port))

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

