use std::env;
use std::path::PathBuf;

fn main() {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let libs_dir = PathBuf::from(&manifest_dir).join("libs");

    println!("cargo:rustc-link-search=native={}", libs_dir.display());
    println!("cargo:rustc-link-lib=dylib=blurwindow");

    // But we actually need it next to the executable for dev.
    // However, Tauri v2 usually handles bundle resources.
    // For `cargo run`, we often need it in the same dir as the exe.

    // Simplest way for tauri dev is to tell cargo to re-run if libs change
    println!("cargo:rerun-if-changed=libs/blurwindow.dll");

    tauri_build::build();
}
