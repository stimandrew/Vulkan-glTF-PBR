#ifndef VULKANAPPLICATION_H
#define VULKANAPPLICATION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <chrono>
#include <map>
#include <unordered_map>
#include "algorithm"

#include <vulkan/vulkan.h>
#include "VulkanExampleBase.h"
#include "VulkanTexture.hpp"
#include "VulkanglTFModel.h"
#include "VulkanUtils.hpp"
#include "ui.hpp"
#include "screenshot.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/*
    PBR example main class
*/
class VulkanApplication : public VulkanExampleBase
{
private:
    struct BackgroundResources {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        bool initialized = false;
        bool needsRecreate = false;
    } backgroundRes;

    void createBackgroundResources();
    void destroyBackgroundResources();

public:
    struct SelectionRect {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        int vertexCount = 0;
        bool initialized = false;
        // Текущие экранные координаты рамки
        float left, right, top, bottom;
    } selectionRect;

    struct YOLOData {
        std::string className = "object";  // Имя класса по умолчанию
        int classId = 0;                    // ID класса по умолчанию
        bool saveForYOLO = true;           // Сохранять ли в формате YOLO
    } yoloData;

    void saveYOLOAnnotation(const std::string& filename);
    std::string getNextScreenshotFilename();
    int getNextScreenshotNumber();
    void saveModelCoordinatesToFile(const std::string& filename, const glm::vec3& modelPos);
    bool showSelectionRect = true;
    void createSelectionRectResources();
    void destroySelectionRectResources();
    void updateSelectionRect(const glm::vec3& modelPos, const glm::mat4& viewProj);
    void renderSelectionRect(VkCommandBuffer commandBuffer);

    glm::vec3 getModelCoordinatesRelativeToScreen();
    glm::vec3 modelRotation = glm::vec3(0.0f);

    glm::vec4 calculateBackgroundDisplayRect();
    Screenshot* screenshot = nullptr;
    void takeScreenshot();
    std::string generateScreenshotFilename();

    struct BackgroundPushConstants {
        glm::vec2 viewportSize;
        glm::vec2 imageSize;
        glm::vec2 offset;
    } backgroundPushConstants;
    glm::vec3 modelPosition = glm::vec3(0.0f);
    bool useStaticBackground = false;
    std::string backgroundFile;

    // Структура для хранения информации о viewport'ах
    struct Viewport {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        VkViewport vkViewport;
        VkRect2D vkScissor;
    };

    // Основные viewport'ы
    Viewport sceneViewport;  // Для 3D сцены
    Viewport uiViewport;     // Для панели управления

    // Настройки разделения экрана
    bool splitScreenEnabled = true;  // Включено ли разделение
    uint32_t uiPanelWidth = 400;     // Ширина панели UI в пикселях
    uint32_t uiPanelHeight;          // Высота панели UI (будет равна высоте окна)

    // Отдельные наборы дескрипторов для разных областей
    VkDescriptorSet uiBackgroundDescriptorSet = VK_NULL_HANDLE;
    VkPipeline uiBackgroundPipeline = VK_NULL_HANDLE;
    VkPipelineLayout uiBackgroundPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiBackgroundDescriptorSetLayout = VK_NULL_HANDLE;

    // Вершинный буфер для полноэкранного квада
    struct {
        VkBuffer buffer;
        VkDeviceMemory memory;
        VkDeviceSize size;
    } fullscreenQuad;

    void createFullscreenQuad();
    void destroyFullscreenQuad();
    void createUIResources();
    void destroyUIResources();
    void updateViewports();
    void recordSceneCommandBuffer(VkCommandBuffer commandBuffer);
    void recordUICommandBuffer(VkCommandBuffer commandBuffer);

    struct Textures {
        vks::TextureCubeMap environmentCube;
        vks::Texture2D empty;
        vks::Texture2D lutBrdf;
        vks::TextureCubeMap irradianceCube;
        vks::TextureCubeMap prefilteredCube;
        vks::Texture2D background;
    } textures;

    struct Models {
        vkglTF::Model scene;
        vkglTF::Model skybox;
    } models;

    struct UniformBufferSet {
        Buffer scene;
        Buffer skybox;
        Buffer params;
    };

    struct UBOMatrices {
        glm::mat4 projection{ 1.0f };
        glm::mat4 model{ 1.0f };
        glm::mat4 view{ 1.0f };
        glm::vec3 camPos{ 0.0f };
    } shaderValuesScene, shaderValuesSkybox;

    struct shaderValuesParams {
        glm::vec4 lightDir;
        float exposure = 4.5f;
        float gamma = 2.2f;
        float prefilteredCubeMipLevels;
        float scaleIBLAmbient = 1.0f;
        float debugViewInputs = 0;
        float debugViewEquation = 0;
    } shaderValuesParams;

    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

    std::unordered_map<std::string, VkPipeline> pipelines;
    VkPipeline boundPipeline{ VK_NULL_HANDLE };

    struct DescriptorSetLayouts {
        VkDescriptorSetLayout scene{ VK_NULL_HANDLE };
        VkDescriptorSetLayout material{ VK_NULL_HANDLE };
        VkDescriptorSetLayout materialBuffer{ VK_NULL_HANDLE };
        VkDescriptorSetLayout meshDataBuffer{ VK_NULL_HANDLE };
    } descriptorSetLayouts;

    struct DescriptorSets {
        VkDescriptorSet scene;
        VkDescriptorSet skybox;
    };
    std::vector<DescriptorSets> descriptorSets;

    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkCommandBuffer> uiCommandBuffers;
    std::vector<UniformBufferSet> uniformBuffers;

    std::vector<VkFence> waitFences;
    std::vector<VkSemaphore> renderCompleteSemaphores;
    std::vector<VkSemaphore> presentCompleteSemaphores;

    const uint32_t renderAhead = 2;
    uint32_t frameIndex = 0;

    int32_t animationIndex = 0;
    float animationTimer = 0.0f;
    bool animate = true;

    bool displayBackground = true;

    struct LightSource {
        glm::vec3 color = glm::vec3(1.0f);
        glm::vec3 rotation = glm::vec3(75.0f, -40.0f, 0.0f);
    } lightSource;

    UI* ui{ nullptr };

    void loadBackground(std::string filename);
    void renderBackground();
    void renderBackgroundInCommandBuffer(VkCommandBuffer commandBuffer, const Viewport& viewport);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    const std::string assetpath = "";
#else
    const std::string assetpath = "./../data/";
#endif

    enum PBRWorkflows{ PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };

    struct alignas(16) ShaderMaterial {
        glm::vec4 baseColorFactor;
        glm::vec4 emissiveFactor;
        glm::vec4 diffuseFactor;
        glm::vec4 specularFactor;
        float workflow;
        int colorTextureSet;
        int PhysicalDescriptorTextureSet;
        int normalTextureSet;
        int occlusionTextureSet;
        int emissiveTextureSet;
        float metallicFactor;
        float roughnessFactor;
        float alphaMask;
        float alphaMaskCutoff;
        float emissiveStrength;
    };
    Buffer shaderMaterialBuffer;
    VkDescriptorSet descriptorSetMaterials{ VK_NULL_HANDLE };

    struct MeshPushConstantBlock {
        int32_t meshIndex;
        int32_t materialIndex;
    };

    struct alignas(16) ShaderMeshData {
        glm::mat4 matrix;
        glm::mat4 jointMatrix[MAX_NUM_JOINTS]{};
        uint32_t jointcount{ 0 };
    };
    std::vector<Buffer> shaderMeshDataBuffers;
    std::vector<VkDescriptorSet> descriptorSetsMeshData;

    std::map<std::string, std::string> environments;
    std::string selectedEnvironment = "papermill";

#if !defined(_WIN32)
    std::map<std::string, std::string> scenes;
    std::string selectedScene = "DamagedHelmet";
#endif

    int32_t debugViewInputs = 0;
    int32_t debugViewEquation = 0;

    const std::vector<std::string> supportedExtensions = {
        "KHR_texture_basisu",
        "KHR_materials_pbrSpecularGlossiness",
        "KHR_materials_unlit",
        "KHR_materials_emissive_strength"
    };

    VulkanApplication();
    ~VulkanApplication();

    void resetCamera();
    void renderNode(vkglTF::Node *node, uint32_t cbIndex, vkglTF::Material::AlphaMode alphaMode);
    void recordCommandBuffer(); // Убрано override
    void createMaterialBuffer();
    void createMeshDataBuffer();
    void updateMeshDataBuffer(uint32_t index);
    void loadScene(std::string filename);
    void loadEnvironment(std::string filename);
    void loadAssets();
    void setupDescriptors();
    void addPipelineSet(const std::string prefix, const std::string vertexShader, const std::string fragmentShader);
    void preparePipelines();
    void generateBRDFLUT();
    void generateCubemaps();
    void prepareUniformBuffers();
    void updateUniformData();
    void updateParams();
    void windowResized() override;
    void prepare() override;
    void updateOverlay();
    void render() override;
    void fileDropped(std::string filename) override;
};

#endif // VULKANAPPLICATION_H
