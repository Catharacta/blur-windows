use blur_windows::*;
use std::thread;
use std::time::Duration;
use windows::Win32::Foundation::HWND;

fn main() -> Result<(), String> {
    println!("Rust BlurWindow Example");

    // Initialize the system
    let system = BlurSystem::new()?;
    println!("System initialized.");

    // Create a window (owner = 0, no owner)
    let window = system.create_window(HWND::default(), 200, 200, 600, 450)?;
    println!("Window created.");

    // Start effect
    window
        .start()
        .map_err(|e| format!("Start failed: {:?}", e))?;
    println!("Effect started. Running for 5 seconds...");

    // Run for 5 seconds and print FPS
    for i in 1..=5 {
        thread::sleep(Duration::from_secs(1));
        println!("  Time: {}s, FPS: {:.1}", i, window.get_fps());
    }

    println!("Stopping effect.");
    window.stop().map_err(|e| format!("Stop failed: {:?}", e))?;

    println!("Example complete. Auto-cleanup via Drop.");
    Ok(())
}
