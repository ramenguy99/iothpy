import iothpy

stack  = iothpy.Stack("stack=vdestack")

#ifindex = stack.if_nametoindex("vde0") 

#stack.linksetupdown(ifindex, True)

#print(ifindex)

ret = stack.iplink_add_vde(-1, "vxvde://234.0.0.1", "vde1")

print(f"return value: {ret}\n")

IP.addr.show()