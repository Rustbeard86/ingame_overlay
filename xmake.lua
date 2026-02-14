-- Build configuration for upstream Nemirtingas/ingame_overlay
-- Adapted from CMakeLists.txt for xmake

-- Architecture-specific defines for all targets
if is_arch("x86") then
    add_defines("_X86_")
end

------------------------------------------------------------------------
-- System library (Nemirtingas utility library)
------------------------------------------------------------------------
target("system_lib")
    set_kind("static")

    add_includedirs("deps/System/include", {public = true})
    add_includedirs("deps/System/deps/utfcpp/include")

    add_files(
        "deps/System/src/CPUExtentions.cpp",
        "deps/System/src/Encoding.cpp",
        "deps/System/src/Filesystem.cpp",
        "deps/System/src/Date.cpp",
        "deps/System/src/Guid.cpp",
        "deps/System/src/Library.cpp",
        "deps/System/src/String.cpp",
        "deps/System/src/System.cpp",
        "deps/System/src/System_internals.cpp",
        "deps/System/src/DotNet.cpp"
    )

    -- Windows extras
    add_files("deps/System/src/WinRegister.cpp")

    add_syslinks("ntdll", "shell32", "advapi32")

------------------------------------------------------------------------
-- mini_detour (function hooking via capstone disassembly)
------------------------------------------------------------------------
target("mini_detour")
    set_kind("static")

    -- Public include dirs (propagated to dependents)
    add_includedirs("deps/mini_detour/include", {public = true})
    add_includedirs("deps/mini_detour/deps/capstone/include", {public = true})

    -- Private include dir (capstone internal headers: cs_priv.h, utils.h, etc.)
    add_includedirs("deps/mini_detour/deps/capstone")

    -- mini_detour source (arch/OS headers are #included conditionally)
    add_files("deps/mini_detour/src/mini_detour.cpp")

    -- Capstone core sources
    add_files(
        "deps/mini_detour/deps/capstone/cs.c",
        "deps/mini_detour/deps/capstone/MCInst.c",
        "deps/mini_detour/deps/capstone/MCInstrDesc.c",
        "deps/mini_detour/deps/capstone/MCRegisterInfo.c",
        "deps/mini_detour/deps/capstone/SStream.c",
        "deps/mini_detour/deps/capstone/utils.c"
    )

    -- Capstone X86 arch sources
    add_files(
        "deps/mini_detour/deps/capstone/arch/X86/X86Disassembler.c",
        "deps/mini_detour/deps/capstone/arch/X86/X86DisassemblerDecoder.c",
        "deps/mini_detour/deps/capstone/arch/X86/X86IntelInstPrinter.c",
        "deps/mini_detour/deps/capstone/arch/X86/X86Mapping.c",
        "deps/mini_detour/deps/capstone/arch/X86/X86Module.c",
        "deps/mini_detour/deps/capstone/arch/X86/X86ATTInstPrinter.c"
    )

    -- Capstone ARM arch sources
    add_files(
        "deps/mini_detour/deps/capstone/arch/ARM/ARMDisassembler.c",
        "deps/mini_detour/deps/capstone/arch/ARM/ARMInstPrinter.c",
        "deps/mini_detour/deps/capstone/arch/ARM/ARMMapping.c",
        "deps/mini_detour/deps/capstone/arch/ARM/ARMModule.c"
    )

    -- Capstone AArch64 arch sources
    add_files(
        "deps/mini_detour/deps/capstone/arch/AArch64/AArch64BaseInfo.c",
        "deps/mini_detour/deps/capstone/arch/AArch64/AArch64Disassembler.c",
        "deps/mini_detour/deps/capstone/arch/AArch64/AArch64InstPrinter.c",
        "deps/mini_detour/deps/capstone/arch/AArch64/AArch64Mapping.c",
        "deps/mini_detour/deps/capstone/arch/AArch64/AArch64Module.c"
    )

    -- Defines (matching upstream CMakeLists.txt)
    add_defines("MINIDETOUR_BUILD")
    add_defines("CAPSTONE_USE_SYS_DYN_MEM", "CAPSTONE_HAS_X86", "CAPSTONE_HAS_ARM64", "CAPSTONE_HAS_ARM")

    -- System libraries (Windows)
    add_syslinks("shell32", "ntdll")

------------------------------------------------------------------------
-- ImGui (Nemirtingas fork with backend extensions)
------------------------------------------------------------------------
target("imgui")
    set_kind("static")
    add_includedirs("deps/ImGui", {public = true})
    add_includedirs("deps/ImGui/backends", "deps/ImGui/misc/cpp")
    add_files("deps/ImGui/*.cpp")
    add_files("deps/ImGui/misc/cpp/*.cpp")

    -- Define necessary macros
    add_defines("IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS")
    add_defines("IMGUI_IMPL_VULKAN_NO_PROTOTYPES")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_CUSTOM")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_GLAD2")
    add_defines("IMGUI_DISABLE_OBSOLETE_KEYIO")
    add_defines("IMGUI_DISABLE_OBSOLETE_FUNCTIONS")
    add_defines("IMGUI_DISABLE_APPLE_GAMEPAD")
    add_includedirs("src/VulkanSDK/include")
    add_includedirs("src/glad2/include")

------------------------------------------------------------------------
-- Main Overlay target
------------------------------------------------------------------------
target("ingame_overlay")
    set_kind("static")
    add_deps("mini_detour", "imgui", "system_lib")

    -- Include directories
    add_includedirs("include", {public = true})
    add_includedirs("deps/ImGui", {public = true})
    add_includedirs("deps/ImGui/backends")
    add_includedirs("deps/ImGui/misc/cpp")
    add_includedirs("src/VulkanSDK/include", {public = true})
    add_includedirs("src/glad2/include")

    -- Source files
    add_files("src/*.cpp")

    -- Windows platform sources
    add_files(
        "src/Windows/DX9Hook.cpp",
        "src/Windows/DX10Hook.cpp",
        "src/Windows/DX11Hook.cpp",
        "src/Windows/DX12Hook.cpp",
        "src/Windows/OpenGLHook.cpp",
        "src/Windows/RendererDetector.cpp",
        "src/Windows/SimpleWindowsGamingInput.cpp",
        "src/Windows/WindowsHook.cpp",
        "src/Windows/VulkanHook.cpp"
    )

    -- ImGui backends for overlay
    add_files("deps/ImGui/backends/imgui_impl_win32.cpp")
    add_files("deps/ImGui/backends/imgui_impl_dx9.cpp")
    add_files("deps/ImGui/backends/imgui_impl_dx10.cpp")
    add_files("deps/ImGui/backends/imgui_impl_dx11.cpp")
    add_files("deps/ImGui/backends/imgui_impl_dx12.cpp")
    add_files("deps/ImGui/backends/imgui_impl_opengl3.cpp")
    add_files("deps/ImGui/backends/imgui_impl_vulkan.cpp")
    add_files("deps/ImGui/backends/imgui_win_shader_blobs.cpp")

    -- ImGui custom config
    add_defines("IMGUI_USER_CONFIG=\"InGameOverlay/ImGuiConfig.h\"")
    add_defines("IMGUI_DISABLE_DEMO_WINDOWS")
    add_defines("IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS")
    add_defines("IMGUI_IMPL_VULKAN_NO_PROTOTYPES")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_CUSTOM")
    add_defines("IMGUI_IMPL_OPENGL_LOADER_GLAD2")
    add_defines("IMGUI_DISABLE_OBSOLETE_KEYIO")
    add_defines("IMGUI_DISABLE_OBSOLETE_FUNCTIONS")
    add_defines("IMGUI_DISABLE_APPLE_GAMEPAD")
    add_includedirs("include/InGameOverlay")

    -- System Libraries
    add_syslinks("user32", "gdi32", "shell32", "opengl32", "d3d9", "d3d10", "d3d11", "d3d12", "dxgi", "d3dcompiler", "dwmapi")

    -- Defines
    add_defines("INGAMEOVERLAY_STATIC")  -- Static library, not DLL
    if is_mode("debug") then
        add_defines("INGAMEOVERLAY_HOOK_DEBUG")
    end
