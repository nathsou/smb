pub(all) struct VectorRoots {
  reset: Int?
  nmi: Int?
  irq: Int?
} derive(Debug)

fn valid_prg_address(image: @layout.Image, address: Int) -> Bool {
  address >= image.origin && address < image.origin + image.bytes.length()
}

fn vector(image: @layout.Image, address: Int) -> Int? {
  match image.read_word(address) {
    Some(target) if valid_prg_address(image, target) => Some(target)
    _ => None
  }
}

// The three vectors are part of the NROM PRG image, not a special source
// annotation.  A malformed or intentionally unused vector is represented by
// None and can be handled by the dispatcher fallback.
pub fn recover_vectors(image: @layout.Image) -> VectorRoots {
  {
    nmi: vector(image, 0xfffa),
    reset: vector(image, 0xfffc),
    irq: vector(image, 0xfffe),
  }
}

pub fn VectorRoots::entries(self: VectorRoots) -> Array[Int] {
  let entries = []
  match self.reset { Some(address) => entries.push(address); None => () }
  match self.nmi { Some(address) => entries.push(address); None => () }
  match self.irq { Some(address) => entries.push(address); None => () }
  entries
}
