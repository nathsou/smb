# SMB Recomp

Static recompilation of SMB using [doppelganger's disassembly](https://raw.githubusercontent.com/threecreepio/smb-disassembly/refs/heads/master/src/smb.asm) ported to cc65.

Static compilation of NES games from raw assembly is not generally feasible, however directives from the disassembly
can be used to separate the data from the code.

# Checkpoints:

- [x] Extract CHR data from a ROM of SMB, add iNES header and assemble the disassembly to a ROM with the same checksum using CC65
- [ ] Parse the disassembly and extract the data and code sections, preserving labels and comments
- [ ] Create a lower intermediate representation for 6502 instructions and convert complex instructions to this IR, for instance:

```assembly
    LDA ($20, X)

    --> 

    %target = GET_INDEXED_INDIRECT_X_ADDR $20
    REG_A = %target
    UPDATE_NZ %target
```

This enables optimisations like not emitting the `UPDATE_NZ` instruction if we can infer that the flags are not used later on.
- [ ] Create an interpreter for the lower intermediate representation and use it to test the correctness of the recompiled code.
- [ ] "Decompile" the IR into a higher-level IR with conditions, loops and function calls.
- [ ] Implement a simple PPU with the bare minimum to run SMB, only run PPU logic when the code waits for a VLANK.
- [ ] Iteratively rewrite the generated code with higher level constructs.
