# Debug - print disassembled instructions individually
rip = ndbg.info_registers()["rip"]
ndbg.log(f"RIP = 0x{rip:x}")
instrs = ndbg.disassemble(rip, 5)
ndbg.log(f"got {len(instrs)} instructions")
for ins in instrs:
    ndbg.log(f"  ins type={type(ins).__name__} val={ins!r}")
