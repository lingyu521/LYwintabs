# LYwintabs 第三方许可声明

LYwintabs 由王凌宇开发，以 Apache License 2.0 发布，完整条款见同目录的 `LICENSE`。以下随项目或安装包分发的第三方内容适用各自的许可和版权声明。表中的文件路径相对于安装目录下包含 `LYwintabs.Explorer.dll` 的版本子目录。

| 第三方内容 | 许可与来源 | 随安装包分发的文本 |
| --- | --- | --- |
| Lucide Icons 的 Folder Open、Settings 和 Star 图标，用于收藏夹、设置和收藏入口 | ISC。保留 Star 图标的 Feather Icons MIT 归属声明，版权归 Cole Bemis 所有。[Lucide 许可](https://lucide.dev/license) · [Feather 许可](https://github.com/feathericons/feather/blob/master/LICENSE) | `Licenses/Lucide-ISC.txt`、`Licenses/Feather-MIT.txt` |
| Microsoft Fluent UI System Icons 的 Communication Person 24 Filled 图标，用于局域网互传入口，调整了颜色与绘制方式 | MIT，Copyright (c) 2020 Microsoft Corporation。[图标原件](https://github.com/microsoft/fluentui-system-icons/blob/6c2c9075cf8952ef48369b2475233b6e9761efed/assets/Communication%20Person/SVG/ic_fluent_communication_person_24_filled.svg) · [MIT 许可](https://github.com/microsoft/fluentui-system-icons/blob/6c2c9075cf8952ef48369b2475233b6e9761efed/LICENSE) | `Licenses/Fluent-UI-System-Icons-MIT.txt` |
| .NET、ASP.NET Core 和 .NET Windows Desktop Runtime，随局域网互传组件自包含分发 | MIT。运行库附带的第三方组件各自适用其声明。[.NET](https://github.com/dotnet/runtime/blob/main/LICENSE.TXT) · [ASP.NET Core](https://github.com/dotnet/aspnetcore/blob/main/LICENSE.txt) · [.NET Windows Desktop](https://github.com/dotnet/windowsdesktop/blob/main/LICENSE) | `Transfer/Licenses/` 中的 `netcore.app/`、`aspnetcore.app/` 和 `windowsdesktop.app/` |
| Microsoft Visual C++ 运行库，静态链接到原生插件和启动器 | [Microsoft Visual C++ 再分发条款说明](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files) | — |
| Inno Setup，生成安装程序 | Inno Setup License，版权归 Jordan Russell 和 Martijn Laan 所有。[Inno Setup 官方网站](https://jrsoftware.org/) | `Licenses/Inno-Setup-License.txt` |

源码中的图标归属、许可文本及安装工具许可副本位于 `explorer/assets/`。自包含发布所用的 .NET 许可及随附声明来自相应版本的运行库包。

局域网互传组件沿用同作者 LYIsland 的 C# 实现，设备发现与传输参考 LocalSend v2 公开协议。[LocalSend 协议规范](https://github.com/localsend/protocol)
