Shader "LunaFlow/Imported Particle"
{
    Properties
    {
        _MainTex ("Particle", 2D) = "white" {}
        [HideInInspector] _SrcBlend ("Source Blend", Float) = 5
        [HideInInspector] _DstBlend ("Destination Blend", Float) = 10
    }
    SubShader
    {
        Tags { "Queue"="Transparent" "RenderType"="Transparent" "IgnoreProjector"="True" }
        Pass
        {
            // No LightMode tag: default unlit pass in both Built-in and URP.
            Blend [_SrcBlend] [_DstBlend]
            ZWrite Off
            Cull Off
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"
            struct Vertex { float4 position:POSITION; float4 color:COLOR; float2 uv:TEXCOORD0; };
            struct Fragment { float4 position:SV_POSITION; float4 color:COLOR; float2 uv:TEXCOORD0; };
            sampler2D _MainTex;
            Fragment vert(Vertex v) { Fragment o; o.position=UnityObjectToClipPos(v.position); o.color=v.color; o.uv=v.uv; return o; }
            fixed4 frag(Fragment v):SV_Target { return tex2D(_MainTex,v.uv)*v.color; }
            ENDCG
        }
    }
}
