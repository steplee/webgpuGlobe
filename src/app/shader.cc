#include "shader.h"

namespace wg {
    ShaderModule create_shader(Device& device, const char* src, const char* label) {

        WGPUShaderModuleWGSLDescriptor shaderCodeDesc {
			.chain = {
				.next = nullptr,
				.sType = WGPUSType_ShaderModuleWGSLDescriptor,
			},
			.code = src
		};

        WGPUShaderModuleDescriptor shaderDesc {
			.nextInChain = &shaderCodeDesc.chain,
			.label = label,
			.hintCount = 0,
			.hints = nullptr
		};

        return device.create(shaderDesc);
    }
}
