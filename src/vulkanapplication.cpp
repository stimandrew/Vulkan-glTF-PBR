#include "vulkanapplication.h"

// Constructor
VulkanApplication::VulkanApplication() : VulkanExampleBase()
{
    title = "Vulkan glTF 2.0 PBR - (C) Sascha Willems (www.saschawillems.de)";
#if defined(TINYGLTF_ENABLE_DRACO)
    std::cout << "Draco mesh compression is enabled" << std::endl;
#endif
}

// Destructor
VulkanApplication::~VulkanApplication()
{
    destroyBackgroundResources();
    destroyUIResources();
    destroyFullscreenQuad();

    for (auto& pipeline : pipelines) {
        vkDestroyPipeline(device, pipeline.second, nullptr);
    }

    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.materialBuffer, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.meshDataBuffer, nullptr);

    models.scene.destroy(device);
    models.skybox.destroy(device);

    for (auto buffer : uniformBuffers) {
        buffer.params.destroy();
        buffer.scene.destroy();
        buffer.skybox.destroy();
    }
    for (auto fence : waitFences) {
        vkDestroyFence(device, fence, nullptr);
    }
    for (auto semaphore : renderCompleteSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (auto semaphore : presentCompleteSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }

    textures.environmentCube.destroy();
    textures.irradianceCube.destroy();
    textures.prefilteredCube.destroy();
    textures.lutBrdf.destroy();
    textures.empty.destroy();

    delete ui;
}

void VulkanApplication::createFullscreenQuad()
{
    // Вершины для полноэкранного квада (2 треугольника)
    struct Vertex {
        float pos[2];
        float uv[2];
    };

    std::vector<Vertex> vertices = {
        { {-1.0f, -1.0f}, {0.0f, 1.0f} },
        { { 1.0f, -1.0f}, {1.0f, 1.0f} },
        { { 1.0f,  1.0f}, {1.0f, 0.0f} },
        { {-1.0f,  1.0f}, {0.0f, 0.0f} }
    };

    std::vector<uint16_t> indices = { 0, 1, 2, 2, 3, 0 };

    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(Vertex);
    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint16_t);
    fullscreenQuad.size = vertexBufferSize + indexBufferSize;

    // Создаем staging буфер
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = fullscreenQuad.size;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

    // Копируем данные в staging буфер
    uint8_t* data;
    VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, (void**)&data));
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy(data + vertexBufferSize, indices.data(), indexBufferSize);
    vkUnmapMemory(device, stagingMemory);

    // Создаем финальный буфер
    bufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &fullscreenQuad.buffer));

    vkGetBufferMemoryRequirements(device, fullscreenQuad.buffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &fullscreenQuad.memory));
    VK_CHECK_RESULT(vkBindBufferMemory(device, fullscreenQuad.buffer, fullscreenQuad.memory, 0));

    // Копируем из staging в финальный буфер
    VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    VkBufferCopy copyRegion{};
    copyRegion.size = fullscreenQuad.size;
    vkCmdCopyBuffer(copyCmd, stagingBuffer, fullscreenQuad.buffer, 1, &copyRegion);

    vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void VulkanApplication::destroyFullscreenQuad()
{
    if (fullscreenQuad.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, fullscreenQuad.buffer, nullptr);
        fullscreenQuad.buffer = VK_NULL_HANDLE;
    }
    if (fullscreenQuad.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, fullscreenQuad.memory, nullptr);
        fullscreenQuad.memory = VK_NULL_HANDLE;
    }
}

void VulkanApplication::createUIResources()
{
    // Создаем дескриптор сет лайаут для фона UI
    VkDescriptorSetLayoutBinding layoutBinding = {
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &uiBackgroundDescriptorSetLayout));

    // Создаем дескриптор сет
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &uiBackgroundDescriptorSetLayout;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &uiBackgroundDescriptorSet));

    // Создаем pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &uiBackgroundDescriptorSetLayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &uiBackgroundPipelineLayout));

    // Создаем шейдеры для фона UI
    auto vertShader = loadShader(device, "ui_background.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fragShader = loadShader(device, "ui_background.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShader, fragShader };

    // Настройки pipeline
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = settings.sampleCount;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = uiBackgroundPipelineLayout;
    pipelineInfo.renderPass = renderPass;

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &uiBackgroundPipeline));

    vkDestroyShaderModule(device, vertShader.module, nullptr);
    vkDestroyShaderModule(device, fragShader.module, nullptr);
}

void VulkanApplication::destroyUIResources()
{
    if (uiBackgroundPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, uiBackgroundPipeline, nullptr);
        uiBackgroundPipeline = VK_NULL_HANDLE;
    }
    if (uiBackgroundPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, uiBackgroundPipelineLayout, nullptr);
        uiBackgroundPipelineLayout = VK_NULL_HANDLE;
    }
    if (uiBackgroundDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, uiBackgroundDescriptorSetLayout, nullptr);
        uiBackgroundDescriptorSetLayout = VK_NULL_HANDLE;
    }
    uiBackgroundDescriptorSet = VK_NULL_HANDLE;
}

void VulkanApplication::updateViewports()
{
    // Обновляем размеры viewport'ов при изменении окна
    if (splitScreenEnabled) {
        // Сцена занимает левую часть окна (всю ширину минус ширина панели UI)
        sceneViewport.x = 0;
        sceneViewport.y = 0;
        sceneViewport.width = std::max(width - uiPanelWidth, 1u);
        sceneViewport.height = height;
        uiPanelHeight = height;

        // Панель UI занимает правую часть
        uiViewport.x = sceneViewport.width;
        uiViewport.y = 0;
        uiViewport.width = uiPanelWidth;
        uiViewport.height = height;
    } else {
        // Если разделение выключено, весь экран для сцены
        sceneViewport.x = 0;
        sceneViewport.y = 0;
        sceneViewport.width = width;
        sceneViewport.height = height;

        uiViewport.x = 0;
        uiViewport.y = 0;
        uiViewport.width = width;
        uiViewport.height = height;
    }

    // Обновляем Vulkan viewport'ы
    sceneViewport.vkViewport = {
        static_cast<float>(sceneViewport.x),
        static_cast<float>(sceneViewport.y),
        static_cast<float>(sceneViewport.width),
        static_cast<float>(sceneViewport.height),
        0.0f, 1.0f
    };

    sceneViewport.vkScissor = {
        { static_cast<int32_t>(sceneViewport.x), static_cast<int32_t>(sceneViewport.y) },
        { sceneViewport.width, sceneViewport.height }
    };

    uiViewport.vkViewport = {
        static_cast<float>(uiViewport.x),
        static_cast<float>(uiViewport.y),
        static_cast<float>(uiViewport.width),
        static_cast<float>(uiViewport.height),
        0.0f, 1.0f
    };

    uiViewport.vkScissor = {
        { static_cast<int32_t>(uiViewport.x), static_cast<int32_t>(uiViewport.y) },
        { uiViewport.width, uiViewport.height }
    };

    // Обновляем соотношение сторон камеры
    camera.updateAspectRatio(static_cast<float>(sceneViewport.width) / static_cast<float>(sceneViewport.height));
}

void VulkanApplication::loadBackground(std::string filename)
{
    std::cout << "Loading background from " << filename << std::endl;

    if (textures.background.image) {
        textures.background.destroy();
    }

    textures.background.loadFromJPG(filename, vulkanDevice, queue);

    // Размеры текстуры автоматически сохраняются в textures.background.width и .height
    std::cout << "Background loaded: " << textures.background.width << "x" << textures.background.height << std::endl;
}

void VulkanApplication::renderBackground()
{
    if (!useStaticBackground || !textures.background.image) {
        return;
    }
    // Этот метод больше не используется, используем renderBackgroundInCommandBuffer
}

void VulkanApplication::renderBackgroundInCommandBuffer(VkCommandBuffer commandBuffer, const Viewport& viewport)
{
    if (!useStaticBackground || !textures.background.image) {
        return;
    }

    if (backgroundRes.needsRecreate) {
        destroyBackgroundResources();
        backgroundRes.needsRecreate = false;
    }

    if (!backgroundRes.initialized) {
        createBackgroundResources();
    }

    // Устанавливаем viewport и scissor для этой области
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport.vkViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &viewport.vkScissor);

    // Обновляем дескриптор с текущей текстурой фона
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = backgroundRes.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.pImageInfo = &textures.background.descriptor;
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    // Вычисляем push constants для сохранения пропорций
    BackgroundPushConstants pushConstants;
    pushConstants.viewportSize = glm::vec2(viewport.width, viewport.height);
    pushConstants.imageSize = glm::vec2(textures.background.width, textures.background.height);
    pushConstants.offset = glm::vec2(viewport.x, viewport.y);

    // Передаем push constants
    vkCmdPushConstants(commandBuffer, backgroundRes.pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(BackgroundPushConstants), &pushConstants);

    // Рендерим фон
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, backgroundRes.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            backgroundRes.pipelineLayout, 0, 1, &backgroundRes.descriptorSet, 0, nullptr);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

void VulkanApplication::resetCamera()
{
    camera.setPosition({ 0.0f, 0.0f, 1.0f });
    camera.setRotation({ 0.0f, 0.0f, 0.0f });
    camera.updateViewMatrix();

    // Сбрасываем позицию модели
    modelPosition = glm::vec3(0.0f);
}

void VulkanApplication::renderNode(vkglTF::Node *node, uint32_t cbIndex, vkglTF::Material::AlphaMode alphaMode)
{
    if (node->mesh) {
        for (vkglTF::Primitive * primitive : node->mesh->primitives) {
            if (primitive->material.alphaMode == alphaMode) {
                std::string pipelineName = "pbr";
                std::string pipelineVariant = "";

                if (primitive->material.unlit) {
                    pipelineName = "unlit";
                };

                if (alphaMode == vkglTF::Material::ALPHAMODE_BLEND) {
                    pipelineVariant = "_alpha_blending";
                } else {
                    if (primitive->material.doubleSided) {
                        pipelineVariant = "_double_sided";
                    }
                }

                const VkPipeline pipeline = pipelines[pipelineName + pipelineVariant];

                if (pipeline != boundPipeline) {
                    vkCmdBindPipeline(commandBuffers[cbIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    boundPipeline = pipeline;
                }

                const std::vector<VkDescriptorSet> descriptorsets = {
                    descriptorSets[cbIndex].scene,
                    primitive->material.descriptorSet,
                    descriptorSetsMeshData[cbIndex],
                    descriptorSetMaterials
                };
                vkCmdBindDescriptorSets(commandBuffers[cbIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorsets.size()), descriptorsets.data(), 0, NULL);

                MeshPushConstantBlock pushConstantBlock{};
                pushConstantBlock.meshIndex = node->mesh->index;
                pushConstantBlock.materialIndex = primitive->material.index;
                vkCmdPushConstants(commandBuffers[cbIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstantBlock), &pushConstantBlock);

                if (primitive->hasIndices) {
                    vkCmdDrawIndexed(commandBuffers[cbIndex], primitive->indexCount, 1, primitive->firstIndex, 0, 0);
                } else {
                    vkCmdDraw(commandBuffers[cbIndex], primitive->vertexCount, 1, 0, 0);
                }
            }
        }

    };
    for (auto child : node->children) {
        renderNode(child, cbIndex, alphaMode);
    }
}

void VulkanApplication::recordSceneCommandBuffer(VkCommandBuffer commandBuffer)
{
    VkClearValue clearValues[3];
    if (settings.multiSampling) {
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        clearValues[2].depthStencil = { 1.0f, 0 };
    } else {
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };
    }

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = static_cast<int32_t>(sceneViewport.x);
    renderPassBeginInfo.renderArea.offset.y = static_cast<int32_t>(sceneViewport.y);
    renderPassBeginInfo.renderArea.extent.width = sceneViewport.width;
    renderPassBeginInfo.renderArea.extent.height = sceneViewport.height;
    renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
    renderPassBeginInfo.pClearValues = clearValues;
    renderPassBeginInfo.framebuffer = frameBuffers[imageIndex];

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdSetViewport(commandBuffer, 0, 1, &sceneViewport.vkViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &sceneViewport.vkScissor);

    VkDeviceSize offsets[1] = { 0 };

    // Сначала рисуем фон (он будет на заднем плане)
    if (useStaticBackground && textures.background.image) {
        renderBackgroundInCommandBuffer(commandBuffer, sceneViewport);
    }

    // Затем небо (если включено)
    if (displayBackground) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[frameIndex].skybox, 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines["skybox"]);
        models.skybox.draw(commandBuffer);
    }

    // И только потом 3D сцену
    vkglTF::Model &model = models.scene;

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &model.vertices.buffer, offsets);
    if (model.indices.buffer != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(commandBuffer, model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    boundPipeline = VK_NULL_HANDLE;

    for (auto node : model.nodes) {
        renderNode(node, frameIndex, vkglTF::Material::ALPHAMODE_OPAQUE);
    }
    for (auto node : model.nodes) {
        renderNode(node, frameIndex, vkglTF::Material::ALPHAMODE_MASK);
    }
    for (auto node : model.nodes) {
        renderNode(node, frameIndex, vkglTF::Material::ALPHAMODE_BLEND);
    }

    vkCmdEndRenderPass(commandBuffer);
}

void VulkanApplication::recordUICommandBuffer(VkCommandBuffer commandBuffer)
{
    // Очищаем только область UI
    VkClearValue clearValue;
    clearValue.color = { { 0.2f, 0.2f, 0.2f, 1.0f } }; // Темно-серый фон для UI

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = static_cast<int32_t>(uiViewport.x);
    renderPassBeginInfo.renderArea.offset.y = static_cast<int32_t>(uiViewport.y);
    renderPassBeginInfo.renderArea.extent.width = uiViewport.width;
    renderPassBeginInfo.renderArea.extent.height = uiViewport.height;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &clearValue;
    renderPassBeginInfo.framebuffer = frameBuffers[imageIndex];

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdSetViewport(commandBuffer, 0, 1, &uiViewport.vkViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &uiViewport.vkScissor);

    // Рисуем фон для UI
    if (uiBackgroundDescriptorSet != VK_NULL_HANDLE && uiBackgroundPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, uiBackgroundPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                uiBackgroundPipelineLayout, 0, 1, &uiBackgroundDescriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }

    // Рисуем UI элементы - создаем UIViewport из наших данных
    UI::UIViewport uiVp;
    uiVp.x = uiViewport.x;
    uiVp.y = uiViewport.y;
    uiVp.width = uiViewport.width;
    uiVp.height = uiViewport.height;
    uiVp.vkViewport = uiViewport.vkViewport;
    uiVp.vkScissor = uiViewport.vkScissor;

    ui->draw(commandBuffer, uiVp);

    vkCmdEndRenderPass(commandBuffer);
}

void VulkanApplication::recordCommandBuffer()
{
    vkResetCommandBuffer(commandBuffers[frameIndex], 0);

    VkCommandBufferBeginInfo cmdBufferBeginInfo{};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[frameIndex], &cmdBufferBeginInfo));

    if (splitScreenEnabled) {
        // Если разделение включено, рендерим две области последовательно
        recordSceneCommandBuffer(commandBuffers[frameIndex]);
        recordUICommandBuffer(commandBuffers[frameIndex]);
    } else {
        // Иначе рендерим всё как обычно
        VkClearValue clearValues[3];
        if (settings.multiSampling) {
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[2].depthStencil = { 1.0f, 0 };
        } else {
            clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            clearValues[1].depthStencil = { 1.0f, 0 };
        }

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
        renderPassBeginInfo.pClearValues = clearValues;
        renderPassBeginInfo.framebuffer = frameBuffers[imageIndex];

        vkCmdBeginRenderPass(commandBuffers[frameIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)width;
        viewport.height = (float)height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffers[frameIndex], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { width, height };
        vkCmdSetScissor(commandBuffers[frameIndex], 0, 1, &scissor);

        if (useStaticBackground && textures.background.image) {
            Viewport fullViewport;
            fullViewport.vkViewport = viewport;
            fullViewport.vkScissor = scissor;
            renderBackgroundInCommandBuffer(commandBuffers[frameIndex], fullViewport);
        }

        if (displayBackground) {
            vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[frameIndex].skybox, 0, nullptr);
            vkCmdBindPipeline(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines["skybox"]);
            models.skybox.draw(commandBuffers[frameIndex]);
        }

        vkglTF::Model &model = models.scene;

        VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(commandBuffers[frameIndex], 0, 1, &model.vertices.buffer, offsets);
        if (model.indices.buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(commandBuffers[frameIndex], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        }

        boundPipeline = VK_NULL_HANDLE;

        for (auto node : model.nodes) {
            renderNode(node, frameIndex, vkglTF::Material::ALPHAMODE_OPAQUE);
        }
        for (auto node : model.nodes) {
            renderNode(node, frameIndex, vkglTF::Material::ALPHAMODE_MASK);
        }
        for (auto node : model.nodes) {
            renderNode(node, frameIndex, vkglTF::Material::ALPHAMODE_BLEND);
        }

        ui->draw(commandBuffers[frameIndex]);

        vkCmdEndRenderPass(commandBuffers[frameIndex]);
    }

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers[frameIndex]));
}

void VulkanApplication::createBackgroundResources()
{
    if (backgroundRes.initialized) return;

    if (descriptorPool == VK_NULL_HANDLE) {
        std::cerr << "Error: descriptorPool is null when creating background resources!" << std::endl;
        return;
    }

    VkDescriptorSetLayoutBinding layoutBinding = {
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &backgroundRes.descriptorSetLayout));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &backgroundRes.descriptorSetLayout;

    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &backgroundRes.descriptorSet);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to allocate background descriptor set: " << result << std::endl;
        return;
    }

    // Создаем pipeline layout с push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BackgroundPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &backgroundRes.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &backgroundRes.pipelineLayout));

    auto vertShader = loadShader(device, "background.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fragShader = loadShader(device, "background.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShader, fragShader };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = settings.sampleCount;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.layout = backgroundRes.pipelineLayout;
    pipelineInfo.renderPass = renderPass;

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &backgroundRes.pipeline));

    vkDestroyShaderModule(device, vertShader.module, nullptr);
    vkDestroyShaderModule(device, fragShader.module, nullptr);

    backgroundRes.initialized = true;
}


void VulkanApplication::destroyBackgroundResources()
{
    if (backgroundRes.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, backgroundRes.pipeline, nullptr);
        backgroundRes.pipeline = VK_NULL_HANDLE;
    }
    if (backgroundRes.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, backgroundRes.pipelineLayout, nullptr);
        backgroundRes.pipelineLayout = VK_NULL_HANDLE;
    }
    if (backgroundRes.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, backgroundRes.descriptorSetLayout, nullptr);
        backgroundRes.descriptorSetLayout = VK_NULL_HANDLE;
    }
    backgroundRes.descriptorSet = VK_NULL_HANDLE;
    backgroundRes.initialized = false;
}

void VulkanApplication::createMaterialBuffer()
{
    std::vector<ShaderMaterial> shaderMaterials{};
    for (auto& material : models.scene.materials) {
        ShaderMaterial shaderMaterial{};

        shaderMaterial.emissiveFactor = material.emissiveFactor;
        shaderMaterial.colorTextureSet = material.baseColorTexture != nullptr ? material.texCoordSets.baseColor : -1;
        shaderMaterial.normalTextureSet = material.normalTexture != nullptr ? material.texCoordSets.normal : -1;
        shaderMaterial.occlusionTextureSet = material.occlusionTexture != nullptr ? material.texCoordSets.occlusion : -1;
        shaderMaterial.emissiveTextureSet = material.emissiveTexture != nullptr ? material.texCoordSets.emissive : -1;
        shaderMaterial.alphaMask = static_cast<float>(material.alphaMode == vkglTF::Material::ALPHAMODE_MASK);
        shaderMaterial.alphaMaskCutoff = material.alphaCutoff;
        shaderMaterial.emissiveStrength = material.emissiveStrength;

        if (material.pbrWorkflows.metallicRoughness) {
            shaderMaterial.workflow = static_cast<float>(PBR_WORKFLOW_METALLIC_ROUGHNESS);
            shaderMaterial.baseColorFactor = material.baseColorFactor;
            shaderMaterial.metallicFactor = material.metallicFactor;
            shaderMaterial.roughnessFactor = material.roughnessFactor;
            shaderMaterial.PhysicalDescriptorTextureSet = material.metallicRoughnessTexture != nullptr ? material.texCoordSets.metallicRoughness : -1;
            shaderMaterial.colorTextureSet = material.baseColorTexture != nullptr ? material.texCoordSets.baseColor : -1;
        } else {
            if (material.pbrWorkflows.specularGlossiness) {
                shaderMaterial.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSSINESS);
                shaderMaterial.PhysicalDescriptorTextureSet = material.extension.specularGlossinessTexture != nullptr ? material.texCoordSets.specularGlossiness : -1;
                shaderMaterial.colorTextureSet = material.extension.diffuseTexture != nullptr ? material.texCoordSets.baseColor : -1;
                shaderMaterial.diffuseFactor = material.extension.diffuseFactor;
                shaderMaterial.specularFactor = glm::vec4(material.extension.specularFactor, 1.0f);
            }
        }

        shaderMaterials.push_back(shaderMaterial);
    }

    if (shaderMaterialBuffer.buffer != VK_NULL_HANDLE) {
        shaderMaterialBuffer.destroy();
    }
    VkDeviceSize bufferSize = shaderMaterials.size() * sizeof(ShaderMaterial);
    Buffer stagingBuffer;
    VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &stagingBuffer.buffer, &stagingBuffer.memory, shaderMaterials.data()));
    VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, &shaderMaterialBuffer.buffer, &shaderMaterialBuffer.memory));

    VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, shaderMaterialBuffer.buffer, 1, &copyRegion);
    vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
    stagingBuffer.device = device;
    stagingBuffer.destroy();

    shaderMaterialBuffer.descriptor.buffer = shaderMaterialBuffer.buffer;
    shaderMaterialBuffer.descriptor.offset = 0;
    shaderMaterialBuffer.descriptor.range = bufferSize;
    shaderMaterialBuffer.device = device;
}

void VulkanApplication::createMeshDataBuffer()
{
    std::vector<ShaderMeshData> shaderMeshData{};
    for (auto& node : models.scene.linearNodes) {
        ShaderMeshData meshData{};
        if (node->mesh) {
            memcpy(meshData.jointMatrix, node->mesh->jointMatrix, sizeof(glm::mat4) * MAX_NUM_JOINTS);
            meshData.jointcount = node->mesh->jointcount;
            meshData.matrix = node->mesh->matrix;
            shaderMeshData.push_back(meshData);
        }
    }

    for (auto& shaderMeshDataBuffer : shaderMeshDataBuffers) {
        if (shaderMeshDataBuffer.buffer != VK_NULL_HANDLE) {
            shaderMeshDataBuffer.destroy();
        }
        VkDeviceSize bufferSize = shaderMeshData.size() * sizeof(ShaderMeshData);
        if (!vulkanDevice->requiresStaging) {
            VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &shaderMeshDataBuffer.buffer, &shaderMeshDataBuffer.memory));
            shaderMeshDataBuffer.device = device;
            shaderMeshDataBuffer.map();
            memcpy(shaderMeshDataBuffer.mapped, shaderMeshData.data(), bufferSize);
        } else {
            Buffer stagingBuffer;
            VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &stagingBuffer.buffer, &stagingBuffer.memory, shaderMeshData.data()));
            VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, &shaderMeshDataBuffer.buffer, &shaderMeshDataBuffer.memory));
            VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, shaderMeshDataBuffer.buffer, 1, &copyRegion);
            vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
            stagingBuffer.device = device;
            stagingBuffer.destroy();
        }
        shaderMeshDataBuffer.descriptor.buffer = shaderMeshDataBuffer.buffer;
        shaderMeshDataBuffer.descriptor.offset = 0;
        shaderMeshDataBuffer.descriptor.range = bufferSize;
        shaderMeshDataBuffer.device = device;
    }
}

void VulkanApplication::updateMeshDataBuffer(uint32_t index)
{
    std::vector<ShaderMeshData> shaderMeshData{};
    for (auto& node : models.scene.linearNodes) {
        ShaderMeshData meshData{};
        if (node->mesh) {
            memcpy(meshData.jointMatrix, node->mesh->jointMatrix, sizeof(glm::mat4) * MAX_NUM_JOINTS);
            meshData.jointcount = node->mesh->jointcount;
            meshData.matrix = node->mesh->matrix;
            shaderMeshData.push_back(meshData);
        }
    }

    VkDeviceSize bufferSize = shaderMeshData.size() * sizeof(ShaderMeshData);

    if (!vulkanDevice->requiresStaging) {
        memcpy(shaderMeshDataBuffers[index].mapped, shaderMeshData.data(), bufferSize);
    }
    else {
        Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &stagingBuffer.buffer, &stagingBuffer.memory, shaderMeshData.data()));
        VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkBufferCopy copyRegion{};
        copyRegion.size = bufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, shaderMeshDataBuffers[index].buffer, 1, &copyRegion);
        vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
        stagingBuffer.device = device;
        stagingBuffer.destroy();
    }
}

void VulkanApplication::loadScene(std::string filename)
{
    std::cout << "Loading scene from " << filename << std::endl;

    models.scene.destroy(device);

    backgroundRes.needsRecreate = true;

    animationIndex = 0;
    animationTimer = 0.0f;

    // Сбрасываем позицию модели при загрузке новой сцены
    modelPosition = glm::vec3(0.0f);

    auto tStart = std::chrono::high_resolution_clock::now();
    models.scene.loadFromFile(filename, vulkanDevice, queue);
    createMaterialBuffer();
    createMeshDataBuffer();

    auto tFileLoad = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();
    std::cout << "Loading took " << tFileLoad << " ms" << std::endl;

    for (auto& ext : models.scene.extensions) {
        if (std::find(supportedExtensions.begin(), supportedExtensions.end(), ext) == supportedExtensions.end()) {
            std::cout << "[WARN] Unsupported extension " << ext << " detected. Scene may not work or display as intended\n";
        }
    }
    resetCamera();
}

void VulkanApplication::loadEnvironment(std::string filename)
{
    std::cout << "Loading environment from " << filename << std::endl;
    if (textures.environmentCube.image) {
        textures.environmentCube.destroy();
        textures.irradianceCube.destroy();
        textures.prefilteredCube.destroy();
    }
    textures.environmentCube.loadFromFile(filename, VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
    generateCubemaps();
}

void VulkanApplication::loadAssets()
{
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    tinygltf::asset_manager = androidApp->activity->assetManager;
    readDirectory(assetpath + "models", "gltf", scenes, true);
#else
    struct stat info;
    if (stat(assetpath.c_str(), &info) != 0) {
        std::string msg = "Could not locate asset path in \"" + assetpath + "\".\nMake sure binary is run from correct relative directory!";
        std::cerr << msg << std::endl;
        exit(-1);
    }
#endif
    readDirectory(assetpath + "environments", "ktx", environments, false);

    textures.empty.loadFromFile(assetpath + "textures/empty.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);

    std::string sceneFile = assetpath + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
    std::string envMapFile = assetpath + "environments/papermill.ktx";
    for (size_t i = 0; i < args.size(); i++) {
        if ((std::string(args[i]).find(".gltf") != std::string::npos) || (std::string(args[i]).find(".glb") != std::string::npos)) {
            std::ifstream file(args[i]);
            if (file.good()) {
                sceneFile = args[i];
            } else {
                std::cout << "could not load \"" << args[i] << "\"" << std::endl;
            }
        }
        if (std::string(args[i]).find(".ktx") != std::string::npos) {
            std::ifstream file(args[i]);
            if (file.good()) {
                envMapFile = args[i];
            }
            else {
                std::cout << "could not load \"" << args[i] << "\"" << std::endl;
            }
        }
    }

    loadScene(sceneFile.c_str());
    models.skybox.loadFromFile(assetpath + "models/Box/glTF-Embedded/Box.gltf", vulkanDevice, queue);
    loadEnvironment(envMapFile.c_str());
}

void VulkanApplication::setupDescriptors()
{
    uint32_t imageSamplerCount = 0;
    uint32_t materialCount = 0;
    uint32_t meshCount = 0;

    imageSamplerCount += 3;

    std::vector<vkglTF::Model*> modellist = { &models.skybox, &models.scene };
    for (auto &model : modellist) {
        for (auto &material : model->materials) {
            imageSamplerCount += 5;
            materialCount++;
        }
        for (auto node : model->linearNodes) {
            if (node->mesh) {
                meshCount++;
            }
        }
    }

    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (4 + meshCount) * swapChain.imageCount },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (imageSamplerCount + 2) * swapChain.imageCount },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 + static_cast<uint32_t>(shaderMeshDataBuffers.size())}
    };
    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = (2 + materialCount + meshCount + 1) * swapChain.imageCount;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

    backgroundRes.needsRecreate = true;

    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                                                                       { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
        descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene));

        for (auto i = 0; i < descriptorSets.size(); i++) {
            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
            descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].scene));

            std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};

            writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSets[0].descriptorCount = 1;
            writeDescriptorSets[0].dstSet = descriptorSets[i].scene;
            writeDescriptorSets[0].dstBinding = 0;
            writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].scene.descriptor;

            writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSets[1].descriptorCount = 1;
            writeDescriptorSets[1].dstSet = descriptorSets[i].scene;
            writeDescriptorSets[1].dstBinding = 1;
            writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;

            writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeDescriptorSets[2].descriptorCount = 1;
            writeDescriptorSets[2].dstSet = descriptorSets[i].scene;
            writeDescriptorSets[2].dstBinding = 2;
            writeDescriptorSets[2].pImageInfo = &textures.irradianceCube.descriptor;

            writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeDescriptorSets[3].descriptorCount = 1;
            writeDescriptorSets[3].dstSet = descriptorSets[i].scene;
            writeDescriptorSets[3].dstBinding = 3;
            writeDescriptorSets[3].pImageInfo = &textures.prefilteredCube.descriptor;

            writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeDescriptorSets[4].descriptorCount = 1;
            writeDescriptorSets[4].dstSet = descriptorSets[i].scene;
            writeDescriptorSets[4].dstBinding = 4;
            writeDescriptorSets[4].pImageInfo = &textures.lutBrdf.descriptor;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }
    }

    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                                                                       { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                       };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
        descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.material));

        for (auto &material : models.scene.materials) {
            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
            descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.material;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &material.descriptorSet));

            std::vector<VkDescriptorImageInfo> imageDescriptors = {
                textures.empty.descriptor,
                textures.empty.descriptor,
                material.normalTexture ? material.normalTexture->descriptor : textures.empty.descriptor,
                material.occlusionTexture ? material.occlusionTexture->descriptor : textures.empty.descriptor,
                material.emissiveTexture ? material.emissiveTexture->descriptor : textures.empty.descriptor
            };

            if (material.pbrWorkflows.metallicRoughness) {
                if (material.baseColorTexture) {
                    imageDescriptors[0] = material.baseColorTexture->descriptor;
                }
                if (material.metallicRoughnessTexture) {
                    imageDescriptors[1] = material.metallicRoughnessTexture->descriptor;
                }
            } else {
                if (material.pbrWorkflows.specularGlossiness) {
                    if (material.extension.diffuseTexture) {
                        imageDescriptors[0] = material.extension.diffuseTexture->descriptor;
                    }
                    if (material.extension.specularGlossinessTexture) {
                        imageDescriptors[1] = material.extension.specularGlossinessTexture->descriptor;
                    }
                }
            }

            std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
            for (size_t i = 0; i < imageDescriptors.size(); i++) {
                writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writeDescriptorSets[i].descriptorCount = 1;
                writeDescriptorSets[i].dstSet = material.descriptorSet;
                writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
                writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
            }

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
        }

        {
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                                                                           { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                                                                           };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
            descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
            descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.materialBuffer));

            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
            descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.materialBuffer;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSetMaterials));

            VkWriteDescriptorSet writeDescriptorSet{};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.dstSet = descriptorSetMaterials;
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.pBufferInfo = &shaderMaterialBuffer.descriptor;
            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }

        {
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                                                                           { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
                                                                           };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
            descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
            descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.meshDataBuffer));

            for (auto i = 0; i < descriptorSetsMeshData.size(); i++) {
                VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
                descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                descriptorSetAllocInfo.descriptorPool = descriptorPool;
                descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.meshDataBuffer;
                descriptorSetAllocInfo.descriptorSetCount = 1;
                VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSetsMeshData[i]));

                VkWriteDescriptorSet writeDescriptorSet{};
                writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writeDescriptorSet.descriptorCount = 1;
                writeDescriptorSet.dstSet = descriptorSetsMeshData[i];
                writeDescriptorSet.dstBinding = 0;
                writeDescriptorSet.pBufferInfo = &shaderMeshDataBuffers[i].descriptor;
                vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
            }
        }
    }

    for (auto i = 0; i < uniformBuffers.size(); i++) {
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].skybox));

        std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

        writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSets[0].descriptorCount = 1;
        writeDescriptorSets[0].dstSet = descriptorSets[i].skybox;
        writeDescriptorSets[0].dstBinding = 0;
        writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].skybox.descriptor;

        writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSets[1].descriptorCount = 1;
        writeDescriptorSets[1].dstSet = descriptorSets[i].skybox;
        writeDescriptorSets[1].dstBinding = 1;
        writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;

        writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSets[2].descriptorCount = 1;
        writeDescriptorSets[2].dstSet = descriptorSets[i].skybox;
        writeDescriptorSets[2].dstBinding = 2;
        writeDescriptorSets[2].pImageInfo = &textures.prefilteredCube.descriptor;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    backgroundRes.needsRecreate = true;
}

void VulkanApplication::addPipelineSet(const std::string prefix, const std::string vertexShader, const std::string fragmentShader)
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
    inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
    rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
    colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
    depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilStateCI.depthTestEnable = (prefix == "skybox" ? VK_FALSE : VK_TRUE);
    depthStencilStateCI.depthWriteEnable = (prefix == "skybox" ? VK_FALSE : VK_TRUE);
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateCI.front = depthStencilStateCI.back;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewportStateCI{};
    viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
    multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

    if (settings.multiSampling) {
        multisampleStateCI.rasterizationSamples = settings.sampleCount;
    }

    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    const std::vector<VkDescriptorSetLayout> setLayouts = {
        descriptorSetLayouts.scene, descriptorSetLayouts.material, descriptorSetLayouts.meshDataBuffer, descriptorSetLayouts.materialBuffer
    };
    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutCI.pSetLayouts = setLayouts.data();
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.size = sizeof(MeshPushConstantBlock);
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

    VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkglTF::Model::Vertex, pos)},
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkglTF::Model::Vertex, normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vkglTF::Model::Vertex, uv0) },
        { 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vkglTF::Model::Vertex, uv1) },
        { 4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(vkglTF::Model::Vertex, joint0) },
        { 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vkglTF::Model::Vertex, weight0) },
        { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vkglTF::Model::Vertex, color) }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
    vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &vertexInputStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    shaderStages[0] = loadShader(device, vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(device, fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline pipeline{};
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    pipelines[prefix] = pipeline;

    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    pipelines[prefix + "_double_sided"] = pipeline;

    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    blendAttachmentState.blendEnable = VK_TRUE;
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    pipelines[prefix + "_alpha_blending"] = pipeline;

    for (auto shaderStage : shaderStages) {
        vkDestroyShaderModule(device, shaderStage.module, nullptr);
    }
}

void VulkanApplication::preparePipelines()
{
    addPipelineSet("skybox", "skybox.vert.spv", "skybox.frag.spv");
    addPipelineSet("pbr", "pbr.vert.spv", "material_pbr.frag.spv");
    addPipelineSet("unlit", "pbr.vert.spv", "material_unlit.frag.spv");
}

void VulkanApplication::generateBRDFLUT()
{
    auto tStart = std::chrono::high_resolution_clock::now();

    const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
    const int32_t dim = 512;

    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = format;
    imageCI.extent.width = dim;
    imageCI.extent.height = dim;
    imageCI.extent.depth = 1;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &textures.lutBrdf.image));
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, textures.lutBrdf.image, &memReqs);
    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &textures.lutBrdf.deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(device, textures.lutBrdf.image, textures.lutBrdf.deviceMemory, 0));

    VkImageViewCreateInfo viewCI{};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.subresourceRange = {};
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.layerCount = 1;
    viewCI.image = textures.lutBrdf.image;
    VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &textures.lutBrdf.view));

    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 1.0f;
    samplerCI.maxAnisotropy = 1.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &textures.lutBrdf.sampler));

    VkAttachmentDescription attDesc{};
    attDesc.format = format;
    attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpassDescription{};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;

    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCI{};
    renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCI.attachmentCount = 1;
    renderPassCI.pAttachments = &attDesc;
    renderPassCI.subpassCount = 1;
    renderPassCI.pSubpasses = &subpassDescription;
    renderPassCI.dependencyCount = 2;
    renderPassCI.pDependencies = dependencies.data();

    VkRenderPass renderpass;
    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

    VkFramebufferCreateInfo framebufferCI{};
    framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferCI.renderPass = renderpass;
    framebufferCI.attachmentCount = 1;
    framebufferCI.pAttachments = &textures.lutBrdf.view;
    framebufferCI.width = dim;
    framebufferCI.height = dim;
    framebufferCI.layers = 1;

    VkFramebuffer framebuffer;
    VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &framebuffer));

    VkDescriptorSetLayout descriptorsetlayout;
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

    VkPipelineLayout pipelinelayout;
    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
    inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
    rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
    colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
    depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilStateCI.depthTestEnable = VK_FALSE;
    depthStencilStateCI.depthWriteEnable = VK_FALSE;
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateCI.front = depthStencilStateCI.back;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewportStateCI{};
    viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
    multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
    emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.layout = pipelinelayout;
    pipelineCI.renderPass = renderpass;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &emptyInputStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages.data();

    shaderStages = {
        loadShader(device, "genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(device, "genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    for (auto shaderStage : shaderStages) {
        vkDestroyShaderModule(device, shaderStage.module, nullptr);
    }

    VkClearValue clearValues[1];
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderpass;
    renderPassBeginInfo.renderArea.extent.width = dim;
    renderPassBeginInfo.renderArea.extent.height = dim;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;
    renderPassBeginInfo.framebuffer = framebuffer;

    VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = (float)dim;
    viewport.height = (float)dim;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent.width = dim;
    scissor.extent.height = dim;

    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmdBuf, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmdBuf);
    vulkanDevice->flushCommandBuffer(cmdBuf, queue);

    vkQueueWaitIdle(queue);

    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
    vkDestroyRenderPass(device, renderpass, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);

    textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
    textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
    textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    textures.lutBrdf.device = vulkanDevice;

    auto tEnd = std::chrono::high_resolution_clock::now();
    auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    std::cout << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
}

void VulkanApplication::generateCubemaps()
{
    enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

    for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++) {

        vks::TextureCubeMap cubemap;

        auto tStart = std::chrono::high_resolution_clock::now();

        VkFormat format;
        int32_t dim;

        switch (target) {
        case IRRADIANCE:
            format = VK_FORMAT_R32G32B32A32_SFLOAT;
            dim = 64;
            break;
        case PREFILTEREDENV:
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            dim = 512;
            break;
        };

        const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

        {
            VkImageCreateInfo imageCI{};
            imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCI.imageType = VK_IMAGE_TYPE_2D;
            imageCI.format = format;
            imageCI.extent.width = dim;
            imageCI.extent.height = dim;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = numMips;
            imageCI.arrayLayers = 6;
            imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &cubemap.image));
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(device, cubemap.image, &memReqs);
            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubemap.deviceMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device, cubemap.image, cubemap.deviceMemory, 0));

            VkImageViewCreateInfo viewCI{};
            viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            viewCI.format = format;
            viewCI.subresourceRange = {};
            viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.levelCount = numMips;
            viewCI.subresourceRange.layerCount = 6;
            viewCI.image = cubemap.image;
            VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &cubemap.view));

            VkSamplerCreateInfo samplerCI{};
            samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCI.magFilter = VK_FILTER_LINEAR;
            samplerCI.minFilter = VK_FILTER_LINEAR;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCI.minLod = 0.0f;
            samplerCI.maxLod = static_cast<float>(numMips);
            samplerCI.maxAnisotropy = 1.0f;
            samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &cubemap.sampler));
        }

        VkAttachmentDescription attDesc{};
        attDesc.format = format;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;

        std::array<VkSubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = 1;
        renderPassCI.pAttachments = &attDesc;
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpassDescription;
        renderPassCI.dependencyCount = 2;
        renderPassCI.pDependencies = dependencies.data();
        VkRenderPass renderpass;
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &renderpass));

        struct Offscreen {
            VkImage image;
            VkImageView view;
            VkDeviceMemory memory;
            VkFramebuffer framebuffer;
        } offscreen;

        {
            VkImageCreateInfo imageCI{};
            imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCI.imageType = VK_IMAGE_TYPE_2D;
            imageCI.format = format;
            imageCI.extent.width = dim;
            imageCI.extent.height = dim;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = 1;
            imageCI.arrayLayers = 1;
            imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &offscreen.image));
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);
            VkMemoryAllocateInfo memAllocInfo{};
            memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAllocInfo.allocationSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &offscreen.memory));
            VK_CHECK_RESULT(vkBindImageMemory(device, offscreen.image, offscreen.memory, 0));

            VkImageViewCreateInfo viewCI{};
            viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCI.format = format;
            viewCI.flags = 0;
            viewCI.subresourceRange = {};
            viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.baseMipLevel = 0;
            viewCI.subresourceRange.levelCount = 1;
            viewCI.subresourceRange.baseArrayLayer = 0;
            viewCI.subresourceRange.layerCount = 1;
            viewCI.image = offscreen.image;
            VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &offscreen.view));

            VkFramebufferCreateInfo framebufferCI{};
            framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCI.renderPass = renderpass;
            framebufferCI.attachmentCount = 1;
            framebufferCI.pAttachments = &offscreen.view;
            framebufferCI.width = dim;
            framebufferCI.height = dim;
            framebufferCI.layers = 1;
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &offscreen.framebuffer));

            VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.image = offscreen.image;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);
        }

        VkDescriptorSetLayout descriptorsetlayout;
        VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCI.pBindings = &setLayoutBinding;
        descriptorSetLayoutCI.bindingCount = 1;
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

        VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo descriptorPoolCI{};
        descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.poolSizeCount = 1;
        descriptorPoolCI.pPoolSizes = &poolSize;
        descriptorPoolCI.maxSets = 2;
        VkDescriptorPool descriptorpool;
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorpool));

        VkDescriptorSet descriptorset;
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorpool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorset));
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorset;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pImageInfo = &textures.environmentCube.descriptor;
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

        struct PushBlockIrradiance {
            glm::mat4 mvp;
            float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
            float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
        } pushBlockIrradiance;

        struct PushBlockPrefilterEnv {
            glm::mat4 mvp;
            float roughness;
            uint32_t numSamples = 32u;
        } pushBlockPrefilterEnv;

        VkPipelineLayout pipelinelayout;
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        switch (target) {
        case IRRADIANCE:
            pushConstantRange.size = sizeof(PushBlockIrradiance);
            break;
        case PREFILTEREDENV:
            pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
            break;
        };

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
        colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendStateCI.attachmentCount = 1;
        colorBlendStateCI.pAttachments = &blendAttachmentState;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
        depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilStateCI.depthTestEnable = VK_FALSE;
        depthStencilStateCI.depthWriteEnable = VK_FALSE;
        depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilStateCI.front = depthStencilStateCI.back;
        depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineViewportStateCreateInfo viewportStateCI{};
        viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCI.viewportCount = 1;
        viewportStateCI.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
        multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

        VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputStateCI.vertexBindingDescriptionCount = 1;
        vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputStateCI.vertexAttributeDescriptionCount = 1;
        vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.layout = pipelinelayout;
        pipelineCI.renderPass = renderpass;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pVertexInputState = &vertexInputStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = 2;
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.renderPass = renderpass;

        shaderStages[0] = loadShader(device, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        switch (target) {
        case IRRADIANCE:
            shaderStages[1] = loadShader(device, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            break;
        case PREFILTEREDENV:
            shaderStages[1] = loadShader(device, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            break;
        };
        VkPipeline pipeline;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
        for (auto shaderStage : shaderStages) {
            vkDestroyShaderModule(device, shaderStage.module, nullptr);
        }

        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffer;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;

        std::vector<glm::mat4> matrices = {
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

        VkViewport viewport{};
        viewport.width = (float)dim;
        viewport.height = (float)dim;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.extent.width = dim;
        scissor.extent.height = dim;

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = numMips;
        subresourceRange.layerCount = 6;

        {
            vulkanDevice->beginCommandBuffer(cmdBuf);
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.image = cubemap.image;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
        }

        for (uint32_t m = 0; m < numMips; m++) {
            for (uint32_t f = 0; f < 6; f++) {

                vulkanDevice->beginCommandBuffer(cmdBuf);

                viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
                vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

                vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                switch (target) {
                case IRRADIANCE:
                    pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
                    vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
                    break;
                case PREFILTEREDENV:
                    pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
                    pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
                    vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
                    break;
                };

                vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

                VkDeviceSize offsets[1] = { 0 };

                models.skybox.draw(cmdBuf);

                vkCmdEndRenderPass(cmdBuf);

                VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                subresourceRange.baseMipLevel = 0;
                subresourceRange.levelCount = numMips;
                subresourceRange.layerCount = 6;

                {
                    VkImageMemoryBarrier imageMemoryBarrier{};
                    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageMemoryBarrier.image = offscreen.image;
                    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                }

                VkImageCopy copyRegion{};

                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.baseArrayLayer = 0;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.layerCount = 1;
                copyRegion.srcOffset = { 0, 0, 0 };

                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.baseArrayLayer = f;
                copyRegion.dstSubresource.mipLevel = m;
                copyRegion.dstSubresource.layerCount = 1;
                copyRegion.dstOffset = { 0, 0, 0 };

                copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                copyRegion.extent.depth = 1;

                vkCmdCopyImage(
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    cubemap.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion);

                {
                    VkImageMemoryBarrier imageMemoryBarrier{};
                    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imageMemoryBarrier.image = offscreen.image;
                    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
                }

                vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
            }
        }

        {
            vulkanDevice->beginCommandBuffer(cmdBuf);
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.image = cubemap.image;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.subresourceRange = subresourceRange;
            vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            vulkanDevice->flushCommandBuffer(cmdBuf, queue, false);
        }

        vkDestroyRenderPass(device, renderpass, nullptr);
        vkDestroyFramebuffer(device, offscreen.framebuffer, nullptr);
        vkFreeMemory(device, offscreen.memory, nullptr);
        vkDestroyImageView(device, offscreen.view, nullptr);
        vkDestroyImage(device, offscreen.image, nullptr);
        vkDestroyDescriptorPool(device, descriptorpool, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelinelayout, nullptr);

        cubemap.descriptor.imageView = cubemap.view;
        cubemap.descriptor.sampler = cubemap.sampler;
        cubemap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cubemap.device = vulkanDevice;

        switch (target) {
        case IRRADIANCE:
            textures.irradianceCube = cubemap;
            break;
        case PREFILTEREDENV:
            textures.prefilteredCube = cubemap;
            shaderValuesParams.prefilteredCubeMipLevels = static_cast<float>(numMips);
            break;
        };

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
    }
}

void VulkanApplication::prepareUniformBuffers()
{
    for (auto &uniformBuffer : uniformBuffers) {
        uniformBuffer.scene.create(vulkanDevice, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesScene));
        uniformBuffer.skybox.create(vulkanDevice, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesSkybox));
        uniformBuffer.params.create(vulkanDevice, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesParams));
    }
    updateUniformData();
}

void VulkanApplication::updateUniformData()
{
    shaderValuesScene.projection = camera.matrices.perspective;
    shaderValuesScene.view = camera.matrices.view;

    float scale = (1.0f / std::max(models.scene.aabb[0][0], std::max(models.scene.aabb[1][1], models.scene.aabb[2][2]))) * 0.5f;
    glm::vec3 translate = -glm::vec3(models.scene.aabb[3][0], models.scene.aabb[3][1], models.scene.aabb[3][2]);
    translate += -0.5f * glm::vec3(models.scene.aabb[0][0], models.scene.aabb[1][1], models.scene.aabb[2][2]);

    // Применяем позицию модели из слайдеров
    shaderValuesScene.model = glm::mat4(1.0f);
    shaderValuesScene.model[0][0] = scale;
    shaderValuesScene.model[1][1] = scale;
    shaderValuesScene.model[2][2] = scale;

    // Сначала перемещаем в центр, затем применяем позицию из слайдеров
    shaderValuesScene.model = glm::translate(shaderValuesScene.model, translate);
    shaderValuesScene.model = glm::translate(shaderValuesScene.model, modelPosition);

    glm::mat4 cv = glm::inverse(camera.matrices.view);
    shaderValuesScene.camPos = glm::vec3(cv[3]);

    shaderValuesSkybox.projection = camera.matrices.perspective;
    shaderValuesSkybox.view = camera.matrices.view;
    shaderValuesSkybox.model = glm::mat4(glm::mat3(camera.matrices.view));
}

void VulkanApplication::updateParams()
{
    shaderValuesParams.lightDir = glm::vec4(
        sin(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
        sin(glm::radians(lightSource.rotation.y)),
        cos(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
        0.0f);
}

void VulkanApplication::windowResized()
{
    vkDeviceWaitIdle(device);
    updateViewports();
    updateUniformData();
    updateOverlay();
}

void VulkanApplication::prepare()
{
    VulkanExampleBase::prepare();

    camera.type = Camera::CameraType::lookat;

    camera.setPerspective(45.0f, (float)width / (float)height, 0.01f, 256.0f);
    camera.rotationSpeed = 0.25f;
    camera.movementSpeed = 0.1f;
    camera.setPosition({ 0.0f, 0.0f, 1.0f });
    camera.setRotation({ 0.0f, 0.0f, 0.0f });

    waitFences.resize(renderAhead);
    presentCompleteSemaphores.resize(swapChain.imageCount);
    renderCompleteSemaphores.resize(swapChain.imageCount);
    commandBuffers.resize(renderAhead);
    uniformBuffers.resize(renderAhead);
    descriptorSets.resize(renderAhead);
    shaderMeshDataBuffers.resize(renderAhead);
    descriptorSetsMeshData.resize(renderAhead);

    for (auto &waitFence : waitFences) {
        VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &waitFence));
    }

    for (auto &semaphore : presentCompleteSemaphores) {
        VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
    }

    for (auto &semaphore : renderCompleteSemaphores) {
        VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
    }

    {
        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = cmdPool;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffers.data()));
    }

    createFullscreenQuad();
    updateViewports();
    loadAssets();
    generateBRDFLUT();
    prepareUniformBuffers();
    setupDescriptors();
    preparePipelines();
    createUIResources();

    ui = new UI(vulkanDevice, renderPass, queue, pipelineCache, settings.sampleCount);
    updateOverlay();

    prepared = true;
}

void VulkanApplication::updateOverlay()
{
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 lastDisplaySize = io.DisplaySize;
    io.DisplaySize = ImVec2((float)uiViewport.width, (float)uiViewport.height);
    io.DeltaTime = frameTimer;

    // Преобразуем координаты мыши в координаты UI viewport'а
    if (mousePos.x >= uiViewport.x && mousePos.x <= uiViewport.x + uiViewport.width &&
        mousePos.y >= uiViewport.y && mousePos.y <= uiViewport.y + uiViewport.height) {
        io.MousePos = ImVec2(mousePos.x - uiViewport.x, mousePos.y - uiViewport.y);
    } else {
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    }

    io.MouseDown[0] = mouseButtons.left;
    io.MouseDown[1] = mouseButtons.right;

    ui->pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
    ui->pushConstBlock.translate = glm::vec2(-1.0f);

    float scale = 1.0f;

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    scale = (float)vks::android::screenDensity / (float)ACONFIGURATION_DENSITY_MEDIUM;
#endif
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(uiViewport.width - 20, uiViewport.height - 20), ImGuiSetCond_Always);
    ImGui::Begin("Vulkan glTF 2.0 PBR", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PushItemWidth(100.0f * scale);

    ui->text("www.saschawillems.de");
    ui->text("%.1d fps (%.2f ms)", lastFPS, (1000.0f / lastFPS));

    if (ui->header("Scene")) {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        if (ui->combo("File", selectedScene, scenes)) {
            vkDeviceWaitIdle(device);
            loadScene(scenes[selectedScene]);
            setupDescriptors();
        }
#else
        if (ui->button("Open gltf file")) {
            std::string filename = "";
#if defined(_WIN32)
            char buffer[MAX_PATH];
            OPENFILENAME ofn;
            ZeroMemory(&buffer, sizeof(buffer));
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = "glTF files\0*.gltf;*.glb\0";
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = "Select a glTF file to load";
            ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn)) {
                filename = buffer;
            }
#elif defined(__linux__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
            char buffer[1024];
            FILE *file = popen("zenity --title=\"Select a glTF file to load\" --file-filter=\"glTF files | *.gltf *.glb\" --file-selection", "r");
            if (file) {
                while (fgets(buffer, sizeof(buffer), file)) {
                    filename += buffer;
                };
                filename.erase(std::remove(filename.begin(), filename.end(), '\n'), filename.end());
                std::cout << filename << std::endl;
            }
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
            opengltfFileButtonClicked = true;
#endif
            if (!filename.empty()) {
                vkDeviceWaitIdle(device);
                loadScene(filename);
                setupDescriptors();
            }
        }
#endif
        if (ui->combo("Environment##env", selectedEnvironment, environments)) {
            vkDeviceWaitIdle(device);
            loadEnvironment(environments[selectedEnvironment]);
            setupDescriptors();
        }

        // Добавляем слайдеры для перемещения модели
        ui->text("Model Position:");
        if (ui->slider("X Position", &modelPosition.x, -1800.0f, 1800.0f)) {
            updateUniformData(); // Обновляем uniform данные при изменении
        }
        if (ui->slider("Y Position", &modelPosition.y, -1200.0f, 1200.0f)) {
            updateUniformData();
        }
        if (ui->slider("Z Position", &modelPosition.z, -2000.0f, 200.0f)) {
            updateUniformData();
        }

        // Добавляем кнопку для сброса позиции
        if (ui->button("Reset Position")) {
            modelPosition = glm::vec3(0.0f);
            updateUniformData();
        }
    }

    if (ui->header("Background")) {
        ui->checkbox("Enable static background", &useStaticBackground);
        if (ui->button("Load JPG background")) {
            std::string filename = "";
#if defined(_WIN32)
            char buffer[MAX_PATH];
            OPENFILENAME ofn;
            ZeroMemory(&buffer, sizeof(buffer));
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = "JPG files\0*.jpg;*.jpeg\0";
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = "Select a JPG file for background";
            ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn)) {
                filename = buffer;
            }
#elif defined(__linux__)
            char buffer[1024];
            FILE *file = popen("zenity --title=\"Select a JPG file for background\" --file-filter=\"JPG files | *.jpg *.jpeg\" --file-selection", "r");
            if (file) {
                while (fgets(buffer, sizeof(buffer), file)) {
                    filename += buffer;
                };
                filename.erase(std::remove(filename.begin(), filename.end(), '\n'), filename.end());
            }
#endif

            if (!filename.empty()) {
                vkDeviceWaitIdle(device);
                loadBackground(filename);
                destroyBackgroundResources();
            }
        }
    }

    if (ui->header("Environment")) {
        ui->checkbox("Show skybox", &displayBackground);
        ui->slider("Exposure", &shaderValuesParams.exposure, 0.1f, 10.0f);
        ui->slider("Gamma", &shaderValuesParams.gamma, 0.1f, 4.0f);
        ui->slider("IBL", &shaderValuesParams.scaleIBLAmbient, 0.0f, 1.0f);
    }

    if (ui->header("Camera")) {
        const std::vector<std::string> cameraTypes = { "Look at", "First Person" };
        int32_t cameraTypeSelection = (int32_t)camera.type;
        if (ui->combo("Type", &cameraTypeSelection, cameraTypes)) {
            camera.type = (Camera::CameraType)cameraTypeSelection;
            resetCamera();
        }
    }

    if (ui->header("Debug view")) {
        const std::vector<std::string> debugNamesInputs = {
            "none", "Base color", "Normal", "Occlusion", "Emissive", "Metallic", "Roughness"
        };
        if (ui->combo("Inputs", &debugViewInputs, debugNamesInputs)) {
            shaderValuesParams.debugViewInputs = static_cast<float>(debugViewInputs);
        }
        const std::vector<std::string> debugNamesEquation = {
            "none", "Diff (l,n)", "F (l,h)", "G (l,v,h)", "D (h)", "Specular"
        };
        if (ui->combo("PBR equation", &debugViewEquation, debugNamesEquation)) {
            shaderValuesParams.debugViewEquation = static_cast<float>(debugViewEquation);
        }
    }

    if (models.scene.animations.size() > 0) {
        if (ui->header("Animations")) {
            ui->checkbox("Animate", &animate);
            std::vector<std::string> animationNames;
            for (auto animation : models.scene.animations) {
                animationNames.push_back(animation.name);
            }
            ui->combo("Animation", &animationIndex, animationNames);
        }
    }

    ImGui::PopItemWidth();
    ImGui::End();
    ImGui::Render();

    ImDrawData* imDrawData = ImGui::GetDrawData();

    if (imDrawData) {
        VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
        VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

        bool updateBuffers = (ui->vertexBuffer.buffer == VK_NULL_HANDLE) || (ui->vertexBuffer.count != imDrawData->TotalVtxCount) || (ui->indexBuffer.buffer == VK_NULL_HANDLE) || (ui->indexBuffer.count != imDrawData->TotalIdxCount);

        if (updateBuffers) {
            vkDeviceWaitIdle(device);
            if (ui->vertexBuffer.buffer) {
                ui->vertexBuffer.destroy();
            }
            ui->vertexBuffer.create(vulkanDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vertexBufferSize);
            ui->vertexBuffer.count = imDrawData->TotalVtxCount;
            if (ui->indexBuffer.buffer) {
                ui->indexBuffer.destroy();
            }
            ui->indexBuffer.create(vulkanDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, indexBufferSize);
            ui->indexBuffer.count = imDrawData->TotalIdxCount;
        }

        ImDrawVert* vtxDst = (ImDrawVert*)ui->vertexBuffer.mapped;
        ImDrawIdx* idxDst = (ImDrawIdx*)ui->indexBuffer.mapped;
        for (int n = 0; n < imDrawData->CmdListsCount; n++) {
            const ImDrawList* cmd_list = imDrawData->CmdLists[n];
            memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmd_list->VtxBuffer.Size;
            idxDst += cmd_list->IdxBuffer.Size;
        }

        ui->vertexBuffer.flush();
        ui->indexBuffer.flush();
    }

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (mouseButtons.left) {
        mouseButtons.left = false;
    }
#endif
}

void VulkanApplication::render()
{
    if (!prepared) {
        return;
    }

    ui->updateTimer -= frameTimer;
    if (ui->updateTimer <= 0.0f) {
        updateOverlay();
        ui->updateTimer = 1.0f / 60.0f;
    }

    VK_CHECK_RESULT(vkWaitForFences(device, 1, &waitFences[frameIndex], VK_TRUE, UINT64_MAX));
    VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[frameIndex]));

    VkResult acquire = swapChain.acquireNextImage(presentCompleteSemaphores[frameIndex], &imageIndex);
    if ((acquire == VK_ERROR_OUT_OF_DATE_KHR) || (acquire == VK_SUBOPTIMAL_KHR)) {
        windowResize();
    }
    else {
        VK_CHECK_RESULT(acquire);
    }

    recordCommandBuffer();

    updateUniformData();
    UniformBufferSet currentUB = uniformBuffers[frameIndex];
    memcpy(currentUB.scene.mapped, &shaderValuesScene, sizeof(shaderValuesScene));
    memcpy(currentUB.params.mapped, &shaderValuesParams, sizeof(shaderValuesParams));
    memcpy(currentUB.skybox.mapped, &shaderValuesSkybox, sizeof(shaderValuesSkybox));

    const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.pWaitSemaphores = &presentCompleteSemaphores[frameIndex];
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderCompleteSemaphores[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[frameIndex];
    submitInfo.commandBufferCount = 1;
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[frameIndex]));

    VkResult present = swapChain.queuePresent(queue, imageIndex, renderCompleteSemaphores[imageIndex]);
    if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) {
        if (present == VK_ERROR_OUT_OF_DATE_KHR) {
            windowResize();
            return;
        }
        else {
            VK_CHECK_RESULT(present);
        }
    }

    if (!paused) {
        if ((animate) && (models.scene.animations.size() > 0)) {
            animationTimer += frameTimer;
            if (animationTimer > models.scene.animations[animationIndex].end) {
                animationTimer -= models.scene.animations[animationIndex].end;
            }
            models.scene.updateAnimation(animationIndex, animationTimer);
            updateMeshDataBuffer(frameIndex);
        }
        updateParams();
    }

    frameIndex = (frameIndex + 1) % renderAhead;
}

void VulkanApplication::fileDropped(std::string filename)
{
    vkDeviceWaitIdle(device);
    loadScene(filename);
    setupDescriptors();
}
