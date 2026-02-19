/*
 * Vulkan physical based rendering glTF 2.0 renderer
 *
 * Copyright (C) 2018-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

// glTF format: https://github.com/KhronosGroup/glTF
// tinyglTF loader: https://github.com/syoyo/tinygltf

#include "vulkanapplication.h"


VulkanApplication *vulkanApplication;

// OS specific macros for the example main entry points
#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanApplication != NULL)
	{
		vulkanApplication->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	for (int32_t i = 0; i < __argc; i++) { VulkanApplication::args.push_back(__argv[i]); };
	vulkanApplication = new VulkanApplication();
	vulkanApplication->initVulkan();
	vulkanApplication->setupWindow(hInstance, WndProc);
	vulkanApplication->prepare();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
	return 0;
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point
void android_main(android_app* state)
{
	vulkanApplication = new VulkanApplication();
	state->userData = vulkanApplication;
	state->onAppCmd = VulkanApplication::handleAppCommand;
	state->onInputEvent = VulkanApplication::handleAppInput;
	androidApp = state;
	vks::android::getDeviceConfig();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
}
#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi
static void handleEvent()
{
}
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanApplication::args.push_back(argv[i]); };
	vulkanApplication = new VulkanApplication();
	vulkanApplication->initVulkan();
	vulkanApplication->prepare();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
	return 0;
}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanApplication::args.push_back(argv[i]); };
	vulkanApplication = new VulkanApplication();
	vulkanApplication->initVulkan();
	vulkanApplication->setupWindow();
	vulkanApplication->prepare();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
	return 0;
}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
static void handleEvent(const xcb_generic_event_t *event)
{
	if (vulkanApplication != NULL)
	{
		vulkanApplication->handleEvent(event);
	}
}
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanApplication::args.push_back(argv[i]); };

	vulkanApplication = new VulkanApplication();
	vulkanApplication->initVulkan();
	vulkanApplication->setupWindow();
	vulkanApplication->prepare();
	vulkanApplication->renderLoop();
	delete(vulkanApplication);
	return 0;
}
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
int main(const int argc, const char *argv[])
{
	@autoreleasepool
	{
		for (size_t i = 0; i < argc; i++) { VulkanApplication::args.push_back(argv[i]); };
		vulkanApplication = new VulkanApplication();
		vulkanApplication->initVulkan();
		vulkanApplication->setupWindow();
		vulkanApplication->prepare();
		vulkanApplication->renderLoop();
		delete(vulkanApplication);
	}
	return 0;
}
#endif
