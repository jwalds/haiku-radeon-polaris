/*
 * vktri: off-screen triangle. Renders a color interpolated triangle into a
 * 256x256 RGBA8 image (optimal tiling), copies it into a host visible
 * buffer, checks a few pixels and writes triangle.png.
 *
 * build: glslangValidator -V tri.vert -o tri.vert.spv
 *        glslangValidator -V tri.frag -o tri.frag.spv
 *        gcc -o vktri vktri.c -lvulkan
 * run in this directory (loads the .spv files)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#define CHECK(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { \
	printf("[!] %s: %d\n", #call, _r); exit(1); } } while (0)

enum { kWidth = 256, kHeight = 256 };

static VkDevice sDevice;
static VkPhysicalDeviceMemoryProperties sMemory;


static uint32_t
MemoryType(uint32_t bits, VkMemoryPropertyFlags flags)
{
	for (uint32_t i = 0; i < sMemory.memoryTypeCount; i++) {
		if ((bits & (1u << i))
			&& (sMemory.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}
	printf("[!] no memory type\n");
	exit(1);
}


static VkShaderModule
LoadShader(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		printf("[!] can't open %s\n", path);
		exit(1);
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint32_t *code = malloc(size);
	if (fread(code, 1, size, file) != (size_t)size)
		exit(1);
	fclose(file);
	VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code,
	};
	VkShaderModule module;
	CHECK(vkCreateShaderModule(sDevice, &info, NULL, &module));
	free(code);
	return module;
}


// --- minimal PNG writer (stored deflate blocks) ---

static uint32_t sCrcTable[256];

static uint32_t
Crc(uint32_t crc, const uint8_t *data, size_t size)
{
	if (sCrcTable[1] == 0) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			sCrcTable[n] = c;
		}
	}
	crc = ~crc;
	for (size_t i = 0; i < size; i++)
		crc = sCrcTable[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}


static void
Put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}


static void
WriteChunk(FILE *file, const char *type, const uint8_t *data, uint32_t size)
{
	uint8_t header[8];
	Put32(header, size);
	memcpy(header + 4, type, 4);
	fwrite(header, 1, 8, file);
	fwrite(data, 1, size, file);
	uint32_t crc = Crc(Crc(0, (const uint8_t *)type, 4), data, size);
	uint8_t tail[4];
	Put32(tail, crc);
	fwrite(tail, 1, 4, file);
}


static int
WritePng(const char *path, const uint8_t *rgba, int width, int height)
{
	size_t rowSize = 1 + width * 4;
	size_t rawSize = rowSize * height;
	uint8_t *raw = malloc(rawSize);
	for (int y = 0; y < height; y++) {
		raw[y * rowSize] = 0;
		memcpy(raw + y * rowSize + 1, rgba + y * width * 4, width * 4);
	}
	size_t blocks = (rawSize + 65534) / 65535;
	size_t zSize = 2 + rawSize + blocks * 5 + 4;
	uint8_t *z = malloc(zSize), *p = z;
	*p++ = 0x78; *p++ = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t offset = 0; offset < rawSize; offset += 65535) {
		size_t length = rawSize - offset < 65535 ? rawSize - offset : 65535;
		*p++ = offset + length == rawSize;
		*p++ = length; *p++ = length >> 8;
		*p++ = ~length; *p++ = ~length >> 8;
		memcpy(p, raw + offset, length);
		p += length;
	}
	for (size_t i = 0; i < rawSize; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	Put32(p, (b << 16) | a);
	p += 4;

	FILE *file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	static const uint8_t signature[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
	fwrite(signature, 1, 8, file);
	uint8_t ihdr[13];
	Put32(ihdr, width);
	Put32(ihdr + 4, height);
	ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	WriteChunk(file, "IHDR", ihdr, 13);
	WriteChunk(file, "IDAT", z, p - z);
	WriteChunk(file, "IEND", NULL, 0);
	fclose(file);
	free(raw);
	free(z);
	return 0;
}


int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vktri",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
	};
	VkInstance instance;
	CHECK(vkCreateInstance(&instanceInfo, NULL, &instance));
	uint32_t count = 1;
	VkPhysicalDevice physical;
	VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
	if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
		printf("[!] no physical device\n");
		return 1;
	}
	vkGetPhysicalDeviceMemoryProperties(physical, &sMemory);

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &priority,
	};
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueInfo,
	};
	CHECK(vkCreateDevice(physical, &deviceInfo, NULL, &sDevice));
	VkQueue queue;
	vkGetDeviceQueue(sDevice, 0, 0, &queue);

	// color attachment in VRAM, optimal tiling
	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = {kWidth, kHeight, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VkImage image;
	CHECK(vkCreateImage(sDevice, &imageInfo, NULL, &image));
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(sDevice, image, &requirements);
	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = MemoryType(requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};
	VkDeviceMemory imageMemory;
	CHECK(vkAllocateMemory(sDevice, &allocInfo, NULL, &imageMemory));
	CHECK(vkBindImageMemory(sDevice, image, imageMemory, 0));
	printf("image: %llu bytes, memory type %u\n",
		(unsigned long long)requirements.size, allocInfo.memoryTypeIndex);

	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	VkImageView view;
	CHECK(vkCreateImageView(sDevice, &viewInfo, NULL, &view));

	// read back buffer in system memory
	VkDeviceSize bufferSize = kWidth * kHeight * 4;
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = bufferSize,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	VkBuffer buffer;
	CHECK(vkCreateBuffer(sDevice, &bufferInfo, NULL, &buffer));
	vkGetBufferMemoryRequirements(sDevice, buffer, &requirements);
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = MemoryType(requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory bufferMemory;
	CHECK(vkAllocateMemory(sDevice, &allocInfo, NULL, &bufferMemory));
	CHECK(vkBindBufferMemory(sDevice, buffer, bufferMemory, 0));
	uint8_t *pixels;
	CHECK(vkMapMemory(sDevice, bufferMemory, 0, bufferSize, 0,
		(void **)&pixels));
	memset(pixels, 0xcd, bufferSize);

	VkAttachmentDescription attachment = {
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	};
	VkAttachmentReference colorRef = {0,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorRef,
	};
	VkSubpassDependency dependency = {
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
	};
	VkRenderPassCreateInfo passInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dependency,
	};
	VkRenderPass renderPass;
	CHECK(vkCreateRenderPass(sDevice, &passInfo, NULL, &renderPass));

	VkFramebufferCreateInfo framebufferInfo = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = renderPass,
		.attachmentCount = 1,
		.pAttachments = &view,
		.width = kWidth,
		.height = kHeight,
		.layers = 1,
	};
	VkFramebuffer framebuffer;
	CHECK(vkCreateFramebuffer(sDevice, &framebufferInfo, NULL, &framebuffer));

	VkShaderModule vertex = LoadShader("tri.vert.spv");
	VkShaderModule fragment = LoadShader("tri.frag.spv");
	VkPipelineShaderStageCreateInfo stages[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment,
			.pName = "main",
		},
	};
	VkPipelineVertexInputStateCreateInfo vertexInput = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	VkViewport viewport = {0, 0, kWidth, kHeight, 0, 1};
	VkRect2D scissor = {{0, 0}, {kWidth, kHeight}};
	VkPipelineViewportStateCreateInfo viewportState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor,
	};
	VkPipelineRasterizationStateCreateInfo raster = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkPipelineColorBlendAttachmentState blendAttachment = {
		.colorWriteMask = 0xf,
	};
	VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendAttachment,
	};
	VkPipelineLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	};
	VkPipelineLayout layout;
	CHECK(vkCreatePipelineLayout(sDevice, &layoutInfo, NULL, &layout));
	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertexInput,
		.pInputAssemblyState = &inputAssembly,
		.pViewportState = &viewportState,
		.pRasterizationState = &raster,
		.pMultisampleState = &multisample,
		.pColorBlendState = &blend,
		.layout = layout,
		.renderPass = renderPass,
	};
	VkPipeline pipeline;
	CHECK(vkCreateGraphicsPipelines(sDevice, VK_NULL_HANDLE, 1, &pipelineInfo,
		NULL, &pipeline));
	printf("pipeline created\n");

	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
	};
	VkCommandPool pool;
	CHECK(vkCreateCommandPool(sDevice, &poolInfo, NULL, &pool));
	VkCommandBufferAllocateInfo cmdInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmd;
	CHECK(vkAllocateCommandBuffers(sDevice, &cmdInfo, &cmd));
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
	VkClearValue clear = {.color = {.float32 = {0.1f, 0.1f, 0.2f, 1.0f}}};
	VkRenderPassBeginInfo renderBegin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = renderPass,
		.framebuffer = framebuffer,
		.renderArea = {{0, 0}, {kWidth, kHeight}},
		.clearValueCount = 1,
		.pClearValues = &clear,
	};
	vkCmdBeginRenderPass(cmd, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRenderPass(cmd);
	VkBufferImageCopy region = {
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.imageExtent = {kWidth, kHeight, 1},
	};
	vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		buffer, 1, &region);
	VkMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
	CHECK(vkEndCommandBuffer(cmd));

	VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence;
	CHECK(vkCreateFence(sDevice, &fenceInfo, NULL, &fence));
	VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	printf("submitting\n");
	CHECK(vkQueueSubmit(queue, 1, &submit, fence));
	result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, 2000000000ull);
	printf("fence: %s (%d)\n", result == VK_SUCCESS ? "signaled"
		: "[!] not signaled", result);
	if (result != VK_SUCCESS)
		return 1;

	// clear color (26, 26, 51) in a corner, the triangle's middle colored
	const uint8_t *corner = pixels;
	const uint8_t *middle = pixels + (kHeight / 2 * kWidth + kWidth / 2) * 4;
	const uint8_t *top = pixels + ((int)(kHeight * 0.15) * kWidth + kWidth / 2) * 4;
	printf("corner %u %u %u %u, middle %u %u %u %u, top %u %u %u %u\n",
		corner[0], corner[1], corner[2], corner[3],
		middle[0], middle[1], middle[2], middle[3],
		top[0], top[1], top[2], top[3]);
	// 0.1 * 255 = 25.5: the hardware rounds to 25 or 26
	int ok = abs(corner[0] - 26) <= 1 && abs(corner[1] - 26) <= 1
		&& abs(corner[2] - 51) <= 1 && corner[3] == 255 && middle[3] == 255
		&& middle[0] + middle[1] + middle[2] > 200 && top[0] > 150;
	if (WritePng("triangle.png", pixels, kWidth, kHeight) == 0)
		printf("wrote triangle.png\n");
	printf("%s\n", ok ? "triangle OK" : "[!] unexpected pixels");

	vkDeviceWaitIdle(sDevice);
	vkDestroyFence(sDevice, fence, NULL);
	vkDestroyCommandPool(sDevice, pool, NULL);
	vkDestroyPipeline(sDevice, pipeline, NULL);
	vkDestroyPipelineLayout(sDevice, layout, NULL);
	vkDestroyShaderModule(sDevice, vertex, NULL);
	vkDestroyShaderModule(sDevice, fragment, NULL);
	vkDestroyFramebuffer(sDevice, framebuffer, NULL);
	vkDestroyRenderPass(sDevice, renderPass, NULL);
	vkUnmapMemory(sDevice, bufferMemory);
	vkDestroyBuffer(sDevice, buffer, NULL);
	vkFreeMemory(sDevice, bufferMemory, NULL);
	vkDestroyImageView(sDevice, view, NULL);
	vkDestroyImage(sDevice, image, NULL);
	vkFreeMemory(sDevice, imageMemory, NULL);
	vkDestroyDevice(sDevice, NULL);
	vkDestroyInstance(instance, NULL);
	return ok ? 0 : 1;
}
