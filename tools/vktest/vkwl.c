/*
 * vkwl: Vulkan on screen through Wayland (Haiku's in-process Wayland server
 * shows it as a native window). A spinning triangle, presented through
 * Mesa's Wayland WSI (wl_shm copy path), for a number of frames.
 *
 * build: wayland-scanner client-header <xdg-shell.xml> xdg-shell-client-protocol.h
 *        wayland-scanner private-code  <xdg-shell.xml> xdg-shell-protocol.c
 *        glslangValidator -V spin.vert -o spin.vert.spv
 *        glslangValidator -V tri.frag -o tri.frag.spv
 *        gcc -o vkwl vkwl.c xdg-shell-protocol.c -lvulkan -lwayland-client -lm
 * usage: vkwl [frames [width height]]
 */
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <wayland-client.h>
#include <vulkan/vulkan.h>
#include "xdg-shell-client-protocol.h"

#define CHECK(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { \
	printf("[!] %s: %d\n", #call, _r); exit(1); } } while (0)

enum { kWidth = 640, kHeight = 480, kMaxImages = 8 };

static struct wl_compositor *sCompositor;
static struct xdg_wm_base *sWmBase;
static int sConfigured, sClosed;


static void
WmBasePing(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener sWmBaseListener = {WmBasePing};


static void
RegistryGlobal(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		sCompositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		sWmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(sWmBase, &sWmBaseListener, NULL);
	}
}

static void
RegistryRemove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener sRegistryListener = {
	RegistryGlobal, RegistryRemove};


static void
SurfaceConfigure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
	sConfigured = 1;
}

static const struct xdg_surface_listener sSurfaceListener = {SurfaceConfigure};


static void
ToplevelConfigure(void *data, struct xdg_toplevel *toplevel, int32_t width,
	int32_t height, struct wl_array *states)
{
}

static void
ToplevelClose(void *data, struct xdg_toplevel *toplevel)
{
	sClosed = 1;
}

static const struct xdg_toplevel_listener sToplevelListener = {
	ToplevelConfigure, ToplevelClose};


static VkShaderModule
LoadShader(VkDevice device, const char *path)
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
	CHECK(vkCreateShaderModule(device, &info, NULL, &module));
	free(code);
	return module;
}


static double
Now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int frames = argc > 1 ? atoi(argv[1]) : 600;
	uint32_t width = argc > 3 ? atoi(argv[2]) : kWidth;
	uint32_t height = argc > 3 ? atoi(argv[3]) : kHeight;

	// --- Wayland window
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		printf("[!] wl_display_connect failed\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &sRegistryListener, NULL);
	wl_display_roundtrip(display);
	if (sCompositor == NULL || sWmBase == NULL) {
		printf("[!] no wl_compositor or xdg_wm_base\n");
		return 1;
	}
	struct wl_surface *surface = wl_compositor_create_surface(sCompositor);
	struct xdg_surface *xdgSurface = xdg_wm_base_get_xdg_surface(sWmBase,
		surface);
	xdg_surface_add_listener(xdgSurface, &sSurfaceListener, NULL);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdgSurface);
	xdg_toplevel_add_listener(toplevel, &sToplevelListener, NULL);
	xdg_toplevel_set_title(toplevel, "RADV on Radeon RX 560");
	wl_surface_commit(surface);
	while (!sConfigured)
		wl_display_dispatch(display);
	printf("window configured\n");

	// --- Vulkan instance, surface, device
	const char *instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkwl",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
		.enabledExtensionCount = 2,
		.ppEnabledExtensionNames = instanceExtensions,
	};
	VkInstance instance;
	CHECK(vkCreateInstance(&instanceInfo, NULL, &instance));
	VkWaylandSurfaceCreateInfoKHR surfaceInfo = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.display = display,
		.surface = surface,
	};
	VkSurfaceKHR vkSurface;
	CHECK(vkCreateWaylandSurfaceKHR(instance, &surfaceInfo, NULL, &vkSurface));

	uint32_t count = 1;
	VkPhysicalDevice physical;
	VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
	if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
		printf("[!] no physical device\n");
		return 1;
	}
	VkBool32 supported = VK_FALSE;
	CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, 0, vkSurface,
		&supported));
	printf("present support on queue family 0: %s\n", supported ? "yes" : "no");
	if (!supported)
		return 1;

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &priority,
	};
	const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueInfo,
		.enabledExtensionCount = 1,
		.ppEnabledExtensionNames = deviceExtensions,
	};
	VkDevice device;
	CHECK(vkCreateDevice(physical, &deviceInfo, NULL, &device));
	VkQueue queue;
	vkGetDeviceQueue(device, 0, 0, &queue);

	// --- swapchain
	uint32_t formatCount = 0;
	CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, vkSurface,
		&formatCount, NULL));
	VkSurfaceFormatKHR formats[32];
	if (formatCount > 32)
		formatCount = 32;
	CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, vkSurface,
		&formatCount, formats));
	VkSurfaceFormatKHR format = formats[0];
	for (uint32_t i = 0; i < formatCount; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
			format = formats[i];
	}
	printf("%u surface formats, using %d\n", formatCount, format.format);
	VkSurfaceCapabilitiesKHR caps;
	CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, vkSurface,
		&caps));
	VkExtent2D extent = {width, height};
	if (caps.currentExtent.width != 0xffffffff)
		extent = caps.currentExtent;
	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount != 0 && imageCount > caps.maxImageCount)
		imageCount = caps.maxImageCount;
	VkSwapchainCreateInfoKHR swapchainInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = vkSurface,
		.minImageCount = imageCount,
		.imageFormat = format.format,
		.imageColorSpace = format.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
	};
	VkSwapchainKHR swapchain;
	CHECK(vkCreateSwapchainKHR(device, &swapchainInfo, NULL, &swapchain));
	VkImage images[kMaxImages];
	imageCount = kMaxImages;
	CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images));
	printf("swapchain: %u images, %ux%u\n", imageCount, extent.width,
		extent.height);

	// --- render pass, framebuffers, pipeline
	VkAttachmentDescription attachment = {
		.format = format.format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	VkAttachmentReference colorRef = {0,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorRef,
	};
	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
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
	CHECK(vkCreateRenderPass(device, &passInfo, NULL, &renderPass));

	VkImageView views[kMaxImages];
	VkFramebuffer framebuffers[kMaxImages];
	for (uint32_t i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo viewInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format.format,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		CHECK(vkCreateImageView(device, &viewInfo, NULL, &views[i]));
		VkFramebufferCreateInfo framebufferInfo = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = renderPass,
			.attachmentCount = 1,
			.pAttachments = &views[i],
			.width = extent.width,
			.height = extent.height,
			.layers = 1,
		};
		CHECK(vkCreateFramebuffer(device, &framebufferInfo, NULL,
			&framebuffers[i]));
	}

	VkShaderModule vertex = LoadShader(device, "spin.vert.spv");
	VkShaderModule fragment = LoadShader(device, "tri.frag.spv");
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
	VkViewport viewport = {0, 0, extent.width, extent.height, 0, 1};
	VkRect2D scissor = {{0, 0}, extent};
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
	VkPushConstantRange pushRange = {VK_SHADER_STAGE_VERTEX_BIT, 0,
		2 * sizeof(float)};
	VkPipelineLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushRange,
	};
	VkPipelineLayout layout;
	CHECK(vkCreatePipelineLayout(device, &layoutInfo, NULL, &layout));
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
	CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
		NULL, &pipeline));

	// --- per frame objects (one frame in flight)
	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = 0,
	};
	VkCommandPool pool;
	CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &pool));
	VkCommandBufferAllocateInfo cmdInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmd;
	CHECK(vkAllocateCommandBuffers(device, &cmdInfo, &cmd));
	VkSemaphoreCreateInfo semaphoreInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkSemaphore acquired, rendered;
	CHECK(vkCreateSemaphore(device, &semaphoreInfo, NULL, &acquired));
	CHECK(vkCreateSemaphore(device, &semaphoreInfo, NULL, &rendered));
	VkFenceCreateInfo fenceInfo = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	VkFence fence;
	CHECK(vkCreateFence(device, &fenceInfo, NULL, &fence));

	// --- frame loop
	double start = Now(), last = start;
	int frame;
	for (frame = 0; frame < frames && !sClosed; frame++) {
		wl_display_dispatch_pending(display);
		result = vkWaitForFences(device, 1, &fence, VK_TRUE, 2000000000ull);
		if (result != VK_SUCCESS) {
			printf("[!] frame %d: fence %d\n", frame, result);
			break;
		}
		CHECK(vkResetFences(device, 1, &fence));
		uint32_t index;
		result = vkAcquireNextImageKHR(device, swapchain, 2000000000ull,
			acquired, VK_NULL_HANDLE, &index);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			printf("[!] frame %d: acquire %d\n", frame, result);
			break;
		}

		CHECK(vkResetCommandBuffer(cmd, 0));
		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
		VkClearValue clear = {.color = {.float32 = {0.1f, 0.1f, 0.2f, 1.0f}}};
		VkRenderPassBeginInfo renderBegin = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = renderPass,
			.framebuffer = framebuffers[index],
			.renderArea = {{0, 0}, extent},
			.clearValueCount = 1,
			.pClearValues = &clear,
		};
		vkCmdBeginRenderPass(cmd, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		float push[2] = {(float)(Now() - start),
			(float)extent.width / extent.height};
		vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
			sizeof(push), push);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
		CHECK(vkEndCommandBuffer(cmd));

		VkPipelineStageFlags waitStage
			= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &acquired,
			.pWaitDstStageMask = &waitStage,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &rendered,
		};
		CHECK(vkQueueSubmit(queue, 1, &submit, fence));
		VkPresentInfoKHR present = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &rendered,
			.swapchainCount = 1,
			.pSwapchains = &swapchain,
			.pImageIndices = &index,
		};
		result = vkQueuePresentKHR(queue, &present);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			printf("[!] frame %d: present %d\n", frame, result);
			break;
		}
		double now = Now();
		if (frame == 0)
			printf("first frame presented\n");
		if (now - last >= 2.0) {
			printf("frame %d, %.1f fps\n", frame + 1, (frame + 1) / (now - start));
			last = now;
		}
	}
	double elapsed = Now() - start;
	printf("%d frames in %.1f s (%.1f fps)%s\n", frame, elapsed,
		frame / elapsed, sClosed ? ", window closed" : "");

	vkDeviceWaitIdle(device);
	vkDestroyFence(device, fence, NULL);
	vkDestroySemaphore(device, acquired, NULL);
	vkDestroySemaphore(device, rendered, NULL);
	vkDestroyCommandPool(device, pool, NULL);
	vkDestroyPipeline(device, pipeline, NULL);
	vkDestroyPipelineLayout(device, layout, NULL);
	vkDestroyShaderModule(device, vertex, NULL);
	vkDestroyShaderModule(device, fragment, NULL);
	for (uint32_t i = 0; i < imageCount; i++) {
		vkDestroyFramebuffer(device, framebuffers[i], NULL);
		vkDestroyImageView(device, views[i], NULL);
	}
	vkDestroyRenderPass(device, renderPass, NULL);
	vkDestroySwapchainKHR(device, swapchain, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroySurfaceKHR(instance, vkSurface, NULL);
	vkDestroyInstance(instance, NULL);
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdgSurface);
	wl_surface_destroy(surface);
	wl_display_disconnect(display);
	return 0;
}
