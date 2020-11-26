#!/usr/bin/python

import sys
import pycoxnet
import time
import threading

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

def handle(conn, addr):
    while True:
        data = conn.recv(1024)
        if not data:
            print("Connection closed by", addr)
            break
        print("Got:", data.decode(), "from", addr)
        conn.send(data)

while True:
    conn, addr = sock.accept()
    print("New connection by ", addr)
    t = threading.Thread(target = handle, args=(conn, addr), daemon=True)
    t.start()

