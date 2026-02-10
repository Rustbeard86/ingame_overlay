/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * The ingame overlay project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "BaseHook.h"
#include "HookDebug.h"
#include <algorithm>
#include <Windows.h> // Required for VirtualProtect

namespace InGameOverlay {

// Check if memory is readable using VirtualQuery (no SEH needed)
static bool IsMemoryReadable(void* address, size_t size = sizeof(void*))
{
    if (address == nullptr)
        return false;
    
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
        return false;
    
    // Check if memory is committed
    if (mbi.State != MEM_COMMIT)
        return false;
    
    // Check for readable protection flags
    DWORD readableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                          PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    
    if ((mbi.Protect & readableFlags) == 0 || (mbi.Protect & PAGE_GUARD) != 0)
        return false;
    
    // Verify the entire region is readable
    if (size > 1)
    {
        void* endAddr = (char*)address + size - 1;
        if (VirtualQuery(endAddr, &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if ((mbi.Protect & readableFlags) == 0 || (mbi.Protect & PAGE_GUARD) != 0)
            return false;
    }
    
    return true;
}

// Safely read a pointer without SEH
static bool SafeReadPointer(void* address, void** outValue)
{
    if (outValue == nullptr)
        return false;
    
    *outValue = nullptr;
    
    if (!IsMemoryReadable(address, sizeof(void*)))
        return false;
    
    // Memory is verified readable, safe to dereference
    *outValue = *(void**)address;
    return true;
}

// VTable hook implementation using safe memory probing (no SEH)
static bool SafeHookVTableImpl(void* pInterface, int index, void* pDetour, void** ppOriginal,
                               void** outVTableEntry, void** outOriginalFunc)
{
    if (outVTableEntry) *outVTableEntry = nullptr;
    if (outOriginalFunc) *outOriginalFunc = nullptr;
    
    // Verify interface pointer is readable
    if (!IsMemoryReadable(pInterface))
        return false;
    
    // Read vtable pointer from interface
    void* vtable = nullptr;
    if (!SafeReadPointer(pInterface, &vtable))
        return false;
    
    if (vtable == nullptr)
        return false;
    
    // Calculate address of vtable entry
    void** vtableEntryAddr = (void**)vtable + index;
    
    // Verify vtable entry is readable
    if (!IsMemoryReadable(vtableEntryAddr))
        return false;
    
    void* pTarget = *vtableEntryAddr;
    if (pTarget == nullptr)
        return false;
    
    DWORD oldProtect;
    // Make the specific VTable entry writable
    if (!VirtualProtect(vtableEntryAddr, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    
    // Store the original address
    if (ppOriginal)
        *ppOriginal = *vtableEntryAddr;
    
    // Store VTable hook info for safe reversion
    if (outVTableEntry) *outVTableEntry = vtableEntryAddr;
    if (outOriginalFunc) *outOriginalFunc = *vtableEntryAddr;
    
    // Swap the pointer
    *vtableEntryAddr = pDetour;
    
    // Restore original memory protection
    VirtualProtect(vtableEntryAddr, sizeof(void*), oldProtect, &oldProtect);
    
    return true;
}

// VTable unhook implementation using safe memory probing (no SEH)
static bool SafeUnhookVTableImpl(void** vtableEntry, void* originalFunc)
{
    if (vtableEntry == nullptr)
        return false;
    
    // Verify vtable entry is still readable
    if (!IsMemoryReadable(vtableEntry))
        return false;
    
    DWORD oldProtect;
    if (!VirtualProtect(vtableEntry, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    
    *vtableEntry = originalFunc;
    VirtualProtect(vtableEntry, sizeof(void*), oldProtect, &oldProtect);
    return true;
}

BaseHook_t::BaseHook_t() :
    _InHookTransaction(false)
{
    HOOK_DEBUG_INIT();
    HOOK_DEBUG_LOG_THREAD("BaseHook_t constructor - initializing MinHook");
    
    // Initialize MinHook once
    MH_STATUS status = MH_Initialize();
    HOOK_DEBUG_LOG_MH("Initialize", status, nullptr, "MH_Initialize");
}

BaseHook_t::~BaseHook_t()
{
    HOOK_DEBUG_LOG_THREAD("BaseHook_t destructor - cleaning up");
    
    UnhookAll();
    
    // Cleanup MinHook
    MH_STATUS status = MH_Uninitialize();
    HOOK_DEBUG_LOG_MH("Uninitialize", status, nullptr, "MH_Uninitialize");
    
    HOOK_DEBUG_CLEANUP();
}

void BaseHook_t::BeginHook()
{
    HOOK_DEBUG_LOG_THREAD("BeginHook - starting hook transaction");
    
    // Lock to prevent concurrent hook operations
    _HookMutex.lock();
    _InHookTransaction = true;
    _PendingHooks.clear();
}

void BaseHook_t::EndHook()
{
    HOOK_DEBUG_LOG_THREAD("EndHook - applying queued hooks");
    
    // Apply all queued hooks in a single transaction for thread safety.
    // MH_ApplyQueued attempts to suspend threads to ensure no thread is
    // executing in the middle of a function being patched.
    if (!_PendingHooks.empty())
    {
        MH_STATUS status = MH_ApplyQueued();
        HOOK_DEBUG_LOG_MH("ApplyQueued", status, nullptr, "batch hooks");
        
        if (status == MH_OK)
        {
            // Move pending hooks to the tracked list
            for (void* pTarget : _PendingHooks)
            {
                _HookedFunctions.push_back(pTarget);
            }
        }
        _PendingHooks.clear();
    }
    
    _InHookTransaction = false;
    _HookMutex.unlock();
}

bool BaseHook_t::HookFunc(std::pair<void**, void*> hook)
{
    void* pTarget = *hook.first;
    void* pDetour = hook.second;
    void* pOriginal = nullptr;

    HOOK_DEBUG_VALIDATE_PTR("HookFunc target", pTarget);
    HOOK_DEBUG_VALIDATE_PTR("HookFunc detour", pDetour);

    // Validate inputs
    if (pTarget == nullptr || pDetour == nullptr)
    {
        HOOK_DEBUG_LOG_OP("FAILED-NULL", pTarget, pDetour, nullptr, "validation failed");
        return false;
    }

    // Create the hook (this prepares the trampoline but doesn't activate it yet)
    MH_STATUS createStatus = MH_CreateHook(pTarget, pDetour, &pOriginal);
    HOOK_DEBUG_LOG_MH("CreateHook", createStatus, pTarget, nullptr);
    
    if (createStatus != MH_OK)
    {
        HOOK_DEBUG_LOG_OP("CREATE-FAILED", pTarget, pDetour, nullptr, "MH_CreateHook failed");
        return false;
    }

    // Store the original function pointer back into the caller's variable
    // BEFORE queueing the enable. This ensures the trampoline pointer is valid
    // before any thread could potentially call the detour.
    *hook.first = pOriginal;
    
    HOOK_DEBUG_LOG_OP("CREATE-SUCCESS", pTarget, pDetour, pOriginal, "trampoline created");

    // Queue the hook for batch enabling instead of enabling immediately.
    // This is safer because MH_ApplyQueued will suspend threads to ensure
    // no thread is executing the function preamble during patching.
    MH_STATUS queueStatus = MH_QueueEnableHook(pTarget);
    HOOK_DEBUG_LOG_MH("QueueEnableHook", queueStatus, pTarget, nullptr);
    
    if (queueStatus != MH_OK)
    {
        // Failed to queue, remove the hook we just created
        MH_RemoveHook(pTarget);
        *hook.first = pTarget; // Restore original pointer
        HOOK_DEBUG_LOG_OP("QUEUE-FAILED", pTarget, pDetour, pOriginal, "rollback performed");
        return false;
    }

    // Track pending hook for batch application in EndHook()
    _PendingHooks.push_back(pTarget);
    return true;
}

bool BaseHook_t::HookVTable(void* pInterface, int index, void* pDetour, void** ppOriginal)
{
    HOOK_DEBUG_LOG_VTABLE("ATTEMPT", pInterface, index, pDetour, nullptr);
    
    // Validate the interface pointer
    if (pInterface == nullptr)
    {
        HOOK_DEBUG_LOG_VTABLE("FAILED-NULL", pInterface, index, pDetour, nullptr);
        return false;
    }

    // Use safe memory probing to perform the VTable hook (no SEH)
    void* vtableEntry = nullptr;
    void* originalFunc = nullptr;
    
    bool success = SafeHookVTableImpl(pInterface, index, pDetour, ppOriginal, &vtableEntry, &originalFunc);
    
    if (success)
    {
        HOOK_DEBUG_LOG_VTABLE("SUCCESS", pInterface, index, pDetour, originalFunc);
        
        // Track this VTable hook for UnhookAll
        std::lock_guard<std::recursive_mutex> lock(_HookMutex);
        VTableHookInfo_t hookInfo;
        hookInfo.VTableEntry = (void**)vtableEntry;
        hookInfo.OriginalFunc = originalFunc;
        _VTableHooks.push_back(hookInfo);
    }
    else
    {
        HOOK_DEBUG_LOG_VTABLE("FAILED", pInterface, index, pDetour, nullptr);
    }
    
    return success;
}

void BaseHook_t::UnhookAll()
{
    HOOK_DEBUG_LOG_THREAD("UnhookAll - removing all hooks");
    
    std::lock_guard<std::recursive_mutex> lock(_HookMutex);

    // Disable and remove all inline hooks managed by MinHook
    for (void* pTarget : _HookedFunctions)
    {
        MH_STATUS disableStatus = MH_DisableHook(pTarget);
        HOOK_DEBUG_LOG_MH("DisableHook", disableStatus, pTarget, "unhook");
        
        MH_STATUS removeStatus = MH_RemoveHook(pTarget);
        HOOK_DEBUG_LOG_MH("RemoveHook", removeStatus, pTarget, "unhook");
    }
    _HookedFunctions.clear();
    _PendingHooks.clear();

    // Restore all VTable hooks using safe memory probing (no SEH)
    for (const VTableHookInfo_t& hookInfo : _VTableHooks)
    {
        SafeUnhookVTableImpl(hookInfo.VTableEntry, hookInfo.OriginalFunc);
    }
    _VTableHooks.clear();
}

} // namespace InGameOverlay
