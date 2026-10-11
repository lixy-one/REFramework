# REFramework — 简体中文汉化版

> 基于 [praydog/REFramework](https://github.com/praydog/REFramework) 的界面汉化分支。
>
> 在保留全部原有功能的前提下，给菜单界面加入**简体中文**支持，并提供**运行时语言切换**。

---

## 改动内容

### 1. 界面汉化（约 250 条翻译）

新增 `src/Localization.hpp` / `src/Localization.cpp`：

```cpp
l10n::tr("About")        // → "关于"
l10n::tr("Camera")       // → "相机"
l10n::tr("Font Size")    // → "字体大小"
```

- **词表驱动**：`std::unordered_map<std::string_view, const char*>`，**查不到就原样返回英文** —— 不会出现空白或乱码，最坏情况就是保持英文。
- **零分配**：命中时直接返回静态字符串指针；未命中时返回传入指针。
- **可关闭**：`l10n::set_enabled(false)` 立刻恢复全英文。

接入位置（覆盖所有 UI 绘制路径）：

| 位置 | 说明 |
|---|---|
| `src/Mod.hpp` | 13 处 —— `ModToggle` / `ModSlider` / `ModComboString` / `ModKey` 等所有控件的 `draw()` |
| `src/mods/*.cpp` | 245 处 —— 各模块的 `CollapsingHeader` / `TextWrapped` / `Button` / `Checkbox` / `.*draw("…")` |

### 2. 运行时语言切换

在 `配置`（Configuration）面板顶部新增 **语言** 下拉框：

```
配置
├─ 语言            [简体中文 ▾]     ← 新增
├─ 菜单键          45
├─ 显示光标键      未绑定
├─ 记住菜单开合状态  ☐
├─ 菜单打开时绘制光标 ☐
├─ 字体            [DEFAULT ▾]
└─ 字体大小         13
```

- 切换**即时生效**（`l10n::tr` 每帧查表，下一帧界面语言就变了）
- 选择会写入 `re2_fw_config.txt` 的 `REFrameworkConfig_Language`，**下次启动保持**
- 选项：`简体中文` / `English`

### 3. 字体的 CJK 字形范围（关键修复）

**官方版本的字体加载没有指定字形范围**，导致 ImGui 只光栅化 ASCII（`0x20–0xFF`），
所有中文都会渲染成 `?` —— 即使放入了带完整中文字形的字体文件也一样。

`src/REFramework.cpp` 的 `init_fonts()` 中补上：

```cpp
const ImWchar* cn_glyph_ranges = fonts->GetGlyphRangesChineseSimplifiedCommon();
...
cfg.GlyphRanges = cn_glyph_ranges;   // 两处字体加载各加一行
```

### 4. 编译选项 `/utf-8`

`cmake.toml` 的 REFramework 目标加入 `/utf-8`：

```toml
compile-options = ["/GS-", "/bigobj", "/EHa", "/utf-8"]
```

没有这个选项时，MSVC 会按系统代码页（中文系统为 GBK）解析源文件，
源码里的 UTF-8 中文字面量会变成乱码。

---

## 使用

### 方式一：直接用编译好的 DLL

1. 下载 `dinput8.dll`
2. 放到游戏根目录（覆盖原文件），例如：
   ```
   ...\RESIDENT EVIL 4  BIOHAZARD RE4\dinput8.dll
   ```
3. 启动游戏，按 `Insert` 打开菜单
4. **如果中文显示为 `?`** → 在 `配置` → `字体` 里选一个中文字体（如 `simhei.ttf`）：
   - 把任意中文字体（`.ttf` / `.otf`）放进 `reframework\fonts\`
   - 或者直接从 `C:\Windows\Fonts\` 复制 `simhei.ttf`（黑体）过去

### 方式二：自己编译

```bat
:: 需要 Visual Studio 2022/2026 + CMake
git clone --recurse-submodules https://github.com/lixyone/REFramework.git
cd REFramework

:: 用 VS 自带的 cmake（PATH 里的旧版可能不支持 VS2026 生成器）
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DDEVELOPER_MODE=ON
cmake --build build --config Release

:: 产物
:: build\bin\REFramework\dinput8.dll
```

> **注意**：CMake 4.0.x 不支持 `Visual Studio 18 2026` 生成器，
> 请使用 **VS 自带的 cmake 4.2+**（位于
> `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`）。
>
> 另外项目含 C# 组件，**不能用 Ninja 生成器**（Ninja 不支持 C#）。

---

## 汉化维护

新增翻译只需编辑 `src/Localization.cpp` 的表：

```cpp
{ "Global FOV",   "全局视场角" },
{ "Font Size",    "字体大小" },
```

然后在 UI 代码里包一层：

```cpp
ImGui::CollapsingHeader(l10n::tr("Camera"))
m_font_size->draw(l10n::tr("Font Size"))
```

---

## 已知情况

- **未覆盖的文本**：部分带格式符的字符串（如 `"%s: %i"`）不翻译 —— 里面含动态内容，翻译会破坏格式。这类会回退英文。
- **框架内置字符串**：`Default Menu Key: Insert` 这类状态栏提示已翻译；
  但运行时错误信息（如 VR 运行时缺失的具体错误码文本）来自系统/驱动，保持原文。
- **脚本菜单**：Lua 脚本自己的菜单文字由脚本提供，不受本汉化影响
  （例如 `RE4 锁头自瞄 & ESP` 是脚本自带的）。

---

## 许可

与上游一致：[MIT License](LICENSE)

原作者：**praydog** — <https://github.com/praydog/REFramework>
