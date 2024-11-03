#include "app/shader.h"
#include "entity/globe/globe.h"

#include "util/gdalDataset.h"
#include "util/fmtEigen.h"
#include "geo/conversions.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <unistd.h>


namespace wg {

    void maybe_make_tiff_obb_file(const std::string& tiffPath, const GlobeOptions& gopts);

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

    struct GpuTileData {
        Buffer ibo;
        Buffer vbo;
        int32_t textureArrayIndex = -1;
		uint32_t nindex = 0;
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

		AppObjects& ao;

        inline GpuResources(AppObjects& ao, const GlobeOptions& opts) : ao(ao) {

			freeTileInds.resize(MAX_TILES);
			for (int i=0; i<MAX_TILES; i++) freeTileInds[i] = i;

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
        bool root     = false;
    };

    // WebGPU Upload Helpers.
    void createVbo_(Buffer& vbo, AppObjects& ao, const uint8_t* ptr, size_t bufSize) {
        WGPUBufferDescriptor desc {
            .nextInChain      = nullptr,
            .label            = "GlobeVbo",
            .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
            .size             = bufSize,
            .mappedAtCreation = true,
        };
        vbo       = ao.device.create(desc);

        void* dst = wgpuBufferGetMappedRange(vbo, 0, bufSize);
        memcpy(dst, ptr, bufSize);

        wgpuBufferUnmap(vbo);
    }
    void createVbo_(Buffer& vbo, AppObjects& ao, const std::vector<uint8_t>& vec) {
        createVbo_(vbo, ao, vec.data(), vec.size());
    }
    void createIbo_(Buffer& ibo, AppObjects& ao, const uint8_t* ptr, size_t bufSize) {
        WGPUBufferDescriptor desc {
            .nextInChain      = nullptr,
            .label            = "GlobeIbo",
            .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
            .size             = bufSize,
            .mappedAtCreation = true,
        };
        ibo       = ao.device.create(desc);

        void* dst = wgpuBufferGetMappedRange(ibo, 0, bufSize);
        memcpy(dst, ptr, bufSize);

        wgpuBufferUnmap(ibo);
    }
    void uploadTex_(Texture& sharedTex, AppObjects& ao, uint32_t textureArrayIndex, const uint8_t* ptr, size_t bufSize, uint32_t w,
                    uint32_t h, uint32_t c) {
        // const WGPUTextureDataLayout& dataLayout, const WGPUExtent3D& writeSize) {

        spdlog::get("tiffRndr")->info("upload tex index {} shape {} {} {} bufSize {}", textureArrayIndex, w,h,c, bufSize);
        ao.queue.writeTexture(
            WGPUImageCopyTexture {
                .nextInChain = nullptr,
                .texture     = sharedTex,
                .mipLevel    = 0,
                .origin      = WGPUOrigin3D { 0, 0, textureArrayIndex },
				.aspect = WGPUTextureAspect_All,
        },
            ptr, bufSize,
            WGPUTextureDataLayout {
                .nextInChain  = nullptr,
                .offset       = 0,
                .bytesPerRow  = w * c,
                .rowsPerImage = h,
            },
            WGPUExtent3D { w, h, 1 });
    }

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

        // The error induced in meters by not opening this node.
        float geoError;

        TileState state;

        Tile* parent                  = nullptr;
        std::array<Tile*, 4> children = { nullptr };
        int nchildren                 = 0;

        UnpackedOrientedBoundingBox obb;

        GpuTileData gpuTileData;

        inline void update(const RenderState& rs, GpuResources& res) {
            // If leaf:
            //    compute sse
            //    if sse < closeThresh: goto SteadyWantsToClose
            //    if sse > openThresh : open()
			// If interior:
			//    compute sse
			//    if all children want to close and not isRoot:
			//        queue load parent, goto OpeningAsParent, set children state Closing

			if (isLeaf()) {
			}
			else if (isInterior()) {
				//
			}

        }

        inline void recvOpenLoadedData(LoadDataResponse&& resp, GpuResources& res) {
            if (resp.action == LoadAction::OpenChildren) {
                assert(state == TileState::OpeningChildrenAsParent);
                assert(resp.parentCoord == coord);
            } else if (resp.action == LoadAction::CloseToParent) {
            } else if (resp.action == LoadAction::LoadRoot) {

                spdlog::get("tiffRndr")->info("root recvOpenLoadedData (for {})", resp.parentCoord);

                assert(resp.items.size() == 1);
                auto& tileData  = resp.items[0];

				createVbo_(gpuTileData.vbo, res.ao, tileData.vertexData);
				createIbo_(gpuTileData.ibo, res.ao, (const uint8_t*)tileData.indices.data(), tileData.indices.size() * sizeof(uint16_t));
				gpuTileData.nindex = tileData.indices.size();

                uint32_t textureArrayIndex = res.takeTileInd();
				// textureArrayIndex = 0;
				gpuTileData.textureArrayIndex = textureArrayIndex;
				assert(textureArrayIndex >= 0 and textureArrayIndex < MAX_TILES);
                spdlog::get("tiffRndr")->info("img shape {} {} {}", tileData.img.rows, tileData.img.cols, tileData.img.channels());
				uploadTex_(res.sharedTex, res.ao, textureArrayIndex, tileData.img.data, tileData.img.total() * tileData.img.elemSize(), tileData.img.cols, tileData.img.rows, tileData.img.channels());

				state = TileState::Steady;
            }
        }

        inline bool shouldDraw() const {
            return state == TileState::Steady || state == TileState::SteadyWantsToClose || state == TileState::Closing
                   || state == TileState::OpeningChildrenAsParent;
        }
        inline bool isLeaf() const {
            return state == TileState::Steady || state == TileState::SteadyWantsToClose || state == TileState::Closing;
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
            if (shouldDraw()) {

                // draw ...
                // spdlog::get("tiffRndr")->trace("render ready leaf {} inds {}", coord, gpuTileData.nindex);

				// rs.pass.setRenderPipeline(rndrPipe);
				// rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
				rs.pass.setVertexBuffer(0, gpuTileData.vbo, 0, gpuTileData.vbo.getSize());
				rs.pass.setIndexBuffer(gpuTileData.ibo, WGPUIndexFormat_Uint16, 0, gpuTileData.ibo.getSize());
				// rs.pass.drawIndexed(gpuTileData.nindex);
				rs.pass.drawIndexed(gpuTileData.nindex, 1, 0, 0, gpuTileData.textureArrayIndex);



            } else if (isInterior()) {
                for (int i = 0; i < nchildren; i++) children[i]->render(rs);
            } else {
                spdlog::get("tiffRndr")->info("non shouldDraw/isInterior ?");
            }
        }

        inline void renderBb(const RenderState& rs, InefficientBboxEntity* bboxEntity) {
            if (shouldDraw()) {
				bboxEntity->set(obb);
				bboxEntity->render(rs);
			} else {
				for (auto & c : children) c->renderBb(rs, bboxEntity);
			}
		}
    };

    struct DataLoader {
        // Note that obbMap is initialized on the calling thread synchronously
        inline DataLoader(const GlobeOptions& opts)
            : obbMap(opts.getString("tiffPath") + ".bb", opts) {
            stop   = false;
            thread = std::thread(&DataLoader::loop, this);
            logger = spdlog::stdout_color_mt("tiffLoader");

			colorDset = std::make_shared<GdalDataset>(opts.getString("tiffPath"));
			dtedDset  = std::make_shared<GdalDataset>(opts.getString("dtedPath"));
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
                std::vector<LoadDataResponse> qOutLocal;
				qOutLocal.reserve(qInCopied.size());
                for (const auto& req : qInCopied) { qOutLocal.emplace_back(load(req)); }

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

                    auto obbIt                    = obbMap.find(childCoord);

                    if (obbIt != obbMap.end()) {
                        TileData item;
                        loadActualData(item, childCoord);
                        item.terminal = obbIt->second.terminal;
                        item.root     = obbIt->second.root;
                        items.push_back(std::move(item));
                    }
                }
            } else if (req.action == LoadAction::CloseToParent or req.action == LoadAction::LoadRoot) {
                auto obbIt = obbMap.find(req.parentCoord);
                assert(obbIt != obbMap.end());

                TileData item;
                loadActualData(item, req.parentCoord);
                item.terminal = obbIt->second.terminal;
                item.root     = obbIt->second.root;
                items.push_back(std::move(item));
            }

            return LoadDataResponse {
                .seq = req.seq, .src = req.src, .parentCoord = req.parentCoord, .action = req.action, .items = std::move(items)
            };
        }

        inline void loadActualData(TileData& item, const QuadtreeCoordinate& c) {
            // Set img.
            // Set vertexData.
            // Set indices.

			Vector4d tlbrWm = c.getWmTlbr();
			spdlog::get("tiffRndr")->info("wm tlbr {}", tlbrWm.transpose());


			constexpr uint32_t E = 8;


			cv::Mat img, dtedImg;
			img.create(256,256, CV_8UC3);
			dtedImg.create(E,E, CV_16UC1);
			colorDset->getWm(tlbrWm, img);
			dtedDset->getWm(tlbrWm, dtedImg);

			cv::cvtColor(img,img,cv::COLOR_RGB2RGBA);
			for (int y=0; y<img.rows; y++) {
				for (int x=0; x<img.rows; x++) {
					// img.data[y*img.step + x*4 + 0] = (std::abs(x-y) < 4) * 255;
					// img.data[y*img.step + x*4 + 1] = 100;
					// img.data[y*img.step + x*4 + 2] = 0;
					img.data[y*img.step + x*4 + 3] = 255;
				}
			}
			item.img = std::move(img);

			item.indices.reserve((E-1)*(E-1)*3*2);
			for (uint16_t y=0; y < E-1; y++) {
				for (uint16_t x=0; x < E-1; x++) {
					uint16_t a = (y  ) * E + (x  );
					uint16_t b = (y  ) * E + (x+1);
					uint16_t c = (y+1) * E + (x+1);
					uint16_t d = (y+1) * E + (x  );

					item.indices.push_back(a);
					item.indices.push_back(b);
					item.indices.push_back(c);

					item.indices.push_back(c);
					item.indices.push_back(d);
					item.indices.push_back(a);
				}
			}

			const int16_t* elevData = (const int16_t*) dtedImg.data;
			Vector4d tlbrUwm   = tlbrWm.array() / Earth::WebMercatorScale;
			Matrix<float, E * E, 3, RowMajor> positions;
			for (uint16_t y=0; y < E; y++) {
				for (uint16_t x=0; x < E; x++) {

					float xx_ = static_cast<float>(x) / static_cast<float>(E - 1);
					float yy_ = static_cast<float>(y) / static_cast<float>(E - 1);

					xx_ = xx_ * .9f + .05f, yy_ = yy_ * .9f + .05f;

					float xx  = xx_ * tlbrUwm(0) + (1 - xx_) * tlbrUwm(2);
					float yy  = yy_ * tlbrUwm(1) + (1 - yy_) * tlbrUwm(3);
					float zz_   = (elevData[y*dtedImg.cols + x]);
					if (zz_ < -1000) zz_ = 0;
					float zz = zz_ / Earth::WebMercatorScale;

					int32_t ii = ((E - 1 - y) * E) + x;
					positions.row(ii) << xx, yy, zz;
				}
			}

			unit_wm_to_ecef(positions.data(), E * E, positions.data(), 3);
			// spdlog::get("tiffRndr")->info("mapped ECEF coords:\n{}", positions);

			std::vector<float> verts;
			int vertWidth = 3+2+3;
			verts.resize(E*E*vertWidth * sizeof(float));
			for (uint32_t y=0; y < E; y++) {
				for (uint32_t x=0; x < E; x++) {
					uint32_t i = y*E+x;

					verts[i*vertWidth + 0] = positions(i,0);
					verts[i*vertWidth + 1] = positions(i,1);
					verts[i*vertWidth + 2] = positions(i,2);
					verts[i*vertWidth + 3] = 1.f - static_cast<float>(x) / static_cast<float>(E - 1);
					verts[i*vertWidth + 4] = 1.f - static_cast<float>(y) / static_cast<float>(E - 1);
					verts[i*vertWidth + 5] = 0;
					verts[i*vertWidth + 6] = 0; // todo: compute normals.
					verts[i*vertWidth + 7] = 0;
				}
			}

			size_t vertexBufSize = verts.size() * sizeof(float);
			item.vertexData.resize(vertexBufSize);
			std::memcpy(item.vertexData.data(), verts.data(), vertexBufSize);
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

		std::shared_ptr<GdalDataset> colorDset;
		std::shared_ptr<GdalDataset> dtedDset;
    };

    struct TiffGlobe : public Globe {

        TiffGlobe(AppObjects& ao, const GlobeOptions& opts)
            : Globe(ao, opts)
            , gpuResources(ao, opts)
            , loader(opts) {

            logger = spdlog::stdout_color_mt("tiffRndr");

			bboxEntity = std::make_shared<InefficientBboxEntity>(ao);
            createAndWaitForRootsToLoad_();
        }

        ~TiffGlobe() {
            for (auto root : roots) delete root;
        }

        inline virtual void render(const RenderState& rs) override {


			rs.pass.setRenderPipeline(gpuResources.renderPipeline);
			rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
			rs.pass.setBindGroup(1, gpuResources.sharedBindGroup);

            for (auto tile : roots) { tile->update(rs, gpuResources); }
            for (auto tile : roots) { tile->render(rs); }

            // if (bboxEntity) for (auto tile : roots) { tile->renderBb(rs, bboxEntity.get()); }
        }

        inline void createAndWaitForRootsToLoad_() {
            auto rootCoordinates = loader.getRootCoordinates();

            for (const auto& c : rootCoordinates) {
                float geoError = 1000; // TODO: ...
                Tile* tile     = new Tile(c, nullptr, TileState::OpeningAsChild, loader.obbMap.find(c)->second, geoError);
                roots.push_back(tile);
            }

            {
                std::deque<LoadDataRequest> reqs;
                for (int i = 0; i < roots.size(); i++) {
                    LoadDataRequest req;
                    req.src         = roots[i];
                    req.seq         = seq++;
                    req.parentCoord = roots[i]->coord;
                    req.action      = LoadAction::LoadRoot;
                    reqs.push_back(req);
                }

                loader.pushRequests(std::move(reqs));
            }

            // Wait for ALL requests to finish.
            std::deque<LoadDataResponse> responses;
            while (responses.size() < roots.size()) {
                usleep(5'000);
                auto responses_ = loader.pullResponses();
                for (auto&& r : responses_) responses.emplace_back(std::move(r));
                logger->info("have {} / {} root datas loaded.", responses.size(), roots.size());
            }

            for (auto& resp : responses) { resp.src->recvOpenLoadedData(std::move(resp), gpuResources); }

            logger->info("createAndWaitForRootsToLoad_ is done.");
        }

        uint32_t seq; // load data sequence counter

        GpuResources gpuResources;

        std::vector<Tile*> roots;

        DataLoader loader;
        std::shared_ptr<spdlog::logger> logger;
        std::shared_ptr<InefficientBboxEntity> bboxEntity;
    };

    std::shared_ptr<Globe> make_tiff_globe(AppObjects& ao, const GlobeOptions& opts) {

        maybe_make_tiff_obb_file(opts.getString("tiffPath"), opts);

        return std::make_shared<TiffGlobe>(ao, opts);
    }

}