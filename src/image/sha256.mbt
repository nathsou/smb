// SHA-256 (FIPS 180-4), to identify ROM dumps.

let k : Array[UInt] = [
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
]

fn rotr(x : UInt, n : Int) -> UInt {
  (x >> n) | (x << (32 - n))
}

// Lower-case hex digest of bytes (0..255 each).
pub fn sha256(data : Array[Int]) -> String {
  let h : Array[UInt] = [
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  ]
  let message = data.copy()
  let bits = data.length() * 8
  message.push(0x80)
  while message.length() % 64 != 56 { message.push(0) }
  for i in 0..<8 { message.push(if i < 4 { 0 } else { (bits >> ((7 - i) * 8)) & 0xff }) }
  let w : Array[UInt] = Array::make(64, 0U)
  let mut chunk = 0
  while chunk < message.length() {
    for t in 0..<16 {
      let b = chunk + t * 4
      w[t] = (message[b].reinterpret_as_uint() << 24) | (message[b + 1].reinterpret_as_uint() << 16) |
        (message[b + 2].reinterpret_as_uint() << 8) | message[b + 3].reinterpret_as_uint()
    }
    for t in 16..<64 {
      let s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3)
      let s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10)
      w[t] = w[t - 16] + s0 + w[t - 7] + s1
    }
    let mut a = h[0]
    let mut b = h[1]
    let mut c = h[2]
    let mut d = h[3]
    let mut e = h[4]
    let mut f = h[5]
    let mut g = h[6]
    let mut hh = h[7]
    for t in 0..<64 {
      let s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)
      let ch = (e & f) ^ (e.lnot() & g)
      let t1 = hh + s1 + ch + k[t] + w[t]
      let s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)
      let maj = (a & b) ^ (a & c) ^ (b & c)
      let t2 = s0 + maj
      hh = g
      g = f
      f = e
      e = d + t1
      d = c
      c = b
      b = a
      a = t1 + t2
    }
    h[0] = h[0] + a
    h[1] = h[1] + b
    h[2] = h[2] + c
    h[3] = h[3] + d
    h[4] = h[4] + e
    h[5] = h[5] + f
    h[6] = h[6] + g
    h[7] = h[7] + hh
    chunk += 64
  }
  let out = StringBuilder::new()
  for word in h {
    out.write_string(word.to_string(radix=16).pad_start(8, '0'))
  }
  out.to_string()
}

test "SHA-256 test vectors" {
  assert_eq(sha256([]), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
  assert_eq(sha256([0x61, 0x62, 0x63]), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
  let long = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  assert_eq(sha256(long.iter().map(fn(c) { c.to_int() }).collect()), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")
}
