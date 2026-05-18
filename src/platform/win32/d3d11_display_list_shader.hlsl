cbuffer FrameConstants : register(b0) {
    float2 target_size;
    float textured;
    float texture_mode;
};

Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

struct VSInput {
    float2 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PSOutput {
    float4 color : SV_Target0;
    float4 blend_coverage : SV_Target1;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    const float2 clip = float2(input.position.x / target_size.x * 2.0f - 1.0f,
                               1.0f - input.position.y / target_size.y * 2.0f);
    output.position = float4(clip, 0.0f, 1.0f);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

PSOutput make_output(float4 color) {
    PSOutput output;
    output.color = color;
    output.blend_coverage = color;
    return output;
}

PSOutput make_subpixel_output(float3 coverage, float4 input_color) {
    const float3 source_coverage = saturate(coverage * input_color.a);
    const float alpha_coverage = max(source_coverage.r, max(source_coverage.g, source_coverage.b));
    PSOutput output;
    output.color = float4(input_color.rgb * saturate(coverage), alpha_coverage);
    output.blend_coverage = float4(source_coverage, alpha_coverage);
    return output;
}

PSOutput ps_main(PSInput input) {
    if (textured > 0.5f) {
        const float4 sample = source_texture.Sample(source_sampler, input.uv);
        if (texture_mode > 3.5f) {
            const float distance = sample.r - 0.5f;
            const float width = max(fwidth(sample.r), 0.001f);
            const float coverage = smoothstep(-width, width, distance);
            return make_output(float4(input.color.rgb * coverage, input.color.a * coverage));
        }
        if (texture_mode > 2.5f) {
            return make_subpixel_output(sample.rgb, input.color);
        }
        if (texture_mode > 1.5f) {
            return make_output(float4(input.color.rgb * sample.r, input.color.a * sample.r));
        }
        return make_output(sample * input.color);
    }
    return make_output(input.color);
}
