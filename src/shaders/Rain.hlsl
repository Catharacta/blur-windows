Texture2D InputTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer Parameters : register(b0) {
    float Time;
    float Intensity;
    float Speed;
    float Brightness;
    float NormalStrength;
    float Zoom;
    float2 Resolution;    // Screen resolution
    float2 TexResolution; // Texture resolution
    int PostProcessing;
    int Lightning;      
    float2 Padding;     
};

#define S(a, b, t) smoothstep(a, b, t)

// --- Random Functions ---
float3 N13(float p) {
    float3 p3 = frac(float3(p, p, p) * float3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yzx + 19.19);
    return frac(float3((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x));
}

float4 N14(float t) {
    return frac(sin(t * float4(123., 1024., 1456., 264.)) * float4(6547., 345., 8799., 1564.));
}

float N(float t) {
    return frac(sin(t * 12345.564) * 7658.76);
}

float Saw(float b, float t) {
    return S(0., b, t) * S(1., b, t);
}

// --- Drop Simulation ---
float2 DropLayer2(float2 uv, float t) {
    float2 UV = uv;

    uv.y += t * 0.75;
    float2 a = float2(6., 1.);
    float2 grid = a * 2.;
    float2 id = floor(uv * grid);

    float colShift = N(id.x);
    uv.y += colShift;

    id = floor(uv * grid);
    float3 n = N13(id.x * 35.2 + id.y * 2376.1);
    float2 st = frac(uv * grid) - float2(.5, 0);

    float x = n.x - .5;

    float y = UV.y * 20.;
    float wiggle = sin(y + sin(y));
    x += wiggle * (.5 - abs(x)) * (n.z - .5);
    x *= .7;
    float ti = frac(t + n.z);
    y = (Saw(.85, ti) - .5) * .9 + .5;
    float2 p = float2(x, y);

    float d = length((st - p) * a.yx);

    float mainDrop = S(.4, .0, d);

    float r = sqrt(S(1., y, st.y));
    float cd = abs(st.x - x);
    float trail = S(.23 * r, .15 * r * r, cd);
    float trailFront = S(-.02, .02, st.y - y);
    trail *= trailFront * r * r;

    y = UV.y;
    float trail2 = S(.2 * r, .0, cd);
    float droplets = max(0., (sin(y * (1. - y) * 120.) - st.y)) * trail2 * trailFront * n.z;
    y = frac(y * 10.) + (st.y - .5);
    float dd = length(st - float2(x, y));
    droplets = S(.3, 0., dd);
    float m = mainDrop + droplets * r * trailFront;

    // x: drop amount, y: distance mask
    return float2(m, m);
}

float StaticDrops(float2 uv, float t) {
    uv *= 40.;

    float2 id = floor(uv);
    uv = frac(uv) - .5;
    float3 n = N13(id.x * 107.45 + id.y * 3543.654);
    float2 p = (n.xy - .5) * .7;
    float d = length(uv - p);

    float fade = Saw(.025, frac(t + n.z));
    float c = S(.3, 0., d) * frac(n.z * 10.) * fade;
    return c;
}

float2 GetDrops(float2 uv, float t, float l0, float l1, float l2) {
    float s = StaticDrops(uv, t) * l0;
    float2 m1 = DropLayer2(uv, t) * l1;
    float2 m2 = DropLayer2(uv * 1.85, t) * l2;

    float c = s + m1.x + m2.x;
    c = S(.3, 1., c);

    return float2(c, max(m1.x * l0, m2.x * l1)); 
}

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    float2 uv = input.Tex;
    
    // UV for simulation (aspect corrected)
    float aspect = Resolution.x / Resolution.y;
    float2 st = uv * float2(aspect, 1.0);
    
    // Time & Zoom
    float T = Time + (sin(Time * sin(Time * sin(Time) * 0.5)) * 0.5);
    float t = T * .2 * Speed;
    
    // Zoom
    float finalZoom = Zoom > 0.0 ? Zoom : 1.0;
    st *= finalZoom; // Original shader code was: uv *= (.7 + zoom * .3) * u_zoom;
    
    float rainAmount = Intensity;

    float staticDrops = S(-.5, 1., rainAmount) * 2.;
    float layer1 = S(.25, .75, rainAmount);
    float layer2 = S(.0, .5, rainAmount);

    float2 c = GetDrops(st, t, staticDrops, layer1, layer2);

    // Calculate Normals (Expensive mode for quality)
    // ddx/ddy often produces blocky artifacts for smooth procedural noise, so manual sampling is preferred for high quality
    float2 e = float2(.001, 0.) * NormalStrength; 
    float cx = GetDrops(st + e, t, staticDrops, layer1, layer2).x;
    float cy = GetDrops(st + e.yx, t, staticDrops, layer1, layer2).x;
    float2 n = float2(cx - c.x, cy - c.x);

    // Sample background with offset
    // Note: Assuming InputTexture is ALREADY blurred by previous passes if desired.
    // The original shader handles blur iterations internally, but we use a separate pipeline.
    float4 col = InputTexture.Sample(LinearSampler, uv + n);

    // Post processing (e.g. slight color shift or lightning)
    if (PostProcessing) {
        col.rgb *= lerp(float3(1.,1.,1.), float3(0.8, 0.9, 1.3), Intensity * 0.5);
    }
    
    // Lightning
    if (Lightning) {
        float timeVal = (T + 3.) * .5;
        float lightning = sin(timeVal * sin(timeVal * 10.));
        lightning *= pow(max(0., sin(timeVal + sin(timeVal))), 10.);
        col.rgb *= 1. + lightning * S(0., 10., T) * mix(1., .1, 0.);
    }

    col.rgb *= Brightness;
    
    return col;
}
