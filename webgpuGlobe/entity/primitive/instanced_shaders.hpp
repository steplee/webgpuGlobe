#pragma once

namespace {

    const char* src_flat_pos = R"(

	Not updated. Will nto work.

struct InstancingData {
	modelRow1: vec4f,
	modelRow2: vec4f,
	modelRow3: vec4f,
	color: vec4f,
}

struct SceneCameraData {
	mvp: mat4x4<f32>,
	imvp: mat4x4<f32>,
	mv: mat4x4<f32>,

	eye: vec3f,
	colorMult: vec4f,

	wh: vec2f,
	sun: vec4f,
	haeAlt: f32,
	haze: f32,
	time: f32,
	dt: f32,
}

@group(0) @binding(0) var<uniform> scd: SceneCameraData;
@group(1) @binding(0) var<uniform> id: InstancingData;

struct VertexInput {
    @location(0) position: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var p = scd.mvp * vec4(vi.position, 1.);
	vo.position = p;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return scd.colorMult;
}
)";

    const char* src_flat_pos_color = R"(

struct InstanceInput {
	@location(5) modelRow1: vec4f,
	@location(6) modelRow2: vec4f,
	@location(7) modelRow3: vec4f,
	@location(8) color: vec4f,
}

struct SceneCameraData {
	mvp: mat4x4<f32>,
	imvp: mat4x4<f32>,
	mv: mat4x4<f32>,

	eye: vec3f,
	colorMult: vec4f,

	wh: vec2f,
	sun: vec4f,
	haeAlt: f32,
	haze: f32,
	time: f32,
	dt: f32,
}

@group(0) @binding(0) var<uniform> scd: SceneCameraData;
// @group(1) @binding(0) var<uniform> id: InstancingData;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
	@location(0) color: vec4<f32>,
};

@vertex
fn vs_main(vi: VertexInput, id: InstanceInput) -> VertexOutput {
	var vo : VertexOutput;

	let model = transpose(mat4x4f(
		id.modelRow1,
		id.modelRow2,
		id.modelRow3,
		vec4f(0.,0.,0.,1.)));

	var p = scd.mvp * model * vec4(vi.position, 1.);
	vo.position = p;
	vo.color = id.color * vi.color;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return vo.color * scd.colorMult;
}
)";



}

