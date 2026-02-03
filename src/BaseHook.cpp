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
#include <algorithm>
#include <Windows.h> // Required for VirtualProtect

namespace InGameOverlay {

BaseHook_t::BaseHook_t()
{
    // Initialize MinHook once
    MH_Initialize();
}

BaseHook_t::~BaseHook_t()
{
    UnhookAll();
    // Cleanup MinHook
    MH_Uninitialize();
}

void BaseHook_t::BeginHook()
{
    // MinHook doesn't strictly require transaction blocks like Detours, 
    // but we keep these for architectural consistency.
}

void BaseHook_t::EndHook()
{
}

bool BaseHook_t::HookFunc(std::pair<void**, void*> hook)
{
    void* pTarget = *hook.first;
    void* pDetour = hook.second;
    void* pOriginal = nullptr;

    // Create the hook
    if (MH_CreateHook(pTarget, pDetour, &pOriginal) != MH_OK)
        return false;

    // Enable the hook
    if (MH_EnableHook(pTarget) != MH_OK)
        return false;

    // Store the original function pointer back into the caller's variable
    // so they can call the original function from their detour.
    *hook.first = pOriginal; 
    
    // Keep track of the target for unhooking later
    _HookedFunctions.push_back(pTarget); 
    return true;
}

bool BaseHook_t::HookVTable(void* pInterface, int index, void* pDetour, void** ppOriginal)
{
    if (!pInterface) return false;

    // The VTable is the first pointer in the object
    void** vtable = *(void***)pInterface;
    void* pTarget = vtable[index];

    DWORD oldProtect;
    // Make the specific VTable entry writable
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    // Store the original address
    if (ppOriginal)
        *ppOriginal = vtable[index];

    // Swap the pointer
    vtable[index] = pDetour;

    // Restore original memory protection
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);

    // Note: VTable hooks aren't managed by MinHook, but we can store them 
    // if we want to automate reversion in UnhookAll.
    return true;
}

void BaseHook_t::UnhookAll()
{
    // Disable and remove all inline hooks managed by MinHook
    for (void* pTarget : _HookedFunctions)
    {
        MH_DisableHook(pTarget);
        MH_RemoveHook(pTarget);
    }
    _HookedFunctions.clear();
}

} // namespace InGameOverlay
