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

#include "HookDebug.h"

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

// Only compile the implementation in debug builds
#if defined(_DEBUG) || defined(DEBUG) || defined(INGAMEOVERLAY_HOOK_DEBUG)

#include <DbgHelp.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <ctime>

#pragma comment(lib, "dbghelp.lib")

namespace InGameOverlay {
namespace HookDebug {

static std::mutex g_DebugMutex;
static bool g_SymbolsInitialized = false;
static HANDLE g_ProcessHandle = nullptr;

// Get current timestamp string
static std::string GetTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << st.wHour << ":"
        << std::setfill('0') << std::setw(2) << st.wMinute << ":"
        << std::setfill('0') << std::setw(2) << st.wSecond << "."
        << std::setfill('0') << std::setw(3) << st.wMilliseconds;
    return oss.str();
}

// Output debug string (goes to debugger output window)
static void DebugOutput(const std::string& message)
{
    std::string fullMessage = "[HookDebug][" + GetTimestamp() + "] " + message + "\n";
    OutputDebugStringA(fullMessage.c_str());
}

void InitializeDebugSymbols()
{
    std::lock_guard<std::mutex> lock(g_DebugMutex);
    
    if (g_SymbolsInitialized)
        return;
    
    g_ProcessHandle = GetCurrentProcess();
    
    // Initialize symbol handler
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    
    if (SymInitialize(g_ProcessHandle, nullptr, TRUE))
    {
        g_SymbolsInitialized = true;
        DebugOutput("Debug symbols initialized successfully");
    }
    else
    {
        DWORD error = GetLastError();
        std::ostringstream oss;
        oss << "Failed to initialize debug symbols, error: " << error;
        DebugOutput(oss.str());
    }
}

void CleanupDebugSymbols()
{
    std::lock_guard<std::mutex> lock(g_DebugMutex);
    
    if (g_SymbolsInitialized && g_ProcessHandle)
    {
        SymCleanup(g_ProcessHandle);
        g_SymbolsInitialized = false;
        g_ProcessHandle = nullptr;
        DebugOutput("Debug symbols cleaned up");
    }
}

std::string CaptureStackTrace(int skipFrames, int maxFrames)
{
    std::lock_guard<std::mutex> lock(g_DebugMutex);
    
    if (!g_SymbolsInitialized)
    {
        return "[Stack trace unavailable - symbols not initialized]";
    }
    
    std::ostringstream oss;
    oss << "Stack Trace:\n";
    
    // Capture stack frames
    void* stack[64];
    USHORT frames = CaptureStackBackTrace(skipFrames + 1, min(maxFrames, 64), stack, nullptr);
    
    // Symbol info buffer
    char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbolBuffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    
    // Line info
    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    
    for (USHORT i = 0; i < frames; i++)
    {
        DWORD64 address = (DWORD64)stack[i];
        DWORD64 displacement = 0;
        DWORD lineDisplacement = 0;
        
        oss << "  [" << std::setw(2) << i << "] ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(sizeof(void*) * 2) << address << " ";
        
        if (SymFromAddr(g_ProcessHandle, address, &displacement, symbol))
        {
            oss << symbol->Name;
            
            if (SymGetLineFromAddr64(g_ProcessHandle, address, &lineDisplacement, &line))
            {
                oss << " (" << line.FileName << ":" << std::dec << line.LineNumber << ")";
            }
        }
        else
        {
            // Try to get module name at least
            std::string modName = GetModuleNameForAddress(stack[i]);
            if (!modName.empty())
            {
                oss << "<" << modName << "+0x" << std::hex << (address - (DWORD64)GetModuleHandleA(modName.c_str())) << ">";
            }
            else
            {
                oss << "<unknown>";
            }
        }
        
        oss << std::dec << "\n";
    }
    
    return oss.str();
}

void LogHookOperation(const char* operation, void* target, void* detour, void* original, const char* funcName)
{
    std::ostringstream oss;
    oss << "HOOK " << operation << ": " << (funcName ? funcName : "<unnamed>") << "\n";
    oss << "  Target:   0x" << std::hex << target;
    
    if (target)
    {
        std::string modName = GetModuleNameForAddress(target);
        if (!modName.empty())
            oss << " [" << modName << "]";
        oss << (IsExecutableAddress(target) ? " (executable)" : " (NOT executable!)");
    }
    oss << "\n";
    
    oss << "  Detour:   0x" << std::hex << detour;
    if (detour)
    {
        std::string modName = GetModuleNameForAddress(detour);
        if (!modName.empty())
            oss << " [" << modName << "]";
    }
    oss << "\n";
    
    oss << "  Original: 0x" << std::hex << original << "\n";
    oss << "  Thread:   " << std::dec << GetCurrentThreadId() << "\n";
    
    DebugOutput(oss.str());
}

void LogMinHookStatus(const char* operation, int status, void* target, const char* funcName)
{
    // MinHook status codes
    static const char* statusNames[] = {
        "MH_UNKNOWN",
        "MH_OK",
        "MH_ERROR_ALREADY_INITIALIZED",
        "MH_ERROR_NOT_INITIALIZED",
        "MH_ERROR_ALREADY_CREATED",
        "MH_ERROR_NOT_CREATED",
        "MH_ERROR_ENABLED",
        "MH_ERROR_DISABLED",
        "MH_ERROR_NOT_EXECUTABLE",
        "MH_ERROR_UNSUPPORTED_FUNCTION",
        "MH_ERROR_MEMORY_ALLOC",
        "MH_ERROR_MEMORY_PROTECT",
        "MH_ERROR_MODULE_NOT_FOUND",
        "MH_ERROR_FUNCTION_NOT_FOUND"
    };
    
    // Status is -1 (MH_UNKNOWN) to 12
    const char* statusStr = (status >= -1 && status <= 12) ? statusNames[status + 1] : "INVALID_STATUS";
    
    std::ostringstream oss;
    oss << "MinHook " << operation << ": " << (funcName ? funcName : "<unnamed>") << "\n";
    oss << "  Status: " << statusStr << " (" << status << ")\n";
    oss << "  Target: 0x" << std::hex << target << "\n";
    
    if (status != 0) // MH_OK
    {
        oss << "  !!! OPERATION FAILED !!!\n";
        oss << CaptureStackTrace(2, 16);
    }
    
    
    
    DebugOutput(oss.str());
}

// Check if memory is readable using VirtualQuery (no SEH needed)
bool IsReadableAddress(void* address)
{
    if (address == nullptr)
        return false;
    
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
        return false;
    
    // Check if memory is committed and has read permissions
    if (mbi.State != MEM_COMMIT)
        return false;
    
    // Check for readable protection flags
    DWORD readableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                          PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    
    return (mbi.Protect & readableFlags) != 0 && (mbi.Protect & PAGE_GUARD) == 0;
}

// Safely read a pointer without SEH - uses VirtualQuery to verify first
bool SafeReadPointer(void* address, void** outValue)
{
    if (outValue == nullptr)
        return false;
    
    *outValue = nullptr;
    
    if (!IsReadableAddress(address))
        return false;
    
    // Also verify the entire pointer-sized region is readable
    if (!IsReadableAddress((char*)address + sizeof(void*) - 1))
        return false;
    
    // Memory is verified readable, safe to dereference
    *outValue = *(void**)address;
    return true;
}

// Safely read VTable info without SEH
static bool SafeReadVTableInfo(void* pInterface, int index, void** outVTable, void** outEntry)
{
    if (outVTable) *outVTable = nullptr;
    if (outEntry) *outEntry = nullptr;
    
    if (!IsReadableAddress(pInterface))
        return false;
    
    // Read vtable pointer from interface
    void* vtable = nullptr;
    if (!SafeReadPointer(pInterface, &vtable))
        return false;
    
    if (outVTable) *outVTable = vtable;
    
    if (vtable == nullptr)
        return false;
    
    // Calculate address of vtable entry
    void** vtableEntryAddr = (void**)vtable + index;
    
    // Read the vtable entry
    void* entry = nullptr;
    if (!SafeReadPointer(vtableEntryAddr, &entry))
        return false;
    
    if (outEntry) *outEntry = entry;
    return true;
}

void LogVTableHook(const char* operation, void* pInterface, int index, void* detour, void* original)
{
    std::ostringstream oss;
    oss << "VTABLE " << operation << ":\n";
    oss << "  Interface: 0x" << std::hex << pInterface << "\n";
    oss << "  Index:     " << std::dec << index << "\n";
    oss << "  Detour:    0x" << std::hex << detour << "\n";
    oss << "  Original:  0x" << std::hex << original << "\n";
    oss << "  Thread:    " << std::dec << GetCurrentThreadId() << "\n";
    
    // Validate interface pointer using safe memory probing (no SEH)
    if (pInterface)
    {
        void* vtable = nullptr;
        void* entry = nullptr;
        
        if (SafeReadVTableInfo(pInterface, index, &vtable, &entry))
        {
            oss << "  VTable:    0x" << std::hex << vtable << "\n";
            oss << "  Entry[" << std::dec << index << "]: 0x" << std::hex << entry << "\n";
        }
        else
        {
            oss << "  !!! UNREADABLE MEMORY - VTable access would crash !!!\n";
        }
    }
    
    DebugOutput(oss.str());
}

void LogSymbolResolution(const char* symbolName, void* address, const char* moduleName)
{
    std::ostringstream oss;
    oss << "SYMBOL: " << symbolName << "\n";
    oss << "  Address: 0x" << std::hex << address;
    
    if (address == nullptr)
    {
        oss << " (FAILED TO RESOLVE!)\n";
        oss << CaptureStackTrace(2, 8);
    }
    else
    {
        oss << " [" << (moduleName ? moduleName : "unknown") << "]\n";
        oss << "  Executable: " << (IsExecutableAddress(address) ? "yes" : "NO!");
    }
    oss << "\n";
    
    DebugOutput(oss.str());
}

void LogThreadContext(const char* context)
{
    std::ostringstream oss;
    oss << "THREAD CONTEXT: " << context << "\n";
    oss << "  Thread ID: " << GetCurrentThreadId() << "\n";
    oss << "  Process ID: " << GetCurrentProcessId() << "\n";
    
    // Check if we're in DllMain (loader lock held)
    // This is a heuristic - we check if we're on a thread that looks like it might be holding loader lock
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll)
    {
        typedef NTSTATUS(NTAPI* RtlGetCurrentPeb_t)();
        // We can't easily detect loader lock, but we can warn about early initialization
    }
    
    oss << CaptureStackTrace(2, 12);
    
    DebugOutput(oss.str());
}

bool IsExecutableAddress(void* address)
{
    if (address == nullptr)
        return false;
    
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
        return false;
    
    // Check if the page has execute permissions
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

std::string GetModuleNameForAddress(void* address)
{
    if (address == nullptr)
        return "";
    
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)address, &hModule))
    {
        return "";
    }
    
    char moduleName[MAX_PATH];
    if (GetModuleFileNameA(hModule, moduleName, MAX_PATH) == 0)
        return "";
    
    // Return just the filename, not the full path
    const char* lastSlash = strrchr(moduleName, '\\');
    return lastSlash ? (lastSlash + 1) : moduleName;
}

void LogPointerValidation(const char* name, void* ptr, bool isValid)
{
    std::ostringstream oss;
    oss << "PTR VALIDATE: " << name << "\n";
    oss << "  Address: 0x" << std::hex << ptr << "\n";
    oss << "  Valid: " << (isValid ? "YES" : "NO") << "\n";
    
    if (ptr)
    {
        std::string modName = GetModuleNameForAddress(ptr);
        if (!modName.empty())
            oss << "  Module: " << modName << "\n";
        
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) != 0)
        {
            oss << "  State: ";
            switch (mbi.State)
            {
                case MEM_COMMIT: oss << "COMMIT"; break;
                case MEM_FREE: oss << "FREE"; break;
                case MEM_RESERVE: oss << "RESERVE"; break;
                default: oss << "UNKNOWN"; break;
            }
            oss << "\n";
            
            oss << "  Protect: 0x" << std::hex << mbi.Protect << "\n";
        }
    }
    
    if (!isValid)
    {
        oss << "  !!! INVALID POINTER !!!\n";
        oss << CaptureStackTrace(2, 8);
    }
    
    DebugOutput(oss.str());
}

} // namespace HookDebug
} // namespace InGameOverlay

#endif // _DEBUG

#endif // Windows
