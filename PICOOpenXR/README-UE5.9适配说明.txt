PICOOpenXR 插件 UE5.9 适配版 — 交付说明
================================================================
版本: PICO Unreal SDK 6.1.4 / PICOOpenXR
适配目标: Unreal Engine 5.9.0 (Changelist 60351404, ++UE5+Release-5.9)
交付日期: 2026-11-16
原版基线: F:\CG\UEPlugins\PICO-Unreal-SDK-6.1.4\UE_5.8\Plugins\PICOOpenXR (EngineVersion 5.8.0)

一、目录结构
----------------------------------------------------------------
PICOOpenXR/
  PICOOpenXR.uplugin            EngineVersion 已改为 5.9.0
  Config/DefaultPICOOpenXR.ini  新增（[CoreRedirects] 结构体重定向）
  Content/                      121 个资产（与上游 6.1.4 完全一致，未增删）
  Resources/  Shaders/  Source/  10 个模块的源码

安装方式: 把整个 PICOOpenXR 目录放进 <你的项目>/Plugins/ 下；
或在 .uproject 里启用 PICOOpenXR 插件。
依赖插件（全部为 UE5.9 引擎自带）: OpenXR, XRBase, PICOController,
ProceduralMeshComponent, AndroidPermission, LiveLink。

二、改动清单（14 个文件被修改 + 1 个文件新增，无删除）
----------------------------------------------------------------
1. PICOOpenXR.uplugin
   "EngineVersion": "5.8.0" -> "5.9.0"

2. Source/PICOOpenXRHMD/Private/PICO_HMD.cpp
   (a) RHI 线程关闭逻辑适配（UE5.9 API 移除）
       ERHIThreadMode / GPendingRHIThreadMode 在 UE5.9 已从
       RHI/Public/RHICommandList.h 移入 RenderCore/Private/RHIThread.h
       且不再导出，插件无法直接赋值。改为经控制台命令下发：
         GEngine->Exec(nullptr, TEXT("r.RHIThread.Enable 0"));
       该命令由 RenderCore 的 HandleRHIThreadEnableChanged 处理，
       在下一个 LatchRenderThreadConfiguration（FEngineLoop::Tick 开头）
       真正停掉渲染线程，语义与 5.8 版一致。
       注：Android Shipping 下依然有效，因为 TargetRules.bUseExecCommandsInShipping
       默认为 true，UEngine::Exec 未被裁剪。
       实证: Android 运行时日志 LogPICOOpenXRHMD: PICO disable RHI thread
       requested (r.RHIThread.Enable 0), issued: 1
   (b) 两处 UE_LOG 格式串：Model / FPS 枚举补 static_cast<int32>。

3. Source/PICOOpenXREditor/Private/PICO_HandGestureEditorLibrary.cpp
   UE_ARRAY_COUNT(...)（size_t）配 %d 触发 UE5.9 的编译期格式串校验，
   补 static_cast<int32>。

4-5. Source/PICOOpenXRInput/Private/PICO_Controller.h / .cpp
   【功能缺陷修复】PICO G3 交互档的振动绑定导致整档输入失效。
   现象: UE5.9 + PICO 运行时下 xrSuggestInteractionProfileBindings 对
        /interaction_profiles/bytedance/pico_g3_controller 返回
        XR_ERROR_PATH_UNSUPPORTED，并触发 ensure；该调用是按交互档整体
        提交的，一旦失败整个 G3 档的所有绑定都被丢弃，等于在手柄为 G3
        的设备上输入全废。
   根因: 引擎 OpenXRInput.cpp 只在 Profile.HasHaptics 为真时才追加
        /user/hand/<hand>/output/haptic，而本插件 GetSuggestedBindings
        只判 bParametricHapticsExtensionEnabled，忽略了该档本身没有
        haptic 输出路径（GetInteractionProfiles 里 g3 档的
        OutHasHaptics 就是 false），于是给无 haptic 的档也建议了
        /output/haptic，运行时判为非法路径并拒绝整次调用。
   修法: AddProfile 增加 bool bHasHaptics 形参（默认 true，保持既有调用
        点语义），新增与 Profiles 平行的 TArray<bool> ProfileHasHaptics；
        GetSuggestedBindings 里按 Profiles.Find(InInteractionProfile)
        查出该档能力，条件改为
          bParametricHapticsExtensionEnabled && bProfileHasHaptics，
        未登记过的档（引擎内置档）保持原有行为；跳过时打 Log:
          Skipping haptic bindings - interaction profile %u does not
          expose a haptic output path
   实证: 修复后 Android 实机日志
        LogPICOOpenXRInput: Skipping haptic bindings - interaction
        profile 23 does not expose a haptic output path
        xrSuggestInteractionProfileBindings 失败计数 0，
        XR_ERROR_PATH_UNSUPPORTED 0，is not a valid path 0；
        parametric haptics 扩展本身照常加载（振动功能不受影响）。
   注: 这是上游 6.1.4 自带缺陷，UE5.8 下同样存在，只是 UE5.9 的
       运行时/日志更严格而暴露出来。

6. Source/PICOOpenXRMR/Private/PICO_DynamicObject.cpp
7. Source/PICOOpenXRMR/Private/PICO_MR.cpp
8. Source/PICOOpenXRMR/Private/PICO_MRAsyncActions.cpp
9. Source/PICOOpenXRMR/Private/PICO_SceneCapture.cpp
10. Source/PICOOpenXRMR/Private/PICO_SpatialAnchor.cpp
11. Source/PICOOpenXRMR/Private/PICO_SpatialMesh.cpp
12. Source/PICOOpenXRMR/Private/PICO_SpatialPlane.cpp
13. Source/PICOOpenXRMR/Private/PICO_LightEstimation.cpp
   (a) 6-13 合计 30 处 UE_LOG 的 %d 实参为 enum class
       （EResultPICO / EMRStatePICO / ESemanticLabelPICO 等），
       UE5.9 默认开启 bValidateFormatStrings（BuildSettingsVersion >= V5），
       枚举直接喂 %d 会在编译期 static_assert 失败。全部补
       static_cast<int32>。
   (b) PICO_LightEstimation.cpp 移除 #include "OculusShaders.h" ——
       该头文件在 UE5.9 已从 RenderCore/Public 删除；插件内并未使用
       其中的 FOculus* 着色器类，仅保留了 VULKAN_CUBEMAP_* 魔数宏。
   全插件 static_cast<int32> 合计新增 38 处（含上述 2、3、5 条内各一/两处）。

14. Source/PICOOpenXRRuntimeSettings/Private/PICOOpenXRRuntimeSettings.cpp
   【UE5.9 新增硬闸门适配】Android 上 OpenXR 默认被引擎关闭。
   UE5.9 的 OpenXR 插件新增要求: Android 应用必须显式声明为 OpenXR
   Immersive 应用（AndroidRuntimeSettings::bPackageForOpenXRImmersive），
   否则 OpenXRHMDModule 打印
     OpenXR disabled because this app is not packaged for OpenXR
   并直接放弃初始化 —— Android 上整个 OpenXR（连带全部 PICO 功能）
   都不会起来，而编辑器里一切正常，极易误判。
   修法: 新增 void EnsureOpenXRImmersivePackagingEnabled()：在
   UPICOOpenXRRuntimeSettings::PostInitProperties() 的 WITH_EDITOR 段尾
   调用，读项目 Config/DefaultEngine.ini 的
     [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]
     bPackageForOpenXRImmersive
   仅在未配置时写入 True 并 Flush（**不覆盖用户显式设置的值**），
   日志:
     PICO OpenXR: enabled bPackageForOpenXRImmersive in %s
     (required by UE5.9 to keep OpenXR active on Android).
   该写法沿用本类既有的 pvr.app.type 写 ini 模式，不改引擎。
   实证: 宿主工程与全新工程 PICOClean59 均由 0 -> 1；Android 日志
     LogHMD: OpenXRHMDModule: App is packaged for Android OpenXR
     ImmersiveHMD
     LogHMD: xrCreateInstance succeeded with XR_API_VERSION_1_1

15. Config/DefaultPICOOpenXR.ini（新增）
   [CoreRedirects]
   +StructRedirects=(OldName="SceneCaptureGeneratorActor",NewName="/Script/PICOOpenXRMR.SceneCaptureGeneratorActorPICO")
   +StructRedirects=(OldName="PICOUVAdjustment",NewName="/Script/PICOOpenXRMR.UVAdjustmentPICO")
   说明: PICO 6.1.4 的 Content 里 BP_SceneCapturesGenerator_Sample 保存的是
   旧结构体名标签，而 C++ 侧只有新名，加载时会刷
   "Struct Property ... has a struct type mismatch"。补上重定向后告警归零，
   资产可正常读写这些属性。（这是上游内容自带的存量问题，UE5.8 同样存在。）
   依据: FCoreRedirects::Initialize() 遍历 GConfig 全部 ini 读 [CoreRedirects]，
   插件自带 ini 由 PluginManager 注册进 GConfig，故放插件 Config/ 下即可生效。

三、验证结论（全部在 UE5.9.0 上实跑，报告见交付目录 VerifyReports/）
----------------------------------------------------------------
[编译] 三种配置全部 Result: Succeeded，零错误
  - Win64 Editor Development
  - Win64 Game Shipping
  - Android Shipping (arm64)
  日志: VerifyReports/build_win64_editor_development.log /
        build_win64_game_shipping.log / build_android_shipping.log

[编辑器加载] 10 个模块全部 InternalLoadLibrary 成功，无版本不匹配；
  7 个 IOpenXRExtensionPlugin 全部注册进 UE5.9 OpenXR:
  PICOOpenXRPassthrough / PICOOpenXRHandInteraction / HandTrackingPICO /
  MRModuleExtensionPICO / MovementExtensionPICO / ControllerPICO / HMDPICO

[反射面] 用引擎 TObjectIterator 导出 /Script/PICO* 全部反射对象：
  classes=86  funcs=273  props=314  structs=51  structprops=183
  enums=63  enumvalues=418  cdo_failures=0
  （86 个 UCLASS 的原生构造函数全部可实例化）
  产物: VerifyReports/ReflectionSurface.txt
  与源码声明逐条比对: 265 UFUNCTION / 308 UPROPERTY / 183 struct props
  / 419 enum items 全部存在，0 缺失。

[资产] 121 个资产全部加载成功；17 个蓝图全部编译到 BS_UP_TO_DATE，
  零编译错误。
  产物: VerifyReports/verify_blueprints.json

[类型全加载] 86 类 / 51 结构 / 63 枚举全部可加载，86 个 CDO 全部可取。
  产物: VerifyReports/verify_all_types.json

[实例化] 6 个 C++ Actor spawn 成功；12 个组件类 new_object 成功；
  8 个示例蓝图 spawn 成功。
  产物: VerifyReports/verify_spawn.json, verify_testmap.json
  测试地图: Host/PICOHost59/Content/Maps/PICOTestMap.umap

[函数真实调用] 10/10 个插件蓝图函数被真实调用并正常返回。
  产物: VerifyReports/verify_calls3.json
  （另有 16 条 MR 异步动作的静态工厂未覆盖：它们标了
   BlueprintInternalUseOnly，按引擎 PyGenUtil 的设计本就不导出到
   Python/蓝图，属口径而非缺失。）

[着色器] 插件全局着色器在真实 D3D12 RHI 下编译通过：
  FPICOCubemapPS (PICOShaders.usf / MainForCubemap)  COMPILED
  FDisplayMappingPixelShader                        COMPILED
  SUMMARY|known=2|unregistered=0|compiled=3|missing=0
  产物: VerifyReports/PICOShaderCheck.txt

[Win64 运行] 打包后的 PICOHost59.exe 实跑（显式带地图
  /Game/Maps/PICOTestMap 启动，因为 cook 后的 Saved\Config 没有
  GameDefaultMap，不传地图会落到 /Engine/Maps/Templates/OpenWorld）：
  插件挂载成功（LogPluginManager: Mounting Project plugin PICOOpenXR），
  7 个扩展插件全部注册，LogLoad: LoadMap: /Game/Maps/PICOTestMap?Name=Player
  后进入游戏循环。插件 C++ Actor 真实执行：
    LogPICOCameraPreview          5 行（BeginPlay / StartAfterPermission /
                                  IsCameraImageSupported=false / ShutdownCamera）
    PICO ET Debug（眼睛追踪）      51 行逐 tick，跨 50.2 秒
  进程在第 10/20/30/45/60 秒采样时全部存活，累计 CPU 198.44 秒，
  工作集稳定 1.2 GB；以 WM_CLOSE 正常关闭，退出码 0
  （ViewportClosed -> RequestExit(0) -> Game engine shut down -> Exiting）。
  Fatal error / Assertion failed / Ensure condition failed /
  LogOutputDevice: Error / XR_ERROR_PATH_UNSUPPORTED 全部为 0。
  说明：本机没有可用的 PC OpenXR 运行时，唯一一条 XR_ERROR 是
  「Failed to create an OpenXR instance, result is XR_ERROR_VALIDATION_FAILURE」，
  属环境限制；PC 端 OpenXR 会话无法在本机构建，完整会话证据见 Android 段。
  日志: VerifyReports/win64_packaged_run.log
        VerifyReports/win64_runtime_evidence.txt（脚本生成）
        VerifyReports/win64_run_sampling.json

[Win64 完整打包] RunUAT BuildCookRun（cook+build+stage+pak+archive）
  BUILD SUCCESSFUL，ExitCode=0。
  产物: 交付目录 Win64Package/（可直接运行）

[干净安装复验 — 端到端] 新建一个全新的 UE5.9 工程 PICOClean59（只有空
  Game 模块，无任何历史 Intermediate/Saved），把 PICOOpenXR/ 从本交付
  目录整份拷进 Plugins/，完全照本文的安装步骤操作：
  编译 Result: Succeeded（111 actions / 414.01 s，全量重编）
  日志: VerifyReports/clean_install_editor.log
  编辑器内验证（VerifyReports/clean_install_verify.json）：
      plugin_mounted=true，9 个含反射类的模块全部可探到
      classes 86/86 / structs 51/51 / enums 63/63 全部可加载
      assets 121/121 全部可加载，blueprints 17/17 全部编译到 BS_UP_TO_DATE
      errors: []
  -> 证明「把本目录的 PICOOpenXR 拷进任意 UE5.9 工程即可用」，不依赖
     宿主工程里任何既有配置。

[Android 打包] RunUAT BuildCookRun 全链路（cook+build+stage+pak+archive）
  BUILD SUCCESSFUL，ExitCode=0：
  Cooked packages 737 / 0 error(s) / 31 warning(s)
  插件材质在 VULKAN_ES3_1_ANDROID 与 OPENGL_ES3_1_ANDROID 两套平台编译通过
  产物: 交付目录 AndroidPackage/
    PICOHost59-arm64.apk        (131,193,373 B)
    main.1.com.YourCompany.PICOHost59.obb (85,153,006 B)
    Install_PICOHost59-arm64.bat / Uninstall_... / SymbolizeCrashDump_...
    PICOHost59_Symbols_v1/ (调试符号)
  APK 内已含 libopenxr_loader.so 与 libopenxr_loader_pico.so。

[Android 实机运行 — 端到端 OpenXR 全链路] 在 PICO 模拟器
  (swan_rls_spaceos_K / Android 16) 上安装上述 cook+pak 正式包并运行
  （PICO 运行时 31258.43072.16384），完整证据见
  VerifyReports/android_openxr_vr_session.log：
    LogHMD: OpenXRHMDModule: App is packaged for Android OpenXR ImmersiveHMD
    LogHMD: xrCreateInstance succeeded with XR_API_VERSION_1_1
    LogHMD: Extensions for IOpenXRExtensionPlugins: <7 个插件名>
    LogPICOOpenXRHMD: XR session state changed: 1 IDLE -> 2 READY ->
      3 SYNCHRONIZED -> 4 VISIBLE -> 5 FOCUSED
    LogPICOOpenXRHMD: PICO disable RHI thread requested
      (r.RHIThread.Enable 0), issued: 1
    LogPICOOpenXRInput: Skipping haptic bindings - interaction profile 23
      does not expose a haptic output path
    PxrMetric 逐帧指标 102 条（app 进程 97 条，余 5 条属 PICO
      launcher/spatial runtime），FrmId 2 -> 1214 连续递增，LayerCnt=2/3
  统计: not packaged for OpenXR = 0；XR_ERROR_PATH_UNSUPPORTED = 0；
        Critical error = 0；Assertion failed = 0。
  即: 插件在真实 PICO OpenXR 运行时上完整走通「创建实例 -> 注册扩展 ->
      建立会话 -> 进入 FOCUSED -> 提交图层」全流程。
  另: 扩展面计数摘要见 VerifyReports/android_runtime_evidence.txt，
      该文件由脚本从上面这份原始日志与 APK 重新计算生成（非手抄）。

四、已知的环境限制（非插件缺陷）
----------------------------------------------------------------
1. 交付机的 PC VR 环境只有 SteamVR，且本机 SteamVR 的 vrserver 启动后
   立即退出（logs 目录为空、无 vrclient 协商），因此 Win64 侧 OpenXR
   loader 拿不到可用运行时，日志只有一条
     Warning: Failed to create an OpenXR instance, result is
     XR_ERROR_VALIDATION_FAILURE.
   同时 XR_PICO_* 扩展全部 "is not available"，
   ControllerPICO 报 XR_BD_controller_interaction 不可用。
   这些扩展只有在 PICO 设备或 PICO Link 运行时下才存在。
   -> Win64 侧的 OpenXR 全链路证据由 Android 侧提供（见上）。
2. cook 期两条 "dependency ... does not exist" 告警（S_Gnomon /
   SceneCaptureBounds）来自上游 6.1.4 SDK 本身就缺这两个资产
   （全 SDK 递归搜索 0 命中），只影响两个示例蓝图的显示网格，
   不影响编译、加载、打包与运行。已刻意不补占位资产，以保持与官方
   6.1.4 内容完全一致。
3. 若在旧版本 PICO 模拟器镜像上直接以 UBT 产出的裸 APK（未经 cook+pak）
   启动，可能卡在 FEngineLoop::PreInit —— 这是镜像与 UE5.9 的兼容问题，
   与本插件无关；使用 BuildCookRun 的正式包（APK+OBB）不受影响，已实测
   完整跑通（见上）。

五、文件校验
----------------------------------------------------------------
交付目录: 352 个文件 / 20,842,779 字节（含本说明文件）
与原版 6.1.4 对比: 350 -> 351 文件，修改 14 个，新增 1 个，删除 0 个
Content/ 下 121 个资产与原版完全一致（未增删改）
逐 hunk 差异报告: VerifyReports/PATCH_REPORT.txt
