# LYwintabs 局域网互传

LYwintabs 启用后，互传组件在后台监听。点击 Windows 文件资源管理器收藏栏左侧的互传按钮或双击托盘图标可打开面板。关闭面板后仍可接收请求。在托盘菜单选择「退出互传」可停止监听。文件枚举、SHA-256 校验和网络连接由互传进程处理。

右键点击通知区域的 LYwintabs 图标，选择「设置」可打开插件设置，包括开机启动和收藏栏显示。设备昵称和文件接收位置可在互传窗口的「设置」页调整。从已启用插件的资源管理器打开新窗口或进入其他文件夹时，会检查后台互传并按需启动。

`Core` 源自同作者 LYIsland 的 `src/LYIsland.Transfer`，沿用设备发现及 LocalSend v2 HTTPS 收发流程。收到文件请求时，桌面弹窗显示发送者、文件数量、大小和保存位置。默认接收位置为当前用户桌面，可在「互传设置」中更改。弹窗中的「另存为…」仅为本批文件选择保存位置，每批文件均放入独立的 `LYwintabs_…` 子文件夹。接收确认后，弹窗显示进度并提供「暂停」「继续」和「终止」操作。终止会清理未完成的文件。

双方都支持新版校验方式时，发送请求只准备文件清单。获准上传后，发送端在读取文件时计算 SHA-256，接收端验证通过后才将临时文件移到最终位置。连接旧版时沿用清单中的 SHA-256。

「发送」页按名称排列附近设备，每台设备占一行，列表超出可见高度时可滚动。搜索框支持设备名称和 IP 地址。

设备昵称保存在当前用户的 `%LOCALAPPDATA%\LYwintabs\Transfer\nickname.txt`。清空并保存后，面板优先读取当前域账户的显示名称，缺失时使用计算机名。

发布时使用 .NET 10 SDK 生成包含运行时的 x64 组件。发布目录作为 `TransferDir` 交给 `explorer/Install-LYwintabs.iss`，并附上所用 .NET、Windows Desktop 和 ASP.NET Core 运行时许可及第三方声明。安装程序将互传组件和插件 DLL 放在所选目录下的同一版本子目录，互传组件相对插件 DLL 的路径为 `Transfer\LYwintabs.Transfer.exe`。

```powershell
dotnet publish .\transfer\LYwintabs.Transfer.csproj -c Release -r win-x64 --self-contained -o .\out\release\Transfer
```
