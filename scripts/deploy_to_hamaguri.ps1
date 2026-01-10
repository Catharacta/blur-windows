$ErrorActionPreference = "Stop"

# Configuration
$TARGET_DIR = "C:\Users\atuya\Documents\develop\hamaguri-blur\blurwindow-x64"
$TAURI_LIBS_DIR = "C:\Users\atuya\Documents\develop\hamaguri-blur\src-tauri\libs"
$BUILD_DIR = "$PSScriptRoot\..\build"
$SOURCE_DIR = "$PSScriptRoot\.."

Write-Host "=== BlurWindow Local Deploy Script ===" -ForegroundColor Cyan
Write-Host "Target: $TARGET_DIR"
Write-Host "Tauri libs: $TAURI_LIBS_DIR"

# 1. Build Release
Write-Host "`n[1/4] Building Release configuration..." -ForegroundColor Yellow
Set-Location $SOURCE_DIR
cmake --build build --config Release --target blurwindow

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed!"
}

# 2. Prepare Target Directory
Write-Host "`n[2/4] Preparing target directory..." -ForegroundColor Yellow
if (-not (Test-Path $TARGET_DIR)) {
    New-Item -ItemType Directory -Force -Path $TARGET_DIR | Out-Null
}

# Create subdirectories if they don't exist
$dirs = @("bin", "lib", "include", "docs")
foreach ($d in $dirs) {
    $p = Join-Path $TARGET_DIR $d
    if (-not (Test-Path $p)) {
        New-Item -ItemType Directory -Force -Path $p | Out-Null
    }
}

# 3. Copy Files to blurwindow-x64
Write-Host "`n[3/4] Copying files to blurwindow-x64..." -ForegroundColor Yellow

# Copy DLL
Copy-Item "$BUILD_DIR\bin\Release\blurwindow.dll" "$TARGET_DIR\bin\" -Force -Verbose
# Copy PDB (Optional, good for debugging)
if (Test-Path "$BUILD_DIR\bin\Release\blurwindow.pdb") {
    Copy-Item "$BUILD_DIR\bin\Release\blurwindow.pdb" "$TARGET_DIR\bin\" -Force
}

# Copy LIB
Copy-Item "$BUILD_DIR\lib\Release\blurwindow.lib" "$TARGET_DIR\lib\" -Force -Verbose

# Copy Headers
Copy-Item "$SOURCE_DIR\include\blurwindow\*.h" "$TARGET_DIR\include\" -Recurse -Force -Verbose

# Copy Docs
Copy-Item "$SOURCE_DIR\docs\*.md" "$TARGET_DIR\docs\" -Force -Verbose

# 4. Copy DLL to Tauri libs directory (for cargo build)
Write-Host "`n[4/4] Copying DLL to Tauri libs directory..." -ForegroundColor Yellow
if (-not (Test-Path $TAURI_LIBS_DIR)) {
    New-Item -ItemType Directory -Force -Path $TAURI_LIBS_DIR | Out-Null
}
Copy-Item "$BUILD_DIR\bin\Release\blurwindow.dll" "$TAURI_LIBS_DIR\" -Force -Verbose
Copy-Item "$BUILD_DIR\lib\Release\blurwindow.lib" "$TAURI_LIBS_DIR\" -Force -Verbose

Write-Host "`n[Success] Deployed to $TARGET_DIR" -ForegroundColor Green
Write-Host "[Success] Deployed DLL/LIB to $TAURI_LIBS_DIR" -ForegroundColor Green
Write-Host "You can now run hamaguri-blur with: cargo tauri dev"

