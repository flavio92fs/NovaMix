fn main() {
    println!("cargo:rerun-if-changed=app.rc");
    println!("cargo:rerun-if-changed=../assets/NovaMix.ico");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    let output =
        std::path::PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("NovaMixUi.res");
    let status = std::process::Command::new("rc.exe")
        .args(["/nologo", "/fo"])
        .arg(&output)
        .arg("app.rc")
        .status()
        .expect("Windows SDK rc.exe is required. Use build-native-ui.ps1.");
    assert!(status.success(), "Failed to compile NovaMix UI resources");
    println!("cargo:rustc-link-arg-bin=NovaMixUi={}", output.display());
}
