// WebAssembly binary encoding.

pub(all) struct Buf {
  bytes : Array[Int]
}

pub fn Buf::new() -> Buf {
  { bytes: [] }
}

pub fn Buf::byte(self : Buf, b : Int) -> Unit {
  self.bytes.push(b & 0xff)
}

pub fn Buf::u32(self : Buf, v : Int) -> Unit {
  let mut v = v
  while true {
    let b = v & 0x7f
    v = v >> 7
    if v == 0 {
      self.byte(b)
      break
    }
    self.byte(b | 0x80)
  }
}

pub fn Buf::i32(self : Buf, v : Int) -> Unit {
  let mut v = v
  while true {
    let b = v & 0x7f
    v = v >> 7 // arithmetic
    if (v == 0 && (b & 0x40) == 0) || (v == -1 && (b & 0x40) != 0) {
      self.byte(b)
      break
    }
    self.byte(b | 0x80)
  }
}

pub fn Buf::name(self : Buf, s : String) -> Unit {
  let utf8 = @utf8.encode(s)
  self.u32(utf8.length())
  for b in utf8 { self.byte(b.to_int()) }
}

pub fn Buf::append(self : Buf, other : Buf) -> Unit {
  for b in other.bytes { self.bytes.push(b) }
}

// A section: id, size, contents.
pub fn Buf::section(self : Buf, id : Int, contents : Buf) -> Unit {
  self.byte(id)
  self.u32(contents.bytes.length())
  self.append(contents)
}

pub fn Buf::to_bytes(self : Buf) -> Bytes {
  Bytes::makei(self.bytes.length(), fn(i) { self.bytes[i].to_byte() })
}

// Opcodes
pub const UNREACHABLE : Int = 0x00
pub const BLOCK : Int = 0x02
pub const LOOP : Int = 0x03
pub const IF : Int = 0x04
pub const ELSE : Int = 0x05
pub const END : Int = 0x0b
pub const BR : Int = 0x0c
pub const BR_TABLE : Int = 0x0e
pub const RETURN : Int = 0x0f
pub const CALL : Int = 0x10
pub const DROP : Int = 0x1a
pub const LOCAL_GET : Int = 0x20
pub const LOCAL_SET : Int = 0x21
pub const LOCAL_TEE : Int = 0x22
pub const GLOBAL_GET : Int = 0x23
pub const I32_LOAD8_U : Int = 0x2d
pub const I32_STORE8 : Int = 0x3a
pub const I32_CONST : Int = 0x41
pub const I32_EQZ : Int = 0x45
pub const I32_EQ : Int = 0x46
pub const I32_NE : Int = 0x47
pub const I32_LT_S : Int = 0x48
pub const I32_GE_S : Int = 0x4e
pub const I32_ADD : Int = 0x6a
pub const I32_SUB : Int = 0x6b
pub const I32_AND : Int = 0x71
pub const I32_OR : Int = 0x72
pub const I32_XOR : Int = 0x73
pub const I32_SHL : Int = 0x74
pub const I32_SHR_S : Int = 0x75
pub const I32_SHR_U : Int = 0x76
pub const VOID : Int = 0x40
pub const I32 : Int = 0x7f
