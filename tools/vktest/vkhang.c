/*
 * vkhang: hangs the GFX ring on purpose.
 *   vkhang unblock   a command buffer waits for a VkEvent (RADV: CP
 *                    WAIT_REG_MEM): the fence must not signal in 500 ms;
 *                    then the CPU sets the event, the GPU must resume and
 *                    fill a buffer
 *   vkhang abandon   the same, but exits with the GPU blocked
 *   vkhang detect    the same, never unblocked: the server must detect the
 *                    hang (lockup timeout), reset the GPU and RADV report
 *                    VK_ERROR_DEVICE_LOST
 *   vkhang shader    a compute shader that never ends: the server's soft
 *                    recovery must kill its waves, the fence signal and the
 *                    device stay usable
 * Exit code 0 if the expected happened.
 *
 * build: glslangValidator -V loop.comp -o loop.comp.spv
 *        gcc -o vkhang vkhang.c -lvulkan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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


static double
Seconds()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}


static void *
ReadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	*size = ftell(file);
	fseek(file, 0, SEEK_SET);
	void *data = malloc(*size);
	if (fread(data, 1, *size, file) != *size) {
		free(data);
		data = NULL;
	}
	fclose(file);
	return data;
}


static VkShaderModule sModule;
static VkDescriptorSetLayout sSetLayout;
static VkPipelineLayout sLayout;
static VkPipeline sPipeline;
static VkDescriptorPool sPool2;


// records a compute dispatch of loop.comp on the buffer
static int
RecordEndlessShader(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
{
	size_t codeSize;
	void *code = ReadFile("loop.comp.spv", &codeSize);
	if (code == NULL) {
		printf("[!] can't read loop.comp.spv\n");
		return 1;
	}
	VkShaderModuleCreateInfo moduleInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = codeSize,
		.pCode = code,
	};
	CHECK(vkCreateShaderModule(sDevice, &moduleInfo, NULL, &sModule));

	VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};
	VkDescriptorSetLayoutCreateInfo setLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};
	CHECK(vkCreateDescriptorSetLayout(sDevice, &setLayoutInfo, NULL,
		&sSetLayout));
	VkPipelineLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &sSetLayout,
	};
	CHECK(vkCreatePipelineLayout(sDevice, &layoutInfo, NULL, &sLayout));
	VkComputePipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = sModule,
			.pName = "main",
		},
		.layout = sLayout,
	};
	CHECK(vkCreateComputePipelines(sDevice, VK_NULL_HANDLE, 1, &pipelineInfo,
		NULL, &sPipeline));

	VkDescriptorPoolSize poolSize = {
		.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
	};
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &poolSize,
	};
	CHECK(vkCreateDescriptorPool(sDevice, &poolInfo, NULL, &sPool2));
	VkDescriptorSetAllocateInfo setInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = sPool2,
		.descriptorSetCount = 1,
		.pSetLayouts = &sSetLayout,
	};
	VkDescriptorSet set;
	CHECK(vkAllocateDescriptorSets(sDevice, &setInfo, &set));
	VkDescriptorBufferInfo bufferDescriptor = {
		.buffer = buffer,
		.offset = 0,
		.range = size,
	};
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &bufferDescriptor,
	};
	vkUpdateDescriptorSets(sDevice, 1, &write, 0, NULL);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sLayout, 0, 1,
		&set, 0, NULL);
	vkCmdDispatch(cmd, 4, 1, 1);
	free(code);
	return 0;
}


static void
DestroyShader()
{
	if (sPipeline == VK_NULL_HANDLE)
		return;
	vkDestroyPipeline(sDevice, sPipeline, NULL);
	vkDestroyDescriptorPool(sDevice, sPool2, NULL);
	vkDestroyPipelineLayout(sDevice, sLayout, NULL);
	vkDestroyDescriptorSetLayout(sDevice, sSetLayout, NULL);
	vkDestroyShaderModule(sDevice, sModule, NULL);
}


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	const char *mode = argc > 1 ? argv[1] : "unblock";
	int abandon = strcmp(mode, "abandon") == 0;
	int detect = strcmp(mode, "detect") == 0;
	int shader = strcmp(mode, "shader") == 0;
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
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = 0,
	};
	CHECK(vkCreateCommandPool(sDevice, &poolInfo, NULL, &sPool));

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
			| VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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
	if (shader) {
		if (RecordEndlessShader(cmd, buffer, size) != 0)
			return 1;
	} else {
		VkMemoryBarrier hostBarrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		};
		vkCmdWaitEvents(cmd, 1, &event, VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 1, &hostBarrier, 0, NULL, 0, NULL);
		vkCmdFillBuffer(cmd, buffer, 0, size, value);
	}
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
	printf("submitting %s\n", shader ? "a shader that never ends"
		: "a command buffer that waits for an event");
	CHECK(vkQueueSubmit(sQueue, 1, &submit, fence));

	result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE,
		500000000ull);
	if (result != VK_TIMEOUT || words[0] == value) {
		printf("[!] the GPU didn't wait: fence %d, word %#010x\n", result,
			words[0]);
		return 1;
	}
	printf("GPU blocked: fence not signaled after 500 ms\n");

	if (detect || shader) {
		// the server's lockup timeout is 10 s by default
		double start = Seconds();
		result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, 60000000000ull);
		double waited = Seconds() - start + 0.5;
		printf("vkWaitForFences(): %d after %.1f s\n", result, waited);
		if (detect) {
			if (result != VK_ERROR_DEVICE_LOST) {
				printf("[!] expected VK_ERROR_DEVICE_LOST\n");
				return 1;
			}
			result = vkQueueSubmit(sQueue, 0, NULL, VK_NULL_HANDLE);
			printf("hang detected, device lost: OK (vkQueueSubmit(): %d)\n",
				result);
			// RADV's objects of a lost device can still be destroyed
			vkDestroyDevice(sDevice, NULL);
			vkDestroyInstance(instance, NULL);
			return 0;
		}
		if (result != VK_SUCCESS) {
			printf("[!] expected the fence after soft recovery\n");
			return 1;
		}
		// the device must still work
		VkResult status = vkGetFenceStatus(sDevice, fence);
		CHECK(vkResetFences(sDevice, 1, &fence));
		CHECK(vkResetCommandBuffer(cmd, 0));
		CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
		vkCmdFillBuffer(cmd, buffer, 0, size, value);
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
		CHECK(vkEndCommandBuffer(cmd));
		CHECK(vkQueueSubmit(sQueue, 1, &submit, fence));
		result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, 2000000000ull);
		if (status != VK_SUCCESS || result != VK_SUCCESS || words[1] != value) {
			printf("[!] the device doesn't work after soft recovery: %d %d\n",
				status, result);
			return 1;
		}
		printf("soft recovered, device still works: OK\n");
		vkDestroyFence(sDevice, fence, NULL);
		vkDestroyEvent(sDevice, event, NULL);
		vkFreeCommandBuffers(sDevice, sPool, 1, &cmd);
		vkDestroyCommandPool(sDevice, sPool, NULL);
		DestroyShader();
		vkUnmapMemory(sDevice, memory);
		vkDestroyBuffer(sDevice, buffer, NULL);
		vkFreeMemory(sDevice, memory, NULL);
		vkDestroyDevice(sDevice, NULL);
		vkDestroyInstance(instance, NULL);
		return 0;
	}

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
