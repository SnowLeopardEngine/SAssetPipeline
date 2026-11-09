-- SAssetPipeline: the asset pipeline of SnowLeopardEngine.
--
-- Targets (Source/xmake.lua):
--   SAssetPipeline        runtime library: cooked asset formats, their readers / writers, the asset registry
--   SAssetPipelineImport  importers (Assimp, stb), import settings (.simport), the import database
--   sap                   command line tool
--
-- The engine either uses the package or includes Source/ directly (`xmake f --sap_dir=<this checkout>` in the
-- engine), so Source/xmake.lua must not depend on anything set here.
set_project("SAssetPipeline")
set_version("0.2.0")
set_languages("cxx20")

option("sap_tests")
    set_default(true)
    set_showmenu(true)
    set_description("Build the SAssetPipeline tests")
option_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

if is_plat("windows") then
    add_cxflags("/utf-8", "/Zc:__cplusplus", {tools = {"msvc", "cl"}})
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

includes("Source")

if has_config("sap_tests") then
    includes("Tests")
end
