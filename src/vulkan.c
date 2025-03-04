/* This is an unfinished Vulkan renderer. The renderer is currently at the
 * "Staging buffer" stage. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cglm/cglm.h"
#include "common.h"
#include "config.h"
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#include "stb_ds.h"

#ifdef NDEBUG
#define ENABLE_VALIDATION_LAYERS false
#else
#define ENABLE_VALIDATION_LAYERS true
#endif

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define ARRAY_COUNT_STATIC(array) (sizeof(array) / sizeof((array)[0]))

enum : size_t {
	/* If you want to change this to a non-power of 2, change the frame
	 * incrementing logic to use modulo instead of AND. */
	MAX_FRAMES_IN_FLIGHT = 2
};

typedef struct ALIGN(32) Vertex {
	vec2 pos;
	vec3 color;
} Vertex;

const Vertex vertices[] = {
    {{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}	
};

struct ALIGN(32) AttributeDescriptions {
	VkVertexInputAttributeDescription descriptions[2];
};

/* Must initialize to nullptr. To use with stb_ds. */
typedef const char **vector_str;

typedef struct QueueFamilyIndices {
	struct {
		uint32_t value;
		bool exists;
	} ALIGN(8) graphicsFamily;
	struct {
		uint32_t value;
		bool exists;
	} ALIGN(8) presentFamily;
} ALIGN(16) QueueFamilyIndices;

typedef struct {
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR formats[128];
	VkPresentModeKHR presentModes[7];
	uint32_t formatsLength;
	uint32_t presentModesLength;
} ALIGN(128) SwapChainSupportDetails;

/* stb_ds.h string hashmap */
extern Arguments arguments;

GLFWwindow *window;
uint32_t currentFrame;
VkBuffer vertexBuffer;
VkCommandBuffer *commandBuffers; /* stb_ds.h array */
VkCommandPool commandPool;
VkDebugUtilsMessengerEXT debugMessenger;
VkDevice device;
VkDeviceMemory vertexBufferMemory;
VkExtent2D swapChainExtent;
VkFence *inFlightFences; /* stb_ds.h array */
VkFormat swapChainImageFormat;
VkFramebuffer *swapChainFramebuffers; /* stb_ds.h array */
VkImage *swapChainImages; /* stb_ds.h array */
VkImageView *swapChainImageViews; /* stb_ds.h array */
VkInstance instance;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkPipeline graphicsPipeline;
VkPipelineLayout pipelineLayout;
VkQueue graphicsQueue;
VkQueue presentQueue;
VkRenderPass renderPass;
VkSemaphore *imageAvailableSemaphores; /* stb_ds.h array */
VkSemaphore *renderFinishedSemaphores; /* stb_ds.h array */
VkSurfaceKHR surface;
VkSwapchainKHR swapChain;

bool framebufferResized;
float lastFrameTimeSec;
float currentFrameTimeSec;
float deltaTimeSec;
uint64_t frameCount;

vec3 cameraPos;

extern GLFWwindow *window;

const char *const validationLayers[] = {
	"VK_LAYER_KHRONOS_validation",
};
const char *const deviceExtensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

void framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
	(void)window;
	(void)width;
	(void)height;
	framebufferResized = true;
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action,
		 int mods)
{
	(void)scancode;
	(void)mods;

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

Error windowInit(void)
{
	if (!glfwInit()) {
		return ERR_WINDOW_CREATION_FAILED;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window = glfwCreateWindow(WIDTH, HEIGHT, ENGINE_NAME, nullptr,
				  nullptr);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

	if (!window) {
		return ERR_WINDOW_CREATION_FAILED;
	}

	glfwSetKeyCallback(window, keyCallback);

	return ERR_OK;
}

void processInput(GLFWwindow *window)
{
	(void)window;
	glfwPollEvents();
}


bool checkValidationLayerSupport(void)
{
	uint32_t layerCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	VkLayerProperties *availableLayers =
		malloc(layerCount * sizeof(VkLayerProperties));
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

	bool layersAllFound = true;
	for (size_t i = 0; i < ARRAY_COUNT_STATIC(validationLayers); i++) {
		bool layerFound = false;
		const char *layerName = validationLayers[i];

		for (uint32_t j = 0; j < layerCount; j++) {
			VkLayerProperties layerProperties = availableLayers[j];
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			layersAllFound = false;
			break;
		}
	}

	free(availableLayers);
	return layersAllFound;
}

/* The caller is responsible for calling `arrfree()` on the returned vector*/
vector_str getRequiredExtensions(void)
{
	uint32_t glfwExtensionCount = 0;
	const char **glfwExtensions = nullptr;
	/* Freed automatically by GLFW. */
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	vector_str extensions = nullptr;
	arrsetcap(extensions, glfwExtensionCount);
	for (uint32_t i = 0; i < glfwExtensionCount; i++) {
		arrput(extensions, glfwExtensions[i]);
	}

	if (ENABLE_VALIDATION_LAYERS) {
		arrput(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	      VkDebugUtilsMessageTypeFlagsEXT messageType,
	      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	      void *pUserData)
{
	(void)pUserData;

	char *severity = "";
	switch (messageSeverity) {
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		/* Not worth reporting a message. */
		return VK_FALSE;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		severity = "Warning";
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
	default:
		severity = "Error";
	}

	char *type = "";
	switch (messageType) {
	case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
		type = "[general] ";
		break;
	case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
		type = "[validation] ";
		break;
	case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
		type = "[performance] ";
		break;
	default:
		break;
	}

	(void)fprintf(stderr, "%s: %s%s\n", severity, type,
		      pCallbackData->pMessage);

	return VK_FALSE;
}

void populateDebugMessengerCreateInfo(
	VkDebugUtilsMessengerCreateInfoEXT *createInfo)
{
	memset(createInfo, 0, sizeof(VkDebugUtilsMessengerCreateInfoEXT));
	createInfo->sType =
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo->messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo->messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo->pfnUserCallback = debugCallback;
}

Error createInstance(void)
{
	if (ENABLE_VALIDATION_LAYERS && !checkValidationLayerSupport()) {
		return ERR_INSTANCE_CREATION_FAILED;
	}

	VkApplicationInfo appInfo = { 0 };
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "hello triangle";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;
	appInfo.pNext = nullptr;

	VkInstanceCreateInfo createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
	vector_str extensions = getRequiredExtensions();
	createInfo.enabledExtensionCount = arrlen(extensions);
	createInfo.ppEnabledExtensionNames = extensions;
	if (ENABLE_VALIDATION_LAYERS) {
		createInfo.enabledLayerCount =
			ARRAY_COUNT_STATIC(validationLayers);
		createInfo.ppEnabledLayerNames = validationLayers;
		populateDebugMessengerCreateInfo(&debugCreateInfo);
		createInfo.pNext = &debugCreateInfo;
	} else {
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
	arrfree(extensions);

	return result == VK_SUCCESS
		? ERR_OK
		: ERR_INSTANCE_CREATION_FAILED;
}

VkResult createDebugUtilsMessengerEXT(
	VkInstance instance,
	const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkDebugUtilsMessengerEXT *pDebugMessenger)
{
	PFN_vkCreateDebugUtilsMessengerEXT func =
		(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
			instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	}
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

Error setupDebugMessenger(void)
{
	if (!ENABLE_VALIDATION_LAYERS) {
		return ERR_OK;
	}

	VkDebugUtilsMessengerCreateInfoEXT createInfo;
	populateDebugMessengerCreateInfo(&createInfo);

	VkResult res = createDebugUtilsMessengerEXT(instance, &createInfo,
						    nullptr, &debugMessenger);
	if (res != VK_SUCCESS) {
		return ERR_DEBUG_MESSENGER_CREATION_FAILED;
	}
	return ERR_OK;
}

void destroyDebugUtilsMessengerEXT(VkInstance instance,
				   VkDebugUtilsMessengerEXT debugMessenger,
				   const VkAllocationCallbacks *pAllocator)
{
	PFN_vkDestroyDebugUtilsMessengerEXT func =
		(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
			instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) {
		func(instance, debugMessenger, pAllocator);
	}
}

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device)
{
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
						 nullptr);
	VkQueueFamilyProperties *queueFamilies = nullptr;
	arrsetlen(queueFamilies, queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
						 queueFamilies);

	QueueFamilyIndices indices = { 0 };

	for (int i = 0; i < arrlen(queueFamilies); i++) {
		VkQueueFamilyProperties queueFamily = queueFamilies[i];
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily.value = i;
			indices.graphicsFamily.exists = true;

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface,
							     &presentSupport);
			if (presentSupport) {
				indices.presentFamily.value = i;
				indices.presentFamily.exists = true;
			}
		}
	}

	arrfree(queueFamilies);

	return indices;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device)
{
	uint32_t extensionCount = 0;
	VkExtensionProperties *availableExtensions = nullptr;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
					     nullptr);
	arrsetlen(availableExtensions, extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
					     availableExtensions);

	size_t deviceExtensionsCount = ARRAY_COUNT_STATIC(deviceExtensions);
	struct ALIGN(16) {
		char *key;
		bool value;
	} *requiredExtensions = nullptr;
	for (size_t i = 0; i < deviceExtensionsCount; i++) {
		shput(requiredExtensions, deviceExtensions[i], true);
	}

	for (uint32_t i = 0; i < extensionCount; i++) {
		(void)shdel(requiredExtensions,
			    availableExtensions[i].extensionName);
	}

	bool supported = shlen(requiredExtensions) == 0;

	shfree(requiredExtensions);
	arrfree(availableExtensions);

	return supported;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR *capabilities)
{
	if (capabilities->currentExtent.width != UINT32_MAX) {
		return capabilities->currentExtent;
	}

	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(window, &width, &height);

	VkExtent2D actualExtent = {
		(uint32_t)width,
		(uint32_t)height,
	};

	actualExtent.width = CLAMP(actualExtent.width,
				   capabilities->minImageExtent.width,
				   capabilities->maxImageExtent.width);
	actualExtent.height = CLAMP(actualExtent.height,
				    capabilities->minImageExtent.height,
				    capabilities->maxImageExtent.height);

	return actualExtent;
}

/* Return VK_PRESENT_MODE_MAILBOX_KHR if present, otherwise
 * VK_PRESENT_MODE_FIFO_KHR. */
VkPresentModeKHR
chooseSwapPresentMode(const VkPresentModeKHR *availablePresentModes,
		      uint32_t availablePresentModesLength) /* must be >= 1 */
{
	assert(availablePresentModesLength >= 1);

	for (uint32_t i = 0; i < availablePresentModesLength; i++) {
		VkPresentModeKHR availablePresentMode =
			availablePresentModes[i];
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkSurfaceFormatKHR
chooseSwapSurfaceFormat(const VkSurfaceFormatKHR *availableFormats,
			uint32_t availableFormatsLength) /* must be >= 1 */
{
	assert(availableFormatsLength >= 1);

	for (uint32_t i = 0; i < availableFormatsLength; i++) {
		VkSurfaceFormatKHR availableFormat = availableFormats[i];
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
		    availableFormat.colorSpace ==
			    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device)
{
	SwapChainSupportDetails details = { 0 };
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
						  &details.capabilities);

	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface,
					     &details.formatsLength, nullptr);

	/* If SwapChainSupportDetails can't fit all formats found, truncate. */
	if (unlikely(details.formatsLength >
		     sizeof(details.formats) / sizeof(VkSurfaceFormatKHR))) {
		VkSurfaceFormatKHR *hugeFormats = calloc(
			details.formatsLength, sizeof(VkSurfaceFormatKHR));
		vkGetPhysicalDeviceSurfaceFormatsKHR(
			device, surface, &details.formatsLength, hugeFormats);
		memcpy(details.formats, hugeFormats, sizeof(details.formats));
		free(hugeFormats);
	} else {
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface,
						     &details.formatsLength,
						     details.formats);
	}

	vkGetPhysicalDeviceSurfacePresentModesKHR(
		device, surface, &details.presentModesLength, nullptr);
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
						  &details.presentModesLength,
						  details.presentModes);

	return details;
}

bool isDeviceSuitable(VkPhysicalDevice device)
{
	QueueFamilyIndices indices = findQueueFamilies(device);
	bool indicesComplete = indices.presentFamily.exists &&
			       indices.graphicsFamily.exists;
	bool extensionsSupported = checkDeviceExtensionSupport(device);
	bool swapChainAdequate = false;
	if (extensionsSupported) {
		SwapChainSupportDetails swapChainSupport =
			querySwapChainSupport(device);
		swapChainAdequate = swapChainSupport.formatsLength > 0 &&
				    swapChainSupport.presentModesLength > 0;
	}
	return indicesComplete && extensionsSupported && swapChainAdequate;
}

bool isDeviceDiscreteGPU(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;
	vkGetPhysicalDeviceProperties(device, &deviceProperties);
	vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

	return deviceProperties.deviceType ==
		       VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
	       isDeviceSuitable(device);
}

Error pickPhysicalDevice(void)
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	if (deviceCount == 0) {
		return ERR_NO_GPU_FOUND;
	}

	VkPhysicalDevice *devices = nullptr;
	arrsetlen(devices, deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices);

	for (int i = 0; i < arrlen(devices); i++) {
		VkPhysicalDevice device = devices[i];
		if (isDeviceDiscreteGPU(device)) {
			physicalDevice = device;
			break;
		}
	}

	/* No discrete GPU found, going for any. */
	if (physicalDevice == VK_NULL_HANDLE) {
		for (int i = 0; i < arrlen(devices); i++) {
			VkPhysicalDevice device = devices[i];
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				break;
			}
		}
	}

	arrfree(devices);

	if (physicalDevice == VK_NULL_HANDLE) {
		return ERR_NO_GPU_FOUND;
	}

	return ERR_OK;
}

Error createSwapChain(void)
{
	SwapChainSupportDetails swapChainSupport =
		querySwapChainSupport(physicalDevice);
	VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(
		swapChainSupport.formats, swapChainSupport.formatsLength);
	VkPresentModeKHR presentMode =
		chooseSwapPresentMode(swapChainSupport.presentModes,
				      swapChainSupport.presentModesLength);
	VkExtent2D extent = chooseSwapExtent(&swapChainSupport.capabilities);

	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

	if (swapChainSupport.capabilities.maxImageCount > 0 &&
	    imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.preTransform =
		swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
	uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value,
					  indices.presentFamily.value };

	if (indices.graphicsFamily.value != indices.presentFamily.value) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0; /* Optional */
		createInfo.pQueueFamilyIndices = nullptr; /* Optional */
	}

	if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) !=
	    VK_SUCCESS) {
		return ERR_SWAP_CHAIN_CREATION_FAILED;
	}

	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
	arrsetlen(swapChainImages, imageCount);
	vkGetSwapchainImagesKHR(device, swapChain, &imageCount,
				swapChainImages);
	swapChainImageFormat = surfaceFormat.format;
	swapChainExtent = extent;

	return ERR_OK;
}

Error createLogicalDevice(void)
{
	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

	/* Graphics queue, and maybe present queue if they're the same. */
	VkDeviceQueueCreateInfo queuesCreateInfo[2] = { 0 };
	size_t queuesCreateInfoLength = 1;
	float queuePriority = 1.0f;
	queuesCreateInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queuesCreateInfo[0].queueFamilyIndex = indices.graphicsFamily.value;
	queuesCreateInfo[0].queueCount = 1;
	queuesCreateInfo[0].pQueuePriorities = &queuePriority;

	/* Present queue if it is different from graphics queue. */
	if (indices.graphicsFamily.value != indices.presentFamily.value) {
		queuesCreateInfoLength = 2;
		queuesCreateInfo[1].sType =
			VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queuesCreateInfo[1].queueFamilyIndex =
			indices.presentFamily.value;
		queuesCreateInfo[1].queueCount = 1;
		queuesCreateInfo[1].pQueuePriorities = &queuePriority;
	}

	VkPhysicalDeviceFeatures deviceFeatures = { 0 };

	VkDeviceCreateInfo createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = queuesCreateInfo;
	createInfo.queueCreateInfoCount = queuesCreateInfoLength;
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = ARRAY_COUNT_STATIC(deviceExtensions);
	createInfo.ppEnabledExtensionNames = deviceExtensions;

	if (ENABLE_VALIDATION_LAYERS) {
		createInfo.enabledLayerCount =
			ARRAY_COUNT_STATIC(validationLayers);
		createInfo.ppEnabledLayerNames = validationLayers;
	} else {
		createInfo.enabledLayerCount = 0;
	}

	if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
	    VK_SUCCESS) {
		return ERR_LOGICAL_DEVICE_CREATION_FAILED;
	}

	vkGetDeviceQueue(device, indices.graphicsFamily.value, 0,
			 &graphicsQueue);
	vkGetDeviceQueue(device, indices.presentFamily.value, 0, &presentQueue);

	return ERR_OK;
}

Error createSurface(void)
{
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
	    VK_SUCCESS) {
		return ERR_WINDOW_SURFACE_CREATION_FAILED;
	}

	return ERR_OK;
}

Error createImageViews(void)
{
	int imageslength = arrlen(swapChainImages);
	arrsetlen(swapChainImageViews, imageslength);
	for (int i = 0; i < imageslength; i++) {
		VkImageViewCreateInfo createInfo = { 0 };
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = swapChainImages[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = swapChainImageFormat;

		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		createInfo.subresourceRange.aspectMask =
			VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &createInfo, nullptr,
				      &swapChainImageViews[i]) != VK_SUCCESS) {
			return ERR_IMAGE_VIEW_CREATION_FAILED;
		}
	}

	return ERR_OK;
}

Error createRenderPass(void)
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass)
	    != VK_SUCCESS) {
		/* Failed to create a render pass. */
		return ERR_RENDER_PASS_CREATION_FAILED;
	}

	return ERR_OK;
}

/* `code` must have padding of 3 extra bytes for uint32_t
 *  interpretation. */
Error createShaderModule(const char *code, size_t length, VkShaderModule *module)
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = length;
	createInfo.pCode = (uint32_t *)code;

	if (vkCreateShaderModule(device, &createInfo, nullptr, module)
	    != VK_SUCCESS) {
		return ERR_SHADER_CREATION_FAILED;
	}

	return ERR_OK;
}

VkVertexInputBindingDescription vertexGetBindingDescription(void)
{
	VkVertexInputBindingDescription bindingDescription = {};

	bindingDescription.binding = 0;
	bindingDescription.stride = sizeof(Vertex);
	bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return bindingDescription;
}

struct AttributeDescriptions vertexGetAttributeDescriptions(void)
{
	struct AttributeDescriptions attributeDescriptions = {};

	attributeDescriptions.descriptions[0].binding = 0;
	attributeDescriptions.descriptions[0].location = 0;
	attributeDescriptions.descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
	attributeDescriptions.descriptions[0].offset = offsetof(Vertex, pos);

	attributeDescriptions.descriptions[1].binding = 0;
	attributeDescriptions.descriptions[1].location = 1;
	attributeDescriptions.descriptions[1].format =
		VK_FORMAT_R32G32B32_SFLOAT;
	attributeDescriptions.descriptions[1].offset = offsetof(Vertex, color);

	return attributeDescriptions;
}

Error createGraphicsPipeline(void)
{
	const char vertCode[] = {
#embed VULKAN_VERTEX_SHADER_PATH
		/* Null term + buffer for 32 bit interpretation */
		, '\0', '\0', '\0', '\0'
	};

	const char fragCode[] = {
#embed VULKAN_FRAGMENT_SHADER_PATH
		/* Null term + buffer for 32 bit interpretation */
		, '\0', '\0', '\0', '\0'
	};

	size_t vertLength = sizeof(vertCode) - 4;
	size_t fragLength = sizeof(fragCode) - 4;

	VkShaderModule vertShaderModule = nullptr;
	VkShaderModule fragShaderModule = nullptr;
	
	Error e = createShaderModule(vertCode, vertLength, &vertShaderModule);
	if (e != ERR_OK) {
		return e;
	}
	e = createShaderModule(fragCode, fragLength, &fragShaderModule);
	if (e != ERR_OK) {
		return e;
	}

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
	vertShaderStageInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = {
		vertShaderStageInfo,
		fragShaderStageInfo
	};

	/* Vertex input */
	VkVertexInputBindingDescription bindingDescription
		= vertexGetBindingDescription();
	struct AttributeDescriptions attributeDescriptions
		= vertexGetAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.vertexAttributeDescriptionCount
		= ARRAY_COUNT_STATIC(attributeDescriptions.descriptions);
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.pVertexAttributeDescriptions
		= attributeDescriptions.descriptions;

	/* Input assembly */
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType =
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	/* Viewport and scissors */
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)swapChainExtent.width;
	viewport.height = (float)swapChainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = swapChainExtent;

	/* Dynamic state */
	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
		sizeof(dynamicStates) / sizeof(VkDynamicState);
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	/* Not sure if I should add that. */
	viewportState.pViewports = &viewport;
	viewportState.pScissors = &scissor;

	/* Rasterization */
	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f;
	rasterizer.depthBiasClamp = 0.0f;
	rasterizer.depthBiasSlopeFactor = 0.0f;

	/* Multisampling */
	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType =
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;

	/* Depth and stencil testing */
	/* ¯\_(ツ)_/¯ */

	/* Color bending */
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
		| VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT
		| VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType =
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

	/* Pipeline Layout */
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pSetLayouts = nullptr;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
				   &pipelineLayout) != VK_SUCCESS) {
		vkDestroyShaderModule(device, vertShaderModule, nullptr);
		vkDestroyShaderModule(device, fragShaderModule, nullptr);
		return ERR_PIPELINE_LAYOUT_CREATION_FAILED;
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = nullptr; // Optional
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	VkResult vkE = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
						 &pipelineInfo, nullptr,
						 &graphicsPipeline);

	vkDestroyShaderModule(device, vertShaderModule, nullptr);
	vkDestroyShaderModule(device, fragShaderModule, nullptr);

	if (vkE != VK_SUCCESS) {
		return ERR_GRAPHICS_PIPELINE_CREATION_FAILED;
	}
	return ERR_OK;
}

Error createFramebuffers(void)
{
	size_t swapChainImageViewsLength = arrlen(swapChainImageViews);
	arrsetlen(swapChainFramebuffers, swapChainImageViewsLength);
	for (size_t i = 0; i < swapChainImageViewsLength; i++) {
		VkImageView attachments[] = {
			swapChainImageViews[i]
		};
		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType =
			VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = swapChainExtent.width;
		framebufferInfo.height = swapChainExtent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
					&swapChainFramebuffers[i])
		    != VK_SUCCESS) {
			return ERR_FRAMEBUFFER_CREATION_FAILED;
		}
	}

	return ERR_OK;
}

Error createCommandPool(void)
{
	QueueFamilyIndices queueFamilyIndices =
		findQueueFamilies(physicalDevice);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value;

	if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool)
	    != VK_SUCCESS) {
		return ERR_COMMAND_POOL_CREATION_FAILED;
	}

	return ERR_OK;
}

Error createCommandBuffers(void)
{
	arrsetlen(commandBuffers, MAX_FRAMES_IN_FLIGHT);

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = arrlen(commandBuffers);

	if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers)
	    != VK_SUCCESS) {
		return ERR_COMMAND_BUFFER_ALLOCATION_FAILED;
	}

	return ERR_OK;
}

/* Return -1 if there are no suitable memory types. */
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if (typeFilter & (1 << i)
		    && (memProperties.memoryTypes[i].propertyFlags
			& properties) == properties) {
			return i;
		}
	}

	return (uint32_t)-1;
}

Error createVertexBuffer(void)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeof(vertices);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer)
	    != VK_SUCCESS) {
		return ERR_SHADER_CREATION_FAILED;
	}

	VkMemoryRequirements memRequirements = {};
	vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

	uint32_t memoryTypeIndex =
		findMemoryType(memRequirements.memoryTypeBits,
			       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			       | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (memoryTypeIndex == (uint32_t)-1) {
		return ERR_SHADER_CREATION_FAILED;
	}

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = memoryTypeIndex;

	if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory)
	    != VK_SUCCESS) {
		return ERR_SHADER_CREATION_FAILED;
	}

	vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

	void *data = NULL;
	vkMapMemory(device, vertexBufferMemory, 0, bufferInfo.size, 0, &data);
	memcpy(data, vertices, (size_t)bufferInfo.size);
	vkUnmapMemory(device, vertexBufferMemory);

	return ERR_OK;
}

Error recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		/* Failed to begin recording command buffer. */
		return ERR_COMMAND_BUFFER_RECORDING_FAILED;
	}

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass;
	renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
	renderPassInfo.renderArea.offset.x = 0;
	renderPassInfo.renderArea.offset.y = 0;
	renderPassInfo.renderArea.extent = swapChainExtent;

	VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues= &clearColor;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
			     VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			  graphicsPipeline);

	VkBuffer vertexBuffers[] = {vertexBuffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)swapChainExtent.width;
	viewport.height = (float)swapChainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = swapChainExtent;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	vkCmdDraw(commandBuffer, ARRAY_COUNT_STATIC(vertices), 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		/* Failed to record command buffer. */
		return ERR_COMMAND_BUFFER_RECORDING_FAILED;
	}

	return ERR_OK;
}

Error createSyncObjects(void)
{
	arrsetlen(imageAvailableSemaphores, MAX_FRAMES_IN_FLIGHT);
	arrsetlen(renderFinishedSemaphores, MAX_FRAMES_IN_FLIGHT);
	arrsetlen(inFlightFences, MAX_FRAMES_IN_FLIGHT);

	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
				      &imageAvailableSemaphores[i])
		    != VK_SUCCESS
		    || vkCreateSemaphore(device, &semaphoreInfo, nullptr,
					 &renderFinishedSemaphores[i])
		    != VK_SUCCESS
		    || vkCreateFence(device, &fenceInfo, nullptr,
				     &inFlightFences[i])
		    != VK_SUCCESS) {
			/* Failed to create semaphores. */
			return ERR_SEMAPHORE_CREATION_FAILED;
		}
	}

	return ERR_OK;
}

void cleanupSwapChain(void)
{
	for (int i = 0; i < arrlen(swapChainFramebuffers); i++) {
		vkDestroyFramebuffer(device, swapChainFramebuffers[i],
				     nullptr);
	}

	for (int i = 0; i < arrlen(swapChainImageViews); i++) {
		vkDestroyImageView(device, swapChainImageViews[i], nullptr);
	}

	vkDestroySwapchainKHR(device, swapChain, nullptr);
}

Error recreateSwapChain(void)
{
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	/* Handle minimized windows. */
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(device);
	cleanupSwapChain();

	Error e = ERR_OK;

	e = createSwapChain();
	if (e != ERR_OK) {
		return e;
	}

	e = createImageViews();
	if (e != ERR_OK) {
		return e;
	}

	e = createFramebuffers();
	if (e != ERR_OK) {
		return e;
	}

	return ERR_OK;
}

void cleanupGraphics(void)
{
	vkDeviceWaitIdle(device);
	cleanupSwapChain();
	vkDestroyBuffer(device, vertexBuffer, nullptr);
	vkFreeMemory(device, vertexBufferMemory, nullptr);
	vkDestroyPipeline(device, graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);
	for (int i = 0; i < arrlen(imageAvailableSemaphores); i++) {
		vkDestroySemaphore(device, imageAvailableSemaphores[i],
				   nullptr);
	}
	for (int i = 0; i < arrlen(renderFinishedSemaphores); i++) {
		vkDestroySemaphore(device, renderFinishedSemaphores[i],
				   nullptr);
	}
	for (int i = 0; i < arrlen(inFlightFences); i++) {
		vkDestroyFence(device, inFlightFences[i], nullptr);
	}
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyDevice(device, nullptr);
	if (ENABLE_VALIDATION_LAYERS) {
		destroyDebugUtilsMessengerEXT(instance, debugMessenger,
					      nullptr);
	}
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
	arrfree(swapChainImages);
	arrfree(swapChainImageViews);
	arrfree(swapChainFramebuffers);
	arrfree(commandBuffers);
	arrfree(imageAvailableSemaphores);
	arrfree(renderFinishedSemaphores);
	arrfree(inFlightFences);
}

Error graphicsInit(void)
{
	if (!glfwVulkanSupported()) {
		return 3;
	}

	Error e = createInstance();
	if (e != ERR_OK) {
		return e;
	}

	e = setupDebugMessenger();
	if (e != ERR_OK) {
		return e;
	}

	e = createSurface();
	if (e != ERR_OK) {
		return e;
	}

	e = pickPhysicalDevice();
	if (e != ERR_OK) {
		return e;
	}

	e = createLogicalDevice();
	if (e != ERR_OK) {
		return e;
	}

	e = createSwapChain();
	if (e != ERR_OK) {
		return e;
	}

	e = createImageViews();
	if (e != ERR_OK) {
		return e;
	}

	e = createRenderPass();
	if (e != ERR_OK) {
		return e;
	}

	e = createGraphicsPipeline();
	if (e != ERR_OK) {
		return e;
	}

	e = createFramebuffers();
	if (e != ERR_OK) {
		return e;
	}

	e = createCommandPool();
	if (e != ERR_OK) {
		return e;
	}

	e = createVertexBuffer();
	if (e != ERR_OK) {
		return e;
	}

	e = createCommandBuffers();
	if (e != ERR_OK) {
		return e;
	}

	e = createSyncObjects();
	if (e != ERR_OK) {
		return e;
	}

	return ERR_OK;
}

Error drawFrame(void) {
	vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
			UINT64_MAX);

	if (framebufferResized) {
		framebufferResized = false;
		recreateSwapChain();
	}

	uint32_t imageIndex = 0;
	VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
			      imageAvailableSemaphores[currentFrame],
			      VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		return recreateSwapChain();
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		/* Failed to acquire swap chain image. */
		return ERR_SWAP_CHAIN_CREATION_FAILED;
	}

	/* Only reset the fence if we are submitting work. */
	vkResetFences(device, 1, &inFlightFences[currentFrame]);

	vkResetCommandBuffer(commandBuffers[currentFrame], 0);
	recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = {
		imageAvailableSemaphores[currentFrame]
	};
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

	VkSemaphore signalSemaphores[] = {
		renderFinishedSemaphores[currentFrame]
	};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
			  inFlightFences[currentFrame])
	    != VK_SUCCESS) {
		return ERR_COMMAND_BUFFER_DRAWING_FAILED;
	}

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = {
		swapChain
	};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = nullptr;

	result = vkQueuePresentKHR(presentQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR
	    || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
		framebufferResized = false;
		Error e = recreateSwapChain();
		if (e != ERR_OK) {
			return e;
		}
	} else if (result != VK_SUCCESS) {
		return ERR_SWAP_CHAIN_PRESENTATION_FAILED;
	}

	currentFrame += 1;
	currentFrame &= MAX_FRAMES_IN_FLIGHT - 1;

	return ERR_OK;
}

void cleanupWindow() {
	glfwDestroyWindow(window);
	glfwTerminate();
}
