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

// Hook debugging utilities - only active in Debug builds or when INGAMEOVERLAY_HOOK_DEBUG is defined
// Provides detailed logging and stack trace capture for diagnosing hook-related crashes
//
// NOTE: This module intentionally avoids SEH (__try/__except) to prevent conflicts with
// C++ exception handling in client code. Memory validation is done via VirtualQuery instead.

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

#include <Windows.h>
#include <string>

namespace InGameOverlay {
namespace HookDebug {

// Initialize debug symbols for stack trace capture
void InitializeDebugSymbols();

// Cleanup debug symbols
void CleanupDebugSymbols();

// Capture and format current stack trace
std::string CaptureStackTrace(int skipFrames = 0, int maxFrames = 32);

// Log hook operation with context
void LogHookOperation(const char* operation, void* target, void* detour, void* original, const char* funcName);

// Log MinHook status with description
void LogMinHookStatus(const char* operation, int status, void* target, const char* funcName);

// Log VTable hook operation
void LogVTableHook(const char* operation, void* pInterface, int index, void* detour, void* original);

// Log module/symbol resolution
void LogSymbolResolution(const char* symbolName, void* address, const char* moduleName);

// Log thread information during hook
void LogThreadContext(const char* context);

// Check if an address is in executable memory (SEH-free, uses VirtualQuery)
bool IsExecutableAddress(void* address);

// Check if an address is readable (SEH-free, uses VirtualQuery)  
bool IsReadableAddress(void* address);

// Safely read a pointer from memory (returns false if not readable)
bool SafeReadPointer(void* address, void** outValue);

// Get module name containing an address
std::string GetModuleNameForAddress(void* address);

// Log detailed pointer validation
void LogPointerValidation(const char* name, void* ptr, bool isValid);

} // namespace HookDebug
} // namespace InGameOverlay

// Debug macros - only active in debug builds or when INGAMEOVERLAY_HOOK_DEBUG is defined
#if defined(_DEBUG) || defined(DEBUG) || defined(INGAMEOVERLAY_HOOK_DEBUG)

#define HOOK_DEBUG_INIT()           InGameOverlay::HookDebug::InitializeDebugSymbols()
#define HOOK_DEBUG_CLEANUP()        InGameOverlay::HookDebug::CleanupDebugSymbols()
#define HOOK_DEBUG_STACKTRACE()     InGameOverlay::HookDebug::CaptureStackTrace(1)
#define HOOK_DEBUG_LOG_OP(op, target, detour, orig, name) \
    InGameOverlay::HookDebug::LogHookOperation(op, target, detour, orig, name)
#define HOOK_DEBUG_LOG_MH(op, status, target, name) \
    InGameOverlay::HookDebug::LogMinHookStatus(op, status, target, name)
#define HOOK_DEBUG_LOG_VTABLE(op, iface, idx, detour, orig) \
    InGameOverlay::HookDebug::LogVTableHook(op, iface, idx, detour, orig)
#define HOOK_DEBUG_LOG_SYMBOL(name, addr, mod) \
    InGameOverlay::HookDebug::LogSymbolResolution(name, addr, mod)
#define HOOK_DEBUG_LOG_THREAD(ctx) \
    InGameOverlay::HookDebug::LogThreadContext(ctx)
#define HOOK_DEBUG_VALIDATE_PTR(name, ptr) \
    InGameOverlay::HookDebug::LogPointerValidation(name, ptr, (ptr) != nullptr && InGameOverlay::HookDebug::IsExecutableAddress(ptr))

#else

#define HOOK_DEBUG_INIT()           ((void)0)
#define HOOK_DEBUG_CLEANUP()        ((void)0)
#define HOOK_DEBUG_STACKTRACE()     std::string()
#define HOOK_DEBUG_LOG_OP(op, target, detour, orig, name) ((void)0)
#define HOOK_DEBUG_LOG_MH(op, status, target, name)       ((void)0)
#define HOOK_DEBUG_LOG_VTABLE(op, iface, idx, detour, orig) ((void)0)
#define HOOK_DEBUG_LOG_SYMBOL(name, addr, mod)            ((void)0)
#define HOOK_DEBUG_LOG_THREAD(ctx)                        ((void)0)
#define HOOK_DEBUG_VALIDATE_PTR(name, ptr)                ((void)0)

#endif // _DEBUG

#endif // Windows

