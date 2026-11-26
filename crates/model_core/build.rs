// Compiles the C++ side of the CXX bridge. Only runs with the `ffi` feature,
// which CMake enables; it passes the include directories through env vars.
fn main() {
    #[cfg(feature = "ffi")]
    {
        let occt = std::env::var("OCCT_INCLUDE_DIR")
            .unwrap_or_else(|_| "/usr/include/opencascade".to_string());
        let bridge_dir = std::env::var("BRIDGE_INCLUDE_DIR").unwrap_or_else(|_| {
            concat!(env!("CARGO_MANIFEST_DIR"), "/../../src/bridge").to_string()
        });

        let mut build = cxx_build::bridge("src/ffi.rs");
        build.include(&bridge_dir).include(&occt).std("c++20");
        // OCCT's headers trigger deprecation warnings we can't fix.
        build.flag_if_supported("-Wno-deprecated-declarations");
        build.compile("model_core_bridge");

        println!("cargo:rerun-if-changed=src/ffi.rs");
        println!("cargo:rerun-if-changed={bridge_dir}/GeometryBridge.h");
        println!("cargo:rerun-if-env-changed=OCCT_INCLUDE_DIR");
        println!("cargo:rerun-if-env-changed=BRIDGE_INCLUDE_DIR");
    }
}
