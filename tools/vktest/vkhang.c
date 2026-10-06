/*
 * vkhang: blocks the GFX ring on purpose. Submits a command buffer that
 * waits for a VkEvent (RADV: a CP WAIT_REG_MEM on the event's memory) and
 * then fills a buffer, and checks that the fence doesn't signal while the
 * GPU waits.
 *   vkhang unblock   sets the event from the CPU: the GPU must resume and
 *                    fill the buffer (exit code 0)
 *   vkhang abandon   exits with the GPU still blocked; the server must
 *                    survive it, and a server restart (soft reset) must
 *                    recover the GPU
 *
 * build: gcc -o vkhang vkhang.c -lvulkan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int abandon = argc > 1 && strcmp(argv[1], "abandon") == 0;
	const uint32_t value = 0xcafe00aa;
	const VkDeviceSize size = 4096;

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkhang",
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

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	VkBuffer buffer;
	CHECK(vkCreateBuffer(sDevice, &bufferInfo, NULL, &buffer));
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(sDevice, buffer, &requirements);
	int type = FindMemoryType(requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

	VkEventCreateInfo eventInfo = {.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
	VkEvent event;
	CHECK(vkCreateEvent(sDevice, &eventInfo, NULL, &event));

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
	VkMemoryBarrier hostBarrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
	};
	vkCmdWaitEvents(cmd, 1, &event, VK_PIPELINE_STAGE_HOST_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 1, &hostBarrier, 0, NULL, 0, NULL);
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
	printf("submitting a command buffer that waits for an event\n");
	CHECK(vkQueueSubmit(sQueue, 1, &submit, fence));

	result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE,
		500000000ull);
	if (result != VK_TIMEOUT || words[0] == value) {
		printf("[!] the GPU didn't wait: fence %d, word %#010x\n", result,
			words[0]);
		return 1;
	}
	printf("GPU blocked: fence not signaled after 500 ms\n");

	if (abandon) {
		printf("exiting with the GPU blocked\n");
		_exit(0);
	}

	CHECK(vkSetEvent(sDevice, event));
	printf("event set\n");
	result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, 2000000000ull);
	uint32_t bad = 0;
	for (VkDeviceSize i = 0; i < size / 4; i++) {
		if (words[i] != value)
			bad++;
	}
	if (result != VK_SUCCESS || bad != 0) {
		printf("[!] the GPU didn't resume: fence %d, %u words not filled\n",
			result, bad);
		return 1;
	}
	printf("GPU resumed, buffer filled: OK\n");

	vkDestroyFence(sDevice, fence, NULL);
	vkDestroyEvent(sDevice, event, NULL);
	vkFreeCommandBuffers(sDevice, sPool, 1, &cmd);
	vkUnmapMemory(sDevice, memory);
	vkDestroyBuffer(sDevice, buffer, NULL);
	vkFreeMemory(sDevice, memory, NULL);
	vkDestroyCommandPool(sDevice, sPool, NULL);
	vkDestroyDevice(sDevice, NULL);
	vkDestroyInstance(instance, NULL);
	return 0;
}
