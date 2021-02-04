#!/usr/bin/python3

import iothpy

stack = iothpy.Stack("vdestack", "vxvde://234.0.0.1")
if_index = stack.if_nametoindex("vde0")

stack.linksetupdown(if_index, 1)
stack.ipaddr_add(iothpy.AF_INET, "10.0.0.1", 24, if_index)

iothpy.override_socket_module(stack)

import http.server
import socketserver

PORT = 8000

Handler = http.server.SimpleHTTPRequestHandler

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print("serving at port", PORT)
    httpd.serve_forever()
