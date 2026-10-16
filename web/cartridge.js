// NROM dumps: an iNES file, or a raw dump of the cartridge (PRG then CHR,
// no header) of 24 or 40 KiB. The recompiler applies the same rules
// (src/image/rom.mbt) and identifies the dump by the SHA-256 of PRG and CHR.

export function splitDump(rom) {
    const headered = rom.length >= 4 && rom[0] === 0x4e && rom[1] === 0x45 && rom[2] === 0x53 && rom[3] === 0x1a;
    if (headered) {
        if (rom.length < 16) throw new Error('Truncated iNES file');
        const mapper = (rom[6] >> 4) | (rom[7] & 0xf0);
        if (mapper !== 0) throw new Error(`Only mapper 0 (NROM) is supported, not ${mapper}`);
        if (rom[4] < 1 || rom[4] > 2) throw new Error('NROM needs 16 or 32 KiB of PRG ROM');
        const start = 16 + (rom[6] & 4 ? 512 : 0);
        const prgSize = rom[4] * 0x4000;
        const chrSize = rom[5] * 0x2000;
        if (rom.length < start + prgSize + chrSize) throw new Error('Truncated iNES file');
        return {
            headered,
            prg: rom.subarray(start, start + prgSize),
            chr: rom.subarray(start + prgSize, start + prgSize + chrSize),
        };
    }
    if (rom.length !== 0x6000 && rom.length !== 0xa000) {
        throw new Error(`Not an iNES file, nor a raw NROM dump (24 or 40 KiB): ${rom.length} bytes`);
    }
    const prgSize = rom.length - 0x2000;
    return { headered, prg: rom.subarray(0, prgSize), chr: rom.subarray(prgSize) };
}
