fn main() {
    println!("cargo:rustc-link-search=native=../build/lib/Release");
    println!("cargo:rustc-link-search=native=../build/bin/Release");
    println!("cargo:rustc-link-lib=dylib=blurwindow");
}
