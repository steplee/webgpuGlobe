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

struct CastData {
	mvp1: mat4x4<f32>,
	mvp2: mat4x4<f32>,
	color1: vec4f,
	color2: vec4f,
	mask: u32,
}

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;

@group(1) @binding(0) var sharedTex: texture_2d_array<f32>;
@group(1) @binding(1) var sharedSampler: sampler;

@group(2) @binding(0) var castTex: texture_2d<f32>;
@group(2) @binding(1) var castSampler: sampler;
@group(2) @binding(2) var<uniform> castData: CastData;


struct VertexInput {
	@builtin(instance_index) instance_index: u32,
    @location(0) position: vec3<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) @interpolate(flat) main_tex_index: u32,
    @location(1) color: vec4<f32>,
    @location(2) uv_main: vec2<f32>,
    @location(3) uv_cast1: vec2<f32>,
    @location(4) uv_cast2: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var p = scd.mvp * vec4(vi.position, 1.);
	vo.position = p;

	vo.color = scd.colorMult;
	vo.uv_main = vi.uv;
	vo.main_tex_index = vi.instance_index;

	var castA_4 = (castData.mvp1 * vec4(vi.position,1.));
	var castA_3 = castA_4.xyz / castA_4.w;
	// if (castA_4.w < 0) { castA_3 = vec3(-1.); }
	// vo.uv_cast1 = castA_3.xy * .5 + .5;
	vo.uv_cast1 = castA_3.xy * vec2f(.5, -.5) + .5;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	let texColor = textureSample(sharedTex, sharedSampler, vo.uv_main, vo.main_tex_index);

	// let texColor = textureSample(castTex, castSampler, vo.uv_main);

	var color = vo.color * texColor;

	if (vo.uv_cast1.x > 0 && vo.uv_cast1.y > 0 && vo.uv_cast1.x < 1 && vo.uv_cast1.y < 1) {
		color += textureSample(castTex, castSampler, vo.uv_cast1);
	}

	return color;
}

)";
}
