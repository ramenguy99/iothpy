#!/usr/bin/python3

import pycoxnet

s = pycoxnet.stack()

print("repr " + repr(s))
print("str " + str(s))
print("getstack " + hex(s.getstack()))

