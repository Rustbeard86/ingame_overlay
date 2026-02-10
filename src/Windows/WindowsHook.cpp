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

 #include "WindowsHook.h"
 #include "../HookDebug.h"

 #include <imgui.h>
 #include <backends/imgui_impl_win32.h>
 #include <System/Library.h>

 #include "WindowsGamingInputVTables.h"

 extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


 namespace InGameOverlay {

 constexpr decltype(WindowsHook_t::DLL_NAME) WindowsHook_t::DLL_NAME;

 WindowsHook_t* WindowsHook_t::_inst = nullptr;

 // Safe memory validation using VirtualQuery (no SEH to avoid exception handling conflicts)
 static bool IsMemoryReadable(void* address, size_t size = sizeof(void*))
 {
     if (address == nullptr)
         return false;
    
     MEMORY_BASIC_INFORMATION mbi;
     if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
         return false;
    
     if (mbi.State != MEM_COMMIT)
         return false;
    
     DWORD readableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    
     if ((mbi.Protect & readableFlags) == 0 || (mbi.Protect & PAGE_GUARD) != 0)
         return false;
    
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

 // Safe VTable entry extraction using VirtualQuery (no SEH)
 static bool SafeGetVTableEntry(void* pInterface, int index, void** outFuncPtr)
 {
     if (pInterface == nullptr || outFuncPtr == nullptr)
         return false;
    
     // Verify interface pointer is readable
     if (!IsMemoryReadable(pInterface))
         return false;
    
     // Read vtable pointer from interface
     void** vtable = *(void***)pInterface;
     if (vtable == nullptr)
         return false;
    
     // Calculate and verify vtable entry address
     void** vtableEntryAddr = vtable + index;
     if (!IsMemoryReadable(vtableEntryAddr))
         return false;
    
     void* funcPtr = *vtableEntryAddr;
     if (funcPtr == nullptr)
         return false;
    
     *outFuncPtr = funcPtr;
     return true;
 }




static int ToggleKeyToNativeKey(InGameOverlay::ToggleKey k)
{
    struct {
        InGameOverlay::ToggleKey lib_key;
        int native_key;
    } mapping[] = {
        { InGameOverlay::ToggleKey::ALT  , VK_MENU    },
        { InGameOverlay::ToggleKey::CTRL , VK_CONTROL },
        { InGameOverlay::ToggleKey::SHIFT, VK_SHIFT   },
        { InGameOverlay::ToggleKey::TAB  , VK_TAB     },
        { InGameOverlay::ToggleKey::F1   , VK_F1      },
        { InGameOverlay::ToggleKey::F2   , VK_F2      },
        { InGameOverlay::ToggleKey::F3   , VK_F3      },
        { InGameOverlay::ToggleKey::F4   , VK_F4      },
        { InGameOverlay::ToggleKey::F5   , VK_F5      },
        { InGameOverlay::ToggleKey::F6   , VK_F6      },
        { InGameOverlay::ToggleKey::F7   , VK_F7      },
        { InGameOverlay::ToggleKey::F8   , VK_F8      },
        { InGameOverlay::ToggleKey::F9   , VK_F9      },
        { InGameOverlay::ToggleKey::F10  , VK_F10     },
        { InGameOverlay::ToggleKey::F11  , VK_F11     },
        { InGameOverlay::ToggleKey::F12  , VK_F12     },
    };

    for (auto const& item : mapping)
    {
        if (item.lib_key == k)
            return item.native_key;
    }

    return 0;
}

bool WindowsHook_t::StartHook(std::function<void()>& keyCombinationCallback, ToggleKey toggleKeys[], int toggleKeysCount)
{
    HOOK_DEBUG_LOG_THREAD("WindowsHook_t::StartHook - beginning Windows hook initialization");
    
    if (!_Hooked)
    {
        if (!keyCombinationCallback)
        {
            INGAMEOVERLAY_ERROR("Failed to hook Windows: No key combination callback.");
            return false;
        }

        if (toggleKeys == nullptr || toggleKeysCount <= 0)
        {
            INGAMEOVERLAY_ERROR("Failed to hook Windows: No key combination.");
            return false;
        }

        HOOK_DEBUG_LOG_THREAD("Resolving user32.dll handle");
        void* hUser32 = System::Library::GetLibraryHandle(DLL_NAME);
        if (hUser32 == nullptr)
        {
            INGAMEOVERLAY_WARN("Failed to hook Windows: Cannot find {}", DLL_NAME);
            return false;
        }
        HOOK_DEBUG_VALIDATE_PTR("user32.dll handle", hUser32);

        System::Library::Library libUser32;
        LibraryName = System::Library::GetLibraryPath(hUser32);
        HOOK_DEBUG_LOG_THREAD(("Opening library: " + LibraryName).c_str());
        
        if (!libUser32.OpenLibrary(LibraryName, false))
        {
            INGAMEOVERLAY_WARN("Failed to hook Windows: Cannot load {}", LibraryName);
            return false;
        }

        struct {
            void** func_ptr;
            void* hook_ptr;
            const char* func_name;
        } hook_array[] = {
            { (void**)&_TranslateMessage , nullptr                                    , "TranslateMessage"  },
            { (void**)&_DefWindowProcA   , nullptr                                    , "DefWindowProcA"    },
            { (void**)&_DefWindowProcW   , nullptr                                    , "DefWindowProcW"    },
            { (void**)&_GetRawInputBuffer, (void*)&WindowsHook_t::_MyGetRawInputBuffer, "GetRawInputBuffer" },
            { (void**)&_GetRawInputData  , (void*)&WindowsHook_t::_MyGetRawInputData  , "GetRawInputData"   },
            { (void**)&_GetKeyState      , (void*)&WindowsHook_t::_MyGetKeyState      , "GetKeyState"       },
            { (void**)&_GetAsyncKeyState , (void*)&WindowsHook_t::_MyGetAsyncKeyState , "GetAsyncKeyState"  },
            { (void**)&_GetKeyboardState , (void*)&WindowsHook_t::_MyGetKeyboardState , "GetKeyboardState"  },
            { (void**)&_GetCursorPos     , (void*)&WindowsHook_t::_MyGetCursorPos     , "GetCursorPos"      },
            { (void**)&_SetCursorPos     , (void*)&WindowsHook_t::_MySetCursorPos     , "SetCursorPos"      },
            { (void**)&_GetClipCursor    , (void*)&WindowsHook_t::_MyGetClipCursor    , "GetClipCursor"     },
            { (void**)&_ClipCursor       , (void*)&WindowsHook_t::_MyClipCursor       , "ClipCursor"        },
            { (void**)&_GetMessageA      , (void*)&WindowsHook_t::_MyGetMessageA      , "GetMessageA"       },
            { (void**)&_GetMessageW      , (void*)&WindowsHook_t::_MyGetMessageW      , "GetMessageW"       },
            { (void**)&_PeekMessageA     , (void*)&WindowsHook_t::_MyPeekMessageA     , "PeekMessageA"      },
            { (void**)&_PeekMessageW     , (void*)&WindowsHook_t::_MyPeekMessageW     , "PeekMessageW"      },
        };

        HOOK_DEBUG_LOG_THREAD("First pass: resolving all symbols");
        
        // First pass: resolve all symbols before starting any hooks.
        // This ensures we abort early if any critical function cannot be found,
        // preventing a partially hooked state.
        for (auto& entry : hook_array)
        {
            void* symbol = libUser32.GetSymbol<void*>(entry.func_name);
            HOOK_DEBUG_LOG_SYMBOL(entry.func_name, symbol, LibraryName.c_str());
            
            if (symbol == nullptr)
            {
                INGAMEOVERLAY_ERROR("Failed to hook Windows: failed to load function {}.", entry.func_name);
                return false;
            }
            *entry.func_ptr = symbol;
        }

        // All symbols resolved successfully, now safe to proceed with hooking
        INGAMEOVERLAY_INFO("All Windows symbols resolved, proceeding with hooks");
        HOOK_DEBUG_LOG_THREAD("All symbols resolved - proceeding with hook installation");
        
        _KeyCombinationCallback = std::move(keyCombinationCallback);

        for (int i = 0; i < toggleKeysCount; ++i)
        {
            uint32_t k = ToggleKeyToNativeKey(toggleKeys[i]);
            if (k != 0 && std::find(_NativeKeyCombination.begin(), _NativeKeyCombination.end(), k) == _NativeKeyCombination.end())
                _NativeKeyCombination.emplace_back(k);
        }

        // Begin a hook transaction - all hooks will be queued and applied together
        // in EndHook() using MH_ApplyQueued() for thread safety
        HOOK_DEBUG_LOG_THREAD("Beginning hook transaction");
        BeginHook();

        for (auto& entry : hook_array)
        {
            if (entry.hook_ptr != nullptr)
            {
                // Validate that the symbol was actually resolved before hooking
                if (*entry.func_ptr == nullptr)
                {
                    INGAMEOVERLAY_ERROR("Failed to hook {}: symbol not resolved", entry.func_name);
                    HOOK_DEBUG_LOG_OP("SKIP-NULL", nullptr, entry.hook_ptr, nullptr, entry.func_name);
                    continue;
                }
                
                HOOK_DEBUG_LOG_OP("ATTEMPT", *entry.func_ptr, entry.hook_ptr, nullptr, entry.func_name);
                
                if (!HookFunc(std::make_pair(entry.func_ptr, entry.hook_ptr)))
                {
                    INGAMEOVERLAY_ERROR("Failed to hook {}", entry.func_name);
                }
            }
        }

        // Apply all queued hooks atomically
        HOOK_DEBUG_LOG_THREAD("Ending hook transaction - applying all queued hooks");
        EndHook();
        
        // Start WGI hooks after main hooks are applied to avoid race conditions
        HOOK_DEBUG_LOG_THREAD("Starting Windows Gaming Input hooks");
        _StartWGIHook();

        // Now that hooks are applied, it's safe to call hooked functions
        _GetClipCursor(&_SavedClipCursor);
        _GetCursorPos(&_SavedCursorPos);

        INGAMEOVERLAY_INFO("Hooked Windows successfully");
        HOOK_DEBUG_LOG_THREAD("Windows hook initialization complete");
        _Hooked = true;
    }
    return true;
}

void WindowsHook_t::HideAppInputs(bool hide)
{
    if (_ApplicationInputsHidden == hide)
        return;

    _ApplicationInputsHidden = hide;
    if (hide)
    {
        _ClipCursor(&_DefaultClipCursor);
    }
    else
    {
        _ClipCursor(&_SavedClipCursor);
    }
}

void WindowsHook_t::HideOverlayInputs(bool hide)
{
    _OverlayInputsHidden = hide;
}

void WindowsHook_t::ResetRenderState(OverlayHookState state)
{
    if (!_Initialized)
        return;

    _Initialized = false;

    HideAppInputs(false);
    HideOverlayInputs(true);

    ImGui_ImplWin32_Shutdown();
}

void WindowsHook_t::SetInitialWindowSize(HWND hWnd)
{
    RECT rect = { 0, 0, 0, 0 };
    ::GetClientRect(hWnd, &rect);
    ImGui::GetIO().DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
}

short WindowsHook_t::_ImGuiGetKeyState(int nVirtKey)
{
    return WindowsHook_t::Inst()->_GetKeyState(nVirtKey);
}

bool WindowsHook_t::PrepareForOverlay(HWND hWnd)
{
    if (_GameHwnd != hWnd)
        ResetRenderState(OverlayHookState::Removing);

    if (!_Initialized)
    {
        _GameHwnd = hWnd;
        ImGui_ImplWin32_Init(_GameHwnd, &WindowsHook_t::_ImGuiGetKeyState);

        _Initialized = true;
    }

    if (_Initialized)
    {
        if (!_OverlayInputsHidden)
        {
            ImGui_ImplWin32_NewFrame();
        }

        return true;
    }

    return false;
}

std::vector<HWND> WindowsHook_t::FindApplicationHWND(DWORD processId)
{
    struct
    {
        DWORD pid;
        std::vector<HWND> windows;
    } windowParams{
        processId
    };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        if (!IsWindowVisible(hwnd) && !IsIconic(hwnd))
            return TRUE;

        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);

        auto params = reinterpret_cast<decltype(windowParams)*>(lParam);

        if (processId == params->pid)
            params->windows.emplace_back(hwnd);

        return TRUE;
    }, reinterpret_cast<LPARAM>(&windowParams));

    return windowParams.windows;
}

/////////////////////////////////////////////////////////////////////////////////////
// Windows window hooks
static bool IgnoreMsg(UINT uMsg)
{
    switch (uMsg)
    {
        // Mouse Events
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_LBUTTONUP: case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        case WM_MOUSEACTIVATE: case WM_MOUSEHOVER: case WM_MOUSELEAVE:
        // Keyboard Events
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSDEADCHAR:
        case WM_CHAR: case WM_UNICHAR: case WM_DEADCHAR:
        // Raw Input Events
        //case WM_INPUT: // Don't ignore, we will handle it and ignore in GetRawInputBuffer/GetRawInputData
            return true;
    }
    return false;
}

void WindowsHook_t::_AppendEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    _WindowEvents.enqueue(WindowsHookEvent_t(hWnd, uMsg, wParam, lParam));
}

void WindowsHook_t::_RawEvent(RAWINPUT& raw)
{
    switch (raw.header.dwType)
    {
    case RIM_TYPEMOUSE:
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            _AppendEvent(_GameHwnd, WM_LBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            _AppendEvent(_GameHwnd, WM_LBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            _AppendEvent(_GameHwnd, WM_RBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            _AppendEvent(_GameHwnd, WM_RBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            _AppendEvent(_GameHwnd, WM_MBUTTONDOWN, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            _AppendEvent(_GameHwnd, WM_MBUTTONUP, 0, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
            _AppendEvent(_GameHwnd, WM_MOUSEWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);
        if (raw.data.mouse.usButtonFlags & RI_MOUSE_HWHEEL)
            _AppendEvent(_GameHwnd, WM_MOUSEHWHEEL, ((WPARAM)raw.data.mouse.usButtonData) << 16, 0);

        if (raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0)
        {
            POINT p;
            _GetCursorPos(&p);
            ::ScreenToClient(_GameHwnd, &p);
            _AppendEvent(_GameHwnd, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        }
        break;

    //case RIM_TYPEKEYBOARD:
        //_AppendEvent(_GameHwnd, raw.data.keyboard.Message, raw.data.keyboard.VKey, 0);
        //break;
    }
}

bool WindowsHook_t::_HandleEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    bool hide_app_inputs = _ApplicationInputsHidden;
    bool hide_overlay_inputs = _OverlayInputsHidden;
    
    if (_Initialized)
    {
        // Is the event is a key press
        if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
        {
            int key_count = 0;
            for (auto const& key : _NativeKeyCombination)
            {
                if (_GetAsyncKeyState(key) & (1 << 15))
                    ++key_count;
            }
    
            if (key_count == _NativeKeyCombination.size())
            {// All shortcut keys are pressed
                if (!_KeyCombinationPushed)
                {
                    _KeyCombinationCallback();
    
                    if (_OverlayInputsHidden)
                        hide_overlay_inputs = true;
    
                    if(_ApplicationInputsHidden)
                    {
                        hide_app_inputs = true;
    
                        // Save the last known cursor pos when opening the overlay
                        // so we can spoof the GetCursorPos return value.
                        _GetCursorPos(&_SavedCursorPos);
                        _GetClipCursor(&_SavedClipCursor);
                    }
                    else
                    {
                        _ClipCursor(&_SavedClipCursor);
                    }
                    _KeyCombinationPushed = true;
                }
            }
            else
            {
                _KeyCombinationPushed = false;
            }
        }
    
        if (uMsg == WM_KILLFOCUS || uMsg == WM_SETFOCUS)
            ImGui::GetIO().SetAppAcceptingEvents(uMsg == WM_SETFOCUS);
    
        WindowsHookEvent_t rawEvent;
        size_t eventCount = _WindowEvents.queue_size();
        while (eventCount-- && _WindowEvents.dequeue(rawEvent))
            ImGui_ImplWin32_WndProcHandler(rawEvent.hWnd, rawEvent.msg, rawEvent.wParam, rawEvent.lParam);

        if (!hide_overlay_inputs || uMsg == WM_KILLFOCUS || uMsg == WM_SETFOCUS)
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        }
    
        if (hide_app_inputs && IgnoreMsg(uMsg))
            return true;
    }
    
    return false;
}

UINT WINAPI WindowsHook_t::_MyGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_GetRawInputBuffer == nullptr)
        return 0;
    
    int res = inst->_GetRawInputBuffer(pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized)
        return res;

    if (!inst->_OverlayInputsHidden && pData != nullptr)
    {
        for (int i = 0; i < res; ++i)
            inst->_RawEvent(pData[i]);
    }

    if (!inst->_ApplicationInputsHidden)
        return res;

    return 0;
}

UINT WINAPI WindowsHook_t::_MyGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_GetRawInputData == nullptr)
        return (UINT)-1;
    
    auto res = inst->_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
    if (!inst->_Initialized || pData == nullptr)
        return res;

    if (!inst->_OverlayInputsHidden && uiCommand == RID_INPUT && res == sizeof(RAWINPUT))
        inst->_RawEvent(*reinterpret_cast<RAWINPUT*>(pData));

    if (!inst->_ApplicationInputsHidden)
        return res;

    memset(pData, 0, *pcbSize);
    *pcbSize = 0;
    return 0;
}

SHORT WINAPI WindowsHook_t::_MyGetKeyState(int nVirtKey)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    // Safety check: ensure the original function pointer is valid
    if (inst->_GetKeyState == nullptr)
        return 0;

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return 0;

    return inst->_GetKeyState(nVirtKey);
}

SHORT WINAPI WindowsHook_t::_MyGetAsyncKeyState(int vKey)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    // Safety check: ensure the original function pointer is valid
    if (inst->_GetAsyncKeyState == nullptr)
        return 0;

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return 0;

    return inst->_GetAsyncKeyState(vKey);
}

BOOL WINAPI WindowsHook_t::_MyGetKeyboardState(PBYTE lpKeyState)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    // Safety check: ensure the original function pointer is valid
    if (inst->_GetKeyboardState == nullptr)
        return FALSE;

    if (inst->_Initialized && inst->_ApplicationInputsHidden)
        return FALSE;

    return inst->_GetKeyboardState(lpKeyState);
}

BOOL  WINAPI WindowsHook_t::_MyGetCursorPos(LPPOINT lpPoint)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_GetCursorPos == nullptr)
        return FALSE;
    
    BOOL res = inst->_GetCursorPos(lpPoint);
    if (inst->_Initialized && inst->_ApplicationInputsHidden && lpPoint != nullptr)
    {
        *lpPoint = inst->_SavedCursorPos;
    }

    return res;
}

BOOL WINAPI WindowsHook_t::_MySetCursorPos(int X, int Y)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    // Safety check: ensure the original function pointer is valid
    if (inst->_SetCursorPos == nullptr)
        return FALSE;

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_SetCursorPos(X, Y);

    return TRUE;
}

BOOL WINAPI WindowsHook_t::_MyGetClipCursor(RECT* lpRect)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_GetClipCursor == nullptr)
        return FALSE;
    
    if (lpRect == nullptr || !inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_GetClipCursor(lpRect);

    *lpRect = inst->_SavedClipCursor;
    return TRUE;
}

BOOL WINAPI WindowsHook_t::_MyClipCursor(CONST RECT* lpRect)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_ClipCursor == nullptr)
        return FALSE;
    
    CONST RECT* v = lpRect == nullptr ? &inst->_DefaultClipCursor : lpRect;

    inst->_SavedClipCursor = *v;

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return inst->_ClipCursor(v);
    
    return inst->_ClipCursor(&inst->_DefaultClipCursor);
}

BOOL WINAPI WindowsHook_t::_MyGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_GetMessageA == nullptr)
        return FALSE;
    
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageA(lpMsg);
    inst->_DefWindowProcA(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

BOOL WINAPI WindowsHook_t::_MyGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_GetMessageW == nullptr)
        return FALSE;
    
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageW(lpMsg);
    inst->_DefWindowProcW(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

BOOL WINAPI WindowsHook_t::_MyPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_PeekMessageA == nullptr)
        return FALSE;
    
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!(wRemoveMsg & PM_REMOVE) && inst->_ApplicationInputsHidden && IgnoreMsg(lpMsg->message))
    {
        // Remove message from queue
        inst->_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE | (wRemoveMsg & (~PM_REMOVE)));
    }

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageA(lpMsg);
    inst->_DefWindowProcA(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

BOOL WINAPI WindowsHook_t::_MyPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();
    
    // Safety check: ensure the original function pointer is valid
    if (inst->_PeekMessageW == nullptr)
        return FALSE;
    
    // Force filters to 0 ?
    wMsgFilterMin = 0;
    wMsgFilterMax = 0;
    BOOL res = inst->_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    if (!inst->_Initialized || lpMsg == nullptr || res == FALSE)
        return res;

    if (!(wRemoveMsg & PM_REMOVE) && inst->_ApplicationInputsHidden && IgnoreMsg(lpMsg->message))
    {
        // Remove message from queue
        inst->_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE | (wRemoveMsg & (~PM_REMOVE)));
    }

    if (!inst->_HandleEvent(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return res;

    inst->_TranslateMessage(lpMsg);
    //inst->_DispatchMessageW(lpMsg);
    inst->_DefWindowProcW(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    lpMsg->message = 0;
    return res;
}

// Windows::Gaming::Input
void WindowsHook_t::_StartWGIHook()
{
    _RawControllerStatics = SimpleWindowsGamingInput::GetRawGameControllerStatics();
    _GamepadStatics = SimpleWindowsGamingInput::GetGamepadStatics();
    
    if (_RawControllerStatics == nullptr || _GamepadStatics == nullptr)
        return;

    auto hookedRaw = false;
    SimpleWindowsGamingInput::VectorView<SimpleWindowsGamingInput::IRawGameController*>* rawControllers = nullptr;
    if (SUCCEEDED(_RawControllerStatics->get_RawGameControllers(&rawControllers)) && rawControllers != nullptr)
    {
        unsigned int s;
        if (SUCCEEDED(rawControllers->get_Size(&s)) && s > 0)
        {
            SimpleWindowsGamingInput::IRawGameController* rawController = nullptr;
            if (SUCCEEDED(rawControllers->GetAt(0, &rawController)) && rawController != nullptr)
            {
                _StartRawControllerHook(rawController);
                hookedRaw = true;

                rawController->Release();
            }
        }

        rawControllers->Release();
    }

    auto hookedGamepad = false;
    SimpleWindowsGamingInput::VectorView<SimpleWindowsGamingInput::IGamepad*>* gamepads = nullptr;
    if (SUCCEEDED(_GamepadStatics->get_Gamepads(&gamepads)) && gamepads != nullptr)
    {
        unsigned int s;
        if (SUCCEEDED(gamepads->get_Size(&s)) && s > 0)
        {
            SimpleWindowsGamingInput::IGamepad* gamepad = nullptr;
            if (SUCCEEDED(gamepads->GetAt(0, &gamepad)) && gamepad != nullptr)
            {
                _StartGamepadHook(gamepad);
                hookedGamepad = true;

                gamepad->Release();
            }
        }

        gamepads->Release();
    }

    if (!hookedRaw)
        _RawControllerStatics->add_RawGameControllerAdded(&_RawControllerAddedHandler, &_OnRawControllerAddedToken);
    
    if (!hookedGamepad)
        _GamepadStatics->add_GamepadAdded(&_GamepadAddedHandler, &_OnGamepadAddedToken);
}

void WindowsHook_t::_StartRawControllerHook(SimpleWindowsGamingInput::IRawGameController* pRawController)
{
    // Validate the controller pointer before accessing its vtable
    if (pRawController == nullptr)
    {
        INGAMEOVERLAY_WARN("Failed to hook RawController: null controller pointer");
        return;
    }

    // Use SEH-safe helper to extract the vtable function pointer
    void* targetFunc = nullptr;
    if (!SafeGetVTableEntry(pRawController, (int)IRawGameControllerVTable::GetCurrentReading, &targetFunc))
    {
        INGAMEOVERLAY_WARN("Failed to hook RawController: could not get GetCurrentReading from vtable");
        return;
    }
    
    *(void**)&_RawControllerGetCurrentReading = targetFunc;

    BeginHook();

    if (!HookFunc(std::make_pair((void**)&_RawControllerGetCurrentReading, (void*)&WindowsHook_t::_MyRawControllerGetCurrentReading)))
    {
        INGAMEOVERLAY_ERROR("Failed to hook RawController::GetCurrentReading");
    }

    EndHook();
}

void WindowsHook_t::_StartGamepadHook(SimpleWindowsGamingInput::IGamepad* pGamepad)
{
    // Validate the gamepad pointer before accessing its vtable
    if (pGamepad == nullptr)
    {
        INGAMEOVERLAY_WARN("Failed to hook Gamepad: null gamepad pointer");
        return;
    }

    // Use SEH-safe helper to extract the vtable function pointer
    void* targetFunc = nullptr;
    if (!SafeGetVTableEntry(pGamepad, (int)IGamepadVTable::GetCurrentReading, &targetFunc))
    {
        INGAMEOVERLAY_WARN("Failed to hook Gamepad: could not get GetCurrentReading from vtable");
        return;
    }
    
    *(void**)&_GamepadGetCurrentReading = targetFunc;

    BeginHook();

    if (!HookFunc(std::make_pair((void**)&_GamepadGetCurrentReading, (void*)&WindowsHook_t::_MyGamepadGetCurrentReading)))
    {
        INGAMEOVERLAY_ERROR("Failed to hook Gamepad::GetCurrentReading");
    }

    EndHook();
}

HRESULT STDMETHODCALLTYPE WindowsHook_t::_MyRawControllerGetCurrentReading(SimpleWindowsGamingInput::IRawGameController* _this, UINT32 buttonArrayLength, boolean* buttonArray, UINT32 switchArrayLength, SimpleWindowsGamingInput::GameControllerSwitchPosition* switchArray, UINT32 axisArrayLength, DOUBLE* axisArray, UINT64* timestamp)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    // Safety check: ensure the original function pointer is valid
    if (inst->_RawControllerGetCurrentReading == nullptr)
        return E_FAIL;

    auto result = (_this->*inst->_RawControllerGetCurrentReading)(buttonArrayLength, buttonArray, switchArrayLength, switchArray, axisArrayLength, axisArray, timestamp);

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return result;

    if (buttonArray != nullptr)
        memset(buttonArray, 0, sizeof(*buttonArray) * buttonArrayLength);

    if (switchArray != nullptr)
        memset(switchArray, 0, sizeof(*switchArray) * switchArrayLength);

    if (axisArray != nullptr)
        memset(axisArray, 0, sizeof(axisArray) * axisArrayLength);

    if (timestamp != nullptr)
        *timestamp = 0;

    return result;
}

HRESULT STDMETHODCALLTYPE WindowsHook_t::_MyGamepadGetCurrentReading(SimpleWindowsGamingInput::IGamepad* _this, SimpleWindowsGamingInput::GamepadReading* value)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    // Safety check: ensure the original function pointer is valid
    if (inst->_GamepadGetCurrentReading == nullptr)
        return E_FAIL;

    auto result = (_this->*inst->_GamepadGetCurrentReading)(value);

    if (!inst->_Initialized || !inst->_ApplicationInputsHidden)
        return result;

    if (value != nullptr)
        memset(value, 0, sizeof(*value));

    return result;
}

HRESULT WindowsHook_t::_OnRawControllerAdded(SimpleWindowsGamingInput::IInspectable*, SimpleWindowsGamingInput::IRawGameController* pRawController)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    inst->_RawControllerStatics->remove_RawGameControllerAdded(inst->_OnRawControllerAddedToken);
    inst->_OnRawControllerAddedToken = {};

    inst->_StartRawControllerHook(pRawController);

    return S_OK;
}

HRESULT WindowsHook_t::_OnGamepadAdded(SimpleWindowsGamingInput::IInspectable*, SimpleWindowsGamingInput::IGamepad* pGamepad)
{
    WindowsHook_t* inst = WindowsHook_t::Inst();

    inst->_GamepadStatics->remove_GamepadAdded(inst->_OnGamepadAddedToken);
    inst->_OnGamepadAddedToken = {};

    inst->_StartGamepadHook(pGamepad);

    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////////////

WindowsHook_t::WindowsHook_t() :
    _Hooked(false),
    _Initialized(false),
    _GameHwnd(nullptr),
    _SavedCursorPos{},
    _SavedClipCursor{},
    _DefaultClipCursor{ LONG(0xFFFF8000), LONG(0xFFFF8000), LONG(0x00007FFF), LONG(0x00007FFF) },
    _ApplicationInputsHidden(false),
    _OverlayInputsHidden(true),
    _KeyCombinationPushed(false),
    _WindowEvents(512),
    _TranslateMessage(nullptr),
    _DefWindowProcA(nullptr),
    _DefWindowProcW(nullptr),
    _GetRawInputBuffer(nullptr),
    _GetRawInputData(nullptr),
    _GetKeyState(nullptr),
    _GetAsyncKeyState(nullptr),
    _GetKeyboardState(nullptr),
    _GetCursorPos(nullptr),
    _SetCursorPos(nullptr),
    _GetClipCursor(nullptr),
    _ClipCursor(nullptr),
    _GetMessageA(nullptr),
    _GetMessageW(nullptr),
    _PeekMessageA(nullptr),
    _PeekMessageW(nullptr),
    // WGI
    _OnRawControllerAddedToken({}),
    _OnGamepadAddedToken({}),
    _RawControllerAddedHandler(&WindowsHook_t::_OnRawControllerAdded),
    _GamepadAddedHandler(&WindowsHook_t::_OnGamepadAdded),
    _RawControllerGetCurrentReading(nullptr),
    _GamepadGetCurrentReading(nullptr)
{
}

WindowsHook_t::~WindowsHook_t()
{
    INGAMEOVERLAY_INFO("Windows Hook removed");

    ResetRenderState(OverlayHookState::Removing);

    //if (_OnRawControllerAddedToken.value != 0)
    //{
    //    _RawControllerStatics->remove_RawGameControllerAdded(_OnRawControllerAddedToken);
    //    _OnRawControllerAddedToken = {};
    //    _RawControllerAddedHandler.ReleaseAndGetAddressOf();
    //}
    //if (_OnGamepadAddedToken.value != 0)
    //{
    //    _GamepadStatics->remove_GamepadAdded(_OnGamepadAddedToken);
    //    _OnGamepadAddedToken = {};
    //    _GamepadAddedHandler.ReleaseAndGetAddressOf();
    //}

    UnhookAll();
    _inst = nullptr;
}

WindowsHook_t* WindowsHook_t::Inst()
{
    if (_inst == nullptr)
        _inst = new WindowsHook_t;

    return _inst;
}

const char* WindowsHook_t::GetLibraryName() const
{
    return LibraryName.c_str();
}

}//namespace InGameOverlay