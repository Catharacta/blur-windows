const { invoke } = window.__TAURI__.core;

let statusText, fpsCounter, indicator, logContainer;

function appendLog(msg) {
  const entry = document.createElement("div");
  entry.className = "log-entry";
  const time = new Date().toLocaleTimeString([], { hour12: false });
  entry.innerHTML = `<span class="log-time">[${time}]</span> ${msg}`;
  logContainer.prepend(entry);
}

async function startBlur() {
  try {
    const effectType = parseInt(document.getElementById("select-effect").value);
    await invoke("start_blur", { effectType });
    document.body.classList.add("running");
    statusText.textContent = "Running";
    appendLog(`Blur started (Effect: ${effectType}).`);
  } catch (e) {
    appendLog(`Error: ${e}`);
  }
}

async function stopBlur() {
  await invoke("stop_blur");
  document.body.classList.remove("running");
  statusText.textContent = "Stopped";
  appendLog("Blur stopped.");
}

function hexToRgba(hex, alphaPercent) {
  const r = parseInt(hex.slice(1, 3), 16) / 255;
  const g = parseInt(hex.slice(3, 5), 16) / 255;
  const b = parseInt(hex.slice(5, 7), 16) / 255;
  const a = alphaPercent / 100;
  return [r, g, b, a];
}

async function updateBlurParams() {
  const effect = parseInt(document.getElementById("select-effect").value);
  const strength = parseInt(document.getElementById("slider-strength").value) / 100;
  const param = parseFloat(document.getElementById("slider-param").value);
  const colorHex = document.getElementById("color-tint").value;
  const alphaPercent = parseInt(document.getElementById("slider-alpha").value);

  await invoke("update_blur_parameters", {
    effectType: effect,
    strength: strength,
    param: param,
    color: hexToRgba(colorHex, alphaPercent)
  });
}

async function updateNoiseParams() {
  const intensity = parseInt(document.getElementById("slider-noise-int").value) / 100;
  const scale = parseFloat(document.getElementById("slider-noise-scale").value);
  const speed = parseInt(document.getElementById("slider-noise-speed").value) / 10;
  const noiseType = parseInt(document.querySelector('input[name="noise-type"]:checked').value);

  await invoke("update_noise_parameters", {
    intensity,
    scale,
    speed,
    noiseType
  });
}

async function updateRainParams() {
  const intensity = parseInt(document.getElementById("slider-rain-int").value) / 100;
  const dropSpeed = parseInt(document.getElementById("slider-rain-speed").value) / 10;
  const refraction = parseInt(document.getElementById("slider-rain-refraction").value) / 100;
  const trailLength = parseInt(document.getElementById("slider-rain-trail").value) / 100;

  await invoke("update_rain_parameters", {
    intensity,
    dropSpeed,
    refraction,
    trailLength
  });
}

function toggleRainSection() {
  const effectType = parseInt(document.getElementById("select-effect").value);
  const rainSection = document.getElementById("rain-section");
  rainSection.style.display = (effectType === 4) ? "block" : "none";
}

window.addEventListener("DOMContentLoaded", () => {
  statusText = document.getElementById("status-text");
  fpsCounter = document.getElementById("fps-counter");
  indicator = document.querySelector(".indicator");
  logContainer = document.getElementById("log-container");

  document.getElementById("btn-start").addEventListener("click", startBlur);
  document.getElementById("btn-stop").addEventListener("click", stopBlur);

  const blurInputs = ["select-effect", "slider-strength", "slider-param", "color-tint", "slider-alpha"];
  blurInputs.forEach(id => {
    document.getElementById(id).addEventListener("input", (e) => {
      const valSpan = document.getElementById(`val-${id.replace('slider-', '').replace('select-', '').replace('color-', '')}`);
      if (valSpan) valSpan.textContent = e.target.value;
      updateBlurParams();
      if (id === "select-effect") toggleRainSection();
    });
  });

  const noiseInputs = ["slider-noise-int", "slider-noise-scale", "slider-noise-speed"];
  noiseInputs.forEach(id => {
    document.getElementById(id).addEventListener("input", (e) => {
      const valSpan = document.getElementById(`val-${id.replace('slider-', '')}`);
      if (valSpan) valSpan.textContent = (id === "slider-noise-speed") ? (e.target.value / 10).toFixed(1) : e.target.value;
      updateNoiseParams();
    });
  });

  document.querySelectorAll('input[name="noise-type"]').forEach(radio => {
    radio.addEventListener("change", updateNoiseParams);
  });

  // Rain effect controls
  const rainInputs = ["slider-rain-int", "slider-rain-speed", "slider-rain-refraction", "slider-rain-trail"];
  rainInputs.forEach(id => {
    document.getElementById(id).addEventListener("input", (e) => {
      const valSpan = document.getElementById(`val-${id.replace('slider-', '')}`);
      if (valSpan) {
        if (id === "slider-rain-speed") {
          valSpan.textContent = (e.target.value / 10).toFixed(1);
        } else {
          valSpan.textContent = e.target.value;
        }
      }
      updateRainParams();
    });
  });

  // Blur Window Bounds controls
  async function updateBlurBounds() {
    const left = parseInt(document.getElementById("slider-pos-x").value);
    const top = parseInt(document.getElementById("slider-pos-y").value);
    const width = parseInt(document.getElementById("slider-size-w").value);
    const height = parseInt(document.getElementById("slider-size-h").value);

    try {
      await invoke("set_blur_bounds", { left, top, width, height });
    } catch (e) {
      appendLog(`Bounds error: ${e}`);
    }
  }

  const boundsInputs = ["slider-pos-x", "slider-pos-y", "slider-size-w", "slider-size-h"];
  boundsInputs.forEach(id => {
    document.getElementById(id).addEventListener("input", (e) => {
      const valSpan = document.getElementById(`val-${id.replace('slider-', '')}`);
      if (valSpan) valSpan.textContent = e.target.value;
      updateBlurBounds();
    });
  });

  setInterval(async () => {
    if (document.body.classList.contains("running")) {
      const fps = await invoke("get_blur_fps");
      fpsCounter.textContent = `${fps.toFixed(1)} FPS`;
    }
  }, 1000);

  appendLog("Tauri Demo UI initialized.");
});
