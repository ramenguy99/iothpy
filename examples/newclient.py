#!/usr/bin/python

import sys
import pycoxnet

if(len(sys.argv) != 6):
    name = sys.argv[0]
    print("Usage: {0} vdeurl client_ip prefix server_ip port\ne,g: {1} vxvde://234.0.0.1 10.0.0.2 24 10.0.0.1 5000\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.stack(sys.argv[1])

prefix = int(sys.argv[3])
port  = int(sys.argv[5])
ifindex = stack.if_nametoindex("vde0")
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])

stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM)

print(sock)

sock.connect((sys.argv[4], port))
sock.send(b'Hello!')

