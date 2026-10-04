/*
 * vkbench: GPU memory bandwidth through Vulkan, to compare clocks.
 *   1. vkCmdFillBuffer of 64 MB in VRAM, 20 times
 *   2. vkCmdCopyBuffer of 64 MB VRAM -> VRAM, 20 times
 * Each test is one submission; the time is from vkQueueSubmit() to the
 * fence (wall clock), best of 3 runs.
 *
 * build: gcc -o vkbench vkbench.c -lvulkan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define CHECK(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { \
	printf("[!] %s: %d\n", #call, _r); exit(1); } } while (0)

enum {
	kSize = 64 << 20,
	kRepeats = 20,
	kRuns = 3,
};

static VkDevice sDevice;
static VkPhysicalDeviceMemoryProperties sMemory;
static VkQueue sQueue;
static VkCommandPool sPool;


static double
Now(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec + time.tv_nsec / 1e9;
}


static VkBuffer
CreateBuffer(VkDeviceMemory *memory)
{
	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = kSize,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	VkBuffer buffer;
	CHECK(vkCreateBuffer(sDevice, &bufferInfo, NULL, &buffer));
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(sDevice, buffer, &requirements);
	int type = -1;
	for (uint32_t i = 0; i < sMemory.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) != 0
			&& (sMemory.memoryTypes[i].propertyFlags
				& VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
			type = i;
			break;
		}
	}
	if (type < 0) {
		printf("[!] no VRAM memory type\n");
		exit(1);
	}
	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = type,
	};
	CHECK(vkAllocateMemory(sDevice, &allocInfo, NULL, memory));
	CHECK(vkBindBufferMemory(sDevice, buffer, *memory, 0));
	return buffer;
}


// seconds for one submission of kRepeats fills (src == NULL) or copies
static double
Run(VkBuffer src, VkBuffer dst)
{
	VkCommandBufferAllocateInfo cmdInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = sPool,
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
	VkMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT
			| VK_ACCESS_TRANSFER_WRITE_BIT,
	};
	VkBufferCopy region = {.size = kSize};
	for (int i = 0; i < kRepeats; i++) {
		if (src == VK_NULL_HANDLE)
			vkCmdFillBuffer(cmd, dst, 0, kSize, i);
		else
			vkCmdCopyBuffer(cmd, src, dst, 1, &region);
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
	}
	CHECK(vkEndCommandBuffer(cmd));

	VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence;
	CHECK(vkCreateFence(sDevice, &fenceInfo, NULL, &fence));
	VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	double start = Now();
	CHECK(vkQueueSubmit(sQueue, 1, &submit, fence));
	CHECK(vkWaitForFences(sDevice, 1, &fence, VK_TRUE, 10000000000ull));
	double seconds = Now() - start;
	vkDestroyFence(sDevice, fence, NULL);
	vkFreeCommandBuffers(sDevice, sPool, 1, &cmd);
	return seconds;
}


static void
Benchmark(const char *name, VkBuffer src, VkBuffer dst)
{
	double best = 1e9;
	for (int i = 0; i < kRuns; i++) {
		double seconds = Run(src, dst);
		if (seconds < best)
			best = seconds;
	}
	double bytes = (double)kSize * kRepeats;
	printf("%s: %d x %d MB in %.1f ms, %.1f GB/s\n", name, kRepeats,
		kSize >> 20, best * 1000, bytes / best / 1e9);
}


int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkbench",
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
	vkGetDeviceQueue(sDevice, 0, 0, &sQueue);
	VkCommandPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
	};
	CHECK(vkCreateCommandPool(sDevice, &poolInfo, NULL, &sPool));

	VkDeviceMemory srcMemory, dstMemory;
	VkBuffer src = CreateBuffer(&srcMemory);
	VkBuffer dst = CreateBuffer(&dstMemory);
	Benchmark("fill", VK_NULL_HANDLE, dst);
	Benchmark("copy", src, dst);

	vkDeviceWaitIdle(sDevice);
	vkDestroyBuffer(sDevice, src, NULL);
	vkDestroyBuffer(sDevice, dst, NULL);
	vkFreeMemory(sDevice, srcMemory, NULL);
	vkFreeMemory(sDevice, dstMemory, NULL);
	vkDestroyCommandPool(sDevice, sPool, NULL);
	vkDestroyDevice(sDevice, NULL);
	vkDestroyInstance(instance, NULL);
	return 0;
}
