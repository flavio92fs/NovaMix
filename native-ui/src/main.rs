#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod app;
mod platform;
mod protocol;
mod scroll_bench;
mod smooth_scroll;
mod worker;

fn main() {
    if let Err(error) = run() {
        // Startup parameters include a session secret. Never log argv or raw URLs.
        if !std::env::args().any(|argument| argument == "--verify-engine") {
            platform::error(&error);
        }
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let launch = app::Launch::parse(std::env::args().skip(1).collect())?;
    if launch.verify {
        let result = app::verify_engine(
            launch
                .connection
                .as_ref()
                .ok_or("Missing engine connection")?,
        );
        if let Some(path) = launch.result {
            let report=match &result {Ok(())=>"PASS: native client applied mixer, hardware volume, language, settings, routes and diagnostics through the preview engine.".to_owned(),Err(error)=>format!("FAIL: {error}")};
            std::fs::write(path, report).map_err(|_| "Could not write verification result.")?;
        }
        return result;
    }
    app::run(launch).map_err(|_| {
        "The native NovaMix interface could not start. Reopen it from the tray.".into()
    })
}
