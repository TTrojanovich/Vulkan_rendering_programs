#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <set>
#include <optional>
#include <fstream>

// In screen coordinates.
const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

// How many frames should be processed concurrently.
const int MAX_FRAMES_IN_FLIGHT = 2;

// Not all graphics card are capable with desired extensions. So we must check their support.
const std::vector<const char*> REQUIRED_PHYSICAL_DEVICE_EXTENSIONS = {
	// Swapchain owns the buffers we will render to before we visualize them on the screen.
	// The swapchain is essentially a queue of images that are waiting to be presented to the screen.
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};


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


void PrintMessage(std::string msg)
{
	std::cout << std::endl << msg << std::endl << std::endl;
}


static std::vector<char> ReadFile(const std::string& filename)
{
	// ::ate - read from end of file. To determine the size of the file for buffer allocation.
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	// Return to beginning of the file.
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
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
		CreateSurface();
		SelectPhysicalDevice();
		CreateLogicalDevice();
		CreateSwapchain();
		CreateImageViews();
		CreateRenderPass();
		CreateGraphicsPipeline();
		CreateFramebuffers();
		CreateCommandPool();
		CreateCommandBuffers();
		CreateSyncObjects();
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
			DrawFrame();
		}

		// All of the operations in DrawFrame are asynchronous. That means that when we exit the loop in MainLoop, 
		// drawing and presentation operations may still be going on. Cleaning up resources while that is happening is a bad idea.
		// To fix that problem, we should wait for the logical device to finish operations before exiting MainLoop 
		// and destroying the window.
		vkDeviceWaitIdle(logicalDevice);
	}


	void CleanUp()
	{
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			vkDestroySemaphore(logicalDevice, renderFinishedSemaphores[i], nullptr);
			vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], nullptr);
			vkDestroyFence(logicalDevice, inFlightFences[i], nullptr);
		}

		vkDestroyCommandPool(logicalDevice, commandPool, nullptr);

		for (auto framebuffer : swapchainFramebuffers) {
			vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
		}

		vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
		vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
		vkDestroyRenderPass(logicalDevice, renderPass, nullptr);

		for (auto imageView : swapchainImageViews) {
			vkDestroyImageView(logicalDevice, imageView, nullptr);
		}

		vkDestroySwapchainKHR(logicalDevice, swapchain, nullptr);

		// Logical devices don't interact directly with instances, which is why it's not included as a parameter.
		vkDestroyDevice(logicalDevice, nullptr);

		if (VALIDATION_LAYERS_ENABLED) {
			DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
		}

		vkDestroySurfaceKHR(instance, surface, nullptr);

		vkDestroyInstance(instance, nullptr);

		glfwDestroyWindow(window);

		glfwTerminate();
	}


	void CreateSyncObjects()
	{
		// Each frame should have its own set of semaphores.
		imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
		imagesInFlight.resize(swapchainImages.size(), VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		// Initialize fence with signaled state to avoid looping in the begining of DrawFrame.
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {

				throw std::runtime_error("Failed to create synchronization objects for a frame");
			}
		}
	}


	void DrawFrame()
	{
		// Takes an array of fences and waits for either any or all of them to be signaled before returning.
		vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
		
		// Acquire an image from the swapchain.
		// Third parameter specifies a timeout in nanoseconds for an image to become available. 
		// Using the maximum value of a 64 bit unsigned integer disables the timeout.
		// Index refers to the VkImage in swapchainImages array.
		uint32_t imageIndex;
		vkAcquireNextImageKHR(logicalDevice, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

		// Check if a previous frame is using this image (i.e. there is its fence to wait on)
		if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
			vkWaitForFences(logicalDevice, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
		}
		// Mark the image as now being in use by this frame
		imagesInFlight[imageIndex] = inFlightFences[currentFrame];

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		// Specify which semaphores to wait on before execution begins and in which stage(s) of the pipeline to wait.
		// We want to wait with writing colors to the image until it's available, so we're specifying the stage of the 
		// graphics pipeline that writes to the color attachment. That means that theoretically the implementation can 
		// already start executing vertex shader and such while the image is not yet available. Each entry in the waitStages 
		// array corresponds to the semaphore with the same index in pWaitSemaphores.
		VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		// The next two parameters specify which command buffers to actually submit for execution. 
		// We should submit the command buffer that binds the swapchain image we just acquired as color attachment.
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
		// The next two parameters specify which semaphores to signal once the command buffer(s) have finished execution.
		VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		// Unlike the semaphores, we manually need to restore the fence to the unsignaled state.
		vkResetFences(logicalDevice, 1, &inFlightFences[currentFrame]);

		// The function takes an array of VkSubmitInfo structures as argument for efficiency when the workload is much larger. 
		// The last parameter references an optional fence that will be signaled when the command buffers finish execution.
		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to submit draw command buffer");
		}

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		// Specify which semaphores to wait on before presentation can happen.
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		// Specify the swapchains to present images to and the index of the image for each swapchain. 
		VkSwapchainKHR swapchains[] = { swapchain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &imageIndex;
		// Specify an array of VkResult values to check for every individual swapchain if presentation was successful.
		// If using a single swapchain, then simply use the return value of the present function.
		presentInfo.pResults = nullptr; // Optional

		// Submits the request to present an image to the swapchain. 
		vkQueuePresentKHR(presentQueue, &presentInfo);

		vkQueueWaitIdle(presentQueue);

		currentFrame = ++currentFrame % MAX_FRAMES_IN_FLIGHT;
	}


	void CreateRenderPass()
	{
		// Specify how many color and depth buffers there will be, how many samples to use for each of them,
		// how their contents should be handled throughout the rendering operations. All of this info is in render pass object.

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = swapchainImageFormat;
		// No multisampling for now.
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

		// loadOp - what to do with the data in the attachment before rendering.
		// storeOp - after rendering.
		// Apply to color and depth data.
		// 1. VK_ATTACHMENT_LOAD_OP_LOAD: Preserve the existing contents of the attachment.
		// 2. VK_ATTACHMENT_LOAD_OP_CLEAR: Clear the values to a constant at the start.
		// 3. VK_ATTACHMENT_LOAD_OP_DONT_CARE: Existing contents are undefined; we don't care about them.
		// 1. VK_ATTACHMENT_STORE_OP_STORE: Rendered contents will be stored in memory and can be read later.
		// 2. VK_ATTACHMENT_STORE_OP_DONT_CARE: Contents of the framebuffer will be undefined after the rendering operation.
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		// Almost same for stencil buffer.
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		// Images need to be transitioned to specific layouts that are suitable for the operation that they're going 
		// to be involved in next. 
		// The initialLayout specifies which layout the image will have before the render pass begins. The finalLayout 
		// specifies the layout to automatically transition to when the render pass finishes. Using VK_IMAGE_LAYOUT_UNDEFINED 
		// for initialLayout means that we don't care what previous layout the image was in. The caveat of this special value 
		// is that the contents of the image are not guaranteed to be preserved, but that doesn't matter since we're going 
		// to clear it anyway. We want the image to be ready for presentation using the swapchain after rendering, 
		// which is why we use VK_IMAGE_LAYOUT_PRESENT_SRC_KHR as finalLayout
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// A single render pass can consist of multiple subpasses. Subpasses are subsequent rendering operations that depend 
		// on the contents of framebuffers in previous passes, for example a sequence of post-processing effects that are 
		// applied one after another. If you group these rendering operations into one render pass, then Vulkan is able to 
		// reorder the operations and conserve memory bandwidth for possibly better performance. 
		// For our very first triangle we'll stick to a single subpass.
		// Every subpass references one or more of the attachments that we've described using the structure in the previous sections.

		VkAttachmentReference colorAttachmentRef{};
		// Attachment index.
		colorAttachmentRef.attachment = 0;
		// Specify which layout we would like the attachment to have during a subpass that uses this reference.
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// Subpass itself.
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		// Specify reference.
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create render pass");
		}

		// The first two fields specify the indices of the dependency and the dependent subpass.
		// The special value VK_SUBPASS_EXTERNAL refers to the implicit subpass before or after the render pass 
		// depending on whether it is specified in srcSubpass or dstSubpass.The index 0 refers to our subpass, 
		// which is the firstand only one. The dstSubpass must always be higher than srcSubpass to prevent cycles 
		// in the dependency graph(unless one of the subpasses is VK_SUBPASS_EXTERNAL).
		// ah...
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		// The next two fields specify the operations to wait on and the stages in which these operations occur. 
		// We need to wait for the swapchain to finish reading from the image before we can access it. 
		// This can be accomplished by waiting on the color attachment output stage itself.
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		// The operations that should wait on this are in the color attachment stage and involve the writing 
		// of the color attachment. These settings will prevent the transition from happening until it's actually necessary 
		// (and allowed): when we want to start writing colors to it.
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;
	}


	void CreateGraphicsPipeline()
	{
		// ---------------------------------------------------
		// DESCRIBE THE PROGRAMMABLE STAGES OF THE PIPELINE.
		// ---------------------------------------------------

		// Upload shaders as bytecode.
		auto vertShaderCode = ReadFile("shaders/vert.spv");
		auto fragShaderCode = ReadFile("shaders/frag.spv");

		VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
		VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

		// Vertex shader stage info.
		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		// Function to invoke, entrypoint. It is possible to combine multiple fragment shaders 
		// into a single shader module and use different entry points to differentiate between their behaviors.
		vertShaderStageInfo.pName = "main";
		// Allows to specify values for shader constants. We can use a single shader module where its behavior 
		// can be configured at pipeline creation by specifying different values for the constants used in it. 
		// This is more efficient than configuring the shader using variables at render time, because the compiler 
		// can do optimizations like eliminating if statements that depend on these values. 
		vertShaderStageInfo.pSpecializationInfo = nullptr;

		// Fragment shader stage info.
		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";
		fragShaderStageInfo.pSpecializationInfo = nullptr;

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		// ---------------------------------------------------
		// DESCRIBE THE FIXED-FUNCTION STAGES OF THE PIPELINE.
		// ---------------------------------------------------

		// Describes the format of the vertex data that will be passed to the vertex shader.
		// It describes this in two ways:
		// 1. Bindings: spacing between data and whether the data is per-vertex or per-instance.
		// 2. Attribute descriptions: type of the attributes passed to the vertex shader, which binding 
		//    to load them from and at which offset.
		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional.
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional.

		// ---------------------------------------------------
		// INPUT ASSEMBLY.
		// ---------------------------------------------------

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		// If true, then it is possible to break up lines and triangles in the _STRIP topology modes
		// by using a special index of 0xFFFF or 0xFFFFFFFF.
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// ---------------------------------------------------
		// VIEWPORT.
		// ---------------------------------------------------

		// Viewport - region of the framebuffer, that the output will be fully rendered to.
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)swapchainExtent.width;
		viewport.height = (float)swapchainExtent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		// ---------------------------------------------------
		// SCISSOR.
		// ---------------------------------------------------

		// Scissor - region of the framebuffer. Any pixels outside the scissor rectangle will be discarded by the rasterizer. 
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = swapchainExtent;

		// Viewport and scissor rectangle need to be combined into a viewport state.
		// It is possible to use multiple viewports and scissor rectangles on some GPUs, so its members reference an array of them.
		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		// ---------------------------------------------------
		// RASTERIZER.
		// ---------------------------------------------------

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		// If VK_TRUE, then fragments that are beyond the near and far planes are clamped instead of discarding. 
		rasterizer.depthClampEnable = VK_FALSE;
		// If VK_TRUE, then geometry never passes through the rasterizer stage. Basically disables any output to the framebuffer.
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		// Determines how fragments are generated for geometry. The following modes are available:
		// 1. VK_POLYGON_MODE_FILL: fill the area of the polygon with fragments.
		// 2. VK_POLYGON_MODE_LINE: polygon edges are drawn as lines.
		// 3. VK_POLYGON_MODE_POINT : polygon vertices are drawn as points.
		// Using any mode other than fill requires enabling a GPU feature.
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		// Describes the thickness of lines in terms of number of fragments.
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		// ---------------------------------------------------
		// MSAA.
		// ---------------------------------------------------

		// Disable multisampling for now.
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		// ---------------------------------------------------
		// DEPTH AND STENCIL TESTING.
		// ---------------------------------------------------

		// Do not use depth and stencil testing for now.
		// VkPipelineDepthStencilStateCreateInfo

		// ---------------------------------------------------
		// COLOR BLENDING.
		// ---------------------------------------------------

		// Disable for now.
		// There are two types of structs to configure color blending.
		// VkPipelineColorBlendAttachmentState - contains the configuration per attached framebuffer.
		// VkPipelineColorBlendStateCreateInfo - contains the global color blending settings.

		// Per attached framebuffer.
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		// Final color (after blending) do AND with this mask (to do not write some channels). 
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		// Two variants for blending:
		// 1. finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
		//    finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
		//    If VK_TRUE, then do blending. If VK_FALSE, then just write new color.
		colorBlendAttachment.blendEnable = VK_FALSE;
		//    Factor for new color.
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		//    Factor for old color.
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		// 2. finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
		//    finalColor.a = newAlpha.a;
		// colorBlendAttachment.blendEnable = VK_TRUE;
		// colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		// colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		// colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		// colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		// colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		// colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		// Global blending settings.
		// Combine the old and new value using a bitwise operation.
		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional


		// ---------------------------------------------------
		// DYNAMIC STATE.
		// ---------------------------------------------------

		// A limited amount of the state that we've specified in the previous structs can actually be changed without 
		// recreating the pipeline. Examples are the size of the viewport, line width and blend constants.
		// This will cause the configuration of these values to be ignored and you will be required to specify the data at drawing time.
		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_LINE_WIDTH
		};

		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		// ---------------------------------------------------
		// PIPELINE LAYOUT.
		// ---------------------------------------------------

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 0; // Optional
		pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
		pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
		pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

		if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create pipeline layout");
		}

		// ---------------------------------------------------
		// CREATE GRAPHICS PIPELINE.
		// ---------------------------------------------------

		// Shader stages.
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		// Fixed-function.
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = nullptr; // Optional
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = nullptr; // Optional
		// Pipeline layout.
		pipelineInfo.layout = pipelineLayout;
		// Render pass.
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0; // index
		// Vulkan allows to create a new graphics pipeline by deriving from an existing pipeline. The idea of pipeline 
		// derivatives is that it is less expensive to set up pipelines when they have much functionality in common 
		// with an existing pipeline and switching between pipelines from the same parent can also be done quicker.
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional

		// Designed to take multiple VkGraphicsPipelineCreateInfo objects and create multiple VkPipeline objects in a single call.
		if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create graphics pipeline");
		}
		else {
			PrintMessage("Graphics pipeline created successfully");
		}


		vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
		vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
	}


	VkShaderModule CreateShaderModule(const std::vector<char>& shaderCode)
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = shaderCode.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(logicalDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create shader module");
		}

		return shaderModule;
	}


	void CreateFramebuffers()
	{
		swapchainFramebuffers.resize(swapchainImageViews.size());

		for (size_t i = 0; i < swapchainImageViews.size(); i++) {
			VkImageView attachments[] = {
				swapchainImageViews[i]
			};

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = swapchainExtent.width;
			framebufferInfo.height = swapchainExtent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create framebuffer");
			}
		}
	}


	void CreateCommandPool()
	{
		// Command buffers are executed by submitting them on one of the device queues, like the graphics and 
		// presentation queues we retrieved. Each command pool can only allocate command buffers that are submitted 
		// on a single type of queue. We are going to record commands for drawing => choose graphics queue family.

		QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(physicalDevice);

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
		// 1. VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: hint that command buffers are rerecorded with new commands 
		//    very often (may change memory allocation behavior).
		// 2. VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT: allow command buffers to be rerecorded individually, 
		//    without this flag they all have to be reset together.
		poolInfo.flags = 0; // Optional

		if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
			throw std::runtime_error("failed to create command pool!");
		}
	}


	void CreateCommandBuffers()
	{
		commandBuffers.resize(swapchainFramebuffers.size());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		// Allocated command buffers can be primary or secondary.
		// 1. VK_COMMAND_BUFFER_LEVEL_PRIMARY: can be submitted to a queue for execution, but cannot be called 
		// from other command buffers.
		// 2. VK_COMMAND_BUFFER_LEVEL_SECONDARY: cannot be submitted directly, but can be called from primary command buffers.
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

		if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate command buffers");
		}

		for (size_t i = 0; i < commandBuffers.size(); i++) {
			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0; // Optional
			// Relevant for secondary command buffers. It specifies which state to inherit from the 
			// calling primary command buffers.
			beginInfo.pInheritanceInfo = nullptr; // Optional

			if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
				throw std::runtime_error("Failed to begin recording command buffer");
			}

			VkRenderPassBeginInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = swapchainFramebuffers[i];
			// Size of the render area. The render area defines where shader loads and stores will take place.
			// The pixels outside this region will have undefined values.
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = swapchainExtent;

			// Define the clear values to use for VK_ATTACHMENT_LOAD_OP_CLEAR.
			VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
			renderPassInfo.clearValueCount = 1;
			renderPassInfo.pClearValues = &clearColor;

			// The third parameter controls how the drawing commands within the render pass will be provided.
			// 1. VK_SUBPASS_CONTENTS_INLINE: the render pass commands will be embedded in the primary command buffer 
			//    itself and no secondary command buffers will be executed.
			// 2. VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: the render pass commands will be executed from secondary 
			//    command buffers.
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			// The second parameter specifies if the pipeline object is a graphics or compute pipeline.
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

			// Vertex count, instance count, firstVertex offset, firstInstance offset.
			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);

			// Finished recording the command buffer.
			if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to record command buffer");
			}
		}
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
			PrintMessage("All GLFW extensions are supported by Vulkan");
		}

		if (!CheckVulkanValidationLayerSupport()) {
			throw std::runtime_error("Some validation layers are not available to Vulkan");
		}
		else {
			PrintMessage("All validation layers available to Vulkan");
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


	struct SwapchainSupportDetails
	{
		// Just checking if a swapchain is available on physical device is not sufficient, 
		// because it may not actually be compatible with our window surface.
		// There are basically three kinds of properties we need to check:
		// 1. Basic surface capabilities (min/max number of images in swapchain, min/max width and height of images).
		// 2. Surface formats(pixel format, color space).
		// 3. Available presentation modes.
		VkSurfaceCapabilitiesKHR surfCapabilities;
		std::vector<VkSurfaceFormatKHR> surfFormats;
		std::vector<VkPresentModeKHR> presentationModes;
	};


	SwapchainSupportDetails QuerySwapchainSupportDetails(const VkPhysicalDevice& device)
	{
		SwapchainSupportDetails details;

		// Get surface capabilities.
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.surfCapabilities);

		// Get surface formats.
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

		if (formatCount != 0) {
			details.surfFormats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.surfFormats.data());
		}

		// Get presentation modes.
		uint32_t presentationModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentationModeCount, nullptr);

		if (presentationModeCount != 0) {
			details.presentationModes.resize(presentationModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentationModeCount, details.presentationModes.data());
		}

		return details;
	}


	VkSurfaceFormatKHR SelectSwapchainSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
		// Each VkSurfaceFormatKHR entry contains a format and a colorSpace member.
		// The format member specifies the color channels and types. For example, VK_FORMAT_B8G8R8A8_SRGB.
		// The colorSpace member indicates if the SRGB color space is supported.
		// (also, if true, then VK_COLOR_SPACE_SRGB_NONLINEAR_KHR flag is not using). 

		// Looking for the preferred format.
		for (const auto& availableFormat : availableFormats) {
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return availableFormat;
			}
		}

		// If the preferred one was not found, just return the first one.
		return availableFormats[0];
	}


	VkPresentModeKHR SelectSwapchainPresentationMode(const std::vector<VkPresentModeKHR>& availablePresentationModes) {
		// Presentation mode represents the actual conditions for showing images to the screen. There are four modes in Vulkan:
		// 1. VK_PRESENT_MODE_IMMEDIATE_KHR: images submitted by application are transferred to the screen immediately.
		//    Not wait for vsync. This mode may result in visible screen tearing.
		// 
		// 2. VK_PRESENT_MODE_FIFO_KHR: the swapchain is a queue where the display takes an image from the front of the queue,
		//    when the display is refreshed, and the program inserts rendered images at the back of the queue. 
		//    If the queue is full then the program has to wait. This is most similar to vsync. 
		//    The moment that the display is refreshed is known as "vertical blank".
		// 
		// 3. VK_PRESENT_MODE_FIFO_RELAXED_KHR: this mode only differs from the previous one if the application is late and 
		//    the queue was empty at the last vertical blank. Instead of waiting for the next vertical blank, the image is transferred
		//    immediately when it finally arrives. This may result in visible tearing.
		// 
		// 4. VK_PRESENT_MODE_MAILBOX_KHR: another variation of the second mode. Instead of blocking the application when the 
		//    queue is full, the images that are already queued are simply replaced with the newer ones. This mode can be used to 
		//    render frames as fast as possible while still avoiding tearing, resulting in fewer latency issues than standard vsync. 
		//    This is commonly known as "triple buffering", although the existence of three buffers alone does not necessarily mean that 
		//    the framerate is unlocked.

		// FIFO_KHR is guaranteed to be available.
		// Looking for the preferred mode.
		for (const auto& availablePresentationMode : availablePresentationModes) {
			if (availablePresentationMode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return availablePresentationMode;
			}
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}


	VkExtent2D SelectSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
		// The swap extent is the resolution of the swapchain images.
		if (capabilities.currentExtent.width != UINT32_MAX) {
			// Mean that currentExtent is the current width and height of the window surface.
			return capabilities.currentExtent;
		}
		else {
			int width, height;
			// Get window resolution in pixels. WIDTH and HEIGHT are in screen coords.
			glfwGetFramebufferSize(window, &width, &height);

			VkExtent2D actualExtent{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

			// Clamp values between the allowed minimum and maximum extents that are supported by the device.
			actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

			return actualExtent;
		}
	}


	void CreateSwapchain()
	{
		// isSwapchainValid == true -> support sufficient. But there are several modes of varying optimality.
		// There are three types of settings to determine:
		// 1. Surface format (color depth).
		// 2. Presentation mode (conditions for "swapping" images to the screen).
		// 3. Swap extent (resolution of images in swapchain).
		// For each find an optimal value.
		SwapchainSupportDetails details = QuerySwapchainSupportDetails(physicalDevice);

		VkSurfaceFormatKHR format = SelectSwapchainSurfaceFormat(details.surfFormats);
		VkPresentModeKHR presentationMode = SelectSwapchainPresentationMode(details.presentationModes);
		VkExtent2D extent = SelectSwapchainExtent(details.surfCapabilities);

		// How many images are in swapchain. Required minimum number for device is in minImageCount.
		// Sticking to this minimum means that we may sometimes have to wait on the driver to complete internal operations,
		// before we can acquire another image to render to. So it is recommended to request at least one more image than the minimum.
		uint32_t imageCount = details.surfCapabilities.minImageCount + 1;

		// Ensure that we do not exceed the maximum. If maxImageCount == 0, then there is no maximum.
		if (details.surfCapabilities.maxImageCount > 0 && imageCount > details.surfCapabilities.maxImageCount) {
			imageCount = details.surfCapabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = format.format;
		createInfo.imageColorSpace = format.colorSpace;
		createInfo.imageExtent = extent;
		// In general always = 1.
		createInfo.imageArrayLayers = 1;
		// Specifies kind of operations for which we will use the images in the swapchain.
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		// Specify how to handle swapchain images that will be used across multiple queue families. 
		// That will be the case in our application if the graphics queue family is different from the presentation queue. 
		// We will be drawing on the images in the swapchain from the graphics queue and then submitting them on the presentation queue. 
		// There are two ways to handle images that are accessed from multiple queues:
		// 1. VK_SHARING_MODE_EXCLUSIVE: an image is owned by one queue family at a time and ownership must be explicitly transferred 
		//    before using it in another queue family. This option offers the best performance.
		// 2. VK_SHARING_MODE_CONCURRENT: images can be used across multiple queue families without explicit ownership transfers.
		// If the queue families differ, then use the concurrent mode for simplicity.

		QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);
		uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

		if (indices.graphicsFamily != indices.presentFamily) {
			// Concurrent mode requires you to specify in advance between which queue families ownership will be shared using the 
			// queueFamilyIndexCountand and pQueueFamilyIndices parameters.
			// This mode requires you to specify at least two distinct queue families.
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0; // Optional
			createInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		// We can specify that a certain transform should be applied to images in the swapchain if it is supported
		// (supportedTransforms in capabilities), like a 90 degree clockwise rotation or horizontal flip.
		// If do not want this, specify currentTransform.
		createInfo.preTransform = details.surfCapabilities.currentTransform;

		// Specify if the alpha channel should be used for blending with other windows in the window system.
		// Just ignore alpha channel.
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

		createInfo.presentMode = presentationMode;

		// If = VK_TRUE, then that means that we do not care about the color of pixels that are obscured.
		createInfo.clipped = VK_TRUE;

		// It is possible that swapchain becomes invalid or unoptimized while application is running.
		// Example: window was resized. In that case the swapchain needs to be recreated from scratch and a reference 
		// to the old one must be specified in this field. NULL_HANDLE for now. 
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		if (vkCreateSwapchainKHR(logicalDevice, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create swapchain");
		}
		else {
			PrintMessage("Swapchain created successfully");
		}

		swapchainImageFormat = format.format;
		swapchainExtent = extent;

		// Retrieve handles for images.
		vkGetSwapchainImagesKHR(logicalDevice, swapchain, &imageCount, nullptr);
		swapchainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(logicalDevice, swapchain, &imageCount, swapchainImages.data());
	}


	void CreateImageViews()
	{
		swapchainImageViews.resize(swapchainImages.size());

		for (size_t i = 0; i < swapchainImages.size(); ++i) {
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = swapchainImages[i];
			// 1D, 2D, 3D texture or cubemap.
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = swapchainImageFormat;
			// Example: we can make that the R channel in the shader maps to the B channel in the actual texture.
			// VK_COMPONENT_SWIZZLE_IDENTITY mean that r = r, g = g, etc.
			createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			// Field subresourceRange describes what the image's purpose is and which part of the image should be accessed.
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create image views");
			}
		}
	}


	struct QueueFamilyIndices
	{
		// std::optional is a wrapper that contains no value until assign something to it.
		// To check if there is a value, use the method has_value().
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool IsValid() { return graphicsFamily.has_value() && presentFamily.has_value(); }
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

		VkBool32 isRequiredExtensionsSupported = CheckPhysicalDeviceRequiredExtensionSupport(device);

		VkBool32 isSwapchainValid = false;
		// If swapchain is available at all.
		if (isRequiredExtensionsSupported) {
			SwapchainSupportDetails details = QuerySwapchainSupportDetails(device);
			// It is enough if there is support for at least one format and one presentation mode.
			isSwapchainValid = !details.surfFormats.empty() && !details.presentationModes.empty();
		}

		VkBool32 isSuitable = indices.IsValid() && isRequiredExtensionsSupported && isSwapchainValid;

		if (isSuitable) {
			std::string str = "Physical Device selected: ";
			str += deviceProperties.deviceName;
			PrintMessage(str);
		}

		return isSuitable;
	}


	void CreateLogicalDevice()
	{
		QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);

		// We know queue families that GPU support. Create queue families with one queue in each.
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfoList{};
		std::set<uint32_t> uniqueQueueFamilyIndices{ indices.graphicsFamily.value(), indices.presentFamily.value() };

		float queuePriority = 1.0f;
		for (const auto& queueFamilyIndex : uniqueQueueFamilyIndices) {
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
			queueCreateInfo.queueCount = 1;

			// Vulkan lets you assign priorities to queues to influence the scheduling of command buffer execution 
			// using floating point numbers between 0.0 and 1.0. This is required even if there is only a single queue.
			queueCreateInfo.pQueuePriorities = &queuePriority;

			queueCreateInfoList.push_back(queueCreateInfo);
		}


		// Specify the set of device features that we'll be using.
		// Assign to VK_FALSE by default for a while.
		VkPhysicalDeviceFeatures deviceFeatures{};

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfoList.data();
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfoList.size());
		createInfo.pEnabledFeatures = &deviceFeatures;

		// Now again we need specify info about extensions and validation layers.
		// It is possible that there are Vulkan devices in the system that does not support some extensions.
		// For example VK_KHR_swapchain is a device specific extension.
		// Previous implementations of Vulkan made a distinction between instance and device specific validation layers, 
		// but this is no longer the case. Set them to be compatible with older implementations.
		createInfo.enabledExtensionCount = static_cast<uint32_t>(REQUIRED_PHYSICAL_DEVICE_EXTENSIONS.size());
		createInfo.ppEnabledExtensionNames = REQUIRED_PHYSICAL_DEVICE_EXTENSIONS.data();
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
		vkGetDeviceQueue(logicalDevice, indices.presentFamily.value(), 0, &presentQueue);
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
		// 1. We need to find at least one queue family that supports VK_QUEUE_GRAPHICS_BIT.
		//    VK_QUEUE_GRAPHICS_BIT specifies that queues in this queue family support graphics operations.
		// 2. We need ensure that physical device supports Window System Integration (VK_KHR_surface).
		//    i.e. physical device can present images to the surface we just created.
		// Points 1 and 2 most likely will be the same queue families.
		// 
		int i = 0;
		for (const auto& queueFamily : queueFamilyList) {
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

			if (presentSupport) {
				indices.presentFamily = i;
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


	bool CheckPhysicalDeviceRequiredExtensionSupport(const VkPhysicalDevice& device)
	{
		// Check support of extensions from REQUIRED_PHYSICAL_DEVICE_EXTENSIONS list.
		uint32_t deviceExtensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, nullptr);

		std::vector<VkExtensionProperties> deviceExtensionList(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, deviceExtensionList.data());

		std::set<std::string> requiredExtensionList(REQUIRED_PHYSICAL_DEVICE_EXTENSIONS.begin(), REQUIRED_PHYSICAL_DEVICE_EXTENSIONS.end());

		for (const auto& deviceExtension : deviceExtensionList) {
			requiredExtensionList.erase(deviceExtension.extensionName);
		}

		VkBool32 isSupported = requiredExtensionList.empty();

		if (isSupported) {
			PrintMessage("All required extensions are supported by Physical Device");
		}

		return isSupported;
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


	void CreateSurface()
	{
		// Platform agnostic creation of window surface.
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create window surface");
		}
	}


	// To work with VK_KHR_surface extension. 
	// VkSurfaceKHR and its usage is platform agnostic, but its creation is not and use VK_KHR_win32_surface.
	// Represents an abstract type of surface to present rendered images to. 
	// The surface in program will be backed by the window that already opened with GLFW.
	VkSurfaceKHR surface;

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

	// Store logical device. Application view on actual device.
	VkDevice logicalDevice;

	// Store handle.
	// The queues are automatically created along with the logical device and
	// this handle needs to interface with these queues.
	// Device queues are implicitly cleaned up when the logical device is destroyed.
	VkQueue graphicsQueue;

	// Queue for present image to the window surface.
	VkQueue presentQueue;

	VkSwapchainKHR swapchain;
	VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;

	// Images created for swapchain by device and automatically cleaned up once the swap chain has been destroyed.
	std::vector<VkImage> swapchainImages;

	// Describes how to access the image and which part of the image to access.
	// Example: if it should be treated as a 2D texture depth texture without any mipmapping levels.
	std::vector<VkImageView> swapchainImageViews;

	// The image that we have to use for the attachment depends on which image the swapchain returns when we retrieve 
	// one for presentation. That means that we have to create a framebuffer for all of the images in the swapchain and 
	// use the one that corresponds to the retrieved image at drawing time.
	std::vector<VkFramebuffer> swapchainFramebuffers;

	VkRenderPass renderPass;

	VkPipeline graphicsPipeline;

	// Specify uniform values for shaders.
	VkPipelineLayout pipelineLayout;

	// Command pools manage the memory that is used to store the buffers and command buffers are allocated from them.
	VkCommandPool commandPool;

	// One of the drawing commands involves binding the right VkFramebuffer => we have to record a command buffer 
	// for every image in the swapchain.
	// Command buffers automatically freed when their command pool is destroyed.
	std::vector<VkCommandBuffer> commandBuffers;

	// Image has been acquired and is ready for rendering.
	std::vector<VkSemaphore> imageAvailableSemaphores;

	// Signal that rendering has finished and presentation can happen.
	std::vector<VkSemaphore> renderFinishedSemaphores;

	// Fences for CPU-GPU synchronization.
	std::vector<VkFence> inFlightFences;

	// If MAX_FRAMES_IN_FLIGHT is higher than the number of swapchain images or vkAcquireNextImageKHR returns 
	// images out-of-order, then it's possible that we may start rendering to a swapchain image that is already in flight. 
	// To avoid this, we need to track for each swapchain image if a frame in flight is currently using it.
	std::vector<VkFence> imagesInFlight;

	// Frame index in terms of MAX_FRAMES_IN_FLIGHT.
	size_t currentFrame;
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