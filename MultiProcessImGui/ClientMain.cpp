#include "imgui.h"
#include "MultiProcessImGui.h"

#include <iostream>
#include <stdio.h>
#include <Windows.h>

void ClientMain()
{
    std::cout << "Starting in client mode." << std::endl;

    Client_Initialize();

    bool showDemoWindow = false;

    while (true)
    {
        Client_FrameStart();

        ImGui::Begin("Client Window");
        ImGui::Text("Hello from the client process!");
        ImGui::Text("PID: %d", GetCurrentProcessId());
        ImGui::Checkbox("Show demo window", &showDemoWindow);
        ImGui::End();

        if (showDemoWindow)
        { ImGui::ShowDemoWindow(&showDemoWindow); }

        Client_FrameEnd();
    }
}
