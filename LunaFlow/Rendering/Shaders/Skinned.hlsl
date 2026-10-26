cbuffer Scene : register(b0)
{
    row_major float4x4 ViewProjection;
    row_major float4x4 Skin[256];
    row_major float4x4 Normal[256];
    float4 BaseColor;
    float4 Options;
};
Texture2D Diffuse : register(t0);
SamplerState LinearWrap : register(s0);
struct Input {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
    float4 color : COLOR0;
};
struct Output {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
Output VSMain(Input input)
{
    float4 position = 0;
    float3 normal = 0;
    [unroll] for (int i = 0; i < 4; ++i) {
        position += mul(float4(input.position,1),Skin[input.joints[i]]) * input.weights[i];
        normal += mul(float4(input.normal,0),Normal[input.joints[i]]).xyz * input.weights[i];
    }
    Output result;
    result.position = mul(position,ViewProjection);
    result.normal = normal;
    result.uv = input.uv;
    result.color = input.color;
    return result;
}
float4 PSMain(Output input) : SV_TARGET
{
    float4 color = BaseColor * input.color;
    if (Options.x > .5) color *= Diffuse.Sample(LinearWrap,input.uv);
    if (Options.y > .5) return color;
    if (color.a < .1) discard;
    float3 n = normalize(input.normal);
    if (!all(isfinite(n))) n = float3(0,1,0);
    float light = max(0,dot(n,normalize(float3(-.5,.8,-.65))));
    float fill = max(0,dot(n,normalize(float3(.7,.2,.5))));
    float hemi = .19 + .15 * (n.y*.5+.5);
    return float4(color.rgb*(hemi+.62*light+.16*fill),1);
}
