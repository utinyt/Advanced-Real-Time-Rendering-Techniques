#include <array>
#include <random>
#include <numeric>
#include <include/imgui/imgui.h>
#include "core/vulkan_app_base.h"
#include "core/vulkan_mesh.h"
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "core/vulkan_imgui.h"
#include "core/vulkan_texture.h"
#include "core/vulkan_pipeline.h"
#include "core/vulkan_framebuffer.h"
#include "core/vulkan_debug.h"

namespace {
	std::random_device device;
	std::mt19937_64 RNGen(device());
	std::uniform_real_distribution<> rdFloat(0.0, 1.0);
	int BUNNY_COUNT_SQRT = 3;
	int LOCAL_LIGHTS_COUNT_SQRT = 26;
	uint32_t SHADOW_MAP_DIM = 2048;
	int SCENE_RADIUS = 25;
	glm::vec3 LIGHT_POS = glm::vec3(30, 36, 30);
}

class Imgui : public ImguiBase {
public:
	virtual void newFrame() override {
		ImGui::NewFrame();
		ImGui::Begin("Setting");
		ImGui::NewLine();
		ImGui::RadioButton("Final Output", &userInput.renderMode, 0);
		if (userInput.renderMode == 0) {
			ImGui::Checkbox("Disable Local Lights", &userInput.disableLocalLight);
		}
		ImGui::NewLine();
		ImGui::Text("Debug View");
		ImGui::RadioButton("Shadow Map", &userInput.renderMode, 1); ImGui::SameLine();
		ImGui::RadioButton("Shadow Index", &userInput.renderMode, 2); ImGui::SameLine();
		ImGui::RadioButton("Pixel Depth", &userInput.renderMode, 3); ImGui::SameLine();
		ImGui::RadioButton("Light Depth", &userInput.renderMode, 4);
		
		ImGui::NewLine();
		ImGui::Text("Shadow Map Filter");
		static int currentKernelWidth = 0;
		ImGui::SliderInt("Kernel Width", &currentKernelWidth, 0, 50);
		if (currentKernelWidth != userInput.kernelWidth) {
			userInput.kernelWidth = currentKernelWidth;
			userInput.shouldUpdateKernel = true;
		}

		ImGui::NewLine();
		ImGui::Text("Alpha = Factor * 10^(-Power)");
		ImGui::SliderInt("Power", &userInput.power, 2, 7);
		ImGui::SliderInt("Factor", &userInput.factor, 1, 9);

		ImGui::End();
		ImGui::Render();
	}

	/* user input collection */
	struct UserInput {
		int renderMode = 0;
		int kernelWidth = -1;
		bool disableLocalLight = false;
		bool shouldUpdateKernel = true;
		int power = 5;
		int factor = 1;
	} userInput;
};

class VulkanApp : public VulkanAppBase {
public:
	/*
	* constructor - get window size & title
	*/
	VulkanApp(int width, int height, const std::string& appName)
		: VulkanAppBase(width, height, appName) {
		imguiBase = new Imgui;
		MAX_FRAMES_IN_FLIGHT = 2;
		buildCommandBufferEveryFrame = true;
		
	}

	/*
	* destructor - destroy vulkan objects created in this level
	*/
	~VulkanApp() {
		if (devices.device == VK_NULL_HANDLE) {
			return;
		}
		//imgui
		imguiBase->cleanup();
		delete imguiBase;
		
		//render targets
		for (auto& framebuffer : geometryFramebuffers) {
			framebuffer.cleanup();
		}
		for (auto& framebuffer : shadowMaps) {
			framebuffer.cleanup();
		}
		for (auto& imageView : shadowMapBlurImageViews) {
			vkDestroyImageView(devices.device, imageView, nullptr);
		}
		for (auto& image : shadowMapBlurImages) {
			devices.memoryAllocator.freeImageMemory(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			vkDestroyImage(devices.device, image, nullptr);
		}

		//models
		devices.memoryAllocator.freeBufferMemory(bunnyBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, bunnyBuffer, nullptr);
		devices.memoryAllocator.freeBufferMemory(floorBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, floorBuffer, nullptr);
		devices.memoryAllocator.freeBufferMemory(sphereBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, sphereBuffer, nullptr);

		//descriptor sets
		vkDestroyDescriptorPool(devices.device, descPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, descLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, gbufferDescPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, gbufferDescLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, computeDescHorizontalPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, computeDescHorizontalSetLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, computeDescVerticalPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, computeDescVerticalSetLayout, nullptr);

		//uniform buffers
		for (size_t i = 0; i < uniformBuffers.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(uniformBuffers[i], 
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, uniformBuffers[i], nullptr);
			devices.memoryAllocator.freeBufferMemory(blurKernelBuffers[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, blurKernelBuffers[i], nullptr);
		}

		//pipelines
		vkDestroyPipeline(devices.device, geometryPassPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, geometryPassPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, lightingPassPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, lightingPassPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, localLightPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, localLightPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, shadowMapPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, shadowMapPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, computeHorizontalPipeline, nullptr);
		vkDestroyPipeline(devices.device, computeVerticalPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, computePipelineLayout, nullptr);

		//renderpass
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
		vkDestroyRenderPass(devices.device, geometryRenderPass, nullptr);
		vkDestroyRenderPass(devices.device, shadowMapRenderPass, nullptr);
		//sampler
		vkDestroySampler(devices.device, sampler, nullptr);

		//swapchain framebuffers
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		VulkanAppBase::initApp();
		camera.camPos = glm::vec3(-8, 10, 20);
		camera.camFront = glm::normalize(glm::vec3(8, -8, -20));
		camera.camUp = glm::vec3(0.f, 1.f, 0.f);

		separatedComputeQueue = devices.indices.computeFamily.value() != devices.indices.graphicsFamily.value();

		//models
		bunny.load("../../meshes/bunny.obj");
		bunnyBuffer = bunny.createModelBuffer(&devices);
		floor.load("../../meshes/cube.obj");
		floorBuffer = floor.createModelBuffer(&devices);
		createSphereModel();
		sphereBuffer = sphere.createModelBuffer(&devices);
		
		/*
		* local lights
		*/
		float range = static_cast<float>(LOCAL_LIGHTS_COUNT_SQRT / 2);
		for (float z = -range; z < range; ++z) {
			for (float x = -range; x < range; ++x) {
				float radius = static_cast<float>(rdFloat(RNGen) * 7.5f + 1.f);
				glm::vec3 lightPos = glm::vec3(x * 0.5f, 5.5f + (rdFloat(RNGen) - 0.5), z * 0.5f);
				glm::vec3 lightColor = glm::vec3(rdFloat(RNGen) * 5, rdFloat(RNGen) * 5, rdFloat(RNGen) * 5);

				localLightInfo.push_back({
					glm::translate(glm::mat4(1.f), lightPos) * glm::scale(glm::mat4(1.f), glm::vec3(radius)),
					lightColor * 2.f,
					radius,
					glm::vec4(lightPos, 1)
				});
			}
		}

		/*
		* model transforms
		*/
		range = static_cast<float>(BUNNY_COUNT_SQRT / 2);
		for (float z = -range; z < range + 1; ++z) {
			for (float x = -range; x < range + 1; ++x) {
				objPushConstants.push_back({
					glm::translate(glm::mat4(1.f), glm::vec3(x * 4.5f, 0.5f, z * 4.5f)) * glm::scale(glm::mat4(1.f), glm::vec3(3, 3, 3)), //model transform
					glm::vec3(0.5f, 0.5f, 0.5f), //color
					0.1f,
					glm::vec3(0.02f, 0.02f, 0.02f), //water
					0.1f
				});
			}
		}
		//floor
		objPushConstants.push_back({
			glm::scale(glm::mat4(1.f), glm::vec3(30, 1.f, 30)),
			glm::vec3(0.5f, 0.5f, 0.5f), //grey
			1.f,
			glm::vec3(0.02f, 0.02f, 0.02f), //water
			1.f
		});

		/*
		* global light
		*/
		lightView = glm::lookAt(LIGHT_POS, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
		lightProj = glm::perspective(glm::radians(45.f), 1.f, 0.1f, 1000.f);
		lightProj[1][1] *= -1;
		shadowMapPushConstant.lightProjView = lightProj * lightView;
		lightingPassPushConstant.lightPos = glm::vec4(LIGHT_POS, 1.f);
		blurComputePushConstant.shadowDim = SHADOW_MAP_DIM;

		//create shadow map render targets
		createShadowMapFramebuffer();
		createShadowMapBlurImages();

		//geometry pass render targets
		createGeometryPassFramebuffer();
		
		//sampler
		VkSamplerCreateInfo samplerInfo = vktools::initializers::samplerCreateInfo(devices.availableFeatures, devices.properties);
		VK_CHECK_RESULT(vkCreateSampler(devices.device, &samplerInfo, nullptr, &sampler));

		//uniform buffers
		createUniformBuffers();
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			updateUniformBuffer(i);
		}

		//descriptor sets
		createDescriptorSet();
		updateDescriptorSets();

		createRenderPass();
		createFramebuffers();
		createPipeline();

		//command buffers
		imguiBase->init(&devices, swapchain.extent.width, swapchain.extent.height, renderPass, MAX_FRAMES_IN_FLIGHT, sampleCount);
	}

private:
	/** swapchain framebuffers */
	std::vector<VkFramebuffer> framebuffers;
	/** render pass for swapchain image */
	VkRenderPass renderPass = VK_NULL_HANDLE;
	/** pipelines */
	VkPipeline lightingPassPipeline = VK_NULL_HANDLE;
	VkPipelineLayout lightingPassPipelineLayout = VK_NULL_HANDLE;
	/** uniform buffers */
	std::vector<VkBuffer> uniformBuffers;
	std::vector<MemoryAllocator::HostVisibleMemory> uniformBufferMemories;
	/** descriptor sets */
	DescriptorSetBindings descBindings;
	VkDescriptorPool descPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> descSets;
	/** models */
	Mesh bunny, floor, sphere;
	/** model buffers */
	VkBuffer bunnyBuffer = VK_NULL_HANDLE, floorBuffer = VK_NULL_HANDLE, sphereBuffer = VK_NULL_HANDLE;
	/** sampler */
	VkSampler sampler = VK_NULL_HANDLE;
	/** lighting pass push constant */
	struct LightingPassPushConstant {
		glm::mat4 shadowMatrix;
		glm::vec4 lightPos;
		glm::vec3 camPos;
		int renderMode = 0;
		float depthMin = 0;
		float depthMax = 0;
		float alpha = 0.01f;
	}lightingPassPushConstant;

	/*
	* geometry pass resources
	*/
	/** model matrix */
	struct ObjPushConstant {
		glm::mat4 model;
		glm::vec3 diffuse;
		float roughness = 0.f;
		glm::vec3 specular;
		float metallic = 0.f;
	};
	std::vector<ObjPushConstant> objPushConstants;
	/** framebuffers for geometry pass */
	std::vector<Framebuffer> geometryFramebuffers;
	/** renderpass for geometry pass */
	VkRenderPass geometryRenderPass = VK_NULL_HANDLE;
	/** descriptor sets */
	DescriptorSetBindings gbufferDescBindings;
	VkDescriptorPool gbufferDescPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout gbufferDescLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> gbufferDescSets;
	/** pipelines */
	VkPipeline geometryPassPipeline = VK_NULL_HANDLE;
	VkPipelineLayout geometryPassPipelineLayout = VK_NULL_HANDLE;

	/*
	* local lights pass resources
	*/
	VkPipeline localLightPipeline = VK_NULL_HANDLE;
	VkPipelineLayout localLightPipelineLayout = VK_NULL_HANDLE;
	struct LocalLightInfo {
		glm::mat4 model;
		glm::vec3 color;
		float radius = 0.f;
		glm::vec4 lightCenter{ 0, 0, 0, 0 };
	};
	std::vector<LocalLightInfo> localLightInfo;
	struct LocalLightInfoPushConstant {
		LocalLightInfo lightInfo;
		glm::vec3 camPos;
		int renderMode = 0;
		bool disable = false;
	} localLightPushConstant;

	/*
	* shadow map resources
	*/
	/** light matrices */
	glm::mat4 lightView{};
	glm::mat4 lightProj{};
	/** shadow map images (framebuffer) */
	std::vector<Framebuffer> shadowMaps;
	/** pipeline */
	VkPipeline shadowMapPipeline = VK_NULL_HANDLE;
	VkPipelineLayout shadowMapPipelineLayout = VK_NULL_HANDLE;
	/** render pass */
	VkRenderPass shadowMapRenderPass = VK_NULL_HANDLE;
	/** push constant */
	struct ShadowMapPushConstant {
		glm::mat4 model;
		glm::mat4 lightProjView;
		float depthMin = 0;
		float depthMax = 0;
	}shadowMapPushConstant;

	/*
	* compute resources
	*/
	/** pipeline */
	VkPipeline computeHorizontalPipeline = VK_NULL_HANDLE;
	VkPipeline computeVerticalPipeline = VK_NULL_HANDLE;
	VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
	/** render targets for blur */
	std::vector<VkImage> shadowMapBlurImages{};
	std::vector<VkImageView> shadowMapBlurImageViews{};
	/** descriptor */
	DescriptorSetBindings computeDescHorizontalBindings;
	VkDescriptorPool computeDescHorizontalPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout computeDescHorizontalSetLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> computeDescHorizontalSets{};
	DescriptorSetBindings computeDescVerticalBindings;
	VkDescriptorPool computeDescVerticalPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout computeDescVerticalSetLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> computeDescVerticalSets{};
	/** blur kernel uniform buffers */
	std::vector<VkBuffer> blurKernelBuffers;
	std::vector<MemoryAllocator::HostVisibleMemory> blurKernelMemories;
	std::array<glm::vec4, 26> blurKernel{};
	/** separated compute queue indicator */
	bool separatedComputeQueue = false;
	/** push constant */
	struct BlurComputePushConstant {
		int shadowDim = 1024;
		int kernelWidth = 5;
	} blurComputePushConstant;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		size_t commandBufferIndex = imageIndex;
		submitInfo.pCommandBuffers = &commandBuffers[commandBufferIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderCompleteSemaphores[currentFrame];
		VK_CHECK_RESULT(vkQueueSubmit(devices.graphicsQueue, 1, &submitInfo, frameLimitFences[currentFrame]));

		submitFrame(imageIndex);
	}

	/*
	* called every frame - udpate glfw & imgui
	*/
	void update() override {
		VulkanAppBase::update();
		updateBlurKernel();
		updateUniformBuffer(currentFrame);
		buildCommandBuffer();
	}

	/*
	* override resize function - update offscreen resources
	*/
	void resizeWindow(bool /*recordCommandBuffer*/) override {
		VulkanAppBase::resizeWindow(false);
		createGeometryPassFramebuffer(false);
		createShadowMapFramebuffer(false);
		updateDescriptorSets();
	}

	/*
	* update blur kernel
	*/
	void updateBlurKernel() {
		Imgui* imgui = static_cast<Imgui*>(imguiBase);
		if (imgui->userInput.shouldUpdateKernel) {
			imgui->userInput.shouldUpdateKernel = false;

			const int width = imgui->userInput.kernelWidth;
			float s = width / 2.f;
			if (width == 0) {
				s = 1;
			}

			float sum = 0;
			for (int i = -width; i <= width; ++i) {
				int vecIndex = (i + width) / 4;
				int elementIndex = (i + width) % 4;
				blurKernel[vecIndex][elementIndex] = static_cast<float>(exp(-0.5 * i * i / s / s));
				sum += blurKernel[vecIndex][elementIndex];
			}

			for (auto& vec : blurKernel) {
				vec /= sum;
			}
		}

	}

	/*
	* create images fpr shadow map blur output (or input for second pass)
	*/
	void createShadowMapBlurImages() {
		//clear
		for (auto& imageView : shadowMapBlurImageViews) {
			vkDestroyImageView(devices.device, imageView, nullptr);
		}
		for (auto& image : shadowMapBlurImages) {
			vkDestroyImage(devices.device, image, nullptr);
		}
		shadowMapBlurImageViews.resize(MAX_FRAMES_IN_FLIGHT);
		shadowMapBlurImages.resize(MAX_FRAMES_IN_FLIGHT);

		//create images & image views
		//image transition from undefined -> general
		VkCommandBuffer cmdBuf = devices.beginCommandBuffer();
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			devices.createImage(shadowMapBlurImages[i],
				{ SHADOW_MAP_DIM, SHADOW_MAP_DIM, 1 },
				VK_FORMAT_R32G32B32A32_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
				1,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			shadowMapBlurImageViews[i] = vktools::createImageView(devices.device,
				shadowMapBlurImages[i],
				VK_IMAGE_VIEW_TYPE_2D,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				VK_IMAGE_ASPECT_COLOR_BIT,
				1
			);
			vktools::setImageLayout(cmdBuf,
				shadowMapBlurImages[i],
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_GENERAL,
				{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
			);
		}
		devices.endCommandBuffer(cmdBuf);
	}

	/*
	* create shadow map framebuffer
	*/
	void createShadowMapFramebuffer(bool createRenderPass = true) {
		/*
		* cleanup
		*/
		for (auto& framebuffer : shadowMaps) {
			framebuffer.cleanup();
		}
		shadowMaps.resize(MAX_FRAMES_IN_FLIGHT);

		/*
		* add attachments
		*/
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			shadowMaps[i].init(&devices);
			VkImageCreateInfo imageInfo = vktools::initializers::imageCreateInfo({ SHADOW_MAP_DIM, SHADOW_MAP_DIM, 1 },
				VK_FORMAT_R32G32B32A32_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
				1);

			//depth (color)
			shadowMaps[i].addAttachment(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_GENERAL, true);
			//depth
			imageInfo.format = depthFormat;
			imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			shadowMaps[i].addAttachment(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //depth
		}

		/*
		* render pass
		*/
		if (createRenderPass) {
			std::vector<VkSubpassDependency> dependencies{};
			dependencies.resize(2);

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstStageMask =
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependencies[0].dstAccessMask =
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask =
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependencies[1].srcAccessMask =
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			shadowMapRenderPass = shadowMaps[0].createRenderPass(dependencies);
		}

		/*
		* framebuffers
		*/
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			shadowMaps[i].createFramebuffer({ SHADOW_MAP_DIM , SHADOW_MAP_DIM }, shadowMapRenderPass);
		}
	}

	/*
	* create framebuffer for geometry pass
	*/
	void createGeometryPassFramebuffer(bool createRenderPass = true) {
		/*
		* cleanup
		*/
		for (auto& framebuffer : geometryFramebuffers) {
			framebuffer.cleanup();
		}
		geometryFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);

		/*
		* add attachments
		*/
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			geometryFramebuffers[i].init(&devices);

			VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			VkImageCreateInfo imageInfo = vktools::initializers::imageCreateInfo(
				{ swapchain.extent.width, swapchain.extent.height, 1 },
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				1
			);

			geometryFramebuffers[i].addAttachment(imageInfo, properties); //position
			geometryFramebuffers[i].addAttachment(imageInfo, properties); //normal
			geometryFramebuffers[i].addAttachment(imageInfo, properties); //diffuse
			geometryFramebuffers[i].addAttachment(imageInfo, properties); //specular
			imageInfo.format	= depthFormat;
			imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			geometryFramebuffers[i].addAttachment(imageInfo, properties); //depth
		}

		/*
		* renderpass
		*/
		if (createRenderPass) {
			std::vector<VkSubpassDependency> dependencies{};
			dependencies.resize(2);

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstStageMask = 
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependencies[0].dstAccessMask = 
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = 
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependencies[1].srcAccessMask = 
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			geometryRenderPass = geometryFramebuffers[0].createRenderPass(dependencies);
		}

		/*
		* framebuffer
		*/
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			geometryFramebuffers[i].createFramebuffer(swapchain.extent, geometryRenderPass);
		}
	}

	/*
	* create procedural generated sphere
	*/
	void createSphereModel() {
		const unsigned int division = 32;
		const float PI = 3.141592f;
		size_t vertexCount = (division + 1) * (division + 1);
		std::vector<glm::vec3> positions;

		//vertex data
		for (unsigned int y = 0; y <= division; ++y) {
			for (unsigned int x = 0; x <= division; ++x) {
				float theta = (float)x / (float)division;
				float phi = (float)y / (float)division;
				float xPos = std::cos(theta * 2.f * PI) * std::sin(phi * PI);
				float yPos = std::cos(phi * PI);
				float zPos = std::sin(theta * 2.f * PI) * std::sin(phi * PI);

				glm::vec3 pos = glm::vec3(xPos, yPos, zPos);
				positions.push_back(pos);
			}
		}

		//indices
		std::vector<uint32_t> indices;
		int topLeft, botLeft;
		for (int i = 0; i < division; ++i) {
			topLeft = i * (division + 1);
			botLeft = topLeft + division + 1;

			for (int j = 0; j < division; ++j) {
				if (i != 0) {
					indices.push_back(topLeft);
					indices.push_back(botLeft);
					indices.push_back(topLeft + 1);
				}
				if (i != (division - 1)) {
					indices.push_back(topLeft + 1);
					indices.push_back(botLeft);
					indices.push_back(botLeft + 1);
				}
				topLeft++;
				botLeft++;
			}
		}

		sphere.load(positions, {}, {}, indices, static_cast<uint32_t>(vertexCount), false, false);
	}

	/*
	* create renderpass for swapchain images
	*/
	void createRenderPass() {
		vkDestroyRenderPass(devices.device, renderPass, nullptr);

		VkAttachmentDescription swapchainImageDesc{};
		swapchainImageDesc.format = swapchain.surfaceFormat.format;
		swapchainImageDesc.samples = sampleCount;
		swapchainImageDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		swapchainImageDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		swapchainImageDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		swapchainImageDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		swapchainImageDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		swapchainImageDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		
		VkAttachmentReference swapchainImageDescRef{};
		swapchainImageDescRef.attachment = 0;
		swapchainImageDescRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.inputAttachmentCount = 0;
		subpass.pInputAttachments = nullptr;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &swapchainImageDescRef;
		subpass.pResolveAttachments = nullptr;
		subpass.pDepthStencilAttachment = nullptr;
		subpass.preserveAttachmentCount = 0;
		subpass.pPreserveAttachments = nullptr;

		std::array<VkSubpassDependency, 2> dependencies{};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = 
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = 
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		info.attachmentCount = 1; // 1 color (swapchain) attachment;
		info.pAttachments = &swapchainImageDesc;
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
		info.dependencyCount = static_cast<uint32_t>(dependencies.size());
		info.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(devices.device, &info, nullptr, &renderPass));
		LOG("created:\tswapchain render pass")
	}

	/*
	* create graphics pipeline
	*/
	void createPipeline() {
		PipelineGenerator gen(devices.device);
		/*
		* geometry pass
		*/
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/geometry_vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/geometry_frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
		gen.addPushConstantRange({ VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjPushConstant)} });
		gen.addDescriptorSetLayout({ descLayout });
		gen.addVertexInputBindingDescription({ bunny.getBindingDescription() });
		gen.addVertexInputAttributeDescription(bunny.getAttributeDescriptions());
		//gen.setDepthStencilInfo();
		gen.setColorBlendInfo(VK_FALSE, static_cast<uint32_t>(geometryFramebuffers[0].attachments.size() - 1));
		gen.generate(geometryRenderPass, &geometryPassPipeline, &geometryPassPipelineLayout);
		gen.resetAll();

		/*
		* lighting pass
		*/
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/lighting_pass_frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
		gen.addPushConstantRange({ VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LightingPassPushConstant)} });
		gen.addDescriptorSetLayout({ gbufferDescLayout });
		gen.setDepthStencilInfo(VK_FALSE, VK_FALSE);
		gen.setColorBlendInfo(VK_FALSE);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		gen.generate(renderPass, &lightingPassPipeline, &lightingPassPipelineLayout);
		gen.resetAll();

		/*
		* local lights pass
		*/
		gen.addVertexInputBindingDescription({ sphere.getBindingDescription() });
		gen.addVertexInputAttributeDescription(sphere.getAttributeDescriptions());
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/local_lights_vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/local_lights_frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
		gen.addPushConstantRange({ VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LocalLightInfoPushConstant)} });
		gen.addDescriptorSetLayout({ descLayout, gbufferDescLayout });
		gen.setDepthStencilInfo(VK_FALSE, VK_FALSE);
		gen.setColorBlendInfo(VK_TRUE);
		
		VkPipelineColorBlendAttachmentState blendState;
		blendState.blendEnable = VK_TRUE;
		blendState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendState.colorBlendOp = VK_BLEND_OP_ADD;
		blendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendState.colorWriteMask = 0xF;
		gen.setColorBlendAttachmentState(blendState);

		//gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		gen.generate(renderPass, &localLightPipeline, &localLightPipelineLayout);
		gen.resetAll();

		/*
		* shadow map pass
		*/
		gen.addVertexInputBindingDescription({ bunny.getBindingDescription() });
		gen.addVertexInputAttributeDescription(bunny.getAttributeDescriptions());
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/shadow_map_vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/shadow_map_frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
		gen.addPushConstantRange({ { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT , 0, sizeof(ShadowMapPushConstant) } });
		gen.setDepthStencilInfo();
		//gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);
		gen.generate(shadowMapRenderPass, &shadowMapPipeline, &shadowMapPipelineLayout);

		/*
		* compute pipeline
		*/
		std::vector<VkDescriptorSetLayout> layouts{ computeDescHorizontalSetLayout };
		std::vector<VkPushConstantRange> ranges{ {VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(BlurComputePushConstant))} };
		VkPipelineLayoutCreateInfo computePipelineCreateInfo = vktools::initializers::pipelineLayoutCreateInfo(layouts, ranges);
		VK_CHECK_RESULT(vkCreatePipelineLayout(devices.device, &computePipelineCreateInfo, nullptr, &computePipelineLayout));

		VkComputePipelineCreateInfo computePipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		computePipelineInfo.layout = computePipelineLayout;
		VkShaderModule blurHorizontalComputeModule = vktools::createShaderModule(devices.device, vktools::readFile("shaders/shadow_map_horizontal_blur_comp.spv"));
		VkShaderModule blurVerticalComputeModule = vktools::createShaderModule(devices.device, vktools::readFile("shaders/shadow_map_vertical_blur_comp.spv"));
		computePipelineInfo.stage = vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, blurHorizontalComputeModule);
		VK_CHECK_RESULT(vkCreateComputePipelines(devices.device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computeHorizontalPipeline));
		computePipelineInfo.stage = vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, blurVerticalComputeModule);
		VK_CHECK_RESULT(vkCreateComputePipelines(devices.device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computeVerticalPipeline));

		vkDestroyShaderModule(devices.device, blurHorizontalComputeModule, nullptr);
		vkDestroyShaderModule(devices.device, blurVerticalComputeModule, nullptr);
		LOG("created:\tpipelines");
	}

	/*
	* create swapchain framebuffer
	*/
	virtual void createFramebuffers() override {
		//delete existing framebuffers
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}
		framebuffers.resize(swapchain.imageCount);

		//create info
		VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		info.renderPass = renderPass;
		info.attachmentCount = 1; // 1 color, no depth, no multisample images
		info.width = swapchain.extent.width;
		info.height = swapchain.extent.height;
		info.layers = 1;

		//create framebuffers per swapchain images
		for (uint32_t i = 0; i < swapchain.imageCount; ++i) {
			info.pAttachments = &swapchain.imageViews[i];
			VK_CHECK_RESULT(vkCreateFramebuffer(devices.device, &info, nullptr, &framebuffers[i]));
		}
		LOG("created:\tframebuffers");
	}

	/*
	* record drawing commands to command buffers
	*/
	virtual void recordCommandBuffer() override { }

	/*
	* record command buffer every frame
	*/
	void buildCommandBuffer() {
		VkCommandBufferBeginInfo cmdBufInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		std::array<VkClearValue, 5> clearColor{};
		clearColor[0].color = { 0.f, 0.f, 0.f, 1.f };
		clearColor[1].color = { 0.f, 0.f, 0.f, 0.f };
		clearColor[2].color = { 0.f, 0.f, 0.f, 0.f };
		clearColor[3].color = { 0.f, 0.f, 0.f, 0.f };
		clearColor[4].depthStencil = {1.f, 0};

		VkRenderPassBeginInfo geometryRenderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		geometryRenderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearColor.size());
		geometryRenderPassBeginInfo.pClearValues = clearColor.data();
		geometryRenderPassBeginInfo.renderPass = geometryRenderPass;
		geometryRenderPassBeginInfo.renderArea = { {0, 0}, swapchain.extent };
		VkDeviceSize offsets = 0;

		std::array<VkClearValue, 2> shadowMapClearColor{};
		shadowMapClearColor[0].color = { 0.f, 0.f, 0.f, 1.f };
		shadowMapClearColor[1].depthStencil = { 1.f, 0 };

		VkRenderPassBeginInfo shadowMapRenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		shadowMapRenderPassInfo.clearValueCount = static_cast<uint32_t>(shadowMapClearColor.size());
		shadowMapRenderPassInfo.pClearValues = shadowMapClearColor.data();
		shadowMapRenderPassInfo.renderPass = shadowMapRenderPass;
		shadowMapRenderPassInfo.renderArea = { {0, 0}, {SHADOW_MAP_DIM, SHADOW_MAP_DIM} };
		
		VkRenderPassBeginInfo lightingPassRenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		lightingPassRenderPassInfo.clearValueCount = 1;
		lightingPassRenderPassInfo.pClearValues = &clearColor[0];
		lightingPassRenderPassInfo.renderPass = renderPass;
		lightingPassRenderPassInfo.renderArea = { {0, 0}, swapchain.extent };

		Imgui* imgui = static_cast<Imgui*>(imguiBase);

		//relative depth range update
		float lightDistance = glm::length(LIGHT_POS);
		shadowMapPushConstant.depthMin = lightDistance - SCENE_RADIUS;
		shadowMapPushConstant.depthMax = lightDistance + SCENE_RADIUS;
		lightingPassPushConstant.depthMin = shadowMapPushConstant.depthMin;
		lightingPassPushConstant.depthMax = shadowMapPushConstant.depthMax;
		lightingPassPushConstant.alpha = (float)imgui->userInput.factor * pow(10, -imgui->userInput.power);

		for (size_t i = 0; i < framebuffers.size(); ++i) {
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[i], &cmdBufInfo));
			
			/*
			* geometry (gbuffer) pass
			*/
			vkdebug::marker::beginLabel(commandBuffers[i], "Geometry Pass");
			geometryRenderPassBeginInfo.framebuffer = geometryFramebuffers[currentFrame].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &geometryRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPassPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, 
				geometryPassPipelineLayout, 0, 1, &descSets[currentFrame], 0, nullptr);
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &bunnyBuffer, &offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], bunnyBuffer, bunny.vertices.bufferSize, VK_INDEX_TYPE_UINT32);

			//bunny
			for (int instance = 0; instance < BUNNY_COUNT_SQRT* BUNNY_COUNT_SQRT; ++instance) {
				vkCmdPushConstants(commandBuffers[i], geometryPassPipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjPushConstant), &objPushConstants[instance]);
				vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(bunny.indices.size()), 1, 0, 0, 0);
			}

			//floor
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &floorBuffer, &offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], floorBuffer, floor.vertices.bufferSize, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(commandBuffers[i], geometryPassPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjPushConstant), &objPushConstants[BUNNY_COUNT_SQRT*BUNNY_COUNT_SQRT]);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(floor.indices.size()), 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);
			vkdebug::marker::endLabel(commandBuffers[i]);

			/*
			* shadow maps
			*/
			vkdebug::marker::beginLabel(commandBuffers[i], "Shadow Map");
			shadowMapRenderPassInfo.framebuffer = shadowMaps[currentFrame].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &shadowMapRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], {SHADOW_MAP_DIM, SHADOW_MAP_DIM });
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMapPipeline);
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &bunnyBuffer, &offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], bunnyBuffer, bunny.vertices.bufferSize, VK_INDEX_TYPE_UINT32);
			//bunny
			for (int instance = 0; instance < BUNNY_COUNT_SQRT * BUNNY_COUNT_SQRT; ++instance) {
				shadowMapPushConstant.model = objPushConstants[instance].model;
				vkCmdPushConstants(commandBuffers[i], shadowMapPipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShadowMapPushConstant), &shadowMapPushConstant);
				vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(bunny.indices.size()), 1, 0, 0, 0);
			}
			//floor
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &floorBuffer, &offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], floorBuffer, floor.vertices.bufferSize, VK_INDEX_TYPE_UINT32);
			shadowMapPushConstant.model = objPushConstants[BUNNY_COUNT_SQRT * BUNNY_COUNT_SQRT].model;
			vkCmdPushConstants(commandBuffers[i], shadowMapPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShadowMapPushConstant), &shadowMapPushConstant);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(floor.indices.size()), 1, 0, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);
			vkdebug::marker::endLabel(commandBuffers[i]);

			if (separatedComputeQueue == false) {
				vkdebug::marker::beginLabel(commandBuffers[i], "Horizontal Blur Shadow Map");
				vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computeHorizontalPipeline);
				vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout,
					0, 1, &computeDescHorizontalSets[currentFrame], 0, 0);
				blurComputePushConstant.kernelWidth = imgui->userInput.kernelWidth;
				vkCmdPushConstants(commandBuffers[i], computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BlurComputePushConstant), &blurComputePushConstant);
				vkCmdDispatch(commandBuffers[i], SHADOW_MAP_DIM / 128, SHADOW_MAP_DIM, 1); //local_group_x = 128
				vkdebug::marker::endLabel(commandBuffers[i]);

				VkImageMemoryBarrier imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
				imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				imageBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageBarrier.image = shadowMapBlurImages[currentFrame];
				vkCmdPipelineBarrier(commandBuffers[i],
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0,
					0, nullptr,
					0, nullptr,
					1, &imageBarrier
				);

				vkdebug::marker::beginLabel(commandBuffers[i], "Vertical Blur Shadow Map");
				vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computeVerticalPipeline);
				vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout,
					0, 1, &computeDescVerticalSets[currentFrame], 0, 0);
				vkCmdPushConstants(commandBuffers[i], computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BlurComputePushConstant), &blurComputePushConstant);
				vkCmdDispatch(commandBuffers[i], SHADOW_MAP_DIM / 128, SHADOW_MAP_DIM, 1); //local_group_x = 128
				vkdebug::marker::endLabel(commandBuffers[i]);

				imageBarrier.image = shadowMaps[currentFrame].attachments[0].image;
				vkCmdPipelineBarrier(commandBuffers[i],
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0,
					0, nullptr,
					0, nullptr,
					1, &imageBarrier
				);
			}

			/*
			* lighting pass
			*/
			vkdebug::marker::beginLabel(commandBuffers[i], "Lighting Pass");
			lightingPassRenderPassInfo.framebuffer = framebuffers[i];
			vkCmdBeginRenderPass(commandBuffers[i], &lightingPassRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPassPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
				lightingPassPipelineLayout, 0, 1, &gbufferDescSets[currentFrame], 0, nullptr);
			lightingPassPushConstant.shadowMatrix =
				glm::translate(glm::mat4(1.f), glm::vec3(0.5f, 0.5f, 0.5f)) *
				glm::scale(glm::mat4(1.f), glm::vec3(0.5f, 0.5f, 0.5f)) *
				shadowMapPushConstant.lightProjView;
			lightingPassPushConstant.camPos = camera.camPos;
			lightingPassPushConstant.renderMode = imgui->userInput.renderMode;
			vkCmdPushConstants(commandBuffers[i], lightingPassPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				sizeof(LightingPassPushConstant), &lightingPassPushConstant);
			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			//vkCmdEndRenderPass(commandBuffers[i]);
			vkdebug::marker::endLabel(commandBuffers[i]);

			/*
			* small local lights
			*/
			if (imgui->userInput.disableLocalLight == false) {
				vkdebug::marker::beginLabel(commandBuffers[i], "Local Lights");
				//vkCmdBeginRenderPass(commandBuffers[i], &lightingPassRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
				vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);
				vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, localLightPipeline);
				std::array<VkDescriptorSet, 2> localLightDescs{ descSets[currentFrame] , gbufferDescSets[currentFrame] };
				vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
					localLightPipelineLayout, 0, static_cast<uint32_t>(localLightDescs.size()), localLightDescs.data(), 0, nullptr);
				vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &sphereBuffer, &offsets);
				vkCmdBindIndexBuffer(commandBuffers[i], sphereBuffer, sphere.vertices.bufferSize, VK_INDEX_TYPE_UINT32);
				for (size_t lightIndex = 0; lightIndex < localLightInfo.size(); ++lightIndex) {
					localLightPushConstant.lightInfo = localLightInfo[lightIndex];
					localLightPushConstant.camPos = camera.camPos;
					localLightPushConstant.renderMode = imgui->userInput.renderMode;
					localLightPushConstant.disable = imgui->userInput.disableLocalLight;
					vkCmdPushConstants(commandBuffers[i], localLightPipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						0, sizeof(localLightPushConstant), &localLightPushConstant);
					vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(sphere.indices.size()), 1, 0, 0, 0);
				}
				vkdebug::marker::endLabel(commandBuffers[i]);
			}

			/*
			* imgui
			*/
			vkdebug::marker::beginLabel(commandBuffers[i], "Imgui");
			imguiBase->drawFrame(commandBuffers[i], currentFrame);
			vkdebug::marker::endLabel(commandBuffers[i]);

			vkCmdEndRenderPass(commandBuffers[i]);
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers[i]));
		}
	}

	/*
	* create MAX_FRAMES_IN_FLIGHT of ubos
	*/
	void createUniformBuffers() {
		VkMemoryPropertyFlags bufferFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		//delete existing uniform buffers
		for (size_t i = 0; i < uniformBuffers.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(uniformBuffers[i], bufferFlags);
			vkDestroyBuffer(devices.device, uniformBuffers[i], nullptr);
			devices.memoryAllocator.freeBufferMemory(blurKernelBuffers[i], bufferFlags);
			vkDestroyBuffer(devices.device, blurKernelBuffers[i], nullptr);
		}
		uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		uniformBufferMemories.resize(MAX_FRAMES_IN_FLIGHT);
		blurKernelBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		blurKernelMemories.resize(MAX_FRAMES_IN_FLIGHT);

		VkBufferCreateInfo camUniformBufferInfo = vktools::initializers::bufferCreateInfo(sizeof(CameraMatrices),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VkBufferCreateInfo lightUniformBufferInfo = vktools::initializers::bufferCreateInfo(
			sizeof(localLightInfo[0]) * localLightInfo.size(),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VkBufferCreateInfo blurKernelBufferInfo = vktools::initializers::bufferCreateInfo(sizeof(glm::vec4) * blurKernel.size(),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &camUniformBufferInfo, nullptr, &uniformBuffers[i]));
			uniformBufferMemories[i] = 
				devices.memoryAllocator.allocateBufferMemory(uniformBuffers[i], bufferFlags);
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &blurKernelBufferInfo, nullptr, &blurKernelBuffers[i]));
			blurKernelMemories[i] =
				devices.memoryAllocator.allocateBufferMemory(blurKernelBuffers[i], bufferFlags);
		}
	}

	/*
	* update matrices in ubo - rotates 90 degrees per second
	*
	* @param currentFrame - index of uniform buffer vector
	*/
	void updateUniformBuffer(size_t currentFrame) {
		uniformBufferMemories[currentFrame].mapData(devices.device, &cameraMatrices);
		blurKernelMemories[currentFrame].mapData(devices.device, blurKernel.data());
	}

	/*
	* set descriptor bindings & allocate destcriptor sets
	*/
	void createDescriptorSet() {
		vkDestroyDescriptorPool(devices.device, descPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, descLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, gbufferDescPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, gbufferDescLayout, nullptr);

		descBindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		descPool	= descBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descLayout	= descBindings.createDescriptorSetLayout(devices.device);
		descSets = vktools::allocateDescriptorSets(devices.device, descLayout, descPool, MAX_FRAMES_IN_FLIGHT);

		gbufferDescBindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		gbufferDescBindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		gbufferDescBindings.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		gbufferDescBindings.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		gbufferDescBindings.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		gbufferDescPool = gbufferDescBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		gbufferDescLayout = gbufferDescBindings.createDescriptorSetLayout(devices.device);
		gbufferDescSets = vktools::allocateDescriptorSets(devices.device, gbufferDescLayout, gbufferDescPool, MAX_FRAMES_IN_FLIGHT);

		computeDescHorizontalBindings.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescHorizontalBindings.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescHorizontalBindings.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescHorizontalPool = computeDescHorizontalBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		computeDescHorizontalSetLayout = computeDescHorizontalBindings.createDescriptorSetLayout(devices.device);
		computeDescHorizontalSets = vktools::allocateDescriptorSets(devices.device, computeDescHorizontalSetLayout, computeDescHorizontalPool, MAX_FRAMES_IN_FLIGHT);

		computeDescVerticalBindings.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescVerticalBindings.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescVerticalBindings.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescVerticalPool = computeDescVerticalBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		computeDescVerticalSetLayout = computeDescVerticalBindings.createDescriptorSetLayout(devices.device);
		computeDescVerticalSets = vktools::allocateDescriptorSets(devices.device, computeDescVerticalSetLayout, computeDescVerticalPool, MAX_FRAMES_IN_FLIGHT);
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			std::vector<VkWriteDescriptorSet> writes;

			//descSets - 1 camera matrix
			VkDescriptorBufferInfo info{ uniformBuffers[i], 0, sizeof(CameraMatrices) };
			writes.push_back(descBindings.makeWrite(descSets[i], 0, &info));

			//gbufferDescSets - 2 images
			VkDescriptorImageInfo posAttachmentInfo{ 
				sampler,
				geometryFramebuffers[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 
			};
			VkDescriptorImageInfo normalAttachmentInfo{ 
				sampler, 
				geometryFramebuffers[i].attachments[1].imageView, 
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 
			};
			VkDescriptorImageInfo diffuseAttachmentInfo{
				sampler,
				geometryFramebuffers[i].attachments[2].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};
			VkDescriptorImageInfo specularAttachmentInfo{
				sampler,
				geometryFramebuffers[i].attachments[3].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};
			VkDescriptorImageInfo shadowMapAttachmentInfo{
				sampler,
				shadowMaps[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_GENERAL
			};

			writes.push_back(gbufferDescBindings.makeWrite(gbufferDescSets[i], 0, &posAttachmentInfo));
			writes.push_back(gbufferDescBindings.makeWrite(gbufferDescSets[i], 1, &normalAttachmentInfo));
			writes.push_back(gbufferDescBindings.makeWrite(gbufferDescSets[i], 2, &diffuseAttachmentInfo));
			writes.push_back(gbufferDescBindings.makeWrite(gbufferDescSets[i], 3, &specularAttachmentInfo));
			writes.push_back(gbufferDescBindings.makeWrite(gbufferDescSets[i], 4, &shadowMapAttachmentInfo));
			
			//compute desc sets
			VkDescriptorImageInfo shadowMapBlurHorizontalAttachmentSrcInfo{
				sampler,
				shadowMaps[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_GENERAL
			};
			VkDescriptorImageInfo shadowMapBlurHorizontalAttachmentDstInfo{
				sampler,
				shadowMapBlurImageViews[i],
				VK_IMAGE_LAYOUT_GENERAL
			};
			VkDescriptorBufferInfo blueKernelBufferInfo{ blurKernelBuffers[i], 0, sizeof(glm::vec4) * blurKernel.size() };
			writes.push_back(computeDescHorizontalBindings.makeWrite(computeDescHorizontalSets[i], 0, &shadowMapBlurHorizontalAttachmentSrcInfo));
			writes.push_back(computeDescHorizontalBindings.makeWrite(computeDescHorizontalSets[i], 1, &shadowMapBlurHorizontalAttachmentDstInfo));
			writes.push_back(computeDescHorizontalBindings.makeWrite(computeDescHorizontalSets[i], 2, &blueKernelBufferInfo));
			writes.push_back(computeDescVerticalBindings.makeWrite(computeDescVerticalSets[i], 0, &shadowMapBlurHorizontalAttachmentDstInfo)); //swapped
			writes.push_back(computeDescVerticalBindings.makeWrite(computeDescVerticalSets[i], 1, &shadowMapBlurHorizontalAttachmentSrcInfo));
			writes.push_back(computeDescVerticalBindings.makeWrite(computeDescVerticalSets[i], 2, &blueKernelBufferInfo));

			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 1200, 800, "CS562");
