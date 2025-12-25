// FullscreenVS.hlsl
// Fullscreen triangle vertex shader - renders a single triangle covering the entire screen
// No vertex buffer needed, uses SV_VertexID to generate vertices

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Generate fullscreen triangle from vertex ID
// Vertex 0: (-1, 3)  -> UV (0, -1)
// Vertex 1: (-1, -1) -> UV (0, 1)  
// Vertex 2: (3, -1)  -> UV (2, 1)
VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;
    
    // Generate UV coordinates
    output.texcoord = float2((vertexId << 1) & 2, vertexId & 2);
    
    // Generate clip-space position
    // UV (0,0) -> Clip (-1, 1)
    // UV (1,1) -> Clip (1, -1)
    output.position = float4(output.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    
    return output;
}
