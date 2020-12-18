#!/usr/bin/python3

import pycoxnet

stack = pycoxnet.stack("picox", "vxvde://234.0.0.1")
if_index = stack.if_nametoindex("vde0")
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.1")
gw_addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.254")

stack.linksetupdown(if_index, 1)
stack.ipaddr_add(pycoxnet.AF_INET, addr, 24, if_index)
stack.iproute_add(pycoxnet.AF_INET, None, 0, gw_addr)

pycoxnet.override_socket_module(stack)


import http.server
import socketserver

PORT = 8000

Handler = http.server.SimpleHTTPRequestHandler

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print("serving at port", PORT)
    httpd.serve_forever()
