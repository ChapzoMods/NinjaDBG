# NinjaDBG Python test script - reads registers and disassembles at RIP
import sys

# Get info
regs = ndbg.info_registers()
rip = regs["rip"]
rsp = regs["rsp"]
ndbg.log(f"RIP = 0x{rip:x}")
ndbg.log(f"RSP = 0x{rsp:x}")

# Disassemble 5 instructions at RIP
instrs = ndbg.disassemble(rip, 5)
ndbg.log(f"Disassembled {len(instrs)} instructions:")
for i, ins in enumerate(instrs):
    ndbg.log(f"  [{i}] {ins}")

# Read 16 bytes at RSP
bytes_ = ndbg.read_bytes(rsp, 16)
ndbg.log(f"16 bytes at RSP: {' '.join(f'{b:02x}' for b in bytes_)}")

# Get backtrace
frames = ndbg.backtrace(8)
ndbg.log(f"Backtrace ({len(frames)} frames):")
for i, f in enumerate(frames):
    sym = f.get("symbol", "?")
    ndbg.log(f"  #{i} 0x{f['rip']:x}  {sym}")
