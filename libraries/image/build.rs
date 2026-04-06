#[allow(unused_must_use)]
fn main() {
    cxx_build::bridge("src/image.rs");
    println!("cargo:rerun-if-changed=src/image.rs");
}
