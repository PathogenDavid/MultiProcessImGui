# ImGui Multi-Process Experiment

[![Unlicense Licensed](https://img.shields.io/github/license/pathogendavid/multiprocessimgui?style=flat-square)](LICENSE.txt)
[![Sponsor](https://img.shields.io/badge/sponsor-%E2%9D%A4-lightgrey?logo=github&style=flat-square)](https://github.com/sponsors/PathogenDavid)

This repository contains a proof-of-concept experiment that shares a single [Dear ImGui](https://github.com/ocornut/imgui/) context between two independent processes.

It works by mapping a segment of shared memory between the two processes and registering a custom allocator with ImGui that allocates memory exclusively from that shared memory. The shared memory is mapped to the same address in both processes so ImGui's internal pointers match up. This works because ImGui does not ever retain pointers to anything you pass into it (at least not as far as I'm aware.) A pair of shared events are provided to synchronize when the client process is allowed to submit UI and to signal when it is done.

Keep in mind this is a proof-of-concept. The heap allocator is inefficient and absolutely terrible. Many simple edge cases are not handled at all. (For instance, starting multiple clients will cause massive corruption, the server does not handle clients going away, the clients only handle the server going away by accident, etc.) You should only use this repo as an example that this is possible and to provide a starting point for your own more robust implementation.

## License

Most of this project is licensed under The Unlicense license. [See the license file for details](LICENSE.txt).

[`ServerMain.cpp`](MultiProcessImGui/ServerMain.cpp) is adapted from Dear ImGui's sample code and is licensed under [the MIT License](THIRD-PARTY-NOTICES.md).

Additionally, this project depends on Dear ImGui, which is licensed under the terms of the MIT License. [See the third-party notice listing for details](THIRD-PARTY-NOTICES.md).

## Limitations

Right now the shared memory is reserved all up front. This means that if ImGui ever needs more than `SHARED_HEAP_SIZE` (10 MB) of memory allocations will fail. Making this approach work with dynamically allocated pages would be non-trivial. 10 MB might be too little to reserve, but I'd recommend a fixed budget for your ImGui allocations.

Since the memory has to be mapped to the same location in both processes, this approach will fail if any pages are reserved within the shared heap range before the memory is mapped. The current approach uses a hard-coded address for the shared memory. (`0x0000_00FF_0000_0000`, see `GetHeapBase`) [Big sky theory](https://en.wikipedia.org/wiki/Big_sky_theory) says this shouldn't ever be an issue, but if you wanted to partially avoid the problem you could let the OS choose where to map the memory in the server, and then communicate that address to the clients before they map the memory. (Obviously the clients would fail to map the pages if there was a conflict on their end. There's no real way around this without drastically modifying Dear ImGui, your best bet is simply to make sure the shared heap is mapped in the client as early as possible to avoid conflict.

This example repository only contains an implementation for Windows x64. This approach should work on 64-bit Linux and MacOS.

Due to the limited virtual address space this approach might not be suitable for 32-bit architectures. x86 has all the tools you need, but the smaller address space means accidental collisions are more likely. You can *probably* get away the approach for avoiding a hard-coded address described above, but big sky theory will no longer be on your side.

Currently only one client is supported (despite the name of `Server_SubmitClients`.) Only one server at a time is supported. (Technically one server per [kernel object namespace](https://docs.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces).) Having a client leave after it has arrived will cause the server to lock up. Having the server leave while the clients is running will either do nothing or crash the client. Other edge cases like this are not handled at all. The server must be started before the client.

## Code Overview

File | Description
-----|-----
[`MultiProcessImGui.cpp`](MultiProcessImGui/MultiProcessImGui.cpp) | This is the core of the inter-process communication that powers this experiment.
[`ClientMain.cpp`](MultiProcessImGui/ClientMain.cpp) | This is the code that runs on the client and submits its UI.
[`ServerMain.cpp`](MultiProcessImGui/ServerMain.cpp) | This is the code that runs on the server, and is a modified version of the Dear ImGui [example_win32_directx11 example](https://github.com/ocornut/imgui/tree/455c21df7100a4727dd6e4c8e69249b7de21d24c/examples/example_win32_directx11).

## Building

### Prerequisites

Tool | Recommended Version
-----|--------------------
[Visual Studio 2019](https://visualstudio.microsoft.com/vs/) | 16.7.6

Visual Studio requires the "Desktop development with C++" workload to be installed.

### Build Steps

1. Ensure Git submodules are up-to-date with `git submodule update --init --recursive`
2. Open `MultiProcessImGui.sln` and build it.

## Running

1. Start Debugging (F5) from Visual Studio to run the server, or simply launch `MultiProcessImGui.exe`
2. Start the client by launching `MultiProcessImGui.exe --client`
