#!/usr/bin/python3

import pycoxnet

s = pycoxnet.stack("null://")

print("repr " + repr(s))
print("str " + str(s))
print("getstack " + hex(s.getstack()))

print("vde0 index: ", s.if_nametoindex("vde0"))
try:
    print("bananas index: ", s.if_nametoindex("bananas"))
except Exception as e:
    print(str(e))


