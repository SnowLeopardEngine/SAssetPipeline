-- add requirements

-- target defination, name: SCore
target("SCore")
    -- set target kind: static library
    set_kind("static")

    -- add include dir
    add_includedirs("include", { public = true }) -- public: let other targets to auto include

    -- set precompiled header file (PCH)
    set_pcxxheader("include/SAssetPipeline/PrecompiledHeader.h")

    -- add header files
    add_headerfiles("include/(SAssetPipeline/**.h)")

    -- add source files
    add_files("src/**.cpp")

    -- add packages