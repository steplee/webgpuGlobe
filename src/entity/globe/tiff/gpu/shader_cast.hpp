#pragma once
namespace {
    static const char* shaderSourceCast = R"(

struct SceneCameraData {
	mvp: mat4x4<f32>,
	imvp: mat4x4<f32>,
	mv: mat4x4<f32>,

	eye: vec3f,
	colorMult: vec4f,

	sun: vec4f,
	haeAlt: f32,
	haze: f32,
	time: f32,
	dt: f32,
}

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;

@group(1) @binding(0) var castTex: texture_2d<f32>;
@group(1) @binding(1) var castSampler: sampler;
@group(1) @binding(2) var castMvp: mat4x4<f32>;


struct VertexInput {
	@builtin(instance_index) instance_index: u32,
    @location(0) position: vec3<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var p = scd.mvp * vec4(vi.position, 1.);
	vo.position = p;

	vo.color = scd.colorMult;
	vo.uv = vi.uv;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	let uv = vo.uv;

	let texColor = textureSample(castTex, castSampler, uv);

	let color = vo.color * texColor;

	return color;
}

)";
}

