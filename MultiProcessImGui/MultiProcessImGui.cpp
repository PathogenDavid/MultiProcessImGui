#include "imgui.h"
#include "MultiProcessImGui.h"

#include <iostream>
#include <stdio.h>
#include <Windows.h>

// Enables the use of the bump pointer allocator
// This allocator is mostly only testing. It cannot free memory.
//#define USE_BUMP_POINTER_ALLOCATOR
// Enables the use of a slightly modified version of dlmalloc for the allocator.
// This allocator is robust and widely used. It doesn't support concurrent use which is fine in our case.
// The version in this repository is modified to fail if it needs to allocate more virtual memory pages, and it will never try to decommit pages it already has.
// As such, it's not the most efficient allocator ever in terms of virtual memory usage.
#define USE_DLMALLOC_ALLOCATOR
// If neither of the above allocators are enabled, a very lazily implemented heap allacator will be used.
// This allocator is very simple but extremely inefficient.
// (It uses a single linked list for all memory chunks, so worst case allocating is O(n) where N is the number of memory chunks, free and allocated.)

#ifdef USE_DLMALLOC_ALLOCATOR
#include "dlmalloc.h"
#endif

void WindowsAssert(bool test)
{
    if (!test)
    {
        DWORD lastError = GetLastError();
        printf("Windows error: %d\n", lastError);
        throw lastError;
    }
}

struct HeapChunk
{
    HeapChunk* PreviousChunk;
    HeapChunk* NextChunk;
    size_t Size; //!< The size of the chunk, not including this header
    bool IsFree;
};

struct HeapHeader
{
    ImGuiContext* ContextPointer;
    bool ClientIsConnected;
#ifdef USE_BUMP_POINTER_ALLOCATOR
    char* NextAlloc;
#elif defined(USE_DLMALLOC_ALLOCATOR)
    mspace Space;
#else
    HeapChunk FirstChunk;
#endif
};

static HeapHeader* SharedHeap = nullptr;

// This is an extremely lazy heap allocator, not recommended for actual use.
static void* Allocate(size_t size, void*)
{
#ifdef USE_BUMP_POINTER_ALLOCATOR
    void* ret = SharedHeap->NextAlloc;
    SharedHeap->NextAlloc += size;
    return ret;
#elif defined(USE_DLMALLOC_ALLOCATOR)
    return mspace_malloc(SharedHeap->Space, size);
#else
    // Find the first free chunk which has enough space
    HeapChunk* chunk = &SharedHeap->FirstChunk;

    while (true)
    {
        // If the chunk is free and the right size, stop searching
        if (chunk->IsFree && chunk->Size >= size)
        {
            break;
        }

        // Check the next chunk
        chunk = chunk->NextChunk;

        // If this was the last chunk, we're out of memory
        if (chunk == nullptr)
        {
            DebugBreak();
            assert(false);
            return nullptr;
        }
    }

    // Figure how much space will be left over in the chunk after allocating
    size_t remaining = chunk->Size - size;

    // If there's enough space for another chunk after this allocation, split the chunk (otherwise this allocation is over-provisioned.)
    if (remaining > (sizeof(HeapChunk) + 1))
    {
        HeapChunk* newNext = (HeapChunk*)(((char*)(chunk + 1)) + size);
        newNext->IsFree = true;
        newNext->NextChunk = chunk->NextChunk;
        newNext->PreviousChunk = chunk;
        newNext->Size = chunk->Size - size - sizeof(HeapChunk);
        assert(newNext->Size + sizeof(HeapChunk) == remaining);
        
        if (newNext->NextChunk != nullptr)
        {
            newNext->NextChunk->PreviousChunk = newNext;
        }

        chunk->NextChunk = newNext;
        chunk->Size = size;
    }

    chunk->IsFree = false;
    //std::cout << "Allocate " << size << " bytes @ 0x" << std::hex << (chunk + 1) << std::dec << std::endl;
    return chunk + 1;
#endif
}

static void Free(void* pointer, void*)
{
#ifdef USE_BUMP_POINTER_ALLOCATOR
#elif defined(USE_DLMALLOC_ALLOCATOR)
    mspace_free(SharedHeap->Space, pointer);
#else
    if (pointer == nullptr)
    {
        return;
    }
    
    // Get the heap chunk header from the pointer
    HeapChunk* chunk = ((HeapChunk*)pointer) - 1;

    //std::cout << "Free " << chunk->Size << " bytes @ 0x" << std::hex << (pointer) << std::dec << std::endl;

    assert(chunk->IsFree == false);
    chunk->IsFree = true;

    // If the previous chunk is free, grow it to consume this chunk
    if (chunk->PreviousChunk != nullptr && chunk->PreviousChunk->IsFree)
    {
        chunk->PreviousChunk->Size += chunk->Size + sizeof(HeapChunk);
        chunk->PreviousChunk->NextChunk = chunk->NextChunk;

        // Update previous pointer for chunk following
        if (chunk->NextChunk != nullptr)
        {
            chunk->NextChunk->PreviousChunk = chunk->PreviousChunk;
        }

        // Continue as if we're the previous chunk so it'll be grown if we were followed by a free chunk
        chunk = chunk->PreviousChunk;
    }

    // If the next chunk is free, grow ourselves to consume it
    if (chunk->NextChunk != nullptr && chunk->NextChunk->IsFree)
    {
        chunk->Size += chunk->NextChunk->Size + sizeof(HeapChunk);
        chunk->NextChunk = chunk->NextChunk->NextChunk;

        // Update previous pointer for chunk following
        if (chunk->NextChunk != nullptr)
        {
            chunk->NextChunk->PreviousChunk = chunk;
        }
    }
#endif
}

#define SHARED_MEMORY_FILE_NAME L"Local\\MultiProcessImGui/SharedMemory"
#define SHARED_HEAP_SIZE (1024 * 1024 * 10)
static HANDLE SharedMemoryFile;

#define SUBMIT_CLIENT_EVENT_NAME L"Local\\SubmitClientEvent"
#define CLIENT_FINISHED_EVENT_NAME L"Local\\ClientFinishedEvent"
static HANDLE SubmitClientEvent;
static HANDLE ClientFinishedEvent;

static void* GetHeapBase()
{
    size_t ret = 0x000000FF00000000;

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    ret -= ret % systemInfo.dwAllocationGranularity;

    return (void*)ret;
}

static void Common_Initialize(bool isServer)
{
    // Map the shared memory
    void* heapBase = GetHeapBase();
    SharedHeap = (HeapHeader*)MapViewOfFileEx(SharedMemoryFile, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_HEAP_SIZE, heapBase);
    WindowsAssert(SharedHeap != NULL);

    if (isServer)
    {
#ifdef USE_BUMP_POINTER_ALLOCATOR
        SharedHeap->NextAlloc = (char*)(SharedHeap + 1);
#elif defined(USE_DLMALLOC_ALLOCATOR)
        void* base = (char*)(SharedHeap + 1);
        SharedHeap->Space = create_mspace_with_base(base, SHARED_HEAP_SIZE - sizeof(HeapHeader), 0);
#else
        SharedHeap->FirstChunk.IsFree = true;
        SharedHeap->FirstChunk.NextChunk = nullptr;
        SharedHeap->FirstChunk.PreviousChunk = nullptr;
        SharedHeap->FirstChunk.Size = SHARED_HEAP_SIZE - sizeof(HeapHeader);
#endif
    }

    // Create events
    SubmitClientEvent = CreateEvent(NULL, FALSE, FALSE, SUBMIT_CLIENT_EVENT_NAME);
    WindowsAssert(SubmitClientEvent != NULL);

    ClientFinishedEvent = CreateEvent(NULL, FALSE, FALSE, CLIENT_FINISHED_EVENT_NAME);
    WindowsAssert(ClientFinishedEvent != NULL);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::SetAllocatorFunctions(Allocate, Free);
}

void Server_Initialize()
{
    // Create the shared memory file
    SharedMemoryFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHARED_HEAP_SIZE, SHARED_MEMORY_FILE_NAME);
    WindowsAssert(SharedMemoryFile != NULL);

    // Perform common initialization
    Common_Initialize(true);

    // Create the ImGui context
    SharedHeap->ContextPointer = ImGui::CreateContext();
}

void Server_Shutdown()
{
    WindowsAssert(UnmapViewOfFile(SharedHeap));
    WindowsAssert(CloseHandle(SharedMemoryFile));
    SharedHeap = nullptr;
}

void Client_Initialize()
{
    // Open the shared memory file
    SharedMemoryFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEMORY_FILE_NAME);
    WindowsAssert(SharedMemoryFile != NULL);

    // Perform common initialization
    Common_Initialize(false);

    // Configure the ImGui context
    ImGui::SetCurrentContext(SharedHeap->ContextPointer);

    assert(SharedHeap->ClientIsConnected == false);
    SharedHeap->ClientIsConnected = true;
}

void Client_Shutdown()
{
    // Client shutdown and server shutdown are the same for now.
    Server_Shutdown();
}

void Server_SubmitClients()
{
#if 1
    if (!SharedHeap->ClientIsConnected)
    {
        return;
    }

    // Tell the client it is allowed to submit
    //TODO: Support multiple clients (and no clients)
    WindowsAssert(SetEvent(SubmitClientEvent));
    // Wait for the client to finish submitting
    WindowsAssert(WaitForSingleObject(ClientFinishedEvent, INFINITE) == WAIT_OBJECT_0);
#else
    static bool showDemoWindow = false;
    ImGui::Begin("Client Window");
    ImGui::Text("Hello from the client process!");
    ImGui::Text("PID: %d", GetCurrentProcessId());
    ImGui::Checkbox("Show demo window", &showDemoWindow);
    ImGui::End();

    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
#endif
}

void Client_FrameStart()
{
    // Wait until we're allowed to submit
    WindowsAssert(WaitForSingleObject(SubmitClientEvent, INFINITE) == WAIT_OBJECT_0);
}

void Client_FrameEnd()
{
    // Tell the server we're done submitting
    WindowsAssert(SetEvent(ClientFinishedEvent));
}

void StartClientProcess()
{
    //CreateProcess
}
