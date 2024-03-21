import iothpy

#create and configure stack
stack = iothpy.Stack("vdestack","vxvde://234.0.0.1")

stack.ioth_config("eth,ip=10.0.0.53/24,gw=10.0.0.1")

addrinfos = stack.getaddrinfo("www.python.org", 80)

print(addrinfos)

addinfo_test = addrinfos[0]
print(addinfo_test)

#get socket
socket = stack.socket()

socket.connect(addinfo_test[4])

