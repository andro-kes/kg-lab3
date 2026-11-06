#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include "graphics_internal.hpp"
#include "application.hpp"

namespace {

constexpr int32_t default_window_width = 1280;
constexpr int32_t default_window_height = 720;
constexpr char default_window_title[] = "Lab 3 - Texture mapping - Cone";

int run(GLFWwindow* window) {
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		const double time = glfwGetTime();

		if (graphics::internal::hasError()) {
			return EXIT_FAILURE;
		}
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		if (width <= 0 || height <= 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
			glfwWaitEventsTimeout(0.05);
			continue;
		}

		const graphics::internal::FrameData fd = graphics::internal::prepare();
		if (fd.command_buffer == VK_NULL_HANDLE) {
			if (graphics::internal::hasError()) {
				return EXIT_FAILURE;
			}
			glfwWaitEventsTimeout(0.01);
			continue;
		}
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		application::update(time);
		ImGui::Render();
		if (!application::render(fd) || !graphics::internal::submitAndPresent()) {
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

} // namespace

int main() {
	std::cout << std::unitbuf;
	int status = EXIT_FAILURE;
	bool imgui_glfw_initialized = false;
	bool graphics_attempted = false;
	bool application_attempted = false;

	glfwSetErrorCallback([](int code, const char* message) {
		std::cerr << "GLFW error " << code << ": " << message << '\n';
	});
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return EXIT_FAILURE;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(default_window_width, default_window_height,
		default_window_title, nullptr, nullptr);
	if (window == nullptr) {
		glfwTerminate();
		return EXIT_FAILURE;
	}
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int width, int height) {
		if (width > 0 && height > 0) {
			graphics::internal::resize(uint32_t(width), uint32_t(height));
		}
	});
	try {
		if (ImGui::CreateContext() == nullptr) {
			std::cerr << "Failed to create ImGui context\n";
			goto cleanup;
		}

		ImGui::GetIO().IniFilename = nullptr;
		ImGui::StyleColorsDark();
		imgui_glfw_initialized = ImGui_ImplGlfw_InitForVulkan(window, true);
		if (!imgui_glfw_initialized) {
			std::cerr << "Failed to initialize ImGui GLFW backend\n";
			goto cleanup;
		}
		graphics_attempted = true;
		if (!graphics::internal::initialize(window)) {
			std::cerr << "Failed to initialize graphics\n";
			goto cleanup;
		}
		application_attempted = true;
		if (!application::initialize()) {
			std::cerr << "Failed to initialize application\n";
			goto cleanup;
		}
		status = run(window);
	} catch (const std::exception& error) {
		std::cerr << "Application failed: " << error.what() << '\n';
		status = EXIT_FAILURE;
	}

cleanup:
	if (application_attempted) {
		if (vkDeviceWaitIdle(graphics::internal::context.device) != VK_SUCCESS) {
			status = EXIT_FAILURE;
		}
		application::shutdown();
	}
	if (graphics_attempted) {
		graphics::internal::shutdown();
	}
	if (graphics::internal::hasError()) {
		status = EXIT_FAILURE;
	}
	if (imgui_glfw_initialized) {
		ImGui_ImplGlfw_Shutdown();
	}
	if (ImGui::GetCurrentContext() != nullptr) {
		ImGui::DestroyContext();
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return status;
}
