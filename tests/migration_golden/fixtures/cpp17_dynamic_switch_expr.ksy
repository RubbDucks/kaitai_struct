meta:
  id: cpp17_dynamic_switch_expr
  endian: le
seq:
  - id: tag
    type: u1
  - id: payload
    type:
      switch-on: tag + 1
      cases:
        '2': u2
        _: u2
