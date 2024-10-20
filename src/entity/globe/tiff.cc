#include "app/shader.h"
#include "globe.h"

#include <opencv2/core.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <unistd.h>

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
                      .nextInChain  = nullptr,
                      .label        = "TiffRendererDesc",
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
            pipelineLayout                    = ao.device.create(WGPUPipelineLayoutDescriptor {
                                   .nextInChain          = nullptr,
                                   .label                = "tiffRenderer",
                                   .bindGroupLayoutCount = 2,
                                   .bindGroupLayouts     = bgls,
            });

            WGPUVertexAttribute attributes[3] = {
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 0 * sizeof(float),
                                     .shaderLocation = 0,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x2,
                                     .offset         = 3 * sizeof(float),
                                     .shaderLocation = 1,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 5 * sizeof(float),
                                     .shaderLocation = 2,
                                     },
            };
            WGPUVertexBufferLayout vbl {
                .arrayStride    = (3 + 2 + 3) * sizeof(float),
                .stepMode       = WGPUVertexStepMode_Vertex,
                .attributeCount = 3,
                .attributes     = attributes,
            };
            auto vertexState       = WGPUVertexState_Default(shader, vbl);

            auto primState         = WGPUPrimitiveState_Default();
            auto multisampleState  = WGPUMultisampleState_Default();
            auto blend             = WGPUBlendState_Default();
            auto cst               = WGPUColorTargetState_Default(ao, blend);
            auto fragmentState     = WGPUFragmentState_Default(shader, cst);
            auto depthStencilState = WGPUDepthStencilState_Default(ao);

            WGPURenderPipelineDescriptor rpDesc { .nextInChain  = nullptr,
                                                  .label        = "TiffRenderer",
                                                  .layout       = pipelineLayout,
                                                  .vertex       = vertexState,
                                                  .primitive    = primState,
                                                  .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined
                                                                      ? nullptr
                                                                      : &depthStencilState,
                                                  .multisample  = multisampleState,
                                                  .fragment     = &fragmentState };
            renderPipeline = ao.device.create(rpDesc);
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

    // Remember that we must model the states corresponding to all interior nodes as well as leaves.
    // Then, a tile can be:
    //
    //       opening_children_as_parent      |
    //       closing                         | Leaf
    //       steady                          | States
    //       steady_wants_to_close           |
    //
    //       opening_as_child                | Unloaded
    //       opening_as_parent               | States
    //
    //       root                            | Root/Interior
    //       interior                        | States
    //
    // Note that `SteadyWantsToClose` is different from `Steady` and from `Closing` because a tile cannot close until
    // all other 3 sibiling also want to.
    // Not until all four children enter the `SteadyWantsToClose` state, will they all then be transferred to the `Closing`
    // state and the `LoadRequest` queued.
    //
    // When a parent _decides_ to open (entering the `opening_children_as_parent`),
    // it will allocate all children objects -- of course they will not have data loaded
    // yet.
    //
    // When a parent _decides_ to open, it also shall "take" a resource index from
    // the `GpuResources` free list.
    // But is this indeed how it ought to work?
    //
    // Once a tile is in a non-steady state, it cannot goto any other state until
    // the data is loaded.
    // So if we zoom in (requiring opening some node), then zoom out before it has loaded,
    // it cannot undo the opening state / load request. It must wait until the data is loaded,
    // then immediately unload.
    //
    // #error "tile state should NOT be an enum -- better expressed with multiple fields."
    enum class TileState { OpeningChildrenAsParent, OpeningAsChild, OpeningAsParent, Closing, Steady, SteadyWantsToClose };
    //
    // NOTE: Well maybe not -- can just store 'root' field in tile obj
    /*
    struct TileState {
            bool openingAsChild;
            bool openingAsParent;
            bool root;
    };
    */

    struct TileData {
        cv::Mat img;
        std::vector<uint8_t> vertexData;
        std::vector<uint16_t> indices;

        // Non gpu data, but feedback from DataLoader none-the-less
        bool terminal = false;
        bool root = false;
    };

    struct Tile;

    enum class LoadAction { LoadRoot, OpenChildren, CloseToParent };

    struct LoadDataRequest {
		Tile* src; // where this request was initiated from.
        int32_t seq;

        // When opening: the parent coordinate of which the four children shall be loaded.
        // When closing: the parent coordinate to load data for.
        QuadtreeCoordinate parentCoord;
        LoadAction action;
    };

    struct LoadDataResponse {
        int32_t seq;
        Tile* src;
        QuadtreeCoordinate parentCoord;
        LoadAction action;
        std::vector<TileData> items;
    };

    struct Tile {

        inline Tile(const QuadtreeCoordinate& coord, Tile* parent, TileState state, const UnpackedOrientedBoundingBox& obb,
                    float geoError)
            : coord(coord)
            , parent(parent)
            , state(state)
            , obb(obb)
            , geoError(geoError) {
        }

        QuadtreeCoordinate coord;
        float geoError;

        TileState state;

        Tile* parent                  = nullptr;
        std::array<Tile*, 4> children = { nullptr };
        int nchildren                 = 0;

        UnpackedOrientedBoundingBox obb;

        GpuTileData gpuTileData;

        inline void update(const RenderState& rs, GpuResources& res) {
            // If state == steady:
            //    compute sse
            //    if sse < closeThresh: goto SteadyWantsToClose
            //    if sse > openThresh : open()
        }

        inline void recvOpenLoadedData(LoadDataResponse&& resp) {
            if (resp.action == LoadAction::OpenChildren) {
                assert(state == TileState::OpeningChildrenAsParent);
                assert(resp.parentCoord == coord);
			} else if (resp.action == LoadAction::CloseToParent) {
			} else if (resp.action == LoadAction::LoadRoot) {
                spdlog::get("tiffRndr")->info("root recvOpenLoadedData");
            }
        }

        // Is this a leaf node with data?
        inline bool isReadyLeaf() const {
            return state == TileState::Steady || state == TileState::SteadyWantsToClose || state == TileState::Closing
                   || state == TileState::OpeningChildrenAsParent;
        }

        inline bool isRoot() const {
            return parent == nullptr;
        }
        inline bool isInterior() const {
            return nchildren > 0;
        }
        inline bool isTerminal() const {
            // FIXME:
            return true; // TODO: This must be data inserted in constructor.
        }

        inline void render(const RenderState& rs) {
            if (isReadyLeaf()) {
                // draw ...
            } else if (isInterior()) {
                for (int i = 0; i < nchildren; i++) children[i]->render(rs);
            } else {
                spdlog::get("tiffRndr")->info("non isReadyLeaf/isInterior ?");
            }
        }
    };

    struct DataLoader {
		// Note that obbMap is initialized on the calling thread synchronously
        inline DataLoader(const GlobeOptions& opts)
            : obbMap(opts) {
            stop   = false;
            thread = std::thread(&DataLoader::loop, this);
            logger = spdlog::stdout_color_mt("tiffLoader");
        }

        inline ~DataLoader() {
            stop = true;
            cv.notify_one();
            if (thread.joinable()) thread.join();
        }

        inline void loop() {
            while (true) {
                if (stop) break;

                // Acquire requests results.
                std::deque<LoadDataRequest> qInCopied;
                {
                    std::unique_lock<std::mutex> lck(mtxIn);
                    cv.wait(lck, [this]() { return stop or qIn.size() > 0; });
                    logger->trace("woke to |qIn| = {}, stop={}", qIn.size(), stop.load());

                    if (stop) break;

                    // With the lock held, copy all requests.
                    qInCopied = std::move(this->qIn);
                }

                if (qInCopied.size() == 0) {
                    // FIXME: Improve this logic...
                    logger->trace("no input requests -- sleeping 100ms");
                    usleep(100'000);
                    continue;
                }

                // Load data.
                std::deque<LoadDataResponse> qOutLocal;
                for (const auto& req : qInCopied) {
					qOutLocal.push_back(load(req));
				}

                // Write results.
                {
                    std::unique_lock<std::mutex> lck(mtxOut);
                    logger->debug("appending {} results, now have {}", qOutLocal.size(), qOut.size() + qOutLocal.size());
                    for (auto& resp : qOutLocal) qOut.push_back(std::move(resp));
                }
            }
        }

		inline LoadDataResponse load(const LoadDataRequest& req) {
			std::vector<TileData> items;

			if (req.action == LoadAction::OpenChildren) {
				for (uint32_t childIndex = 0; childIndex < 4; childIndex++) {
					QuadtreeCoordinate childCoord = req.parentCoord.child(childIndex);

					auto obbIt = obbMap.find(childCoord);

					if (obbIt != obbMap.end()) {
						TileData item;
						loadActualData(item, childCoord);
						item.terminal = obbIt->second.terminal;
						item.root = obbIt->second.root;
						items.push_back(std::move(item));
					}

				}
			} else if (req.action == LoadAction::CloseToParent or req.action == LoadAction::LoadRoot) {
				auto obbIt = obbMap.find(req.parentCoord);
				assert(obbIt != obbMap.end());

				TileData item;
				loadActualData(item, req.parentCoord);
				item.terminal = obbIt->second.terminal;
				item.root = obbIt->second.root;
				items.push_back(std::move(item));
			}

			return LoadDataResponse {
				.seq = req.seq,
				.src = req.src,
				.parentCoord = req.parentCoord,
				.action = req.action,
				.items = std::move(items)
			};
		}

		inline void loadActualData(TileData& item, const QuadtreeCoordinate& c) {
			// Set img.
			// Set vertexData.
			// Set indices.
		}

        // WARNING: This is called from the render thread -- not `this->thread`.
        inline std::vector<QuadtreeCoordinate> getRootCoordinates() {
			return obbMap.getRoots();
        }

		// Called from main thread, typically.
		inline void pushRequests(std::deque<LoadDataRequest>&& reqs) {
			{
				std::unique_lock<std::mutex> lck(mtxIn);
				// What is the difference between the following two lines, and is one more correct?
				for (auto& req : reqs) qIn.push_back(std::move(req));
				// for (auto&& req : reqs) qIn.push_back(std::move(req));
			}
			cv.notify_one();
		}

		// Called from main thread, typically.
		inline std::deque<LoadDataResponse> pullResponses() {
            std::unique_lock<std::mutex> lck(mtxOut);
			return std::move(qOut);
		}

        ObbMap obbMap;

        std::deque<LoadDataRequest> qIn;
        std::deque<LoadDataResponse> qOut;

        std::condition_variable cv;
        std::mutex mtxIn, mtxOut;
        std::thread thread;
        std::atomic<bool> stop;
        std::shared_ptr<spdlog::logger> logger;
    };

    struct TiffGlobe : public Globe {

        TiffGlobe(AppObjects& ao, const GlobeOptions& opts)
            : Globe(ao, opts)
            , gpuResources(ao, opts)
            , loader(opts) {

            logger = spdlog::stdout_color_mt("tiffRndr");

            createAndWaitForRootsToLoad_();
        }
		
        ~TiffGlobe() {
			for (auto root : roots) delete root;
		}

        inline virtual void render(const RenderState& rs) override {

            for (auto tile : roots) { tile->update(rs, gpuResources); }
            for (auto tile : roots) { tile->render(rs); }
        }

        inline void createAndWaitForRootsToLoad_() {
            auto rootCoordinates = loader.getRootCoordinates();

			for (const auto& c : rootCoordinates) {
				float geoError = 1000; // TODO: ...
				Tile* tile = new Tile(c, nullptr, TileState::OpeningAsChild, loader.obbMap.find(c)->second, geoError);
				roots.push_back(tile);
			}

			{
				std::deque<LoadDataRequest> reqs;
				for (int i=0; i<roots.size(); i++) {
					LoadDataRequest req;
					req.src = roots[i];
					req.seq = seq++;
					req.parentCoord = roots[i]->coord;
					req.action = LoadAction::LoadRoot;
					reqs.push_back(req);
				}

				loader.pushRequests(std::move(reqs));
			}

			// Wait for ALL requests to finish.
			std::deque<LoadDataResponse> responses;
			while (responses.size() < roots.size()) {
				usleep(5'000);
				logger->info("have {} / {} root datas loaded.", responses.size(), roots.size());
			}

			for (auto& resp : responses) {
				resp.src->recvOpenLoadedData(std::move(resp));
			}


        }

		uint32_t seq; // load data sequence counter

        GpuResources gpuResources;

        std::vector<Tile*> roots;

        DataLoader loader;
        std::shared_ptr<spdlog::logger> logger;
    };

    std::shared_ptr<Globe> make_tiff_globe(AppObjects& ao, const GlobeOptions& opts) {
        return std::make_shared<TiffGlobe>(ao, opts);
    }

}
