#include "app/shader.h"
#include "globe.h"

namespace wg {

    // NOTE:
    // Just wrote code for 2d texture array of layers.
    // But we need to let each draw call know what index of the array to use.
    // WebGPU does not have push constant.
    // Can I do this with `firstInstance` and reading the instance id in shader?
    // This could be done in batch fashion too with one draw call using drawIndirect

    constexpr static int32_t MAX_TILES = 1024;

    struct NoTilesAvailableExecption : std::runtime_error {
        inline NoTilesAvailableExecption()
            : std::runtime_error("No tiles available right now") {
        }
    };

    static const char* shaderSource = R"(

struct SceneCameraData {
	mvp: mat4x4<f32>,
	mv: mat4x4<f32>,

	eye: vec3f,
	colorMult: vec4f,

	sun: vec4f,
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

	return vo.color;
}

)";

    struct GpuTileData {
        Buffer ibo;
        Buffer vbo;
        int32_t textureArrayIndex = -1;
    };

    struct GpuResources {
        Texture sharedTex;
        TextureView sharedTexView;

        Sampler sampler;

        std::vector<int32_t> freeTileInds;

        // "shared" because the same texture is used for all tiles -- by way of array layers / subresources.
        BindGroupLayout sharedBindGroupLayout;
        BindGroup sharedBindGroup;

        PipelineLayout pipelineLayout;
        RenderPipeline renderPipeline;

        inline GpuResources(AppObjects& ao, const GlobeOptions& opts) {

			// ------------------------------------------------------------------------------------------------------------------------------------------
			//     Texture & Sampler
			// ------------------------------------------------------------------------------------------------------------------------------------------

            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain   = nullptr,
                      .label         = "TiffRendererDesc",
                      .addressModeU  = WGPUAddressMode_ClampToEdge,
                      .addressModeV  = WGPUAddressMode_ClampToEdge,
                      .addressModeW  = WGPUAddressMode_ClampToEdge,
                      .magFilter     = WGPUFilterMode_Linear,
                      .minFilter     = WGPUFilterMode_Linear,
                      .mipmapFilter  = WGPUMipmapFilterMode_Nearest,
                      .lodMinClamp   = 0,
                      .lodMaxClamp   = 32,
                      // .compare       = WGPUCompareFunction_Less,
                      .compare       = WGPUCompareFunction_Undefined,
                      .maxAnisotropy = 1,
            });

            sharedTex     = ao.device.create(WGPUTextureDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "TiffGlobeTextureArray",
                    .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
                    .dimension       = WGPUTextureDimension_2D,
                    .size            = WGPUExtent3D { 256, 256, MAX_TILES },
                    .format          = WGPUTextureFormat_RGBA8Unorm,
                    .mipLevelCount   = 1,
                    .sampleCount     = 1,
                    .viewFormatCount = 0,
                    .viewFormats     = 0
            });

            sharedTexView = sharedTex.createView(WGPUTextureViewDescriptor {
                .nextInChain = nullptr,
                .label       = "TiffRenderer_sharedTexView",
                .format      = WGPUTextureFormat_RGBA8Unorm,
                // .dimension       = WGPUTextureViewDimension_2D,
                .dimension       = WGPUTextureViewDimension_2DArray,
                .baseMipLevel    = 0,
                .mipLevelCount   = 1,
                .baseArrayLayer  = 0,
                .arrayLayerCount = MAX_TILES,
                .aspect          = WGPUTextureAspect_All,
            });

			// ------------------------------------------------------------------------------------------------------------------------------------------
			//     BindGroupLayout & BindGroup
			// ------------------------------------------------------------------------------------------------------------------------------------------

            /*
                        WGPUBindGroupLayoutEntry uboLayoutEntry {
                                .nextInChain    = nullptr,
                                .binding        = 0,
                                .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                                .buffer         = WGPUBufferBindingLayout { .nextInChain      = nullptr,
                                                                                                                .type             =
               WGPUBufferBindingType_Uniform, .hasDynamicOffset = false, .minBindingSize   = 0 }, .sampler        = { .nextInChain =
               nullptr, .type = WGPUSamplerBindingType_Undefined }, .texture        = { .nextInChain = nullptr, .sampleType =
               WGPUTextureSampleType_Undefined }, .storageTexture = { .nextInChain = nullptr, .access =
               WGPUStorageTextureAccess_Undefined },
                        };
            */
            WGPUBindGroupLayoutEntry sharedTexLayoutEntries[2] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Float,
                 .viewDimension = WGPUTextureViewDimension_2DArray,
                 .multisampled  = false },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 1,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Filtering },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
            };
            sharedBindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "TiffRendererSharedBGL", .entryCount = 2, .entries = sharedTexLayoutEntries });

            WGPUBindGroupEntry groupEntries[2] = {
                { .nextInChain = nullptr,
                 .binding     = 0,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = sharedTexView                                                                                    },
                { .nextInChain = nullptr, .binding = 1, .buffer = 0, .offset = 0, .size = 0, .sampler = sampler, .textureView = 0 },
            };
            sharedBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "TiffRendererSharedBG",
                                                                         .layout      = sharedBindGroupLayout,
                                                                         .entryCount  = 2,
                                                                         .entries     = groupEntries });

			// ------------------------------------------------------------------------------------------------------------------------------------------
			//     Shader
			// ------------------------------------------------------------------------------------------------------------------------------------------

            ShaderModule shader { create_shader(ao.device, shaderSource, "tiffRendererShader") };

			// ------------------------------------------------------------------------------------------------------------------------------------------
			//     RenderPipeline & Layout
			// ------------------------------------------------------------------------------------------------------------------------------------------

			WGPUBindGroupLayout bgls[2] = {
				ao.getSceneBindGroupLayout().ptr,
				sharedBindGroupLayout.ptr,
			};
			pipelineLayout = ao.device.create(WGPUPipelineLayoutDescriptor {
					.nextInChain = nullptr,
					.label = "tiffRenderer",
					.bindGroupLayoutCount = 2,
					.bindGroupLayouts = bgls,
			});

        }

        inline int32_t takeTileInd() {
            if (freeTileInds.size() == 0) {
                throw NoTilesAvailableExecption {};
            } else {
                int32_t out = freeTileInds.back();
                freeTileInds.pop_back();
                return out;
            }
        }
    };

    struct Tile {
        std::array<Tile*, 4> children;
        int nchildren = 0;

        UnpackedOrientedBoundingBox obb;

        GpuTileData gpuTileData;

        inline void render(const RenderState& rs, GpuResources& res) {
        }
    };

    struct TiffGlobe : public Globe {

        TiffGlobe(AppObjects& ao, const GlobeOptions& opts)
            : Globe(ao, opts)
            , gpuResources(ao, opts) {
        }

        inline virtual void render(const RenderState& rs) override {

            for (auto tile : roots) { tile->render(rs, gpuResources); }
        }

        GpuResources gpuResources;

        std::vector<Tile*> roots;
    };

    std::shared_ptr<Globe> make_tiff_globe(AppObjects& ao, const GlobeOptions& opts) {
        return std::make_shared<TiffGlobe>(ao, opts);
    }

}
