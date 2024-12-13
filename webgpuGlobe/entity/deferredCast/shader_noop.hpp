namespace {
    const char* noop_source = R"(

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

@group(1) @binding(0) var texColor: texture_2d<f32>;
@group(1) @binding(1) var texDepth: texture_depth_2d;
@group(1) @binding(2) var texSampler: sampler;

struct VertexInput {
    @builtin(vertex_index) vertex_index: u32
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {

	let vertex_index = vi.vertex_index;
	var ps = array<vec4f, 6> (
		vec4f(-1., -1., 1., 1.),
		vec4f( 1., -1., 1., 1.),
		vec4f( 1.,  1., 1., 1.),

		vec4f( 1.,  1., 1., 1.),
		vec4f(-1.,  1., 1., 1.),
		vec4f(-1., -1., 1., 1.),
	);

	var vo : VertexOutput;
	vo.position = ps[vi.vertex_index];
	vo.uv = ps[vi.vertex_index].xy * .5 + .5;
	vo.uv.y = 1. - vo.uv.y;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	let texColor = textureSample(texColor, texSampler, vo.uv);
	let c = texColor;
	return c;
}
)";
}
