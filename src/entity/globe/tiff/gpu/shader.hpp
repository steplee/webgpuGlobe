#pragma once
namespace {
    static const char* shaderSource = R"(

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

@group(1) @binding(0) var sharedTex: texture_2d_array<f32>;
@group(1) @binding(1) var sharedSampler: sampler;


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
    @location(2) @interpolate(flat) tex_index: u32,
};

//
// NOTE: This uses the trick of encoding the tile's texture array index as `instance_index`.
//

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var p = scd.mvp * vec4(vi.position, 1.);
	vo.position = p;

	vo.color = scd.colorMult;
	vo.uv = vi.uv;

	vo.tex_index = vi.instance_index;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	let uv = vo.uv;

	let texColor = textureSample(sharedTex, sharedSampler, uv, vo.tex_index);

	let color = vo.color * texColor;

	return color;
}

)";
}
