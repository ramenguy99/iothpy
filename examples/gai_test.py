import iothpy



#create and configure stack
stack = iothpy.Stack("vdestack","vxvde://234.0.0.1")

stack.ioth_config("eth,ip=10.0.0.53/24,gw=10.0.0.1")

link = "/about"
host = "www.python.org"
port = 80


addrinfos = stack.getaddrinfo(host, port)

print(addrinfos)

addinfo_test = addrinfos[0]
print(addinfo_test)

# create socket from the stack
socket = stack.socket()

socket.connect(addinfo_test[4])
msg = "GET " + link + " HTTP/1.0\r\n\r\n"
s.sendall(msg)
s.recv(4096)
