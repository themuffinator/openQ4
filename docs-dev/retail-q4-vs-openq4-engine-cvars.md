# Retail Q4 vs OpenQ4 Engine CVar Divergences

Generated on 2026-04-19.

## Scope

- Retail exact names were resolved from engine-owned `idCVar` symbols recovered from `E:\Repositories\Quake4Decompiled-main`, then matched against live retail Q4 runtime cvars.
- OpenQ4 exact names were taken from `idCVar` constructor definitions in `src/`, with `r_minDisplayRefresh` and `r_maxDisplayRefresh` supplemented from the earlier extraction because the simple parser misses those two render declarations.
- Shared metadata diffs compare retail dedicated runtime metadata against OpenQ4 dedicated runtime metadata, with OpenQ4 client runtime used only as a fallback for BSE cvars that are intentionally not registered in the dedicated path.
- `flags` come from `listCvars -flags`, after dropping the transient `MO` marker and the leading storage-type token (`B` / `I` / `F` / `S`).
- `type/range` is the exact `listCvars -type` output, so enum domains and numeric min/max ranges show up there when the engine reports them.
- The four retail decomp symbols that could not be matched to live retail runtime cvars are listed at the end and were excluded from the exact-name diff.

## Summary

| Category | Count |
| --- | ---: |
| Retail-only exact engine-side cvars | 131 |
| OpenQ4-only exact engine-side cvars | 212 |
| Shared exact names with metadata differences | 51 |

## 1. Retail-Only

| CVar | Retail symbol(s) | Retail source(s) |
| --- | --- | --- |
| `bse_showBounds` | bse_showBounds | bse/bse_effect.cpp:651 |
| `cm_debugRotation` | cm_debugRotation | cm/collisionmodel_debug.cpp:346 |
| `cm_debugTranslation` | cm_debugTranslation | cm/collisionmodel_debug.cpp:274 |
| `cm_drawIndexScale` | cm_drawIndexScale | cm/collisionmodel_debug.cpp:212<br>cm/collisionmodel_debug.cpp:232<br>cm/collisionmodel_debug.cpp:253<br>`+1 more` |
| `cm_drawIndices` | cm_drawIndices | cm/collisionmodel_debug.cpp:209<br>cm/collisionmodel_debug.cpp:668 |
| `cm_testTranslation` | cm_testTranslation | cm/collisionmodel_debug.cpp:1097 |
| `com_Bundler` | com_Bundler | framework/common.cpp:4247<br>framework/common.cpp:4771<br>framework/common.cpp:4774<br>`+16 more` |
| `com_Limits` | com_Limits | renderer/rendersystem.cpp:1420<br>renderer/rendersystem.cpp:1702<br>renderer/rendersystem.cpp:1705 |
| `com_MakeLoadScreens` | com_MakeLoadScreens | framework/common.cpp:4304 |
| `com_QuickLoad` | com_QuickLoad | renderer/image_init.cpp:4231<br>renderer/tr_deform.cpp:1923<br>sound/default/snd_cache.cpp:501<br>`+1 more` |
| `com_SingleDeclFile` | com_SingleDeclFile | framework/declmanager.cpp:1431<br>framework/declmanager.cpp:1663<br>renderer/guimodel.cpp:841 |
| `com_allowBadSavegameVersion` | idSessionLocal::com_allowBadSavegameVersion | framework/session_save.cpp:1014<br>framework/session_save.cpp:1026 |
| `com_lastQuicksave` | idSessionLocal::com_lastQuicksave | framework/session_save.cpp:1074<br>framework/session_save.cpp:1131<br>framework/session_save.cpp:1132<br>`+3 more` |
| `com_skipLevelLoadPause` | idSessionLocal::com_skipLevelLoadPause | framework/session.cpp:4464 |
| `com_syncGameFrame` | idSessionLocal::com_syncGameFrame | framework/async/asyncserver.cpp:5193<br>framework/session.cpp:5092 |
| `com_uniqueWarnings` | com_uniqueWarnings | framework/common.cpp:2960 |
| `demo_debug` | idDemo::debug | framework/async/asyncclient.cpp:826<br>framework/async/demo.cpp:184<br>framework/async/demo.cpp:192<br>`+4 more` |
| `demo_scale` | idDemo::scale | framework/async/asyncclient.cpp:1600<br>framework/async/asyncclient.cpp:1603<br>framework/async/asyncclient.cpp:1605 |
| `fs_devpath` | idFileSystemLocal::fs_devpath | framework/filesystem.cpp:11444<br>framework/filesystem.cpp:11445<br>framework/filesystem.cpp:11446<br>`+3 more` |
| `fs_importpath` | idFileSystemLocal::fs_importpath | framework/filesystem.cpp:1052<br>framework/filesystem.cpp:1053<br>framework/filesystem.cpp:1060<br>`+1 more` |
| `g_favoritesList` | g_favoritesList | framework/async/serverscan.cpp:332<br>framework/async/serverscan.cpp:341<br>framework/async/serverscan.cpp:343<br>`+4 more` |
| `g_friendsList` | g_friendsList | framework/async/serverscan.cpp:2217<br>framework/async/serverscan.cpp:2275<br>framework/async/serverscan.cpp:2312<br>`+13 more` |
| `image_anisotropy` | image_anisotropy | renderer/image_init.cpp:1754<br>renderer/image_init.cpp:1759<br>renderer/image_init.cpp:923<br>`+2 more` |
| `image_cacheMegs` | image_cacheMegs | renderer/image_init.cpp:1641 |
| `image_cacheMinK` | image_cacheMinK | renderer/image_load.cpp:2278<br>renderer/image_load.cpp:2293 |
| `image_colorMipLevels` | image_colorMipLevels | renderer/image_load.cpp:2706 |
| `image_downSize` | image_downSize | renderer/image_init.cpp:1917<br>renderer/image_init.cpp:1918<br>renderer/tr_subview.cpp:529 |
| `image_downSizeLimit` | image_downSizeLimit | renderer/tr_subview.cpp:536<br>renderer/tr_subview.cpp:537<br>renderer/tr_subview.cpp:543<br>`+1 more` |
| `image_filter` | image_filter | renderer/image_init.cpp:1753<br>renderer/image_init.cpp:1758<br>renderer/image_init.cpp:922<br>`+1 more` |
| `image_ignoreHighQuality` | image_ignoreHighQuality | renderer/material.cpp:2211<br>renderer/material.cpp:3206<br>renderer/shaders.cpp:1000 |
| `image_lodbias` | image_lodbias | renderer/image_init.cpp:1755<br>renderer/image_init.cpp:1760<br>renderer/image_init.cpp:924<br>`+1 more` |
| `image_preload` | image_preload | renderer/image_init.cpp:1352<br>renderer/image_init.cpp:4222<br>renderer/image_init.cpp:4322<br>`+1 more` |
| `image_showBackgroundLoads` | image_showBackgroundLoads | renderer/image_init.cpp:1602<br>renderer/image_init.cpp:1649<br>renderer/image_init.cpp:1721<br>`+1 more` |
| `image_useCache` | image_useCache | renderer/image_load.cpp:2274 |
| `image_useNormalCompression` | image_useNormalCompression | renderer/draw_r200.cpp:13<br>renderer/image_init.cpp:840<br>renderer/image_init.cpp:843<br>`+5 more` |
| `image_useNormalCompressionLoadDDSForPal` | image_useNormalCompressionLoadDDSForPal | renderer/image_load.cpp:2318 |
| `image_useOfflineCompression` | image_useOffLineCompression | renderer/image_load.cpp:1635 |
| `image_usePrecompressedTextures` | image_usePrecompressedTextures | renderer/image_load.cpp:1532<br>renderer/image_load.cpp:2215<br>renderer/image_load.cpp:2329<br>`+4 more` |
| `image_writeNormalTGA` | image_writeNormalTGA | renderer/image_load.cpp:2650 |
| `image_writePrecompressedTextures` | image_writePrecompressedTextures | renderer/image_load.cpp:1531<br>renderer/image_load.cpp:2214 |
| `image_writeProgramImages` | image_writeProgramImages | framework/declmanager.cpp:3961<br>renderer/image_files.cpp:539<br>renderer/image_load.cpp:2216<br>`+6 more` |
| `image_writeTGA` | image_writeTGA | renderer/image_load.cpp:2245<br>renderer/image_load.cpp:2653 |
| `in_joystickLeftStickMove` | idUsercmdGenLocal::in_joystickLeftStickMove | framework/usercmdgen.cpp:906<br>framework/usercmdgen.cpp:912<br>framework/usercmdgen.cpp:918<br>`+1 more` |
| `m_maxInput` | idUsercmdGenLocal::m_maxInput | framework/usercmdgen.cpp:552<br>framework/usercmdgen.cpp:556<br>framework/usercmdgen.cpp:557<br>`+2 more` |
| `mat_useHitMaterials` | mat_useHitMaterials | framework/declmattype.cpp:94 |
| `mat_writeHitMaterials` | mat_writeHitMaterials | framework/declmattype.cpp:149 |
| `net_debugClient` | idAsyncNetwork::debugClient | framework/async/asyncclient.cpp:2057<br>framework/async/asyncclient.cpp:2093<br>framework/async/asyncclient.cpp:2118<br>`+1 more` |
| `net_debugStartLevel` | idAsyncNetwork::debugStartLevel | framework/async/asyncclient.cpp:4854<br>framework/async/asyncclient.cpp:4872<br>framework/async/asyncserver.cpp:4841<br>`+1 more` |
| `net_forceInternet` | idAsyncNetwork::forceInternet | framework/async/asyncserver.cpp:1261 |
| `r_actualRenderer` | r_actualRenderer | renderer/rendersystem.cpp:945<br>renderer/rendersystem.cpp:951<br>renderer/rendersystem.cpp:958<br>`+3 more` |
| `r_alphaToCoverage` | r_alphaToCoverage | renderer/draw_common.cpp:505 |
| `r_aspectRatio` | r_aspectRatio | renderer/rendersystem.cpp:1743 |
| `r_convertMD5toMD5R` | r_convertMD5toMD5R | renderer/modelmanager.cpp:1033 |
| `r_convertProcToMD5R` | r_convertProcToMD5R | renderer/renderworld_load.cpp:2326<br>renderer/renderworld_load.cpp:2392<br>renderer/renderworld_load.cpp:2461 |
| `r_convertStaticToMD5R` | r_convertStaticToMD5R | renderer/modelmanager.cpp:1075 |
| `r_debugSphereSubdivision` | r_debugSphereSubdivision | renderer/rendersystem_init.cpp:5206<br>renderer/renderworld.cpp:3844<br>renderer/renderworld.cpp:4045<br>`+2 more` |
| `r_deriveBiTangents` | r_deriveBiTangents | renderer/model_md5.cpp:563 |
| `r_drawBoundInfo` | r_drawBoundInfo | renderer/rendersystem_init.cpp:5281<br>renderer/renderworld.cpp:4219 |
| `r_forceConvertMD5R` | r_forceConvertMD5R | renderer/modelmanager.cpp:933<br>renderer/renderworld_load.cpp:2209 |
| `r_forceDiffuseOnly` | r_forceDiffuseOnly | renderer/draw_nv10.cpp:106<br>renderer/draw_nv20.cpp:245 |
| `r_inhibitNativePowerOfTwo` | r_inhibitNativePowerOfTwo | renderer/rendersystem_init.cpp:280<br>renderer/rendersystem_init.cpp:5476 |
| `r_limitBatchSize` | r_limitBatchSize | renderer/rendersystem_init.cpp:4966<br>renderer/tr_light.cpp:563 |
| `r_lod_animations_coverage` | r_lod_animations_coverage | renderer/model_md5.cpp:52<br>renderer/rendersystem_init.cpp:5581 |
| `r_lod_animations_distance` | r_lod_animations_distance | renderer/model_md5.cpp:45<br>renderer/model_md5.cpp:48<br>renderer/rendersystem_init.cpp:5551 |
| `r_lod_animations_wait` | r_lod_animations_wait | renderer/model_md5.cpp:47<br>renderer/rendersystem_init.cpp:5566 |
| `r_lod_entities` | r_lod_entities | renderer/rendersystem_init.cpp:5506<br>renderer/tr_light.cpp:1634 |
| `r_lod_entities_percent` | r_lod_entities_percent | renderer/rendersystem_init.cpp:5536<br>renderer/tr_light.cpp:1636 |
| `r_lod_shadows` | r_lod_shadows | renderer/interaction.cpp:1068<br>renderer/interaction.cpp:1095<br>renderer/rendersystem_init.cpp:5491 |
| `r_lod_shadows_percent` | r_lod_shadows_percent | renderer/interaction.cpp:1091<br>renderer/rendersystem_init.cpp:5521 |
| `r_penumbraMapDepthBias` | r_penumbraMapDepthBias | renderer/rendersystem_init.cpp:4321 |
| `r_shadowMapDepthBias` | r_shadowMapDepthBias | renderer/rendersystem_init.cpp:4291 |
| `r_shadowMapSlopeScaleBias` | r_shadowMapSlopeScaleBias | renderer/rendersystem_init.cpp:4306 |
| `r_showBatchSize` | r_showBatchSize | renderer/rendersystem_init.cpp:4951<br>renderer/tr_rendertools.cpp:777<br>renderer/tr_rendertools.cpp:785<br>`+1 more` |
| `r_showDebugGraph` | r_showDebugGraph | renderer/rendersystem.cpp:1272<br>renderer/rendersystem_init.cpp:4921 |
| `r_showEditorImages` | r_showEditorImages | renderer/draw_common.cpp:1871<br>renderer/rendersystem_init.cpp:5656 |
| `r_showHitImages` | r_showHitImages | renderer/rendersystem_init.cpp:5671<br>renderer/tr_rendertools.cpp:302<br>renderer/tr_rendertools.cpp:333 |
| `r_showLightPortals` | r_showLightPortals | renderer/rendersystem_init.cpp:5641<br>renderer/tr_rendertools.cpp:2134 |
| `r_showMegaTexture` | idMegaTexture::r_showMegaTexture | renderer/megatexture.cpp:1576 |
| `r_showMegaTextureLabels` | idMegaTexture::r_showMegaTextureLabels | renderer/megatexture.cpp:312<br>renderer/megatexture.cpp:679<br>renderer/megatexture.cpp:681 |
| `r_showOverdrawDivisor` | r_showOverdrawDivisor | renderer/rendersystem_init.cpp:5611<br>renderer/tr_rendertools.cpp:828<br>renderer/tr_rendertools.cpp:867<br>`+1 more` |
| `r_showOverdrawMax` | r_showOverdrawMax | renderer/rendersystem_init.cpp:5596<br>renderer/tr_rendertools.cpp:827<br>renderer/tr_rendertools.cpp:864<br>`+2 more` |
| `r_showRenderTrace` | r_showRenderTrace | renderer/rendersystem.cpp:591<br>renderer/rendersystem_init.cpp:4681 |
| `r_showSafeArea` | r_showSafeArea | renderer/rendersystem.cpp:1738<br>renderer/rendersystem_init.cpp:5221 |
| `r_showTriangleTangents` | r_showTriangleTangents | renderer/rendersystem_init.cpp:5131<br>renderer/tr_rendertools.cpp:2940<br>renderer/tr_rendertools.cpp:3064 |
| `r_showUnweld` | r_showUnweld | renderer/rendersystem_init.cpp:5626<br>renderer/tr_rendertools.cpp:1027<br>renderer/tr_rendertools.cpp:952 |
| `r_skipDecals` | r_skipDecals | renderer/draw_common.cpp:1905<br>renderer/modeldecal.cpp:1330<br>renderer/rendersystem_init.cpp:5446 |
| `r_skipDownsize` | r_skipDownsize | renderer/image_program.cpp:759<br>renderer/rendersystem_init.cpp:3751 |
| `r_skipMegaTexture` | idMegaTexture::r_skipMegaTexture | renderer/megatexture.cpp:715 |
| `r_skipTextures` | r_skipTextures | renderer/rendersystem_init.cpp:2769<br>renderer/rendersystem_init.cpp:2777<br>renderer/rendersystem_init.cpp:5686<br>`+4 more` |
| `r_suppressMultipleUpdates` | r_suppressMultipleUpdates | renderer/rendersystem_init.cpp:5461<br>renderer/renderworld.cpp:2324<br>renderer/renderworld.cpp:2454<br>`+2 more` |
| `r_terrainScale` | idMegaTexture::r_terrainScale | renderer/megatexture.cpp:1604 |
| `r_test` | r_test | renderer/rendersystem_init.cpp:5431 |
| `r_testSpecialEffect` | r_testSpecialEffect | renderer/rendersystem.cpp:662<br>renderer/rendersystem.cpp:663<br>renderer/rendersystem.cpp:665<br>`+4 more` |
| `r_testSpecialEffectParm` | r_testSpecialEffectParm | renderer/rendersystem.cpp:672<br>renderer/rendersystem_init.cpp:3781 |
| `r_testSpecialEffectParmValue` | r_testSpecialEffectParmValue | renderer/rendersystem.cpp:673<br>renderer/rendersystem_init.cpp:3796 |
| `r_trackTextureUsage` | r_trackTextureUsage | renderer/rendersystem.cpp:2182<br>renderer/rendersystem_init.cpp:5731 |
| `r_useFastSkinning` | r_useFastSkinning | renderer/model_md5.cpp:532 |
| `r_useNewSkinning` | r_useNewSkinning | renderer/model_md5.cpp:1500<br>renderer/model_md5.cpp:525 |
| `r_usePenumbraMapShadows` | r_usePenumbraMapShadows | renderer/rendersystem_init.cpp:4276 |
| `r_useSimpleInteraction` | r_useSimpleInteraction | renderer/draw_arb2.cpp:329<br>renderer/rendersystem_init.cpp:4261 |
| `r_videoCard` | r_videoCard | renderer/rendersystem_init.cpp:2492<br>renderer/rendersystem_init.cpp:5716 |
| `r_videoSettingsFailed` | r_videoSettingsFailed | renderer/rendersystem_init.cpp:2440 |
| `s_clipVolumes` | idSoundSystemLocal::s_clipVolumes | sound/default/snd_world.cpp:1251 |
| `s_decompressionLimit` | s_decompressionLimit | sound/default/snd_system.cpp:2209<br>sound/default/snd_system.cpp:2218<br>sound/snd_shader.cpp:361<br>`+1 more` |
| `s_dotbias2` | idSoundSystemLocal::s_dotbias2 | sound/default/snd_world.cpp:840<br>sound/default/snd_world.cpp:842<br>sound/default/snd_world.cpp:845<br>`+1 more` |
| `s_dotbias6` | idSoundSystemLocal::s_dotbias6 | sound/default/snd_world.cpp:799<br>sound/default/snd_world.cpp:800<br>sound/default/snd_world.cpp:807<br>`+7 more` |
| `s_force22kHz` | s_force22kHz | sound/default/snd_cache.cpp:170 |
| `s_frequencyShift` | idSoundSystemLocal::s_frequencyShift | sound/default/snd_world.cpp:1127 |
| `s_globalFraction` | s_globalFraction | sound/default/snd_world.cpp:1227<br>sound/default/snd_world.cpp:1228<br>sound/default/snd_world.cpp:1229<br>`+3 more` |
| `s_loadOpenALFailed` | idSoundSystemLocal::s_loadOpenALFailed | sound/default/snd_system.cpp:2000<br>sound/default/snd_system.cpp:2001 |
| `s_maxChannelsMixed` | s_maxChannelsMixed | sound/default/snd_system.cpp:2251<br>sound/default/snd_world.cpp:2616 |
| `s_maxSoundsPerShader` | s_maxSoundsPerShader | sound/snd_shader.cpp:681 |
| `s_minStereo` | idSoundSystemLocal::s_minStereo | sound/default/snd_system.cpp:2252 |
| `s_minVolume2` | idSoundSystemLocal::s_minVolume2 | sound/default/snd_world.cpp:847<br>sound/default/snd_world.cpp:848<br>sound/default/snd_world.cpp:850<br>`+1 more` |
| `s_minVolume6` | idSoundSystemLocal::s_minVolume6 | sound/default/snd_world.cpp:802<br>sound/default/snd_world.cpp:803<br>sound/default/snd_world.cpp:810<br>`+7 more` |
| `s_muteEAXReverb` | idSoundSystemLocal::s_muteEAXReverb | sound/default/snd_world.cpp:2304<br>sound/default/snd_world.cpp:2563 |
| `s_quadraticFalloff` | s_quadraticFalloff | sound/default/snd_world.cpp:1742<br>sound/default/snd_world.cpp:937 |
| `s_radioChatterFraction` | idSoundSystemLocal::s_radioChatterFraction | sound/default/snd_world.cpp:952 |
| `s_realTimeDecoding` | idSoundSystemLocal::s_realTimeDecoding | sound/default/snd_decoder.cpp:195 |
| `s_reverse` | idSoundSystemLocal::s_reverse | sound/default/snd_system.cpp:747<br>sound/default/snd_system.cpp:822 |
| `s_skipStartSound` | s_skipStartSound | sound/default/snd_emitter.cpp:534 |
| `s_spatializationDecay` | idSoundSystemLocal::s_spatializationDecay | sound/default/snd_world.cpp:841 |
| `s_useDeferredSettings` | idSoundSystemLocal::s_useDeferredSettings | sound/default/snd_system.cpp:719<br>sound/default/snd_system.cpp:734<br>sound/default/snd_system.cpp:798<br>`+1 more` |
| `s_useEAXOcclusion` | idSoundSystemLocal::s_useEAXOcclusion | sound/default/snd_system.cpp:2005<br>sound/default/snd_system.cpp:2006<br>sound/default/snd_system.cpp:2060<br>`+8 more` |
| `sys_country` | sys_country | framework/common.cpp:3908 |
| `sys_language` | sys_language | framework/common.cpp:3910 |
| `win_enableFPUExceptions` | __unnamed::win_enableFPUExceptions | sys/win32/win_main.cpp:905 |
| `win_sysErrorNoWait` | __unnamed::win_sysErrorNoWait | sys/win32/win_main.cpp:1457<br>sys/win32/win_main.cpp:826 |
| `win_viewlog_update_count` | win_viewlog_update_count | sys/win32/win_syscon.cpp:565 |
| `win_viewlog_xpos` | win_viewlog_xpos | sys/win32/win_syscon.cpp:553<br>sys/win32/win_syscon.cpp:557<br>sys/win32/win_syscon.cpp:562 |
| `win_viewlog_ypos` | win_viewlog_ypos | sys/win32/win_syscon.cpp:553<br>sys/win32/win_syscon.cpp:558<br>sys/win32/win_syscon.cpp:563 |

## 2. OpenQ4-Only

| CVar | OpenQ4 source(s) |
| --- | --- |
| `bearTurretAngle` | src/ui/GameBearShootWindow.cpp:45 |
| `bearTurretForce` | src/ui/GameBearShootWindow.cpp:46 |
| `bse_frameCounters` | src/renderer/tr_light.cpp:35 |
| `bse_respectConnectedArea` | src/renderer/tr_light.cpp:40 |
| `bse_showbounds` | src/bse/BSE_Manager.cpp:20 |
| `bse_speeds` | src/bse/BSE_Manager.cpp:16 |
| `bse_useFrustumCull` | src/renderer/tr_light.cpp:45 |
| `cl_gunfov` | src/renderer/RenderSystem_init.cpp:199 |
| `cl_gunfov_adjust` | src/renderer/RenderSystem_init.cpp:200 |
| `cm_debugCollision` | src/cm/CollisionModel_debug.cpp:123 |
| `cm_testAngle` | src/cm/CollisionModel_debug.cpp:354 |
| `com_activeGameModule` | src/framework/Common.cpp:100 |
| `com_asyncInput` | src/framework/Common.cpp:71 |
| `com_autoScreenshot` | src/framework/Common.cpp:96 |
| `com_buildInfo` | src/framework/Common.cpp:65 |
| `com_guid` | src/framework/Session.cpp:56 |
| `com_loadingContinueAutoAdvance` | src/framework/Session.cpp:57 |
| `com_maxfps` | src/framework/Common.cpp:87 |
| `com_memoryMarker` | src/framework/Common.cpp:69 |
| `com_nextGameModule` | src/framework/Common.cpp:101 |
| `com_pid` | src/sys/posix/posix_main.cpp:68 |
| `com_platformProfile` | src/framework/Common.cpp:102 |
| `com_product_lang_ext` | src/framework/Common.cpp:104 |
| `com_productionMode` | src/framework/Common.cpp:80 |
| `com_showFramePacing` | src/framework/Common.cpp:88 |
| `com_showLevelshotBounds` | src/framework/Session.cpp:60 |
| `com_skipLoadingContinue` | src/framework/Session.cpp:58 |
| `com_skipLogoVideos` | src/framework/Session.cpp:59 |
| `con_allowConsole` | src/framework/Common.cpp:84 |
| `con_completionPopup` | src/framework/Console.cpp:341 |
| `con_height` | src/framework/Console.cpp:331 |
| `con_scrollLines` | src/framework/Console.cpp:342 |
| `con_scrollSmooth` | src/framework/Console.cpp:339 |
| `con_scrollSmoothSpeed` | src/framework/Console.cpp:340 |
| `decl_warnOnOverride` | src/framework/DeclManager.cpp:307 |
| `fs_homepath` | src/framework/FileSystem.cpp:1098 |
| `fs_validateOfficialPaks` | src/framework/FileSystem.cpp:1109 |
| `gui_filter_game` | src/framework/async/ServerScan.cpp:36 |
| `gui_filter_gameType` | src/framework/async/ServerScan.cpp:34 |
| `gui_filter_idle` | src/framework/async/ServerScan.cpp:35 |
| `gui_filter_password` | src/framework/async/ServerScan.cpp:32 |
| `gui_filter_players` | src/framework/async/ServerScan.cpp:33 |
| `gui_set_audio_scroll` | src/framework/Session_menu.cpp:44 |
| `gui_set_game_scroll` | src/framework/Session_menu.cpp:45 |
| `gui_set_sys_scroll` | src/framework/Session_menu.cpp:43 |
| `image_highQualityCompression` | src/renderer/BinaryImage.cpp:43 |
| `in_dgamouse` | src/sys/linux/input.cpp:35 |
| `in_joystickDeadZone` | src/sys/win32/win_sdl3.cpp:136 |
| `in_joystickTriggerThreshold` | src/sys/win32/win_sdl3.cpp:137 |
| `in_nograb` | src/sys/linux/input.cpp:36 |
| `in_runDefaultMigrated` | src/framework/UsercmdGen.cpp:449 |
| `in_tty` | src/sys/posix/posix_main.cpp:62 |
| `lcp_showFailures` | src/idlib/math/Lcp.cpp:5 |
| `net_clientMaxRate` | src/framework/async/AsyncNetwork.cpp:48 |
| `net_master0` | src/framework/async/AsyncNetwork.cpp:60 |
| `net_master1` | src/framework/async/AsyncNetwork.cpp:61 |
| `net_master2` | src/framework/async/AsyncNetwork.cpp:62 |
| `net_master3` | src/framework/async/AsyncNetwork.cpp:63 |
| `net_master4` | src/framework/async/AsyncNetwork.cpp:64 |
| `net_port` | src/sys/posix/posix_net.cpp:52<br>src/sys/win32/win_net.cpp:41 |
| `net_serverAllowServerMod` | src/framework/async/AsyncNetwork.cpp:67 |
| `net_socksEnabled` | src/sys/win32/win_net.cpp:45 |
| `net_socksPassword` | src/sys/win32/win_net.cpp:49 |
| `net_socksPort` | src/sys/win32/win_net.cpp:47 |
| `net_socksServer` | src/sys/win32/win_net.cpp:46 |
| `net_socksUsername` | src/sys/win32/win_net.cpp:48 |
| `preLoad_Images` | src/renderer/ImageManager.cpp:40 |
| `preLoad_Samples` | src/sound/snd_system.cpp:53 |
| `r_bloom` | src/renderer/RenderSystem_init.cpp:103 |
| `r_bloomIntensity` | src/renderer/RenderSystem_init.cpp:107 |
| `r_bloomMipCount` | src/renderer/RenderSystem_init.cpp:109 |
| `r_bloomRadius` | src/renderer/RenderSystem_init.cpp:108 |
| `r_bloomSoftKnee` | src/renderer/RenderSystem_init.cpp:106 |
| `r_bloomThreshold` | src/renderer/RenderSystem_init.cpp:105 |
| `r_borderless` | src/renderer/RenderSystem_init.cpp:153 |
| `r_cgFragmentProfile` | src/renderer/RenderSystem_init.cpp:373 |
| `r_cgVertexProfile` | src/renderer/RenderSystem_init.cpp:372 |
| `r_crt` | src/renderer/RenderSystem_init.cpp:141 |
| `r_crtAmount` | src/renderer/RenderSystem_init.cpp:142 |
| `r_crtChromatic` | src/renderer/RenderSystem_init.cpp:146 |
| `r_crtCurvature` | src/renderer/RenderSystem_init.cpp:145 |
| `r_crtMaskStrength` | src/renderer/RenderSystem_init.cpp:144 |
| `r_crtScanlineStrength` | src/renderer/RenderSystem_init.cpp:143 |
| `r_enhancedMaterialFresnel` | src/renderer/RenderSystem_init.cpp:179 |
| `r_enhancedMaterialNormalScale` | src/renderer/RenderSystem_init.cpp:177 |
| `r_enhancedMaterialSpecularBoost` | src/renderer/RenderSystem_init.cpp:178 |
| `r_enhancedMaterials` | src/renderer/RenderSystem_init.cpp:176 |
| `r_forceAmbient` | src/renderer/RenderSystem_init.cpp:228 |
| `r_forceSpecialEffects` | src/renderer/RenderSystem_init.cpp:118 |
| `r_fullscreenDesktop` | src/renderer/RenderSystem_init.cpp:152 |
| `r_hdrAdaptDownSpeed` | src/renderer/RenderSystem_init.cpp:135 |
| `r_hdrAdaptUpSpeed` | src/renderer/RenderSystem_init.cpp:134 |
| `r_hdrAutoExposure` | src/renderer/RenderSystem_init.cpp:130 |
| `r_hdrContrast` | src/renderer/RenderSystem_init.cpp:129 |
| `r_hdrDebugView` | src/renderer/RenderSystem_init.cpp:140 |
| `r_hdrExposure` | src/renderer/RenderSystem_init.cpp:122 |
| `r_hdrGain` | src/renderer/RenderSystem_init.cpp:126 |
| `r_hdrGamutCompression` | src/renderer/RenderSystem_init.cpp:137 |
| `r_hdrHighlightDesaturation` | src/renderer/RenderSystem_init.cpp:136 |
| `r_hdrKeyValue` | src/renderer/RenderSystem_init.cpp:131 |
| `r_hdrLift` | src/renderer/RenderSystem_init.cpp:124 |
| `r_hdrMaxExposure` | src/renderer/RenderSystem_init.cpp:133 |
| `r_hdrMinExposure` | src/renderer/RenderSystem_init.cpp:132 |
| `r_hdrPostGamma` | src/renderer/RenderSystem_init.cpp:125 |
| `r_hdrSRGB` | src/renderer/RenderSystem_init.cpp:139 |
| `r_hdrSRGBTextures` | src/renderer/RenderSystem_init.cpp:138 |
| `r_hdrSaturation` | src/renderer/RenderSystem_init.cpp:128 |
| `r_hdrSceneTarget` | src/renderer/RenderSystem_init.cpp:120 |
| `r_hdrToneMap` | src/renderer/RenderSystem_init.cpp:121 |
| `r_hdrVibrance` | src/renderer/RenderSystem_init.cpp:127 |
| `r_hdrWhitePoint` | src/renderer/RenderSystem_init.cpp:123 |
| `r_interactionColorMode` | src/renderer/RenderSystem_init.cpp:210 |
| `r_lensFlare` | src/renderer/RenderSystem_init.cpp:104 |
| `r_lightGridBakeAsyncReadback` | src/renderer/RenderSystem_init.cpp:231 |
| `r_lightGridBakeMemoryMB` | src/renderer/RenderSystem_init.cpp:232 |
| `r_lightGridBakeReadbackSlots` | src/renderer/RenderSystem_init.cpp:233 |
| `r_lightGridBakeWorkers` | src/renderer/RenderSystem_init.cpp:230 |
| `r_logFile` | src/renderer/RenderSystem_init.cpp:258 |
| `r_maxDisplayRefresh` | src/sys/osx/macosx_glimp.mm:47 |
| `r_minDisplayRefresh` | src/sys/osx/macosx_glimp.mm:46 |
| `r_msaaAlphaToCoverage` | src/renderer/RenderSystem_init.cpp:148 |
| `r_msaaResolveDepth` | src/renderer/RenderSystem_init.cpp:147 |
| `r_multiScreen` | src/sys/win32/win_sdl3.cpp:139 |
| `r_postAA` | src/renderer/RenderSystem_init.cpp:102 |
| `r_resolutionScaleMode` | src/renderer/RenderSystem_init.cpp:308 |
| `r_resolutionScaleSharpness` | src/renderer/RenderSystem_init.cpp:309 |
| `r_screen` | src/sys/win32/win_sdl3.cpp:138 |
| `r_shaderReport` | src/renderer/RenderSystem_init.cpp:211 |
| `r_shadowMapBias` | src/renderer/RenderSystem_init.cpp:265 |
| `r_shadowMapCSM` | src/renderer/RenderSystem_init.cpp:169 |
| `r_shadowMapCascadeBlend` | src/renderer/RenderSystem_init.cpp:275 |
| `r_shadowMapCascadeCount` | src/renderer/RenderSystem_init.cpp:272 |
| `r_shadowMapCascadeDistance` | src/renderer/RenderSystem_init.cpp:273 |
| `r_shadowMapCascadeLambda` | src/renderer/RenderSystem_init.cpp:274 |
| `r_shadowMapCascadeStabilize` | src/renderer/RenderSystem_init.cpp:282 |
| `r_shadowMapDebugMode` | src/renderer/RenderSystem_init.cpp:279 |
| `r_shadowMapDebugOverlay` | src/renderer/RenderSystem_init.cpp:276 |
| `r_shadowMapFilterRadius` | src/renderer/RenderSystem_init.cpp:269 |
| `r_shadowMapHashedAlpha` | src/renderer/RenderSystem_init.cpp:170 |
| `r_shadowMapNormalBias` | src/renderer/RenderSystem_init.cpp:266 |
| `r_shadowMapPointBias` | src/renderer/RenderSystem_init.cpp:267 |
| `r_shadowMapPointFarScale` | src/renderer/RenderSystem_init.cpp:283 |
| `r_shadowMapPointFilterRadius` | src/renderer/RenderSystem_init.cpp:270 |
| `r_shadowMapPointNormalBias` | src/renderer/RenderSystem_init.cpp:268 |
| `r_shadowMapPolygonFactor` | src/renderer/RenderSystem_init.cpp:284 |
| `r_shadowMapPolygonOffset` | src/renderer/RenderSystem_init.cpp:285 |
| `r_shadowMapProjectionPad` | src/renderer/RenderSystem_init.cpp:271 |
| `r_shadowMapReport` | src/renderer/RenderSystem_init.cpp:174 |
| `r_shadowMapReportInterval` | src/renderer/RenderSystem_init.cpp:175 |
| `r_shadowMapSize` | src/renderer/RenderSystem_init.cpp:264 |
| `r_shadowMapTranslucentDensity` | src/renderer/RenderSystem_init.cpp:172 |
| `r_shadowMapTranslucentMinAlpha` | src/renderer/RenderSystem_init.cpp:173 |
| `r_shadowMapTranslucentMoments` | src/renderer/RenderSystem_init.cpp:171 |
| `r_showLightGrid` | src/renderer/RenderSystem_init.cpp:339 |
| `r_showViewLights` | src/renderer/RenderSystem_init.cpp:336 |
| `r_showViewLightsInterval` | src/renderer/RenderSystem_init.cpp:337 |
| `r_showViewLightsVisuals` | src/renderer/RenderSystem_init.cpp:338 |
| `r_skipGlowOverlay` | src/framework/Common.cpp:105 |
| `r_skipParticles` | src/renderer/RenderSystem_init.cpp:289 |
| `r_skipSky` | src/renderer/RenderSystem_init.cpp:218 |
| `r_ssao` | src/renderer/RenderSystem_init.cpp:110 |
| `r_ssaoBias` | src/renderer/RenderSystem_init.cpp:112 |
| `r_ssaoDebug` | src/renderer/RenderSystem_init.cpp:117 |
| `r_ssaoIntensity` | src/renderer/RenderSystem_init.cpp:113 |
| `r_ssaoMaxDistance` | src/renderer/RenderSystem_init.cpp:115 |
| `r_ssaoPower` | src/renderer/RenderSystem_init.cpp:114 |
| `r_ssaoRadius` | src/renderer/RenderSystem_init.cpp:111 |
| `r_ssaoSamples` | src/renderer/RenderSystem_init.cpp:116 |
| `r_stencilTranslucentShadows` | src/renderer/RenderSystem_init.cpp:184 |
| `r_stretched` | src/sys/osx/PreferencesDialog.cpp:36 |
| `r_useInfiniteFarZ` | src/renderer/RenderSystem_init.cpp:193 |
| `r_useInteractionTable` | src/renderer/RenderSystem_init.cpp:181 |
| `r_useLightGrid` | src/renderer/RenderSystem_init.cpp:229 |
| `r_useShadowMap` | src/renderer/RenderSystem_init.cpp:168 |
| `r_useSmp` | src/renderer/RenderSystem_init.cpp:190 |
| `r_vertexBufferMegs` | src/renderer/VertexCache.cpp:39 |
| `r_windowHeight` | src/renderer/RenderSystem_init.cpp:155 |
| `r_windowWidth` | src/renderer/RenderSystem_init.cpp:154 |
| `radiant_entityMode` | src/tools/radiant/Radiant.cpp:46 |
| `rbfg_DefaultHeight` | src/tools/common/RenderBumpFlatDialog.cpp:35 |
| `rbfg_DefaultWidth` | src/tools/common/RenderBumpFlatDialog.cpp:34 |
| `s_alsa_lib` | src/sys/linux/sound_alsa.cpp:36 |
| `s_alsa_pcm` | src/sys/linux/sound_alsa.cpp:35 |
| `s_centerFractionVO` | src/sound/snd_emitter.cpp:36 |
| `s_cushionFadeChannels` | src/sound/snd_world.cpp:35 |
| `s_cushionFadeLimit` | src/sound/snd_world.cpp:37 |
| `s_cushionFadeOver` | src/sound/snd_world.cpp:38 |
| `s_cushionFadeRate` | src/sound/snd_world.cpp:36 |
| `s_debugHardware` | src/sound/OpenAL/AL_SoundVoice.cpp:76 |
| `s_device` | src/sound/OpenAL/AL_SoundHardware.cpp:39<br>src/sys/osx/macosx_sound.cpp:35 |
| `s_driver` | src/sys/linux/sound.cpp:47<br>src/sys/linux/sound.cpp:49 |
| `s_dsp` | src/sys/linux/sound.cpp:147 |
| `s_lockListener` | src/sound/snd_world.cpp:32 |
| `s_maxEmitterChannels` | src/sound/snd_world.cpp:34 |
| `s_meterPosition` | src/sound/OpenAL/AL_SoundHardware.cpp:38 |
| `s_muteUnfocused` | src/framework/Session.cpp:61 |
| `s_openALEfxDebugMode` | src/sound/snd_system.cpp:39 |
| `s_showPerfData` | src/sound/OpenAL/AL_SoundHardware.cpp:40 |
| `s_showVoices` | src/sound/snd_world.cpp:42 |
| `s_skipHardwareSets` | src/sound/OpenAL/AL_SoundVoice.cpp:75 |
| `s_unpauseFadeInTime` | src/sound/snd_world.cpp:39 |
| `s_useCompression` | src/sound/snd_system.cpp:44<br>src/sound/snd_system.cpp:48 |
| `s_volume_dB` | src/sound/snd_world.cpp:43 |
| `s_warnOnMissingSamples` | src/sound/snd_system.cpp:41 |
| `si_idleServer` | src/framework/async/AsyncNetwork.cpp:68 |
| `sv_cheats` | src/framework/async/AsyncNetwork.cpp:39 |
| `sys_lang` | src/sys/sys_local.cpp:38 |
| `sys_showMallocs` | src/sys/win32/win_main.cpp:441 |
| `sys_videoRam` | src/sys/linux/glimp.cpp:41<br>src/sys/linux/linux_sdl3.cpp:44 |
| `ui_aspectCorrection` | src/ui/UserInterface.cpp:39 |
| `win_printScreenToSystemTool` | src/sys/win32/win_main.cpp:77 |

## 3. Shared Names With Metadata Differences

| CVar | Changed | Retail default | OpenQ4 default | Retail flags | OpenQ4 flags | Retail type/range | OpenQ4 type/range |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `bse_rateCost` | default, flags | `1.0` | `1` | `ST AR` | `ST CH` | `float` | `float` |
| `bse_rateLimit` | default, flags | `3.0` | `1` | `ST AR` | `ST CH` | `float` | `float` |
| `bse_scale` | flags | `1` | `1` | `ST AR` | `ST CH` | `float` | `float` |
| `cm_drawMask` | type/range | `none` | `none` | `GAME ST CH` | `GAME ST CH` | `string [none, solid, opaque, water, playerclip, monsterclip, moveableclip, ikclip, blood, body, corpse, trigger, aas_solid, aas_obstacle, flashlight_trigger, sightClip, largeShotClip, solid, notacticalfeatures, vehicleclip, flyclip, shotClip, itemclip]` | `string [none, solid, opaque, water, playerclip, monsterclip, moveableclip, ikclip, blood, body, shotclip, corpse, rendermodel, trigger, aas_solid, aas_obstacle, flashlight_trigger, sightclip, largeshotclip, notacticalfeatures, vehicleclip, areaportal, nocsg, flyclip, itemclip, projectileclip, fog, lava, slime]` |
| `cm_drawNormals` | type/range | `0` | `0` | `GAME ST CH` | `GAME ST CH` | `float` | `bool` |
| `com_showFPS` | type/range | `0` | `0` | `SYS ST AR` | `SYS ST AR` | `int` | `bool` |
| `com_videoRam` | flags | `64` | `64` | `SYS ST IN AR` | `SYS ST AR` | `int` | `int` |
| `fs_game` | default | `q4mp` | `baseoq4` | `SYS SI ST IN` | `SYS SI ST IN` | `string` | `string` |
| `gui_debug` | type/range | `0` | `0` | `GUI ST CH` | `GUI ST CH` | `int` | `bool` |
| `gui_debugScript` | type/range | `0` | `0` | `GUI ST CH` | `GUI ST CH` | `bool` | `int` |
| `in_joystick` | default, flags | `0` | `1` | `SYS ST` | `SYS ST AR` | `bool` | `bool` |
| `net_LANServer` | flags | `0` | `0` | `SYS ST AR` | `SYS ST` | `bool` | `bool` |
| `net_clientPrediction` | default, flags | `10` | `16` | `SYS ST AR` | `SYS ST` | `int` | `int` |
| `net_clientServerTimeout` | default | `60` | `40` | `SYS ST` | `SYS ST` | `int` | `int` |
| `net_clientUsercmdBackup` | default, type/range | `1` | `5` | `SYS ST` | `SYS ST` | `int [0, 5]` | `int` |
| `net_ip` | flags | `localhost` | `localhost` | `SYS ST` | `SYS ST CH` | `string` | `string` |
| `net_serverClientTimeout` | default | `60` | `40` | `SYS ST` | `SYS ST` | `int` | `int` |
| `net_serverMaxClientRate` | default | `10000` | `25600` | `SYS ST AR` | `SYS ST AR` | `int` | `int` |
| `net_serverSnapshotDelay` | default | `80` | `50` | `SYS ST` | `SYS ST` | `int` | `int` |
| `r_brightness` | default | `1.2` | `1` | `RNDR ST AR` | `RNDR ST AR` | `float [0.5, 2]` | `float [0.5, 2]` |
| `r_customHeight` | default | `486` | `1080` | `RNDR ST AR` | `RNDR ST AR` | `int` | `int` |
| `r_customWidth` | default | `720` | `1920` | `RNDR ST AR` | `RNDR ST AR` | `int` | `int` |
| `r_displayRefresh` | flags | `0` | `0` | `RNDR ST AR` | `RNDR ST` | `int [0, 200]` | `int [0, 200]` |
| `r_lightDetailLevel` | default, flags, type/range | `9` | `0` | `RNDR ST` | `RNDR ST AR` | `float` | `float [0, 10]` |
| `r_mode` | default | `3` | `-2` | `RNDR ST AR` | `RNDR ST AR` | `int` | `int` |
| `r_renderer` | type/range | `best` | `best` | `RNDR ST AR` | `RNDR ST AR` | `string [best, arb, arb2, nv10, nv20, r200]` | `string [best, arb, arb2, Cg, exp, nv10, nv20, r200]` |
| `r_screenFraction` | flags | `100` | `100` | `RNDR ST CH` | `RNDR ST AR` | `int` | `int` |
| `r_showDemo` | type/range | `0` | `0` | `RNDR ST CH` | `RNDR ST CH` | `int` | `bool` |
| `r_showSurfaceInfo` | type/range | `0` | `0` | `RNDR ST CH` | `RNDR ST CH` | `int` | `bool` |
| `r_useEntityScissors` | default | `1` | `0` | `RNDR ST CH` | `RNDR ST CH` | `bool` | `bool` |
| `r_znear` | default | `3` | `3.0` | `RNDR ST CH` | `RNDR ST CH` | `float [0.001, 200]` | `float [0.001, 200]` |
| `s_constantAmplitude` | flags | `-1` | `-1` | `SND ST CH` | `ST CH` | `float` | `float` |
| `s_deviceName` | flags | `(empty)` | `(empty)` | `SND ST IN AR` | `ST AR` | `string` | `string` |
| `s_doorDistanceAdd` | flags | `150` | `150` | `SND ST AR` | `ST CH` | `float` | `float` |
| `s_drawSounds` | flags | `0` | `0` | `SND ST CH` | `ST CH` | `int [0, 2]` | `int [0, 2]` |
| `s_meterTopTime` | default, flags | `2000` | `1000` | `SND ST AR` | `ST AR` | `int` | `int` |
| `s_musicVolume` | flags | `0.5` | `0.5` | `SND ST AR` | `ST AR` | `float [0, 1]` | `float [0, 1]` |
| `s_noSound` | default, flags | `1` | `0` | `SND ST RO` | `ST CH` | `bool` | `bool` |
| `s_numberOfSpeakers` | default, flags, type/range | `2` | `6` | `SND ST AR` | `ST AR` | `string` | `int` |
| `s_playDefaultSound` | flags | `1` | `1` | `SND ST AR` | `ST CH` | `bool` | `bool` |
| `s_showLevelMeter` | flags | `0` | `0` | `SND ST CH` | `ST AR` | `bool` | `bool` |
| `s_showStartSound` | flags, type/range | `0` | `0` | `SND ST CH` | `ST CH` | `int` | `bool` |
| `s_singleEmitter` | default, flags | `-1` | `0` | `SND ST CH` | `ST CH` | `int` | `int` |
| `s_speakerFraction` | default, flags | `0.65` | `0.5` | `SND ST AR` | `ST AR` | `float` | `float` |
| `s_subFraction` | flags | `0.5` | `0.5` | `SND ST AR` | `ST AR` | `float` | `float` |
| `s_useEAXReverb` | flags | `1` | `1` | `SND ST AR` | `ST AR` | `bool` | `bool` |
| `s_useOcclusion` | flags | `1` | `1` | `SND ST AR` | `ST CH` | `bool` | `bool` |
| `s_useOpenAL` | default, flags | `0` | `1` | `SND ST AR` | `ST AR` | `bool` | `bool` |
| `s_volume` | default, flags, type/range | `0.5` | `1.0` | `SND ST AR` | `ST AR` | `float` | `float [0, 1]` |
| `si_version` | default | `Quake4  V1.4.3 win-x86 Oct 20 2010` | `OpenQ4 0.1.010-nightly.20260319.1+g47f946e3.dirty` | `SYS SI ST RO` | `SYS SI ST RO` | `string` | `string` |
| `timescale` | type/range | `1` | `1` | `SYS ST CH` | `SYS ST CH` | `string` | `float [0.1, 10]` |

## Unresolved Retail Symbols

These decompiled retail engine symbols were observed, but no exact live retail runtime cvar name could be confirmed for them, so they were excluded from the exact-name comparison table above.

| Retail symbol | Retail source(s) |
| --- | --- |
| `idAsyncNetwork::debugFrameTime` | framework/async/asyncclient.cpp:4852<br>framework/async/asyncclient.cpp:5049<br>framework/async/asyncclient.cpp:5088<br>`+8 more` |
| `idAsyncNetwork::debugTraffic` | framework/common.cpp:1752 |
| `idAsyncNetwork::idleServer` | framework/async/asyncserver.cpp:5058<br>framework/async/asyncserver.cpp:5061<br>framework/async/asyncserver.cpp:5063<br>`+1 more` |
| `image_dontUsePrecompressedSkyboxesForCGW` | renderer/image_load.cpp:2399 |
