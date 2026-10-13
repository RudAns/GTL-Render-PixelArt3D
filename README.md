# GTL-Render-PixelArt3D

3D 场景实时渲染成像素画的交互演示（UE 5.8，C++）。对应 GameTech-Lab 调研报告《3D 像素画渲染：从 Never's End 到 UE5》，复现 Hypersect 在 GDC 2026《Never's End》讲座中的整套做法，并把每一步拆成可开关、可调参、可视化的展台。

![总览](Docs/shots/00_overview.png)

## 运行

1. 用 UE 5.8 打开 `PixelArt3D.uproject`，首次打开会编译 C++ 模块，然后点 Play；或者编译后双击 `Run.bat` 以独立窗口运行。
2. 默认地图 `/Game/Maps/Workshop`。地形、展台和说明牌都由 `AIslandBuilder` 在运行时生成。

## 操作

| 按键 | 作用 |
|---|---|
| WASD / 方向键 | 移动士兵（相对相机朝向）。走进彩色圆台进入对应展台，底部显示原理说明 |
| 1–0、P、T、H、B、G | 分步开关：低分辨率、相机吸附、亚像素补偿、物体与骨骼吸附、朝向量化与阶跃帧、排序模式、切环策略、光照位置、描边归属、描边清理、抖动、toon 色阶、自建阴影、放大方式、像素格 |
| F1 / V / F2 | 调试视图（物体 ID、排序层级、真实深度、推移后深度、纯光照）/ 前后关系图 / 排序体积线框 |
| Q / R | 旋转视角，每档 45°（参数页可改为 30° 或 90°），切换时平滑过渡 |
| Z X、, .、[ ] | 像素尺寸、每米像素、光源方向；Tab 在「管线开关」和「参数」两页之间切换，参数可点击 − / + 调整 |
| M | 测量帧间闪烁率（GPU 回读低分辨率画面，对齐整像素平移后统计变化像素） |
| C | 挑战模式：随机关掉一项技术，猜是哪一项（数字键作答，N 下一题） |
| E | 在展台 ④ 推动红色横杆，观察循环遮挡如何被拆开 |
| Home / F3 | 恢复默认设置 / 隐藏界面 |

## 展台

| 展台 | 演示内容 |
|---|---|
| ① 分辨率与 texel 密度 | 三块不同贴图密度的地砖，对比原生分辨率、整数倍最近邻放大、双线性放大，可叠加像素格 |
| ② 平移跑道 | 相机自动平移，对比吸附开 / 关 / 亚像素补偿，右上角实时显示闪烁率（实测约 0% / 2% / 0%） |
| ③ 墓室与长矛 | 逐像素深度下长矛插进墙里被截断；精灵排序后整根长矛作为一层画在墙前，光照仍按真实位置计算 |
| ④ 三杆成环 | 45° 视角下三根横杆两两遮挡成环，对比最小面积切边与合并成一层两种策略，V 显示前后关系图 |
| ⑤ 描边、色阶与抖动 | 描边归前方 / 归后方 / 两侧都画、3×3 查表清理、屏幕空间与物体空间抖动 |
| ⑥ 转台 | 朝向量化为 8 方向、8 fps 阶跃关键帧、骨骼吸附，自建阴影图与 toon 光照 |

| 无排序（长矛被墙截断） | 精灵排序 |
|---|---|
| ![](Docs/shots/08_wall_nosort.png) | ![](Docs/shots/10_wall_sort.png) |

| 循环遮挡：合并成一层 | 排序层级调试视图 |
|---|---|
| ![](Docs/shots/15_cycle_merge.png) | ![](Docs/shots/16_cycle_levels.png) |

## 代码结构

| 位置 | 内容 |
|---|---|
| `Source/PixelArtRender` | 渲染模块（PostConfigInit）。`FPixelArtViewExtension` 控制内部分辨率，`FPixelUpscaler`（`ISpatialUpscaler`）做整数倍最近邻放大和亚像素平移，并提供闪烁率用的 GPU 回读 |
| `Shaders/PixelUpscale.usf` | 放大着色器 |
| `Source/PixelArt3D/PixelArtSubsystem` | 精灵注册、相机与物体吸附、精灵排序（分离轴判定、Tarjan 强连通分量、切环 / 合并 / 滞回、分层）、自建阴影图、材质参数 |
| `Source/PixelArt3D/PixelCameraManager` | 正交相机、像素网格吸附、亚像素余量 |
| `Source/PixelArt3D/PixelAnimInstance` | 阶跃动画与骨骼吸附（C++ AnimInstance Proxy） |
| `Source/PixelArt3D/IslandBuilder` | 场景与展台生成 |
| `Tools/import_kaykit.py` | 无头导入 KayKit 角色、动画、道具 |
| `Tools/build_assets.py` | 无头生成材质（排序推移 WPO、toon 光照与阴影、描边后处理）、贴图集和地图 |
| `Tools/iterate.ps1`、`Tools/capture.ps1` | 编译、重建资产、离屏自动截图（`-PixelCapture=<目录>`） |

## 资源

角色、动画与场景道具来自 Kay Lousberg 的 KayKit Adventurers 2.0 与 Halloween Bits（CC0），源文件和许可证在 `SourceAssets/KayKit`，由 `Tools/import_kaykit.py` 无头导入（统一缩放到角色约 160 cm，贴图最近邻、无 mip）。它们的贴图是渐变色条，网格 UV 落在色条上取色，正好适合 toon 色阶和低分辨率。地面用包里的 floor_dirt 地砖铺成，石子路、围栏、墓室、树木都来自同一个包；只有密度石板和三根彩色横梁仍用程序生成的方块与 16 texel/米 贴图集（`Tools/gen_atlas.py`）。

重新导入：删除 `Content/KayKit` 后运行

```
UnrealEditor-Cmd.exe PixelArt3D.uproject -run=pythonscript -script="<绝对路径>/Tools/import_kaykit.py"
```
