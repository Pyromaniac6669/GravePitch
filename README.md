# GravePitch

GravePitch 是一款面向吉他手的开源调音器，目标是同时提供 Windows 独立程序和 VST3 插件，便于日常练琴、录音和在 DAW 中快速检查调弦状态。

当前版本处于早期开发阶段，核心功能包括：

- 实时单音音高检测
- 当前音名、频率和 cents 偏差显示
- 按所选调弦匹配最近目标弦
- 常用吉他调弦预设
- 自定义六弦调弦
- A4 校准频率调整
- 英文／简体中文界面切换
- Standalone 和 VST3 两种构建目标

## 适用场景

- 用麦克风或声卡输入给吉他调音
- 在 DAW 轨道中作为 VST3 插件查看实时音高
- 使用 Standard、Drop D、Drop C、降半音、降全音、C Standard 等常见调弦
- 为低调弦或自定义调弦保存自己的六弦目标音

## 构建要求

- Windows
- Visual Studio C++ Build Tools
- CMake 3.22 或更新版本
- JUCE 源码

## 构建方式

如果 `cmake` 已在 PATH 中：

```powershell
cmake -S . -B build -DGRAVEPITCH_JUCE_PATH=external/JUCE
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

如果只在 Visual Studio Build Tools 环境里能使用 CMake，打开 Developer PowerShell for VS 后执行：

```powershell
cmake -S . -B build -G Ninja -DGRAVEPITCH_JUCE_PATH="D:/path/to/JUCE"
cmake --build build
ctest --test-dir build --output-on-failure
```

如果 JUCE 不在 `external/JUCE`，请把 `GRAVEPITCH_JUCE_PATH` 改成你本机的 JUCE 源码路径。

## 项目结构

- `include/gravepitch/core/`：不依赖 JUCE 的核心接口
- `src/core/`：音名/频率换算、调弦模型、音高检测、平滑和调音引擎
- `src/juce/`：JUCE 音频处理器、Standalone/VST3 壳层和界面
- `tests/`：核心逻辑测试

## 当前限制

- 仅实现单音调音，不支持和弦或复音识别。
- 首版主要面向 Windows。
- 目前不提供安装器，需要从源码构建。

## 授权

GravePitch 使用 AGPL-3.0-or-later 授权。
