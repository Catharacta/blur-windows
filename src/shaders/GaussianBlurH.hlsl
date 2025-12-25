// GaussianBlurH.hlsl
// Horizontal pass of separable Gaussian blur

Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;   // 1.0 / textureSize
    float sigma;        // Gaussian sigma
    int radius;         // Sample radius (number of samples on each side)
    float4 padding;     // Padding to 16-byte alignment
};

// Gaussian weight calculation
float GaussianWeight(float x, float s) {
    float coefficient = 1.0f / (sqrt(2.0f * 3.14159265f) * s);
    return coefficient * exp(-0.5f * (x * x) / (s * s));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    
    // Sample horizontally
    [unroll(64)]
    for (int i = -radius; i <= radius; i++) {
        float weight = GaussianWeight(float(i), sigma);
        float2 sampleOffset = float2(float(i) * texelSize.x, 0.0f);
        
        color += inputTexture.Sample(linearSampler, texcoord + sampleOffset) * weight;
        weightSum += weight;
    }
    
    // Normalize
    return color / weightSum;
}
