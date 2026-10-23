# 把 3D 场景实时渲染成像素画

从《Never's End》的 GDC 2026 讲座到 UE5：技术调研 + UE 5.8 可交互复现。

[![调研报告封面](Docs/shots/00_research_cover.png)](Docs/Research.html)

## 调研报告

**[Docs/Research.html](Docs/Research.html)**：单文件网页，约 14,000 字，73 条中日韩英文献，带 4 个交互演示，下载后用浏览器打开。

报告以 Hypersect 的 Ryan Juckett 在 GDC 2026 的讲座「How We Draw a 3D Sprite World: The Stylized Art of Never's End」为主线，拆解用 3D 模型呈现手绘像素精灵观感的完整管线，比较了业界五条技术路线（含 t3ssel8r、《Dave the Diver》、HD-2D 等做法），并给出手游分档与 UE5 复现蓝图。主要结论：

- 低分辨率 3D 渲染不等于像素画。像素画有六项隐性约束：texel 与屏幕像素一一对应、整像素位移、按图层整体遮挡、单像素描边、受限色阶、阶跃关键帧，每一项都要在管线里单独重建。
- UE5 的核心难点只有一个：精灵排序把次序写进深度后，引擎的延迟光照会按被推移的位置计算。用 Unlit 材质按真实世界坐标自己算 toon 明暗、自建阴影图，可以不改引擎。
- 循环遮挡时，强连通分量内互不相交的物体可以合并成一层、保留真实深度，大多数情况不需要切边。
- 手游可行，低分辨率渲染让填充率下降一个数量级以上。

## 技术落地文档

**[Docs/Implementation.html](Docs/Implementation.html)**：按管线顺序整理每一步的做法、参数、UE 接入要点和坑点清单，示意图内嵌，可以直接对照源码搬进自己的项目。其中「相机跟随：分层合成」和「角色影子跟随角色层」两节是本工程在原方案之外解决运动时抖动的做法。

> 提示：如需在自己的项目中使用，可以把本工程的源码和这份 HTML 一起交给 AI 编程助手，让它参照文档把需要的部分接入你的工程。

## UE 5.8 可交互 Demo

操控一名骑士在像素风墓园里走动，六个彩色踏板各对应一项技术，走上去就弹出原理说明。左侧面板可以逐项开关管线里的每一步，右上角实时显示内部分辨率、闪烁率、角色在屏幕上的抖动和排序统计。

![45° 视角总览](Docs/shots/27_yaw45.png)

### 运行

- 引擎：UE 5.8。`Run.bat` 会自动查找引擎：环境变量 `UE_ROOT`、启动器注册表、各磁盘根目录下的 `UE_5.8` 与 `Program Files\Epic Games\UE_5.8`；都找不到时先设置 `UE_ROOT`。
- 双击 `Run.bat`：编译编辑器模块后以游戏窗口启动。也可以直接用编辑器打开 `PixelArt3D.uproject`，地图是 `/Game/Maps/Workshop`，场景和展台在运行时生成。

### 操作

| 按键 | 作用 |
|---|---|
| WASD / 方向键 | 移动（按屏幕方向等速，斜向走干净的 45° 像素线） |
| Q / R | 旋转视角，每档 45°（参数页可改为 30° 或 90°） |
| 1–0、P、T、H、B、G、J | 管线开关：低分辨率、相机吸附、亚像素补偿、物体与骨骼吸附、朝向量化与阶跃帧、排序模式、切环策略、光照位置、描边归属、描边清理、抖动、toon 色阶、自建阴影、放大方式、像素格、相机跟随方式 |
| F1 / V / F2 | 调试视图（物体 ID、排序层级、真实深度、推移后深度、纯光照）/ 前后关系图 / 排序体积线框 |
| Tab | 在「管线开关」和「参数」两页之间切换；参数页可点击 − / + 调像素尺寸、每米像素、俯角、光源方向等 |
| M | 测量帧间闪烁率（GPU 回读低分辨率画面） |
| C | 找穿帮挑战：随机关掉一项技术，猜是哪一项 |
| E | 在展台 ④ 推动红色横杆，改变循环遮挡的重叠面积 |
| Home / F3 | 恢复默认设置 / 隐藏界面 |

### 六个展台

| 展台 | 演示内容 |
|---|---|
| ① 分辨率与 texel 密度 | 三块不同贴图密度的石板，对比原生分辨率、整数倍最近邻放大、双线性放大，可叠加像素格 |
| ② 平移跑道 | 镜头自动平移，对比吸附开 / 关 / 亚像素补偿，实测闪烁率约 0% / 1.4–2% / 0%；J 切换四种相机跟随方式 |
| ③ 墓室与长矛 | 贴着墓室侧墙走：逐像素深度下长矛插进墙里被截断，精灵排序后整根长矛作为一层画在墙前，光照仍按真实位置计算 |
| ④ 三杆成环 | 45° 视角下三根横杆两两遮挡成环，对比最小面积切边、合并成一层、滞回三种策略 |
| ⑤ 描边、色阶与抖动 | 描边归前方 / 归后方 / 两侧都画、3×3 查表清理、明暗交界的屏幕空间与物体空间抖动 |
| ⑥ 转台 | 朝向量化为 8 方向、阶跃关键帧、骨骼吸附，自建阴影图与 toon 光照 |

| 无排序：长矛被墙截断 | 精灵排序后 |
|---|---|
| ![](Docs/shots/08_wall_nosort.png) | ![](Docs/shots/10_wall_sort.png) |

| 循环遮挡：合并成一层 | 排序层级调试视图 |
|---|---|
| ![](Docs/shots/15_cycle_merge.png) | ![](Docs/shots/16_cycle_levels.png) |

| 描边两侧都画 | 纯光照与自建阴影 |
|---|---|
| ![](Docs/shots/19_outline_both.png) | ![](Docs/shots/25_light_only.png) |

### 代码结构

| 位置 | 内容 |
|---|---|
| `Source/PixelArtRender` | 渲染模块：控制内部分辨率，自定义 `ISpatialUpscaler` 做整数倍放大、亚像素平移、角色层与角色影子合成，以及 GPU 回读 |
| `Shaders/PixelUpscale.usf` | 放大着色器 |
| `Source/PixelArt3D/PixelArtSubsystem` | 精灵注册与排序（分离轴、Tarjan、合并 / 切边 / 滞回、分层）、三张自建阴影图、材质参数 |
| `Source/PixelArt3D/PixelCameraManager` | 正交相机、像素网格吸附、四种跟随方式 |
| `Source/PixelArt3D/PixelCharacter`、`PixelAnimInstance` | 网格根吸附（落地、斜向）、骨骼吸附（回差）、阶跃动画 |
| `Source/PixelArt3D/IslandBuilder` | 场景与展台生成 |
| `Tools/build_assets.py` | 无头生成材质（排序推移 WPO、toon 光照与阴影、描边后处理）、贴图集和地图 |
| `Tools/import_kaykit.py` | 无头导入角色、动画和场景道具 |
| `Tools/iterate.ps1`、`Tools/capture.ps1` | 编译、重建资产、离屏自动截图；自测命令行开关见技术落地文档第 9 节 |

### 资源

角色、动画与场景道具来自 Kay Lousberg 的 KayKit Adventurers 2.0 与 Halloween Bits（CC0），源文件和许可证在 `SourceAssets/KayKit`。它们的贴图是渐变色条，网格 UV 落在色条上取色，正好适合 toon 色阶和低分辨率。只有密度石板和三根彩色横杆是程序生成的方块，用 16 texel/米 的贴图集（`Tools/gen_atlas.py`）。

## 许可证

本仓库自己编写的代码、脚本、着色器、文档和截图以 [MIT 许可证](LICENSE) 发布。下列第三方内容不在 MIT 范围内，仍按各自的许可使用：

- 角色、动画与场景道具（`Content/KayKit`、`SourceAssets/KayKit`）：Kay Lousberg 的 KayKit，[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)，许可证原文在 `SourceAssets/KayKit`。
- Unreal Engine 5.8：打开、编译和运行本工程需遵守 [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal)。
