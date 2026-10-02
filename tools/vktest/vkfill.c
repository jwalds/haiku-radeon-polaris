/*
 * vkfill: first GPU work through Vulkan. Fills buffers with
 * vkCmdFillBuffer on the graphics queue and checks them on the CPU:
 *   1. 4 KB in system memory (RADV uses CP DMA for small fills)
 *   2. 1 MB in system memory (RADV uses a compute shader)
 *   3. 1 MB in CPU visible VRAM (compute shader)
 * Every submission waits on a fence with a 2 s timeout.
 *
 * build: gcc -o vkfill vkfill.c -lvulkan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { \
	printf("[!] %s: %d\n", #call, _r); return 1; } } while (0)

static VkDevice sDevice;
static VkPhysicalDeviceMemoryProperties sMemory;
static VkQueue sQueue;
static VkCommandPool sPool;


static int
FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required,
	VkMemoryPropertyFlags forbidden)
{
	for (uint32_t i = 0; i < sMemory.memoryTypeCount; i++) {
		VkMemoryPropertyFlags flags = sMemory.memoryTypes[i].propertyFlags;
		if ((typeBits & (1u << i)) && (flags & required) == required
			&& (flags & forbidden) == 0)
			return i;
	}
	return -1;
}


static int
FillTest(const char *name, VkDeviceSize size, VkMemoryPropertyFlags required,
	VkMemoryPropertyFlags forbidden, uint32_t value)
{
	printf("%s: %llu bytes\n", name, (unsigned long long)size);

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	VkBuffer buffer;
	CHECK(vkCreateBuffer(sDevice, &bufferInfo, NULL, &buffer));
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(sDevice, buffer, &requirements);
	int type = FindMemoryType(requirements.memoryTypeBits, required,
		forbidden);
	if (type < 0) {
		printf("[!] no memory type\n");
		return 1;
	}
	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = type,
	};
	VkDeviceMemory memory;
	CHECK(vkAllocateMemory(sDevice, &allocInfo, NULL, &memory));
	CHECK(vkBindBufferMemory(sDevice, buffer, memory, 0));
	uint32_t *words;
	CHECK(vkMapMemory(sDevice, memory, 0, size, 0, (void **)&words));
	memset(words, 0, size);
	printf("  memory type %d, mapped at %p\n", type, (void *)words);

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
	vkCmdFillBuffer(cmd, buffer, 0, size, value);
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
	printf("  submitting\n");
	CHECK(vkQueueSubmit(sQueue, 1, &submit, fence));
	VkResult result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE,
		2000000000ull);
	printf("  fence: %s (%d)\n", result == VK_SUCCESS ? "signaled"
		: "[!] not signaled", result);

	uint32_t bad = 0;
	for (VkDeviceSize i = 0; i < size / 4; i++) {
		if (words[i] != value) {
			if (bad < 4)
				printf("  [!] word %llu = %#010x\n", (unsigned long long)i,
					words[i]);
			bad++;
		}
	}
	printf("  %s: %s\n", name, bad == 0 && result == VK_SUCCESS ? "OK"
		: "[!] FAILED");

	vkDestroyFence(sDevice, fence, NULL);
	vkFreeCommandBuffers(sDevice, sPool, 1, &cmd);
	vkUnmapMemory(sDevice, memory);
	vkDestroyBuffer(sDevice, buffer, NULL);
	vkFreeMemory(sDevice, memory, NULL);
	return bad == 0 && result == VK_SUCCESS ? 0 : 1;
}


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int tests = argc > 1 ? atoi(argv[1]) : 3;

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkfill",
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

	const VkMemoryPropertyFlags hostVisible
		= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	int failed = 0;
	if (tests >= 1) {
		failed |= FillTest("1. fill 4 KB, system memory", 4096, hostVisible,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0xcafe0001);
	}
	if (tests >= 2 && !failed) {
		failed |= FillTest("2. fill 1 MB, system memory", 1 << 20, hostVisible,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0xcafe0002);
	}
	if (tests >= 3 && !failed) {
		failed |= FillTest("3. fill 1 MB, visible VRAM", 1 << 20,
			hostVisible | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, 0xcafe0003);
	}

	vkDeviceWaitIdle(sDevice);
	vkDestroyCommandPool(sDevice, sPool, NULL);
	vkDestroyDevice(sDevice, NULL);
	vkDestroyInstance(instance, NULL);
	printf("%s\n", failed ? "[!] failed" : "all OK");
	return failed;
}
