use super::*;
use std::ptr;
use std::ffi::CString;
use windows::Win32::Foundation::HWND;

pub struct BlurSystem {
    handle: BlurSystemHandle,
}

impl BlurSystem {
    pub fn new() -> Result<Self, String> {
        let options = BlurSystemOptionsC {
            enable_logging: 1,
            log_path: ptr::null(),
            default_preset: BlurQualityPreset::Balanced,
        };
        
        unsafe {
            let handle = blur_init(&options);
            if handle.0.is_null() {
                let err = blur_get_last_error();
                if !err.is_null() {
                    let c_str = std::ffi::CStr::from_ptr(err);
                    return Err(c_str.to_string_lossy().into_owned());
                }
                return Err("Failed to initialize blur system".into());
            }
            Ok(BlurSystem { handle })
        }
    }

    pub fn create_window(&self, owner: HWND, x: i32, y: i32, w: i32, h: i32) -> Result<BlurWindow, String> {
        let opts = BlurWindowOptionsC {
            owner,
            bounds: BlurRect { left: x, top: y, right: x + w, bottom: y + h },
            top_most: 1,
            click_through: 1,
        };

        unsafe {
            let win_handle = blur_create_window(self.handle, owner, &opts);
            if win_handle.0.is_null() {
                return Err("Failed to create blur window".into());
            }
            Ok(BlurWindow { handle: win_handle })
        }
    }
}

impl Drop for BlurSystem {
    fn drop(&mut self) {
        unsafe {
            blur_shutdown(self.handle);
        }
    }
}

pub struct BlurWindow {
    handle: BlurWindowHandle,
}

impl BlurWindow {
    pub fn start(&self) -> Result<(), BlurErrorCode> {
        let code = unsafe { blur_start(self.handle) };
        if code == BlurErrorCode::Ok { Ok(()) } else { Err(code) }
    }

    pub fn stop(&self) -> Result<(), BlurErrorCode> {
        let code = unsafe { blur_stop(self.handle) };
        if code == BlurErrorCode::Ok { Ok(()) } else { Err(code) }
    }

    pub fn set_preset(&self, preset: BlurQualityPreset) -> Result<(), BlurErrorCode> {
        let code = unsafe { blur_set_preset(self.handle, preset) };
        if code == BlurErrorCode::Ok { Ok(()) } else { Err(code) }
    }

    pub fn set_pipeline(&self, json: &str) -> Result<(), BlurErrorCode> {
        let c_json = CString::new(json).map_err(|_| BlurErrorCode::InvalidParameter)?;
        let code = unsafe { blur_set_pipeline(self.handle, c_json.as_ptr()) };
        if code == BlurErrorCode::Ok { Ok(()) } else { Err(code) }
    }

    pub fn get_fps(&self) -> f32 {
        unsafe { blur_get_fps(self.handle) }
    }
}

impl Drop for BlurWindow {
    fn drop(&mut self) {
        unsafe {
            blur_destroy_window(self.handle);
        }
    }
}

unsafe impl Send for BlurWindow {}
unsafe impl Sync for BlurWindow {}
