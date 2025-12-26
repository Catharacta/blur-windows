use std::ffi::c_char;
use windows::Win32::Foundation::HWND;

// Forward matches with c_api.h

#[repr(C)]
#[derive(Copy, Clone)]
pub struct BlurSystemHandle(pub *mut std::ffi::c_void);

#[repr(C)]
#[derive(Copy, Clone)]
pub struct BlurWindowHandle(pub *mut std::ffi::c_void);

#[repr(i32)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum BlurQualityPreset {
    High = 0,
    Balanced = 1,
    Performance = 2,
    Minimal = 3,
}

#[repr(i32)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum BlurErrorCode {
    Ok = 0,
    NotInitialized = -1,
    InvalidHandle = -2,
    InvalidParameter = -3,
    D3D11Failed = -4,
    CaptureFailed = -5,
    Unknown = -99,
}

#[repr(C)]
pub struct BlurRect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

#[repr(C)]
pub struct BlurSystemOptionsC {
    pub enable_logging: i32,     // 0 = false, 1 = true
    pub log_path: *const c_char, // NULL for console
    pub default_preset: BlurQualityPreset,
}

#[repr(C)]
pub struct BlurWindowOptionsC {
    pub owner: HWND,
    pub bounds: BlurRect,
    pub top_most: i32,      // 0 = false, 1 = true
    pub click_through: i32, // 0 = false, 1 = true
}

#[link(name = "blurwindow")]
extern "C" {
    pub fn blur_init(opts: *const BlurSystemOptionsC) -> BlurSystemHandle;
    pub fn blur_shutdown(sys: BlurSystemHandle);
    pub fn blur_create_window(
        sys: BlurSystemHandle,
        owner: HWND,
        opts: *const BlurWindowOptionsC,
    ) -> BlurWindowHandle;
    pub fn blur_destroy_window(window: BlurWindowHandle);
    pub fn blur_start(window: BlurWindowHandle) -> BlurErrorCode;
    pub fn blur_stop(window: BlurWindowHandle) -> BlurErrorCode;
    pub fn blur_set_preset(window: BlurWindowHandle, preset: BlurQualityPreset) -> BlurErrorCode;
    pub fn blur_set_pipeline(window: BlurWindowHandle, json_config: *const c_char)
        -> BlurErrorCode;
    pub fn blur_set_bounds(window: BlurWindowHandle, bounds: *const BlurRect) -> BlurErrorCode;
    pub fn blur_get_fps(window: BlurWindowHandle) -> f32;
    pub fn blur_get_last_error() -> *const c_char;
}

// Safe wrapper implementation would go here...
pub mod safe;
pub use safe::*;
