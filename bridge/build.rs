use std::{env, fs, path::PathBuf, time::SystemTime};

// nss-rs builds libmozpkix.a (C++) but doesn't link it, which leaves
// mozilla::pkix::* symbols undefined. Find the newest NSS build and link it.
fn main() {
    println!("cargo:rerun-if-changed=build.rs");

    // OUT_DIR = <target>/debug/build/neqo-ns3-bridge-<hash>/out
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let build_dir = out.ancestors().nth(2).unwrap().to_path_buf();

    let mut best: Option<(SystemTime, PathBuf)> = None;
    if let Ok(entries) = fs::read_dir(&build_dir) {
        for e in entries.flatten() {
            if !e.file_name().to_string_lossy().starts_with("nss-rs-") {
                continue;
            }
            let lib = e.path().join("out/dist/Release/lib");
            if let Ok(meta) = fs::metadata(lib.join("libmozpkix.a")) {
                let t = meta.modified().unwrap_or(SystemTime::UNIX_EPOCH);
                if best.as_ref().map_or(true, |(bt, _)| t > *bt) {
                    best = Some((t, lib));
                }
            }
        }
    }

    match best {
        Some((_, lib)) => {
            println!("cargo:rustc-link-search=native={}", lib.display());
            println!("cargo:rustc-link-lib=static=mozpkix");
            println!("cargo:rustc-link-lib=dylib=stdc++");
	    println!("cargo:rustc-link-arg-cdylib=-Wl,-rpath,{}", lib.display());
        }
        None => println!("cargo:warning=libmozpkix.a not found; NSS not built yet?"),
    }
}
