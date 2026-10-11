# REFramework — 简体中文版

> 基于 [praydog/REFramework](https://github.com/praydog/REFramework) 的中文分支。
>
> 在保留**全部原有功能**的前提下，加入**完整界面汉化**，并内置一台 **MCP / HTTP API 服务**，
> 让外部程序（AI 助手、调试脚本）能直接读取游戏运行时数据。

---

## 新增功能一览

| 功能 | 说明 | 主要文件 |
|---|---|---|
| **① 界面汉化** | 约 580 条词表 + 500 余处 UI 接入，覆盖全部菜单 | `src/Localization.{hpp,cpp}` |
| **② 语言切换** | 「配置 → 语言」下拉框，**即时生效**并持久化 | `src/mods/REFrameworkConfig.*` |
| **③ 中文显示修复** | 显式声明 CJK 字形范围（官方版中文显示 `?` 的根因） | `src/REFramework.cpp` |
| **④ `/utf-8` 编译选项** | 防止源码里的中文字面量被按系统代码页解析成乱码 | `cmake.toml` |
| **⑤ 内置 MCP 服务** | HTTP 接口读取游戏数据，**无需额外插件 DLL** | `src/mods/MCP.{hpp,cpp}` |

**原有功能全部保留**：脚本系统、插件加载器、对象浏览器、调用链查看器、相机/画面/自由相机、
VR 支持、松散文件加载、损坏文件检测、超宽屏修复等。

---

## ⑤ 内置 MCP 服务（HTTP API）

在游戏进程内运行一台 HTTP 服务，把游戏运行时数据暴露给外部程序。

### 快速上手

```bash
# 存活检查
curl "http://127.0.0.1:17890/api/ping"
# → {"ok":true,"service":"reframework-mcp","port":17890}

# 统计信息
curl "http://127.0.0.1:17890/api/stats"
# → {"ok":true,"types":162644}

# 搜索类型
curl "http://127.0.0.1:17890/api/search?q=CharacterManager&limit=5"
# → chainsaw.CharacterManager (size=280)

# 拿单例地址
curl "http://127.0.0.1:17890/api/singleton?name=chainsaw.CharacterManager"
# → {"native":"...","managed":"0000025448CF2C40"}

# 读它的全部字段
curl "http://127.0.0.1:17890/api/fields?addr=0000025448CF2C40&type=chainsaw.CharacterManager&max=10"
# → 46 个字段的名称/类型/数值
```

### 接口清单

| 接口 | 参数 | 说明 |
|---|---|---|
| `GET /api/ping` | — | 存活检查与端口号 |
| `GET /api/stats` | — | 类型数量等统计 |
| `GET /api/search` | `q=<关键字>&limit=N` | 按名字搜索类型 |
| `GET /api/type` | `name=<全名>&fields=1&methods=1` | 类型详情（字段/方法默认关闭，避免卡顿） |
| `GET /api/singleton` | `name=<类型全名>` | 取原生 / 托管单例地址 |
| `GET /api/mem` | `addr=<hex>&size=N` | 读内存（hex + 整数 + 浮点三种视图） |
| `GET /api/fields` | `addr=<hex>&type=<全名>&max=N` | 读对象的全部字段值 ★最实用 |
| `GET /api/field` | `addr=<hex>&type=<全名>&name=<字段>` | 读单个字段 |
| `GET /api/write` | `addr=<hex>&hex=<字节>` | 写内存（**需开启写开关**） |
| `GET /api/alloc` | `size=N` | 分配内存（**需开启写开关**） |

### 配置（菜单：配置 → MCP）

| 配置项 | 默认 | 说明 |
|---|---|---|
| **启用 MCP 服务** | 关 | 勾选即启动监听，取消即停止 |
| **端口** | `17890` | **运行中不可修改**（输入框自动禁用，需先停服务） |
| **允许写操作 / 调用方法** | 关 | 写接口的总开关，**默认拒绝** |

配置文件为 `re2_fw_config.txt`：

```ini
MCP_Enabled=true
MCP_Port=17890
MCP_AllowWrite=false
```

### 设计要点

- **只监听 `127.0.0.1`** —— 不对外网暴露。
- **线程安全**：HTTP 线程只做 socket 收发；请求进入队列，由**游戏线程**（`on_frame`）取出处理。
  所有 SDK 访问都发生在拥有它的线程上，避免跨线程调用崩溃。
- **端口不能使用 `ModInt32`** —— 它的 `draw()` 是 `ImGui::SliderInt(..., 5, 40)`，
  会把 `17890` 强制夹成 `40`。因此端口用普通 `int` + `InputInt`。
- **停止服务时关闭监听 socket** —— 阻塞中的 `accept()` 只有 socket 被关闭才会返回，
  单设标志位不够（会导致"关了还在跑"）。
- **超时保护**：单次请求最多等 10 秒，超时返回 `{"ok":false,"err":"timeout"}`。

### 使用场景

| 场景 | 做法 |
|---|---|
| **AI 助手辅助调试** | 把接口告诉 AI，它可以自己查询类型结构、对象字段、内存 |
| **Mod 开发** | 快速定位字段偏移、确认运行时数值，不用反复截图 |
| **自动化脚本** | 用任意语言（Python / PowerShell / Node）读取游戏状态 |

---

## ① 界面汉化

```cpp
l10n::tr("About")        // → "关于"
l10n::tr("Camera")       // → "相机"
l10n::tr("Font Size")    // → "字体大小"
```

- **词表驱动**：`std::unordered_map<std::string_view, const char*>`。
  **查不到就原样返回英文** —— 不会出现空白或乱码，最坏情况就是保持英文。
- **零分配**：命中时直接返回静态字符串指针；未命中时返回传入指针。
- **可整体关闭**：`l10n::set_enabled(false)` 立刻恢复全英文。

接入范围（覆盖所有 UI 绘制路径）：

| 位置 | 说明 |
|---|---|
| `src/Mod.hpp` | `ModToggle` / `ModSlider` / `ModComboString` / `ModKey` 等所有控件的 `draw()` |
| `src/mods/*.cpp`（23 个文件） | `CollapsingHeader` / `Text*` / `Button` / `Checkbox` / `Combo` / `Slider*` / `Drag*` / `Input*` / `TreeNode` / `SetTooltip` / `.*draw("…")` |

---

## ② 运行时语言切换

「配置」面板顶部新增 **语言** 下拉框：

```
配置
├─ 语言              [简体中文 ▾]     ← 新增
├─ 菜单键            45
├─ 显示光标键        未绑定
├─ 记住菜单开合状态   ☐
├─ 菜单打开时绘制光标 ☐
├─ 字体              [DEFAULT ▾]
└─ 字体大小          13
```

- 切换**即时生效**（`l10n::tr` 每帧查表，下一帧界面就变了）
- 写入 `re2_fw_config.txt` 的 `REFrameworkConfig_Language`，**下次启动保持**
- 选项：`简体中文` / `English`

---

## ③ 中文字体的 CJK 字形范围（关键修复）

**官方版本的字体加载没有指定字形范围**，导致 ImGui 只光栅化 ASCII（`0x20–0xFF`），
所有中文都会渲染成 `?` —— 即使放入了带完整中文字形的字体文件也一样。

修复位于 `REFramework.cpp` 的 `init_fonts()`：

```cpp
const ImWchar* cn_glyph_ranges = fonts->GetGlyphRangesChineseSimplifiedCommon();
...
cfg.GlyphRanges = cn_glyph_ranges;   // 两处字体加载各加一行
```

**如果中文仍显示为 `?`**：把任意中文字体（`.ttf` / `.otf`）放进 `reframework/fonts/`，
例如直接从 `C:\Windows\Fonts\` 复制 `simhei.ttf`（黑体）过去。

---

## ④ `/utf-8` 编译选项

`cmake.toml` 的 REFramework 目标：

```toml
compile-options = ["/GS-", "/bigobj", "/EHa", "/utf-8"]
```

没有它时，MSVC 会按系统代码页（中文系统为 GBK）解析源文件，
源码里的 UTF-8 中文字面量会变成乱码。

---

## 构建

```bat
:: 需要 Visual Studio 2022/2026 + CMake 4.2+
git clone --recurse-submodules https://github.com/lixy-one/REFramework.git
cd REFramework

:: 用 VS 自带的 cmake（PATH 里的旧版可能不识别 VS2026 生成器）
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

%CMAKE% -S . -B build -G "Visual Studio 18 2026" -A x64 -DDEVELOPER_MODE=ON
%CMAKE% --build build --config Release

:: 产物：build\bin\REFramework\dinput8.dll
```

**注意事项**

| 问题 | 说明 |
|---|---|
| **CMake 版本** | 4.0.x 不识别 `Visual Studio 18 2026` 生成器；需用 **VS 自带的 cmake 4.2+** |
| **不能用 Ninja** | 项目含 C# 组件，Ninja 生成器不支持 C# |
| **`DEVELOPER_MODE`** | `ON` = 带对象浏览器/调用链查看器/场景对象显示；`OFF` = 不带（体积几乎不变，代码仍会编译进去） |
| **`BAREBONES`** | 定义后砍掉 MCP、VR、相机、画面等一大片，**不要用** |

---

## 使用

1. 下载 `dinput8.dll`
2. 放到游戏根目录（覆盖原文件）：
   ```
   ...\RESIDENT EVIL 4  BIOHAZARD RE4\dinput8.dll
   ```
3. 启动游戏，按 `Insert` 打开菜单
4. 需要 MCP 时：`配置 → MCP → 启用 MCP 服务`

---

## 汉化维护

新增翻译只需编辑 `src/Localization.cpp` 的词表：

```cpp
{ "Global FOV",   "全局视场角" },
{ "Font Size",    "字体大小" },
```

然后在 UI 代码里包一层：

```cpp
ImGui::CollapsingHeader(l10n::tr("Camera"))
m_font_size->draw(l10n::tr("Font Size"))
```

**批量排查未翻译项**：提取所有 `l10n::tr("…")` 的字面量，与词表 key 求差集即可。

---

## 已知情况

- **未覆盖的文本**：含格式符且含义依赖上下文的字符串（如 `"%s: %i"`）不翻译 —— 翻译会破坏格式，这类回退英文。
- **运行时错误信息**：来自系统 / 驱动 / VR 运行时的具体错误文本保持原文。
- **脚本菜单**：Lua 脚本自己的菜单文字由脚本提供，不受本汉化影响。
- **`is_valuetype` / `is_primitive` / `is_enum`**：在部分游戏版本上会长时间阻塞游戏线程，本分支未修改这些 API。

---

## 与原版的差异（技术摘要）

```
src/Localization.hpp / .cpp        新增：翻译查表
src/mods/MCP.hpp / .cpp            新增：内置 HTTP 服务
src/Mod.hpp                        每个控件的 draw() 走 l10n::tr
src/REFramework.cpp                字体加载指定 CJK 字形范围
src/mods/REFrameworkConfig.*       新增「语言」下拉框
src/mods/*.cpp (21 个)             全部 UI 文本走 l10n::tr
src/Mods.cpp                       注册 MCP 模块
cmake.toml                         /utf-8 + 链接 ws2_32
README.zh-CN.md                    本文件
```

---

## 许可

与上游一致：[MIT License](LICENSE)

原作者：**praydog** — <https://github.com/praydog/REFramework>
