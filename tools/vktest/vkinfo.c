/*
 * vkinfo: a small vulkaninfo for bring-up (Vulkan-Tools isn't packaged for
 * Haiku). Lists the physical devices with their properties, memory heaps
 * and queue families, then creates and destroys a logical device on each.
 *
 * build: gcc -o vkinfo vkinfo.c -lvulkan
 * run:   VK_DRIVER_FILES=~/gpu/install/data/vulkan/icd.d/radeon_icd.x86_64.json ./vkinfo
 */
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#define CHECK(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { \
	printf("[!] %s: %d\n", #call, _r); return 1; } } while (0)

static const char *
DeviceType(VkPhysicalDeviceType type)
{
	switch (type) {
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
		case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
		default: return "other";
	}
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkinfo",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
	};
	VkInstance instance;
	CHECK(vkCreateInstance(&instanceInfo, NULL, &instance));

	uint32_t count = 0;
	CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
	printf("%u physical device(s)\n", count);
	VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
	CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));

	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(devices[i], &props);
		printf("\n%s (%s)\n", props.deviceName, DeviceType(props.deviceType));
		printf("  API %u.%u.%u, driver %#x, vendor %#x, device %#x\n",
			VK_VERSION_MAJOR(props.apiVersion),
			VK_VERSION_MINOR(props.apiVersion),
			VK_VERSION_PATCH(props.apiVersion), props.driverVersion,
			props.vendorID, props.deviceID);
		printf("  timestamp period %.2f ns, max image 2D %u, max compute "
			"workgroup invocations %u\n", props.limits.timestampPeriod,
			props.limits.maxImageDimension2D,
			props.limits.maxComputeWorkGroupInvocations);

		VkPhysicalDeviceMemoryProperties memory;
		vkGetPhysicalDeviceMemoryProperties(devices[i], &memory);
		for (uint32_t h = 0; h < memory.memoryHeapCount; h++) {
			printf("  heap %u: %llu MB%s\n", h,
				(unsigned long long)(memory.memoryHeaps[h].size >> 20),
				(memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
					? ", device local" : "");
		}
		for (uint32_t t = 0; t < memory.memoryTypeCount; t++) {
			printf("  memory type %u: heap %u, flags %#x\n", t,
				memory.memoryTypes[t].heapIndex,
				memory.memoryTypes[t].propertyFlags);
		}

		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount,
			NULL);
		VkQueueFamilyProperties *families = calloc(familyCount,
			sizeof(*families));
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount,
			families);
		for (uint32_t f = 0; f < familyCount; f++) {
			printf("  queue family %u: %u queue(s), flags %#x\n", f,
				families[f].queueCount, families[f].queueFlags);
		}

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
		VkDevice device;
		VkResult result = vkCreateDevice(devices[i], &deviceInfo, NULL,
			&device);
		printf("  vkCreateDevice: %s (%d)\n",
			result == VK_SUCCESS ? "OK" : "[!] failed", result);
		if (result == VK_SUCCESS)
			vkDestroyDevice(device, NULL);
		free(families);
	}

	free(devices);
	vkDestroyInstance(instance, NULL);
	return 0;
}
