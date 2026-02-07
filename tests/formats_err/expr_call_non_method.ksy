# expr_call_non_method.ksy: /instances/broken/value:
# 	error: can't call expression BinOp(IntNum(1),Add,IntNum(2)) directly; only method calls like `obj.method(...)` are supported
#
meta:
  id: expr_call_non_method
instances:
  broken:
    value: (1 + 2)(3)
