#include "fog.h"
#include "app/shader.h"

namespace {
    const char* quadSource = R"(

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


// The `w` coord has distance.
fn first_wgs84_intersection(eye: vec3f, rd: vec3f, alt: f32) -> vec4f {
	let a = 1.;
	// let b = 6378137.0/6356752.314245179;
	let b = 1.; // FIXME: RE-ENABLE
	let axes_scale = vec3f(1, 1, a/b);

	let p1 = eye * axes_scale;
	var v1 = rd * axes_scale;

	let c_ = dot(p1,p1) - pow(1.0 + alt, 2.);

	// if (c_ < 0.) {v1 = -v1;}

	let a_ = dot(v1,v1);
	let b_ = 2 * dot(v1,p1);
	let discrim = b_*b_ - 4*a_*c_;
	if (discrim > 0) {
		var t1 = (-b_ - sqrt(discrim)) / (2 * a_);
		var t2 = (-b_ + sqrt(discrim)) / (2 * a_);
		if (t1 != t1) { t1 = -9e4; }
		if (t2 != t2) { t2 = -9e4; }

		// if (c_ <= 0.) {t1 = -t1;}
		// if (c_ <= 0.) {t2 = -t2;}

		/*
		var t = t1;
		if (t1 <= 0) { t = t2; }
		if (t < 0) {t = 0.;}
		// if (t2 <= t1) { t = t2; }
		*/

		// NOTE: Actually return FARTHER.
		// if (c_ <= 0.) {t1 = -t1;}
		// if (c_ <= 0.) {t2 = -t2;}
		var t = t2;
		if (t2 <= 0) { t = t1; }
		if (t < 0) {t = 0.;}



		let xpt = p1 + v1 * t;
		return vec4(xpt, length(xpt - eye));
	} else {
		return vec4f(-1);
	}
}

//
// If we are lower than a certain altitude, smoothly start adding fog with depth.
// Does not handle sky well.
// Overall, bad. But was useful for proof-of-concept when working with passes etc.
//
fn blend_fog_v1(base_color: vec4f, eye: vec3f, world_pt: vec3f, dist: f32, haeAlt: f32) -> vec4f {
	let fog_color = vec4f(.01, .3, .9, .9);

	var fog = 1. - exp(-dist*8.);
	// fog *= pow(smoothstep(1.3,.997, haeAlt), 4.);
	fog *= pow(smoothstep(.9,.0, haeAlt), 6.);

	var c = mix(base_color, fog_color, fog);
	return c;
}

//
//
//
fn blend_fog_v2(base_color: vec4f, eye: vec3f, rd: vec3f, world_pt: vec3f, dist: f32, distNdc: f32, haeAlt: f32) -> vec4f {

	var fog_color = vec4f(.01, .3, .8, 1.);
	var fog : f32;


	if (distNdc < .999999) {
		fog = 1. - exp(-dist*15.);
		// fog *= pow(smoothstep(.9,.0, haeAlt), 6.);

		let d = dot(normalize(world_pt), rd);
		fog *= 1. - clamp(-d, 0., 1.);

		let dd = pow(1. - clamp(-d, 0., 1.), 8.);
		fog_color = mix(fog_color, vec4(.7,.68,.71,1.), dd);
	}

	else {
		let alt = .03;
		let xpt_t = first_wgs84_intersection(eye, rd, alt);
		let t = xpt_t.w;

		if (t > 0.) {
			// fog = t;

			let xpt = xpt_t.xyz;
			var d0 = dot(normalize(xpt), rd);

			let up_d = dot(normalize(eye), rd);

			let distToPt = length(xpt - eye);
			// let d = d0 * clamp(distToPt*25., 0., 1.);
			let d = max((up_d+.3)*d0, d0) * clamp(distToPt*25., 0., 1.);


			fog = smoothstep(.0, .25, d);

			var cs = pow(1. - d0*d0, 6.0);
			fog_color = mix(fog_color, vec4(.7,.68,.71,1.), cs);
		}


	}

	var c = mix(base_color, fog_color, fog);
	return c;
}

fn blend_fog_v3(base_color: vec4f, eye: vec3f, rd: vec3f, world_pt: vec3f, dist: f32, distNdc: f32, haeAlt: f32) -> vec4f {
	var fog_color = vec4f(.01, .3, .8, 1.);
	var fog : f32;

	if (distNdc < .999999) {
		fog = 1. - exp(-dist*15.);
		// fog *= pow(smoothstep(.9,.0, haeAlt), 6.);

		let d = dot(normalize(world_pt), rd);
		fog *= 1. - clamp(-d, 0., 1.);

		let dd = pow(1. - clamp(-d, 0., 1.), 8.);
		fog_color = mix(fog_color, vec4(.7,.68,.71,1.), dd);
	}

	else {

		var c = vec4(0.);

		let t = dot(rd, -eye) / dot(rd,rd);

		if (t > 0.) {
			let spt = eye + rd * t;
			let dr = length(spt) - 1.;

			c = vec4(abs(spt)*exp(-dr*5.), 1.);
			// c = vec4(abs(spt)*dr, 1.);
		} else {
			let dup = clamp(dot(normalize(eye), rd) + .4, 0., 1.);
			c = vec4(abs(eye)*dup, 1.);
		}

		return c;
	}

	var c = mix(base_color, fog_color, fog);
	return c;
}

fn blend_fog_v4(base_color: vec4f, eye: vec3f, rd: vec3f, world_pt: vec3f, dist: f32, distNdc: f32, haeAlt: f32) -> vec4f {
	var fog_color = vec4f(.01, .3, .8, 1.);
	var fog : f32;

	if (distNdc < .999999) {
		fog = 1. - exp(-dist*15.);
		// fog *= pow(smoothstep(.9,.0, haeAlt), 6.);

		let d = dot(normalize(world_pt), rd);
		fog *= 1. - clamp(-d, 0., 1.);

		let dd = pow(1. - clamp(-d, 0., 1.), 8.);
		fog_color = mix(fog_color, vec4(.7,.68,.71,1.), dd);
	}

	else {
		var c = vec4(0.);

		let alt = .02;
		let xpt_t = first_wgs84_intersection(eye, rd, alt);
		let xpt = (xpt_t.xyz);
		let nxpt = normalize(xpt_t.xyz);
		let t = xpt_t.w;

		if (t > 0.) {

			// var d = asin(dot(xpt,rd));
			// var d = acos(dot(xpt,rd));
			var d = 1. - (dot(nxpt,rd));
			d *= t*3.;

			// d = max(d, smoothstep(alt,0.,haeAlt));
			d += (1.-dot(nxpt,rd)) * smoothstep(alt,0.,haeAlt);
			d += smoothstep(alt*.25,0.,haeAlt);

			// d = smoothstep(1./(1.+haeAlt), 1., d);
			// d = smoothstep(1./(1.+haeAlt), (1.+alt)/(1.+haeAlt), d);

			// d *= smoothstep(0., alt*.5, t);
			// d *= pow(dot(xpt,normalize(eye)), 4.);

			let theta0 = asin(1. / (1. + haeAlt));
			let theta1 = asin((1.+alt) / (1. + haeAlt));

			// d = smoothstep(theta1,theta0,d);
			// d = smoothstep(theta1,theta0,d);

			// c = vec4f(d,0.,0.,1.);
			// return c;

			// fog = d;
			// fog = clamp(d,0.,1.);
			fog = smoothstep(0.,1.,d);

			let e = (t+.9) * pow(1. - dot(nxpt,rd), 4.);
			// let e = (t / (max(1.00000001,1.+haeAlt) - 1.))  * .01;
			fog_color = mix(fog_color, vec4(1.f), e);

		} else {
			fog = 0.;
		}

		// return c;
	}

	var c = mix(base_color, fog_color, fog);
	return c;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	let texColor = textureSample(texColor, texSampler, vo.uv);
	// let texColor = vec4f(1.);

	var c = texColor;

	let depth : f32 = textureSample(texDepth, texSampler, vo.uv);
	let screen_pt = vec4f(vo.uv.x*2-1, -(vo.uv.y*2-1), depth, 1.);
	let world_pt4 = scd.imvp * screen_pt;

	let world_pt3 = world_pt4.xyz / world_pt4.w;
	let d = distance(scd.eye,world_pt3);

	// c = blend_fog_v1(c, scd.eye, world_pt3, d, scd.haeAlt);

	let rd = normalize(world_pt3 - scd.eye); // WARNING: Not the most efficient nor precise way to get this?
	// c = blend_fog_v2(c, scd.eye, rd, world_pt3, d, depth, scd.haeAlt);
	// c = blend_fog_v3(c, scd.eye, rd, world_pt3, d, depth, scd.haeAlt);
	c = blend_fog_v4(c, scd.eye, rd, world_pt3, d, depth, scd.haeAlt);

	return c;
}
)";
}

namespace wg {
	Fog::Fog(AppObjects& ao, const GlobeOptions& gopts, const AppOptions& appOpts) : ao(ao) {
		//
		// Textures
		//

		uint32_t w = appOpts.initialWidth;
		uint32_t h = appOpts.initialHeight;


            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain  = nullptr,
                      .label        = "FogSampler",
                      .addressModeU = WGPUAddressMode_ClampToEdge,
                      .addressModeV = WGPUAddressMode_ClampToEdge,
                      .addressModeW = WGPUAddressMode_ClampToEdge,
                      .magFilter    = WGPUFilterMode_Linear,
                      .minFilter    = WGPUFilterMode_Linear,
                      .mipmapFilter = WGPUMipmapFilterMode_Nearest,
                      .lodMinClamp  = 0,
                      .lodMaxClamp  = 32,
                // .compare       = WGPUCompareFunction_Less,
                      .compare       = WGPUCompareFunction_Undefined,
                      .maxAnisotropy = 1,
            });

		//
		// BindGroup + Layout
		//

            WGPUBindGroupLayoutEntry bglEntries[3] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Float,
                 .viewDimension = WGPUTextureViewDimension_2D,
                 .multisampled  = false },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 1,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Depth,
                 .viewDimension = WGPUTextureViewDimension_2D,
                 .multisampled  = false },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 2,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Filtering },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
            };
            bindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "FogBGL", .entryCount = 3, .entries = bglEntries });

		createTextures(ao,w,h);

		//
		// Pipeline
		//

			ShaderModule shader { create_shader(ao.device, quadSource, "fogShader") };

            WGPUBindGroupLayout bgls[2] = {
                ao.getSceneBindGroupLayout().ptr,
                bindGroupLayout.ptr,
            };
            quadPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "fog",
                     .bindGroupLayoutCount = 2,
                     .bindGroupLayouts     = bgls,
            });

            auto vertexState       = WGPUVertexState_Default(shader);

            auto primState         = WGPUPrimitiveState_Default();
            primState.cullMode     = WGPUCullMode_None;
            auto multisampleState  = WGPUMultisampleState_Default();
            auto blend             = WGPUBlendState_Default();
            auto cst               = WGPUColorTargetState_Default(ao, blend);
            auto fragmentState     = WGPUFragmentState_Default(shader, cst);
            auto depthStencilState = WGPUDepthStencilState_Default(ao);

            WGPURenderPipelineDescriptor rpDesc { .nextInChain  = nullptr,
                                                  .label        = "fog",
                                                  .layout       = quadPipelineAndLayout.layout,
                                                  .vertex       = vertexState,
                                                  .primitive    = primState,
                                                  .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined
                                                                      ? nullptr
                                                                      : &depthStencilState,
                                                  .multisample  = multisampleState,
                                                  .fragment     = &fragmentState };
            quadPipelineAndLayout.pipeline = ao.device.create(rpDesc);

	}

	void Fog::createTextures(AppObjects& ao, int w, int h) {
		spdlog::get("wg")->info("Fog::createTextures {} {}", w,h);

				fboW = w;
				fboH = h;

                colorTexture     = ao.device.create(WGPUTextureDescriptor {
                        .nextInChain     = nullptr,
                        .label           = "FogColorTex",
                        .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
                        .dimension       = WGPUTextureDimension_2D,
                        .size            = WGPUExtent3D { w,h, 1 },
                        .format          = ao.surfaceColorFormat,
                        .mipLevelCount   = 1,
                        .sampleCount     = 1,
                        .viewFormatCount = 0,
                        .viewFormats     = 0
                });

                colorTexView = colorTexture.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "FogColorTexView",
                    .format          = ao.surfaceColorFormat,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_All,
                });

		depthTexture = ao.device.createDepthTexture(w,h, ao.surfaceDepthStencilFormat);

                depthTexView = depthTexture.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "FogDepthTexView",
                    .format          = ao.surfaceDepthStencilFormat,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_DepthOnly,
                });

            WGPUBindGroupEntry groupEntries[3] = {
                { .nextInChain = nullptr,
                 .binding     = 0,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = colorTexView                                                                                    },
                { .nextInChain = nullptr,
                 .binding     = 1,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = depthTexView                                                                                    },
                { .nextInChain = nullptr, .binding = 2, .buffer = 0, .offset = 0, .size = 0, .sampler = sampler, .textureView = 0 },
            };
            bindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "FogBG",
                                                                         .layout      = bindGroupLayout,
                                                                         .entryCount  = 3,
                                                                         .entries     = groupEntries });

	}

		void Fog::beginPass(CommandEncoder& ce, int w, int h) {
			if (w != fboW or h != fboH) {
				createTextures(ao, w,h);
			}

			rpe = ce.beginRenderPassBasic(ao, colorTexView, depthTexView, "fogPass");
		}
		void Fog::endPass() {
			rpe.end();
			rpe.release();
		}

		void Fog::renderAfterEndingPass(RenderState& rs) {
            rs.pass.setRenderPipeline(quadPipelineAndLayout.pipeline);
			rs.pass.setBindGroup(0, ao.getSceneBindGroup());
			rs.pass.setBindGroup(1, bindGroup);
            rs.pass.draw(6);
		}
}
