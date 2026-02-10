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

#pragma once

#include <vector>
#include <utility>
#include <mutex>
#include <MinHook.h>

namespace InGameOverlay {

// Structure to track VTable hooks for safe reversion
struct VTableHookInfo_t
{
    void** VTableEntry;  // Pointer to the VTable entry
    void* OriginalFunc;  // Original function pointer
};

class BaseHook_t
{
public:
    BaseHook_t();
    virtual ~BaseHook_t();

    void BeginHook();
    void EndHook();

    // Standard inline hook - uses queued enabling for thread safety
    bool HookFunc(std::pair<void**, void*> hook);

    // New: VTable swap hook for better stability with translation layers like DXVK
    bool HookVTable(void* pInterface, int index, void* pDetour, void** ppOriginal);

    void UnhookAll();

protected:
    // MinHook manages hooks via target addresses, so we store the targets here
    std::vector<void*> _HookedFunctions;
    
    // Track VTable hooks for safe reversion
    std::vector<VTableHookInfo_t> _VTableHooks;
    
    // Track pending queued hooks for batch enabling
    std::vector<void*> _PendingHooks;
    
    // Mutex for thread-safe hook operations
    std::recursive_mutex _HookMutex;
    
    // Flag to indicate if we're in a BeginHook/EndHook transaction
    bool _InHookTransaction;

    BaseHook_t(const BaseHook_t&) = delete;
    BaseHook_t(BaseHook_t&&) = delete;
    BaseHook_t& operator =(const BaseHook_t&) = delete;
    BaseHook_t& operator =(BaseHook_t&&) = delete;
};

} // namespace InGameOverlay
