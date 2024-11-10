#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <webgpu/webgpu.h>

struct GLFWwindow;
// Implemented in glfw/glfw3webgpu.cc
WGPUSurface glfwGetWGPUSurface(WGPUInstance instance, GLFWwindow* window);

namespace wg {

    struct AppObjects;
    struct FrameData;

    WGPURequiredLimits defaultRequiredLimits();

    template <class T> struct Resource {
        using Type = T;
        inline Resource()
            : ptr(nullptr) {
        }
        inline Resource(T ptr)
            : ptr(ptr) {
        }
        inline static Resource fromPtr(T& ptr) {
            return Resource(ptr);
        }
        Resource(Resource&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
        }
        Resource& operator=(Resource&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        Resource(const Resource& o)            = delete;
        Resource& operator=(const Resource& o) = delete;
        inline Resource& operator=(const T& o) {
            ptr = o;
            return *this;
        }
        inline Resource& operator=(std::nullptr_t) {
            ptr = nullptr;
            return *this;
        }
        inline void clear() {
            ptr = nullptr;
        }
        T ptr;

        inline operator T() {
            return ptr;
        }
    };

    template <class T> struct Descriptor : public T {
        inline Descriptor() {
            memset(this, 0, sizeof(this));
        }
    };

    struct Buffer;
    struct BufferMapping {
        Buffer* target;

        // TODO: .

        void unmap();

        inline ~BufferMapping() {
            unmap();
        }
    };

    struct SurfaceTexture;
    struct Surface : Resource<WGPUSurface> {
        // why is this type special?
        inline Surface()
            : Resource(nullptr) {
        }
        inline Surface(WGPUSurface o) {
            this->ptr = o;
        }
        inline Surface(const Surface& o) = delete;
        inline Surface(Surface&& o)
            : Resource(std::move(o)) {
        }
        inline Surface& operator=(Surface&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~Surface() {
            if (ptr) wgpuSurfaceRelease(ptr);
        }

        SurfaceTexture getCurrentTexture();
        void configure(const WGPUSurfaceConfiguration& config);
        inline void present() {
            wgpuSurfacePresent(ptr);
        }
    };

    struct BindGroupLayout : Resource<WGPUBindGroupLayout> {
		using Resource::Resource;
        inline BindGroupLayout& operator=(BindGroupLayout&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~BindGroupLayout() {
            if (ptr) wgpuBindGroupLayoutRelease(ptr);
        }
    };
    struct BindGroup : Resource<WGPUBindGroup> {
		using Resource::Resource;
        inline BindGroup& operator=(BindGroup&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~BindGroup() {
            if (ptr) wgpuBindGroupRelease(ptr);
        }
    };

    struct Buffer : Resource<WGPUBuffer> {
        using Resource::Resource;
        inline Buffer& operator=(Buffer&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~Buffer() {
            if (ptr) wgpuBufferRelease(ptr);
        }
        inline size_t getSize() {
            return wgpuBufferGetSize(ptr);
        }
    };

    struct RenderPassEncoder;
    struct CommandBuffer;
    struct CommandEncoder : Resource<WGPUCommandEncoder> {
        using Resource::Resource;
        inline CommandEncoder(CommandEncoder&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
        }
        inline ~CommandEncoder() {
            release();
        }
        inline void release() {
            if (ptr) wgpuCommandEncoderRelease(ptr);
            ptr = nullptr;
        }

        RenderPassEncoder beginRenderPass(const WGPURenderPassDescriptor& desc);
        RenderPassEncoder beginRenderPassForSurface(const AppObjects& ao, FrameData& frameData);

        CommandBuffer finish(const WGPUCommandBufferDescriptor& desc);
        CommandBuffer finish(const char* name);
    };

    struct PipelineLayout : Resource<WGPUPipelineLayout> {
		using Resource::Resource;
        inline PipelineLayout(PipelineLayout&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
        }
        inline PipelineLayout& operator=(PipelineLayout&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~PipelineLayout() {
            if (ptr) wgpuPipelineLayoutRelease(ptr);
        }
    };

    struct RenderPipeline : Resource<WGPURenderPipeline> {
        using Resource::Resource;
        inline RenderPipeline(RenderPipeline&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
        }
        inline RenderPipeline& operator=(RenderPipeline&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~RenderPipeline() {
            if (ptr) wgpuRenderPipelineRelease(ptr);
        }
    };

    struct Buffer;
    struct RenderPassEncoder : Resource<WGPURenderPassEncoder> {
        inline ~RenderPassEncoder() {
            release();
        }
        inline void release() {
            if (ptr) wgpuRenderPassEncoderRelease(ptr);
            ptr = nullptr;
        }

        inline void end() {
            wgpuRenderPassEncoderEnd(ptr);
        }
        inline void draw(uint32_t vertexCnt, uint32_t instanceCnt=1, uint32_t vertexOffset=0, uint32_t instanceOffset=0) {
            wgpuRenderPassEncoderDraw(ptr, vertexCnt, instanceCnt, vertexOffset, instanceOffset);
        }
        inline void drawIndexed(uint32_t indexCnt, uint32_t instanceCnt=1, uint32_t indexOffset=0, uint32_t vertexOffset=0, uint32_t instanceOffset=0) {
            wgpuRenderPassEncoderDrawIndexed(ptr, indexCnt, instanceCnt, indexOffset, vertexOffset, instanceOffset);
        }
        inline void setViewport(float x, float y, float w, float h, float mindepth, float maxdepth) {
            wgpuRenderPassEncoderSetViewport(ptr, x, y, w, h, mindepth, maxdepth);
        }
        inline void setVertexBuffer(uint32_t slot, Buffer& buffer, uint64_t offset, uint64_t size) {
            wgpuRenderPassEncoderSetVertexBuffer(ptr, slot, buffer, offset, size);
        }
        inline void setBindGroup(uint32_t groupIndex, BindGroup& group, size_t dynOffCnt=0, uint32_t* dynOffsets=0) {
            wgpuRenderPassEncoderSetBindGroup(ptr, groupIndex, group, dynOffCnt, dynOffsets);
        }
        inline void setRenderPipeline(RenderPipeline& r) {
            wgpuRenderPassEncoderSetPipeline(ptr, r);
        }
        inline void setIndexBuffer(Buffer& b, WGPUIndexFormat format, uint64_t offset, uint64_t size) {
            wgpuRenderPassEncoderSetIndexBuffer(ptr, b.ptr, format, offset, size);
        }
    };

    struct Sampler : Resource<WGPUSampler> {
		using Resource::Resource;
		inline Sampler& operator=(Sampler&& o) { ptr = o.ptr; o.ptr = nullptr; return *this; }
        inline ~Sampler() {
            if (ptr) wgpuSamplerRelease(ptr);
        }
    };

    struct TextureView : public Resource<WGPUTextureView> {
        using Resource::Resource;
        inline TextureView(TextureView&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
        }
        inline TextureView& operator=(TextureView&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        inline ~TextureView() {
            if (ptr) wgpuTextureViewRelease(ptr);
        }
    };

    struct Texture : public Resource<WGPUTexture> {
        using Resource::Resource;

        inline Texture(Texture&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
        }
        inline Texture& operator=(Texture&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }

        inline ~Texture() {
            if (ptr) wgpuTextureRelease(ptr);
        }
        inline TextureView createView(const WGPUTextureViewDescriptor& desc) {
            return { wgpuTextureCreateView(ptr, &desc) };
        }
    };

    struct ShaderModule : Resource<WGPUShaderModule> {
        inline ~ShaderModule() {
            if (ptr) wgpuShaderModuleRelease(ptr);
        }
    };

    struct CommandBuffer : Resource<WGPUCommandBuffer> {
        inline ~CommandBuffer() {
            release();
        }
        inline void release() {
            if (ptr) wgpuCommandBufferRelease(ptr);
            ptr = nullptr;
        }
    };

    struct Queue : public Resource<WGPUQueue> {
        inline Queue& operator=(Queue&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }

        inline ~Queue() {
            if (ptr) wgpuQueueRelease(ptr);
        }
        inline void submit(size_t commandCount, CommandBuffer* commands) {
            wgpuQueueSubmit(ptr, commandCount, (WGPUCommandBuffer*)commands);
        }
        inline void submit(std::vector<CommandBuffer>& commands) {
            wgpuQueueSubmit(ptr, commands.size(), (WGPUCommandBuffer*)commands.data());
        }
        inline void writeBuffer(Buffer& buffer, uint64_t offset, void const* data, size_t size) {
            wgpuQueueWriteBuffer(ptr, buffer, offset, data, size);
        }
        inline void writeTexture(const WGPUImageCopyTexture& dst, void const* data, size_t size,
                                 const WGPUTextureDataLayout& dataLayout, const WGPUExtent3D& writeSize) {
            wgpuQueueWriteTexture(ptr, &dst, data, size, &dataLayout, &writeSize);
        }
    };

    // Not opaque -- a simple struct with a `texture` member.
    struct SurfaceTexture {
        Texture texture;
        bool suboptimal;
        WGPUSurfaceGetCurrentTextureStatus status;

        inline SurfaceTexture() {
        }
        inline SurfaceTexture(WGPUSurfaceTexture obj)
            : texture(obj.texture)
            , suboptimal(obj.suboptimal)
            , status(obj.status) {
        }
        SurfaceTexture(const SurfaceTexture& o)            = delete;
        SurfaceTexture& operator=(const SurfaceTexture& o) = delete;
        inline SurfaceTexture(SurfaceTexture&& o)
            : texture(std::move(o.texture)) {
            suboptimal = o.suboptimal;
            status     = o.status;
            o.texture.clear();
        }
        inline SurfaceTexture& operator=(SurfaceTexture&& o) {
            texture    = std::move(o.texture);
            suboptimal = o.suboptimal;
            status     = o.status;
            o.texture.clear();
            return *this;
        }
    };

    struct Device : Resource<WGPUDevice> {
        inline ~Device() {
            if (ptr) wgpuDeviceRelease(ptr);
        }

        inline Device& operator=(Device&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }

        inline BindGroup create(const WGPUBindGroupDescriptor& desc) {
            return { wgpuDeviceCreateBindGroup(ptr, &desc) };
        }
        inline BindGroupLayout create(const WGPUBindGroupLayoutDescriptor& desc) {
            return { wgpuDeviceCreateBindGroupLayout(ptr, &desc) };
        }
        inline PipelineLayout create(const WGPUPipelineLayoutDescriptor& desc) {
            return { wgpuDeviceCreatePipelineLayout(ptr, &desc) };
        }
        inline RenderPipeline create(const WGPURenderPipelineDescriptor& desc) {
            return { wgpuDeviceCreateRenderPipeline(ptr, &desc) };
        }
        inline Sampler create(const WGPUSamplerDescriptor& desc) {
            return { wgpuDeviceCreateSampler(ptr, &desc) };
        }
        inline ShaderModule create(const WGPUShaderModuleDescriptor& desc) {
            return { wgpuDeviceCreateShaderModule(ptr, &desc) };
        }
        inline Texture create(const WGPUTextureDescriptor& desc) {
            return { wgpuDeviceCreateTexture(ptr, &desc) };
        }
        inline CommandEncoder create(const WGPUCommandEncoderDescriptor& desc) {
            return { wgpuDeviceCreateCommandEncoder(ptr, &desc) };
        }
        inline Buffer create(const WGPUBufferDescriptor& desc) {
            return { wgpuDeviceCreateBuffer(ptr, &desc) };
        }
        inline Queue getQueue() {
            return { wgpuDeviceGetQueue(ptr) };
        }

		Texture createDepthTexture(uint32_t w, uint32_t h, WGPUTextureFormat fmt);
    };

    struct Adapter : public Resource<WGPUAdapter> {
        inline ~Adapter() {
            if (ptr) wgpuAdapterRelease(ptr);
        }

        inline Adapter& operator=(Adapter&& o) {
            return (Adapter&)Resource<WGPUAdapter>::operator=(std::move(o));
        }

        // std::unique_ptr<WGPUAdapterRequestDeviceCallback> requestDevice(WGPUDeviceDescriptor& desc,
        // WGPUAdapterRequestDeviceCallback&& callback);
        Device requestDevice(const WGPUDeviceDescriptor& desc);
    };

    struct Instance : public Resource<WGPUInstance> {
        using Resource::Resource;
        inline ~Instance() {
            if (ptr) wgpuInstanceRelease(ptr);
        }
        inline Instance& operator=(Instance&& o) {
            ptr   = o.ptr;
            o.ptr = nullptr;
            return *this;
        }
        static Instance create(const WGPUInstanceDescriptor& desc);

        Adapter requestAdapter(const WGPURequestAdapterOptions& options);
    };

	struct RenderPipelineWithLayout {
        PipelineLayout layout;
        RenderPipeline pipeline;

		inline operator RenderPipeline&() {
			return pipeline;
		}
		inline operator const RenderPipeline&() const {
			return pipeline;
		}
	};

    inline RenderPassEncoder CommandEncoder::beginRenderPass(const WGPURenderPassDescriptor& desc) {
        return { wgpuCommandEncoderBeginRenderPass(ptr, &desc) };
    }
    inline CommandBuffer CommandEncoder::finish(const WGPUCommandBufferDescriptor& desc) {
        return { wgpuCommandEncoderFinish(ptr, &desc) };
    }
    inline CommandBuffer CommandEncoder::finish(const char* label) {
        WGPUCommandBufferDescriptor desc { .nextInChain = nullptr, .label = label };
        return finish(desc);
    }

    inline void BufferMapping::unmap() {
        if (target) wgpuBufferUnmap(*target);
        target = nullptr;
    }

    inline SurfaceTexture Surface::getCurrentTexture() {
        WGPUSurfaceTexture t;
        wgpuSurfaceGetCurrentTexture(ptr, &t);
        return SurfaceTexture { t };
    }
    inline void Surface::configure(const WGPUSurfaceConfiguration& config) {
        wgpuSurfaceConfigure(ptr, &config);
    }

    WGPUStencilFaceState WGPUStencilFaceState_Default();

	// These are not the only ways that these structs may need to be created,
	// but they capture a good default
	WGPUPrimitiveState WGPUPrimitiveState_Default();
	WGPUBlendState WGPUBlendState_Default();
	WGPUColorTargetState WGPUColorTargetState_Default(const AppObjects& ao, WGPUBlendState& blend);
	WGPUFragmentState WGPUFragmentState_Default(ShaderModule& shader, WGPUColorTargetState& cts, const char *entry = "fs_main");
	WGPUVertexState WGPUVertexState_Default(ShaderModule& shader, WGPUVertexBufferLayout& vbl, const char *entry = "vs_main");
	WGPUMultisampleState WGPUMultisampleState_Default();

    WGPUDepthStencilState WGPUDepthStencilState_Default(AppObjects& ao);

}
