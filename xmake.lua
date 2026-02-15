includes("lib/commonlibsse")

set_project("hdtSMP64")
set_version("2.5.0")
set_license("MIT")
set_languages("c++23")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("microsoft-detours", "xbyak v7.06")

set_config("commonlib_xbyak", true)

if is_mode("debug") then
    add_defines("_DEBUG")
end

rule("msvc_settings")
    on_config(function (target)
        if is_mode("releasedbg") then
            target:add("cxflags", "cl::/Gy", {force=true})
            if target:kind() ~= "static" then
                target:add("ldflags", "link::-OPT:REF", "link::-OPT:ICF")
            end
        end
    end)

option("avx")
    set_default(false)
    set_showmenu(true)
    set_description("Enable AVX instructions")
    add_defines("USE_AVX")
    add_vectorexts("avx")

option("avx2")
    set_default(false)
    set_showmenu(true)
    set_description("Enable AVX2 instructions")
    add_defines("USE_AVX2")
    add_vectorexts("avx2")

option("avx512")
    set_default(false)
    set_showmenu(true)
    set_description("Enable AVX-512 instructions")
    add_defines("USE_AVX512")
    add_vectorexts("avx512")
   
target("commonlibsse")
    add_rules("msvc_settings")

target("commonlib-shared")
    add_rules("msvc_settings")

target("bullet3")
    set_arch("x64")
    set_kind("static")
    
    add_files("lib/bullet3/src/Bullet3Collision/**.cpp")
    add_files("lib/bullet3/src/Bullet3Dynamics/**.cpp")
    add_files("lib/bullet3/src/BulletCollision/**.cpp")
    add_files("lib/bullet3/src/BulletDynamics/**.cpp")
    add_files("lib/bullet3/src/LinearMath/**.cpp")
    add_includedirs("lib/bullet3/src", {public = true})

    add_rules("msvc_settings")

    add_defines("BT_THREADSAFE", {public = true})
    add_defines("BT_USE_PPL", {public = true})

    add_options("avx", "avx2", "avx512")

target("hdtSSEUtils")
    set_arch("x64")
    set_kind("static")
    
    add_deps("commonlibsse")
    
    add_files("hdtSSEUtils/**.cpp")
    add_headerfiles("hdtSSEUtils/**.h")
    set_pcxxheader("hdtSSEUtils/stdafx.h")

    add_rules("msvc_settings")

    add_options("avx", "avx2", "avx512")

target("hdtSMP64")
    add_rules("commonlibsse.plugin", {
        name = "hdtSMP64",
        author = "hydrogensaysHDT",
        description = "Faster HDT-SMP, ported to CommonLibSSE"
    })
    
    add_deps("hdtSSEUtils")
    add_deps("bullet3")

    add_packages("microsoft-detours", "xbyak")
    
    add_files("hdtSMP64/**.cpp")
    add_headerfiles("hdtSMP64/**.h", "hdtSMP64/**.hpp")
    set_pcxxheader("hdtSMP64/pch.h")

    add_includedirs("hdtSMP64", {public = true})

    add_rules("msvc_settings")

    add_options("avx", "avx2", "avx512")
