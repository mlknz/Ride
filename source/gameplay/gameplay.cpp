#include "gameplay.hpp"

#include <imgui/imgui.h>

#include "core/view.hpp"
#include "core/camera/camera.hpp"
#include "core/input/input.hpp"

namespace ez {

Gameplay::Gameplay(std::unique_ptr<View>&& aView) : view(std::move(aView))
{
    view->SwitchToDefaultScene();

    camera = std::make_unique<Camera>();
    input = std::make_unique<Input>();
}

void Gameplay::SetViewportExtent(uint32_t width, uint32_t height)
{
    if (camera)
    {
        camera->SetViewportExtent(width, height);
    }
}

void Gameplay::Update(double curTime, double deltaTime)
{
    glm::vec3 cameraMovement;
    if (input->IsKeyPressed(SDLK_a))
    {
        cameraMovement.x += 1.0f;
    }
    if (input->IsKeyPressed(SDLK_d))
    {
        cameraMovement.x -= 1.0f;
    }
    if (input->IsKeyPressed(SDLK_s))
    {
        cameraMovement.z -= 1.0f;
    }
    if (input->IsKeyPressed(SDLK_w))
    {
        cameraMovement.z += 1.0f;
    }
    cameraMovement *= deltaTime;

    camera->MovePreserveDirection(cameraMovement);

    if (input->IsKeyPressed(SDLK_1))
    {
        view->ToggleSceneTest();
    }   

    ImGui::NewFrame();
    ImGui::Begin("Hello, world!");
    ImGui::Text("Hello, %d", 42);
    // ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    ImGui::End();
}

}
