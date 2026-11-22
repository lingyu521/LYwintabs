# LYwintabs

LYwintabs 是 Windows 文件资源管理器插件。Windows 10 在资源管理器窗口中增加标签页和横向收藏栏。Windows 11 沿用系统原生标签页，并增加横向收藏栏。

## 下载和使用

[下载 0.1.0-preview.27 安装包（Windows x64）](https://github.com/lingyu521/LYwintabs/releases/download/v0.1.0-preview.27/LYwintabs-Win10-x64-0.1.0-preview.27-Setup.exe) · [查看发布说明](https://github.com/lingyu521/LYwintabs/releases/tag/v0.1.0-preview.27)

运行安装包，阅读最终用户许可协议并选择安装目录。安装完成后，用任务栏、Win+E 或双击文件夹打开 Windows 文件资源管理器。可从齿轮菜单调整主题、收藏栏显示和开机启动。

- **标签页**：在 Win10 中新建、切换及关闭标签，拖动标签调整顺序。标签宽度按目录名和可用空间调整。
- **横向收藏栏**：点击地址栏旁的星标收藏当前目录，或从收藏栏空白处的右键菜单添加。支持书签文件夹、拖动排序、移动书签及修改目标地址。
- **局域网互传**：点击收藏栏左侧的互传图标，搜索附近设备并收发文件。收到请求时可选择接收、另存为或拒绝。接收过程中支持暂停、继续和终止。[互传使用说明](transfer/README.md)

![最终用户许可协议安装页]

## 兼容性

目标平台为 Windows 10 22H2 x64 和 Windows 11 x64。当前安装包的 EULA 页面、安装及卸载已在 Windows 10 22H2（19045.6456）实测。preview.27 的原生文件夹导航和互传自动启动已在 Windows 10 22H2 和 Windows 11 25H2 验证。

## 从源码构建

在 Visual Studio 2026 中打开 `LYwintabs.sln`，选择 `Release | x64` 生成 Explorer 插件和启动器。原生项目使用 C++20、MSVC v145 和 Windows SDK 10.0.26100.0。输出在 `out/explorer/x64/Release/`。

互传模块使用 .NET 10 和 C# WinForms。发布方式见[互传构建说明](transfer/README.md)。安装包使用 Inno Setup 7，脚本为 [Install-LYwintabs.iss](explorer/Install-LYwintabs.iss)，编译时传入插件 DLL、启动器 EXE、自包含互传目录及构建标识。

## 开源许可

LYwintabs 采用 [Apache License 2.0](LICENSE)。安装向导显示[最终用户许可协议（EULA）](EULA.txt)，内含中文说明及 Apache-2.0 原文。图标、运行库及安装程序的适用许可见[第三方许可声明](THIRD-PARTY-NOTICES.md)。

开发者 王凌宇 · [提交问题](https://github.com/lingyu521/LYwintabs/issues)
