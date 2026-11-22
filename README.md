# LYwintabs

LYwintabs 集成在 Windows 自带的文件资源管理器中。通过任务栏、`Win+E` 或双击文件夹打开资源管理器，就能在使用系统地址栏、搜索和文件操作的同一个窗口里，用标签切换目录、在横向收藏栏打开常用路径，或启动局域网互传。

| 系统 | 标签页与收藏栏 |
| --- | --- |
| Windows 10 22H2 x64 | 在窗口顶部增加标签栏，在系统地址栏下方显示横向收藏栏。可以新建、复制和关闭标签，也能拖动调整顺序。标签宽度随目录名称和可用空间调整。顶部空间不足时，标签栏会移到地址栏下方。 |
| Windows 11 x64 | 沿用资源管理器原生标签页，在窗口中增加横向收藏栏。可以用系统标签页浏览多个目录，同时在收藏栏中打开常用路径。 |

## 下载和使用

[下载 0.1.0-preview.27 安装包（Windows x64）](https://github.com/lingyu521/LYwintabs/releases/download/v0.1.0-preview.27/LYwintabs-Win10-x64-0.1.0-preview.27-Setup.exe) · [查看发布说明](https://github.com/lingyu521/LYwintabs/releases/tag/v0.1.0-preview.27)

运行安装包，阅读最终用户许可协议并选择安装目录。插件的齿轮菜单提供主题、收藏栏显示和开机启动等设置。

![最终用户许可协议安装页]

## 横向收藏栏

可通过收藏栏空白处的右键菜单收藏当前目录，地址栏旁显示星标时也可点击星标。保存时可以选择收藏栏根目录或已有的书签文件夹。点击书签就能跳转到目标目录，也可以展开书签文件夹查看其中的项目。

可直接拖动收藏项调整顺序或移入文件夹。书签管理器支持创建文件夹，修改书签名称和目标路径，适合集中整理常用目录。

## 局域网互传

点击收藏栏左侧的互传图标，可以搜索同一局域网中的附近设备，选择或拖入要发送的文件和文件夹。互传在后台开启时，关闭面板也能收到桌面通知。收到请求后可选择接收、另存为或拒绝，接收过程中支持暂停、继续和终止。设备昵称和默认接收位置可在互传设置中修改。

设备发现与文件传输参考 **LocalSend v2** 公开协议，互传核心沿用同作者 LYIsland 的 C# 实现。[LocalSend 官方项目](https://github.com/localsend/localsend) · [LocalSend 协议规范](https://github.com/localsend/protocol) · [LYwintabs 互传使用说明](transfer/README.md)

## 兼容性

目标平台为 Windows 10 22H2 x64 和 Windows 11 x64。当前安装包的 EULA 页面、安装及卸载已在 Windows 10 22H2（19045.6456）实测。preview.27 的原生文件夹导航和互传自动启动已在 Windows 10 22H2 和 Windows 11 25H2 验证。

## 从源码构建

在 Visual Studio 2026 中打开 `LYwintabs.sln`，选择 `Release | x64` 生成 Explorer 插件和启动器。原生项目使用 C++20、MSVC v145 和 Windows SDK 10.0.26100.0。输出在 `out/explorer/x64/Release/`。

互传模块使用 .NET 10 和 C# WinForms。发布方式见[互传构建说明](transfer/README.md)。安装包使用 Inno Setup 7，脚本为 [Install-LYwintabs.iss](explorer/Install-LYwintabs.iss)，编译时传入插件 DLL、启动器 EXE、自包含互传目录及构建标识。

## 开源许可

LYwintabs 采用 [Apache License 2.0](LICENSE)。安装向导显示[最终用户许可协议（EULA）](EULA.txt)，内含中文说明及 Apache-2.0 原文。图标、运行库及安装程序的适用许可见[第三方许可声明](THIRD-PARTY-NOTICES.md)。

开发者 王凌宇 · [提交问题](https://github.com/lingyu521/LYwintabs/issues)
