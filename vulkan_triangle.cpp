#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <optional>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const std::vector<const char*> VALIDATION_LAYERS_LIST = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
const bool VALIDATION_LAYERS_ENABLED = false;
#else
const bool VALIDATION_LAYERS_ENABLED = true;
#endif


// vkCreateDebugUtilsMessengerEXT is an extension func. We need to upload it manually.
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	}
	else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}


// Same for this.
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) {
		func(instance, debugMessenger, pAllocator);
	}
}



class TriangleApplication
{
public:
	void Run()
	{
		InitWindow();
		InitVulkan();
		MainLoop();
		CleanUp();
	}
private:
	void InitWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Do not use OpenGL.
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // No resize for a while.

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	}


	void InitVulkan()
	{
		CreateInstance();
		SetupDebugMessenger();
		SelectPhysicalDevice();
		CreateLogicalDevice();
	}


	void SetupDebugMessenger()
	{
		if (!VALIDATION_LAYERS_ENABLED) {
			return;
		}

		VkDebugUtilsMessengerCreateInfoEXT createInfo;
		FillDebugMessengerCreateInfo(createInfo);

		if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
			throw std::runtime_error("Failed to set up debug messenger");
		}
	}


	void FillDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
	{
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = DebugCallback;
	}


	void MainLoop()
	{
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
		}
	}


	void CleanUp()
	{
		// Logical devices don't interact directly with instances, which is why it's not included as a parameter.
		vkDestroyDevice(logicalDevice, nullptr);

		if (VALIDATION_LAYERS_ENABLED) {
			DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
		}

		vkDestroyInstance(instance, nullptr);

		glfwDestroyWindow(window);

		glfwTerminate();
	}


	void CreateInstance()
	{
		// Fill application info.
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;


		// Specify app info, global extensions and validation layers we want to use.
		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		// App info.
		createInfo.pApplicationInfo = &appInfo;

		const auto& extList = GetRequiredExtensions();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extList.size());
		createInfo.ppEnabledExtensionNames = extList.data();

		// Specify validation layers.
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (VALIDATION_LAYERS_ENABLED) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS_LIST.size());
			createInfo.ppEnabledLayerNames = VALIDATION_LAYERS_LIST.data();

			FillDebugMessengerCreateInfo(debugCreateInfo);

			// Pointer to a structure extending this structure.
			// Create an additional debug messenger, which will be used automatically in 
			// vkCreateInstance and vkDestroyInstance and cleaned up after.
			createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
		}
		else {
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;
		}

		if (!CheckVulkanExtensions()) {
			throw std::runtime_error("Some GLFW extensions are not supported by Vulkan");
		}
		else {
			std::cout << std::endl << "All GLFW extensions are supported by Vulkan" << std::endl << std::endl;
		}

		if (!CheckVulkanValidationLayerSupport()) {
			throw std::runtime_error("Some validation layers are not available to Vulkan");
		}
		else {
			std::cout << std::endl << "All validation layers available to Vulkan" << std::endl << std::endl;
		}

		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create instance");
		}
	}


	void SelectPhysicalDevice()
	{
		// Find GPUs with Vulkan support and then initialize physicalDevice.
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount == 0) {
			throw std::runtime_error("Failed to find Physical Device with Vulkan support");
		}

		std::vector<VkPhysicalDevice> deviceList(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, deviceList.data());

		for (const auto& device : deviceList) {
			if (CheckPhysicalDeviceRequirements(device)) {
				physicalDevice = device;
				break;
			}
		}

		if (physicalDevice == VK_NULL_HANDLE) {
			throw std::runtime_error("Failed to find a suitable Physical Device");
		}


	}


	bool CheckVulkanExtensions()
	{
		// Request number of supported extensions from Vulkan.
		uint32_t vulkanExtCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &vulkanExtCount, nullptr);

		// Create vector to hold detailed info about each extension.
		std::vector<VkExtensionProperties> vulkanExtList(vulkanExtCount);

		// Call function again to get array of extensions.
		vkEnumerateInstanceExtensionProperties(nullptr, &vulkanExtCount, vulkanExtList.data());

		const auto& extList = GetRequiredExtensions();

		for (int i = 0; i < static_cast<uint32_t>(extList.size()); ++i) {
			bool found = false;
			for (const auto& vulkanExt : vulkanExtList) {
				if (strcmp(vulkanExt.extensionName, extList[i])) {
					found = true;
					break;
				}
			}

			if (!found) {
				return false;
			}
		}

		return true;
	}


	bool CheckVulkanValidationLayerSupport()
	{
		uint32_t vulkanLayerCount;
		vkEnumerateInstanceLayerProperties(&vulkanLayerCount, nullptr);

		std::vector<VkLayerProperties> vulkanLayerList(vulkanLayerCount);
		vkEnumerateInstanceLayerProperties(&vulkanLayerCount, vulkanLayerList.data());

		for (const char* layerName : VALIDATION_LAYERS_LIST) {
			bool found = false;
			for (const auto& vulkanLayerName : vulkanLayerList) {
				if (strcmp(vulkanLayerName.layerName, layerName)) {
					found = true;
					break;
				}
			}

			if (!found) {
				return false;
			}
		}

		return true;
	}


	struct QueueFamilyIndices
	{
		// std::optional is a wrapper that contains no value until assign something to it.
		// To check if there is a value, use the method has_value().
		std::optional<uint32_t> graphicsFamily;

		bool IsValid() { return graphicsFamily.has_value(); }
	};


	bool CheckPhysicalDeviceRequirements(const VkPhysicalDevice& device)
	{
		// Name, type, supported Vulkan version.
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		// Whether GPU support texture compression, 64 bit floats, multiview rendering.
		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		QueueFamilyIndices indices = FindQueueFamilies(device);

		if (indices.IsValid()) {
			std::cout << std::endl << "Physical Device selected: " << deviceProperties.deviceName << std::endl << std::endl;
		}

		return indices.IsValid();
	}


	void CreateLogicalDevice()
	{
		QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);

		// We know queue families that GPU support.
		// Create queue family for graphics operations with one queue.
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
		queueCreateInfo.queueCount = 1;

		// Vulkan lets you assign priorities to queues to influence the scheduling of command buffer execution 
		// using floating point numbers between 0.0 and 1.0. This is required even if there is only a single queue.
		float queuePriority = 1.0f;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		// Specify the set of device features that we'll be using.
		// Assign to VK_FALSE by default for a while.
		VkPhysicalDeviceFeatures deviceFeatures{};

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pEnabledFeatures = &deviceFeatures;

		// Now again we need specify info about extensions and validation layers.
		// It is possible that there are Vulkan devices in the system that does not have some extensions.
		// For example VK_KHR_swapchain is a device specific extension.
		// Previous implementations of Vulkan made a distinction between instance and device specific validation layers, 
		// but this is no longer the case. Set them to be compatible with older implementations.
		createInfo.enabledExtensionCount = 0;
		if (VALIDATION_LAYERS_ENABLED) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS_LIST.size());
			createInfo.ppEnabledLayerNames = VALIDATION_LAYERS_LIST.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create Logical Device");
		}

		// Retrieve queue handle for our queue family. The third parameter is an index of queue in queue family.
		vkGetDeviceQueue(logicalDevice, indices.graphicsFamily.value(), 0, &graphicsQueue);
	}


	QueueFamilyIndices FindQueueFamilies(const VkPhysicalDevice& device)
	{
		// Find queue families that GPU supports. 
		// Almost every operation in Vulkan (drawing, texture uploading) requires commands to be submitted to a queue.
		// There are different types of queues - queue families. Each queue family allows only subset of commands. 
		// Example: one family for compute commands, another for memory transfer commands. 
		QueueFamilyIndices indices;

		// Get the list of queue families that GPU supports.
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilyList(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyList.data());

		// VkQueueFamilyProperties contains info about type of operations that are supported and 
		// the number of queues that can be created based on that family.
		// We need to find at least one queue family that supports VK_QUEUE_GRAPHICS_BIT.
		// VK_QUEUE_GRAPHICS_BIT specifies that queues in this queue family support graphics operations.
		int i = 0;
		for (const auto& queueFamily : queueFamilyList) {
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			if (indices.IsValid()) {
				break;
			}

			++i;
		}

		return indices;
	}


	std::vector<const char*> GetRequiredExtensions()
	{
		// Specify the desired global extensions.
		// Vulkan is platform agnostic, so for dealing with windows, we need an extension.
		// Get extensions from GLFW.
		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> extList(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (VALIDATION_LAYERS_ENABLED) {
			extList.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extList;
	}


	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)
	{

		std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;

		return VK_FALSE;
	}


	GLFWwindow* window = nullptr;

	// Is a connection between application and vulkan lib. Global context (states).
	VkInstance instance;

	// The debug messenger will provide detailed feedback on the application’s use of Vulkan 
	// when events of interest occur.
	// When an event of interest does occur, the debug messenger will submit a debug message 
	// to the debug callback that was provided during its creation.
	VkDebugUtilsMessengerEXT debugMessenger;

	// Store physical device (GPU). Implicitly destroyed, when VkInstance destroyed.
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	// Store logical device. Application view on actual device
	VkDevice logicalDevice;

	// The queues are automatically created along with the logical device and
	// this handle needs to interface with them.
	// Device queues are implicitly cleaned up when the logical device is destroyed.
	VkQueue graphicsQueue;
};

int main()
{
	TriangleApplication app;

	try {
		app.Run();
	}
	catch (std::exception& e) {
		std::cout << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}