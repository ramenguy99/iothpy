#!/usr/bin/python

import sys
import pycoxnet

if(len(sys.argv) != 5):
    name = sys.argv[0]
    print("Usage: {0} vdeurl ip prefix port\ne,g: {1} vxvde://234.0.0.1 10.0.0.1 24 5000\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.stack("picox", sys.argv[1])

prefix = int(sys.argv[3])
port  = int(sys.argv[4])
ifindex = stack.if_nametoindex("vde0")
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])

stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM)

sock.bind(('', port))

sock.listen(1)

while True:
    (clientsock, address) = sock.accept()
    msg = clientsock.recv(1024)
    clientsock.close()
    print("New connnection: {0}".format(address))
    print(msg)




