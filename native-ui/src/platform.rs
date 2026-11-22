use std::{
    ffi::OsString,
    os::windows::ffi::{OsStrExt, OsStringExt},
};
use windows_sys::Win32::{
    Foundation::{CloseHandle, HANDLE, WAIT_TIMEOUT},
    System::Threading::{OpenProcess, WaitForSingleObject},
    UI::{
        Controls::Dialogs::{
            CommDlgExtendedError, GetSaveFileNameW, OFN_NOCHANGEDIR, OFN_OVERWRITEPROMPT,
            OFN_PATHMUSTEXIST, OPENFILENAMEW,
        },
        WindowsAndMessaging::{MB_ICONERROR, MB_OK, MessageBoxW},
    },
};

fn wide(text: &str) -> Vec<u16> {
    std::ffi::OsStr::new(text)
        .encode_wide()
        .chain(Some(0))
        .collect()
}

pub fn error(text: &str) {
    // This function is only called with static/sanitized errors, never argv.
    unsafe {
        MessageBoxW(
            std::ptr::null_mut(),
            wide(text).as_ptr(),
            wide("NovaMix").as_ptr(),
            MB_OK | MB_ICONERROR,
        );
    }
}

pub struct Parent(HANDLE);
// An owned, wait-only process handle may be moved to the IPC worker.
unsafe impl Send for Parent {}
impl Parent {
    pub fn open(pid: u32) -> Result<Self, String> {
        const PROCESS_SYNCHRONIZE: u32 = 0x00100000;
        let handle = unsafe { OpenProcess(PROCESS_SYNCHRONIZE, 0, pid) };
        if handle.is_null() {
            Err("NovaMix engine is not available. Start NovaMix.exe.".into())
        } else {
            Ok(Self(handle))
        }
    }
    pub fn alive(&self) -> bool {
        unsafe { WaitForSingleObject(self.0, 0) == WAIT_TIMEOUT }
    }
}
impl Drop for Parent {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

pub fn save_report(report: &str, english: bool) -> Result<bool, String> {
    if report.is_empty() || report.len() > 2 * 1024 * 1024 || report.contains('\0') {
        return Err("Invalid diagnostic report.".into());
    }
    let mut path = vec![0u16; 32768];
    let name = wide(if english {
        "novamix-diagnostics.txt"
    } else {
        "diagnostica-novamix.txt"
    });
    path[..name.len()].copy_from_slice(&name);
    let title = wide(if english {
        "Save NovaMix diagnostics"
    } else {
        "Salva diagnostica NovaMix"
    });
    let filter = wide("Text report (*.txt)\0*.txt\0\0");
    let extension = wide("txt");
    let mut dialog: OPENFILENAMEW = unsafe { std::mem::zeroed() };
    dialog.lStructSize = std::mem::size_of::<OPENFILENAMEW>() as u32;
    dialog.lpstrFile = path.as_mut_ptr();
    dialog.nMaxFile = path.len() as u32;
    dialog.lpstrFilter = filter.as_ptr();
    dialog.lpstrTitle = title.as_ptr();
    dialog.lpstrDefExt = extension.as_ptr();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if unsafe { GetSaveFileNameW(&mut dialog) } == 0 {
        return if unsafe { CommDlgExtendedError() } == 0 {
            Ok(false)
        } else {
            Err("Could not open the save dialog.".into())
        };
    }
    let length = path
        .iter()
        .position(|&ch| ch == 0)
        .ok_or("Invalid report path.")?;
    let selected = OsString::from_wide(&path[..length]);
    std::fs::write(std::path::PathBuf::from(selected), report)
        .map_err(|_| "Could not save the report in the selected location.".to_owned())?;
    Ok(true)
}
