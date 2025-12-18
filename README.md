OpenGL 纹理与纹理映射综合实验报告
一、实验名称
OpenGL 纹理与纹理映射综合实验：多纹理混合、纹理动画、法线贴图、Mipmap 与 FBO 后处理
________________________________________
二、实验目的
1.	复习并掌握纹理（Texture）与纹理映射（Texture Mapping）的基本概念：纹理对象、纹理坐标、采样器、过滤与环绕。
2.	掌握在现代 OpenGL（3.3 Core）中实现纹理渲染的标准流程：
o	stb_image 加载图片 → 创建纹理对象 → 上传像素 → 设置参数 → 绑定采样器 → shader 采样。
3.	在基础贴图的基础上实现高级纹理效果（用于提升实验难度与得分）：
o	多纹理叠加混合（包含 Alpha 透明叠加）
o	纹理动画（滚动 + 波纹扰动）
o	法线贴图（Normal Mapping）增强微表面细节与光照质感
o	Mipmap 开/关对比（抗闪烁、抗锯齿的效果验证）
o	FBO 渲染到纹理 + 屏幕后处理（灰度/边缘/模糊）
________________________________________
三、实验环境与配置
•	操作系统：Windows
•	开发工具：Visual Studio（C++）
•	依赖管理：vcpkg（已集成）
•	库：GLFW、GLAD、stb_image
•	OpenGL：3.3 Core Profile
项目目录结构（真实磁盘目录）
项目目录/
 ├─ main.cpp
 └─ assets/
    ├─ diffuse.jpg       // 底图
    ├─ overlay.png       // RGBA 透明叠加纹理
    └─ normal.png        // 法线贴图
运行时路径关键设置（VS）
为保证相对路径 assets/... 可正确读取，在 VS 项目属性中设置：
项目 → 属性 → 调试 → 工作目录：$(ProjectDir)
________________________________________
四、实验原理
4.1 纹理与纹理映射
纹理可看作一张二维像素数组，上传至 GPU 后由 sampler2D 进行采样。纹理映射过程为：
1）顶点携带纹理坐标 UV；
2）光栅化阶段对 UV 插值；
3）片元着色器使用 UV 从纹理中采样得到颜色；
4）输出片元颜色到帧缓冲。
4.2 多纹理混合与 Alpha 透明叠加
本实验使用两张纹理：
•	diffuse（RGB）：作为底图
•	overlay（RGBA）：作为透明叠加特效层（气泡、光斑等）
透明叠加的关键是利用 overlay 的 alpha 通道作为权重。为了让 overlay 更像“光效层”，而不是“盖住底图”，采用加色式的叠加逻辑：
•	color = base + overlay.rgb * overlay.a * mixFactor
这样既保留底图细节，又能呈现自然的透明光效。
4.3 纹理动画（滚动 + 波纹）
纹理动画通过对 UV 进行时间相关变换实现，不需要改变几何：
•	滚动：uv += vec2(time * speed, 0)
•	波纹：uv.y += sin(uv.x * freq + time * k) * amp
其优点是实现简单、成本低、视觉提升明显，适合实验展示。
4.4 Mipmap 的作用与对比
Mipmap 是纹理的多级缩小版本，主要用于解决：
•	缩小时的闪烁（shimmering）
•	远处纹理的锯齿与摩尔纹
本实验支持 M 键切换 mipmap 使用与否，通过修改 GL_TEXTURE_MIN_FILTER 控制：
•	开启：GL_LINEAR_MIPMAP_LINEAR
•	关闭：GL_LINEAR
4.5 法线贴图（Normal Mapping）
法线贴图通过纹理存储每个像素的法线方向（切线空间），使平面呈现凹凸细节。流程：
1）从 normal map 采样 n（范围 0~1）
2）映射到 -1~1：n = n*2 - 1
3）通过 TBN 矩阵将切线空间法线转换到世界空间：N = TBN * n
4）用 N 参与 Blinn-Phong 光照计算（diffuse + specular）
本实验支持 N 键开启/关闭法线贴图，以对比“普通平面光照”与“凹凸质感光照”的差异。
4.6 FBO 后处理
后处理流程采用两次渲染（Two-pass）：
•	Pass1：绑定 FBO，将场景渲染到颜色纹理 colorTex
•	Pass2：解绑 FBO，渲染全屏四边形，用 colorTex 做输入并在屏幕 shader 中实现滤镜
支持 1~4 切换后处理模式：
•	1：正常
•	2：灰度
•	3：边缘检测（Sobel 简化）
•	4：轻微模糊（3×3）
________________________________________
五、实验实现流程
1.	初始化 GLFW，创建窗口与 OpenGL 上下文（3.3 Core）
2.	初始化 GLAD，加载 OpenGL 函数指针
3.	构建带 TBN 的 quad 顶点数据（Pos/UV/Normal/Tangent/Bitangent）
4.	编译并链接 Scene Shader（纹理动画 + 混合 + 法线贴图光照）
5.	编译并链接 Screen Shader（后处理）
6.	加载三张纹理（diffuse/overlay/normal），设置纹理参数并生成 Mipmap
7.	创建 FBO（colorTex + depth/stencil RBO）
8.	主循环：
o	读取键盘输入（N/M/1~4）更新开关与模式
o	Pass1：渲染场景到 FBO
o	Pass2：后处理并输出到屏幕
9.	退出释放资源
________________________________________
六、运行效果与对比实验
6.1 最终运行截图
<img width="865" height="631" alt="image" src="https://github.com/user-attachments/assets/e55e08c3-8bc8-46c5-97d5-4dc4355107bc" />



图 1：最终效果（多纹理 + 动画 + 法线贴图 + FBO 后处理）
6.2 法线贴图开/关对比（N 键）
•	关闭法线贴图（NormalMap OFF）：表面光照变化较平滑，缺少“砖缝凹凸”的细节。
•	开启法线贴图（NormalMap ON）：同样的几何平面呈现明显凹凸、细节更丰富，高光变化更真实。
 
6.3 Mipmap 开/关对比（M 键）
•	关闭 Mipmap（Mipmap OFF）：纹理缩小或斜视时更容易出现闪烁和锯齿。
•	开启 Mipmap（Mipmap ON）：缩小显示更稳定、细节更平滑，抗闪烁效果明显。
 
 

6.4 后处理模式切换（1~4）
•	正常：显示原场景
•	灰度：验证后处理对纹理颜色的变换
•	边缘：边缘检测增强轮廓
•	模糊：降低高频细节，展示卷积思想
   
________________________________________
七、关键代码模块说明（按功能）
1）纹理加载模块（stb_image）
•	输入：图片路径
•	输出：OpenGL 纹理对象 ID
•	关键：对齐 glPixelStorei(GL_UNPACK_ALIGNMENT, 1)；失败检测防崩溃。
2）多纹理与 Alpha 混合
•	两个采样器：diffuse + overlay
•	使用 overlay alpha 控制叠加强度，实现自然透明光效。
3）纹理动画模块
•	对 UV 做滚动 + 波纹扰动，动态效果明显且无需修改几何。
4）法线贴图模块（TBN）
•	构造 TBN 矩阵并传入片元
•	法线贴图采样后转换到世界空间参与 Blinn-Phong。
5）FBO 与后处理模块
•	Pass1 输出到 colorTex
•	Pass2 全屏 quad 实现灰度/边缘/模糊等效果。
6）交互控制模块
•	N：开关法线贴图
•	M：开关 Mipmap
•	1~4：切换后处理模式
窗口标题实时显示开关状态，便于截图与对比实验记录。
________________________________________
八、问题与解决（体现“自己做过”）
8.1 纹理加载失败：Failed to load texture
现象：运行时提示无法加载 assets/overlay.png。
原因：VS 默认工作目录可能为 x64/Debug/，导致相对路径解析错误。
解决：将 VS 工作目录设置为 $(ProjectDir)，并确保 assets/ 是真实磁盘目录。
8.2 黑屏或驱动级异常的预防
图形程序若 shader 编译失败或 uniform 失效，有可能出现“黑屏无提示”。
因此在程序中加入：
•	shader 编译/链接日志输出
•	uniform location 缓存 + -1 判断
•	texture 读取失败时直接退出并输出路径
使问题定位更清晰，提高程序稳定性。
________________________________________
九、实验结论与心得
本实验从基础纹理映射出发，完整实现了现代 OpenGL 中常见的纹理技术链路：多纹理、透明叠加、动态 UV 动画、法线贴图增强质感，以及 FBO 后处理的两阶段渲染。通过 N/M 键的对比实验可以直观验证法线贴图与 Mipmap 的作用：法线贴图提升材质细节与光照真实感，Mipmap 提升缩小场景下纹理稳定性与抗闪烁能力。
此外，FBO 后处理结构为后续进一步实现 Bloom、HDR、色调映射等高级渲染效果提供了可扩展基础。
________________________________________
十、附录：运行说明（提交很加分）
•	资源文件放置：assets/diffuse.jpg、assets/overlay.png、assets/normal.png
•	VS 工作目录：$(ProjectDir)
•	按键：
o	N：开/关法线贴图
o	M：开/关 Mipmap
o	1~4：后处理模式切换（正常/灰度/边缘/模糊）

