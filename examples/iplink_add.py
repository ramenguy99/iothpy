import iothpy

stack  = iothpy.Stack("picox", "vxvde://234.0.0.1")

ret = stack.iplink_add_vde(-1, "vxvde://234.0.0.2", "vde1")

print(f"return value: {ret}\n")