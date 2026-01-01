use std::sync::Mutex;

// C API structure matching c_api.h
#[repr(C)]
struct BlurRect {
    left: i32,
    top: i32,
    right: i32,
    bottom: i32,
}

#[repr(C)]
struct BlurSystemOptionsC {
    enable_logging: i32,
    log_path: *const std::ffi::c_char,
    default_preset: i32,
}

#[repr(C)]
struct BlurWindowOptionsC {
    owner: *mut std::ffi::c_void,
    bounds: BlurRect,
    top_most: i32,
    click_through: i32,
}

extern "C" {
    fn blur_init(opts: *const BlurSystemOptionsC) -> *mut std::ffi::c_void;
    #[allow(dead_code)]
    fn blur_shutdown(sys: *mut std::ffi::c_void);
    fn blur_create_window(
        sys: *mut std::ffi::c_void,
        owner: *mut std::ffi::c_void,
        opts: *const BlurWindowOptionsC,
    ) -> *mut std::ffi::c_void;
    fn blur_destroy_window(window: *mut std::ffi::c_void);
    fn blur_start(window: *mut std::ffi::c_void) -> i32;
    fn blur_stop(window: *mut std::ffi::c_void) -> i32;
    fn blur_set_effect_type(window: *mut std::ffi::c_void, effect_type: i32) -> i32;
    fn blur_set_strength(window: *mut std::ffi::c_void, strength: f32) -> i32;
    fn blur_set_blur_param(window: *mut std::ffi::c_void, param: f32) -> i32;
    fn blur_set_tint_color(window: *mut std::ffi::c_void, r: f32, g: f32, b: f32, a: f32) -> i32;
    fn blur_set_noise_intensity(window: *mut std::ffi::c_void, intensity: f32) -> i32;
    fn blur_set_noise_scale(window: *mut std::ffi::c_void, scale: f32) -> i32;
    fn blur_set_noise_speed(window: *mut std::ffi::c_void, speed: f32) -> i32;
    fn blur_set_noise_type(window: *mut std::ffi::c_void, noise_type: i32) -> i32;
    fn blur_get_fps(window: *mut std::ffi::c_void) -> f32;

    // Rain Effect API
    fn blur_set_rain_intensity(window: *mut std::ffi::c_void, intensity: f32) -> i32;
    fn blur_set_rain_drop_speed(window: *mut std::ffi::c_void, speed: f32) -> i32;
    fn blur_set_rain_refraction(window: *mut std::ffi::c_void, strength: f32) -> i32;
    fn blur_set_rain_trail_length(window: *mut std::ffi::c_void, length: f32) -> i32;
    fn blur_set_rain_drop_size(window: *mut std::ffi::c_void, min_size: f32, max_size: f32) -> i32;
}

struct BlurState {
    sys: Mutex<Option<*mut std::ffi::c_void>>,
    window: Mutex<Option<*mut std::ffi::c_void>>,
}

unsafe impl Send for BlurState {}
unsafe impl Sync for BlurState {}

#[tauri::command]
fn start_blur(state: tauri::State<'_, BlurState>, effect_type: Option<i32>) -> Result<(), String> {
    let mut sys_lock = state.sys.lock().unwrap();
    let mut window_lock = state.window.lock().unwrap();

    if window_lock.is_some() {
        return Ok(());
    }

    unsafe {
        if sys_lock.is_none() {
            let sys_opts = BlurSystemOptionsC {
                enable_logging: 1,
                log_path: std::ptr::null(),
                default_preset: 0,
            };
            let sys = blur_init(&sys_opts);
            if sys.is_null() {
                return Err("Failed to initialize blur system".into());
            }
            *sys_lock = Some(sys);
        }

        let sys = sys_lock.unwrap();
        let opts = BlurWindowOptionsC {
            owner: std::ptr::null_mut(),
            bounds: BlurRect {
                left: 100,
                top: 100,
                right: 600,
                bottom: 500,
            },
            top_most: 1,
            click_through: 0,
        };

        // Passing null as owner for standalone window
        let window = blur_create_window(sys, std::ptr::null_mut(), &opts);
        if !window.is_null() {
            blur_start(window);

            // Apply effect type if specified (default: 0 = Gaussian)
            if let Some(t) = effect_type {
                blur_set_effect_type(window, t);
            }

            *window_lock = Some(window);
            Ok(())
        } else {
            Err("Failed to create blur window".into())
        }
    }
}

#[tauri::command]
fn stop_blur(state: tauri::State<'_, BlurState>) {
    let mut window_lock = state.window.lock().unwrap();
    if let Some(window) = window_lock.take() {
        unsafe {
            blur_stop(window);
            blur_destroy_window(window);
        }
    }
}

#[tauri::command]
fn update_blur_parameters(
    state: tauri::State<'_, BlurState>,
    effect_type: Option<i32>,
    strength: Option<f32>,
    param: Option<f32>,
    color: Option<(f32, f32, f32, f32)>,
) {
    let window_lock = state.window.lock().unwrap();
    if let Some(window) = *window_lock {
        unsafe {
            if let Some(t) = effect_type {
                blur_set_effect_type(window, t);
            }
            if let Some(s) = strength {
                blur_set_strength(window, s);
            }
            if let Some(p) = param {
                blur_set_blur_param(window, p);
            }
            if let Some((r, g, b, a)) = color {
                blur_set_tint_color(window, r, g, b, a);
            }
        }
    }
}

#[tauri::command]
fn update_noise_parameters(
    state: tauri::State<'_, BlurState>,
    intensity: Option<f32>,
    scale: Option<f32>,
    speed: Option<f32>,
    noise_type: Option<i32>,
) {
    let window_lock = state.window.lock().unwrap();
    if let Some(window) = *window_lock {
        unsafe {
            if let Some(i) = intensity {
                blur_set_noise_intensity(window, i);
            }
            if let Some(s) = scale {
                blur_set_noise_scale(window, s);
            }
            if let Some(v) = speed {
                blur_set_noise_speed(window, v);
            }
            if let Some(t) = noise_type {
                blur_set_noise_type(window, t);
            }
        }
    }
}

#[tauri::command]
fn get_blur_fps(state: tauri::State<'_, BlurState>) -> f32 {
    let window_lock = state.window.lock().unwrap();
    if let Some(window) = *window_lock {
        unsafe { blur_get_fps(window) }
    } else {
        0.0
    }
}

#[tauri::command]
fn update_rain_parameters(
    state: tauri::State<'_, BlurState>,
    intensity: Option<f32>,
    drop_speed: Option<f32>,
    refraction: Option<f32>,
    trail_length: Option<f32>,
    min_size: Option<f32>,
    max_size: Option<f32>,
) {
    let window_lock = state.window.lock().unwrap();
    if let Some(window) = *window_lock {
        unsafe {
            if let Some(i) = intensity {
                blur_set_rain_intensity(window, i);
            }
            if let Some(s) = drop_speed {
                blur_set_rain_drop_speed(window, s);
            }
            if let Some(r) = refraction {
                blur_set_rain_refraction(window, r);
            }
            if let Some(t) = trail_length {
                blur_set_rain_trail_length(window, t);
            }
            if let (Some(min), Some(max)) = (min_size, max_size) {
                blur_set_rain_drop_size(window, min, max);
            }
        }
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(BlurState {
            sys: Mutex::new(None),
            window: Mutex::new(None),
        })
        .invoke_handler(tauri::generate_handler![
            start_blur,
            stop_blur,
            update_blur_parameters,
            update_noise_parameters,
            update_rain_parameters,
            get_blur_fps
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
