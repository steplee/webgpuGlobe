#pragma once

namespace {

    const char* src_flat_pos_color = R"(
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

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;

struct VertexInput {
    @location(0) positionThickness1: vec4<f32>,
    @location(1) positionThickness2: vec4<f32>,
    @location(2) color: vec4<f32>,
    @builtin(vertex_index) vertex_index: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
	@location(0) color: vec4<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	// The perspective divide is usually the rendering API pipeline's / gpu's responsibility.
	// But by doing it now (and outputting w=1 later) we can more easily control screen space width.
	var p1 = scd.mvp * vec4(vi.positionThickness1.xyz, 1.);
	var p2 = scd.mvp * vec4(vi.positionThickness2.xyz, 1.);
	var p1_3 = p1.xyz / p1.w;
	var p2_3 = p2.xyz / p2.w;
	if (p1.w < 0.) {p1_3 *= -1.;} // FIXME: This is wrong somehow. Weird distortion. Must clip?
	if (p2.w < 0.) {p2_3 *= -1.;}

	var p : vec3f;

	var d = vi.positionThickness1.w;
	if (vi.vertex_index == 1 ||
		vi.vertex_index == 2 ||
		vi.vertex_index == 3) {
		d = vi.positionThickness2.w;
	}
	d /= scd.wh.y;

	var direction = normalize(p1.xyz/p1.w - p2.xyz/p2.w);
	let perp3 = -normalize(cross(direction, vec3f(0.,0.,1.)));
	let perp = d * perp3.xyz;

	if (vi.vertex_index % 6 == 0) {
		p = p1_3 + perp;
	}
	else if (vi.vertex_index % 6 == 1) {
		p = p2_3 + perp;
	}
	else if (vi.vertex_index % 6 == 2) {
		p = p2_3 - perp;
	}
	else if (vi.vertex_index % 6 == 3) {
		p = p2_3 - perp;
	}
	else if (vi.vertex_index % 6 == 4) {
		p = p1_3 - perp;
	}
	else if (vi.vertex_index % 6 == 5) {
		p = p1_3 + perp;
	}

	vo.position = vec4(p, 1.);
 
	vo.color = vi.color;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return vo.color * scd.colorMult;
}
)";



}

