#include "ExplorerLanguage.h"
#include "ExplorerPreferences.h"

#include <string_view>

namespace {

constexpr wchar_t LanguageValue[] = L"UiLanguage";

struct Translation {
    std::wstring_view chinese;
    const wchar_t* english;
};

constexpr Translation translations[] = {
    {L"界面语言", L"Display language"},
    {L"LYwintabs 设置", L"LYwintabs Settings"},
    {L"界面主题", L"Appearance"},
    {L"深色会同步切换 Windows 和默认应用模式。", L"Dark sets Windows and apps to dark mode."},
    {L"标签高度", L"Tab height"},
    {L"书签栏高度", L"Bookmarks bar height"},
    {L"显示原生窗口标题", L"Show the Explorer window title"},
    {L"隐藏快速访问工具栏", L"Hide the Quick Access Toolbar"},
    {L"显示书签栏", L"Show the bookmarks bar"},
    {L"开机启动（登录 Windows 后启用插件）", L"Start when I sign in to Windows"},
    {L"书签管理器…", L"Bookmark Manager…"},
    {L"关于…", L"About…"},
    {L"收藏此文件夹…", L"Bookmark folder…"},
    {L"确定", L"OK"},
    {L"取消", L"Cancel"},
    {L"重命名书签", L"Rename bookmark"},
    {L"名称", L"Name"},
    {L"LYwintabs 书签管理器", L"LYwintabs Bookmark Manager"},
    {L"书签文件夹", L"Bookmark folders"},
    {L"书签栏", L"Bookmarks bar"},
    {L"添加书签…", L"Add bookmark…"},
    {L"新建文件夹…", L"New folder…"},
    {L"编辑…", L"Edit…"},
    {L"移动到…", L"Move to…"},
    {L"移除", L"Remove"},
    {L"编辑书签", L"Edit bookmark"},
    {L"文件夹地址", L"Folder path"},
    {L"浏览…", L"Browse…"},
    {L"添加书签", L"Add bookmark"},
    {L"保存到", L"Save in"},
    {L"添加", L"Add"},
    {L"移动书签", L"Move bookmark"},
    {L"选择目标书签文件夹", L"Choose a destination folder"},
    {L"移动", L"Move"},
    {L"跟随 Windows", L"Follow Windows"},
    {L"深色（同步 Windows）", L"Dark (sync Windows)"},
    {L"紧凑", L"Compact"},
    {L"加高", L"Tall"},
    {L"设置", L"Settings"},
    {L"局域网互传", L"LAN File Transfer"},
    {L"更多…", L"More…"},
    {L"互传", L"Send"},
    {L"收藏当前文件夹", L"Bookmark this folder"},
    {L"（空文件夹）", L"(Empty folder)"},
    {L"关于 LYwintabs", L"About LYwintabs"},
    {L"Windows 文件资源管理器插件\nWindows 10 标签与书签栏，Windows 11 横向书签栏",
     L"File Explorer extension\nTabs and bookmarks on Windows 10, bookmarks on Windows 11"},
    {L"无法打开默认邮件程序。", L"Could not open the default mail application."},
    {L"无法更新 Windows 配色。错误代码 ", L"Could not update Windows colors. Error code "},
    {L"。", L"."},
    {L"无法保存开机启动设置。", L"Could not save the startup setting."},
    {L"无法保存界面语言。", L"Could not save the display language."},
    {L"无法保存设置。", L"Could not save settings."},
    {L"这个收藏不是文件夹路径。", L"This bookmark does not point to a folder."},
    {L"无法访问这个书签的文件夹。", L"Could not access the bookmarked folder."},
    {L"资源管理器无法打开这个收藏。", L"File Explorer could not open this bookmark."},
    {L"当前文件夹无法收藏。", L"This folder cannot be bookmarked."},
    {L"收藏保存失败。", L"Could not save the bookmark."},
    {L"请先在书签管理器里移走或移除文件夹内的书签。", L"Move or remove the bookmarks in this folder before removing it."},
    {L"目标文件夹中已有相同地址的书签。", L"The destination folder already contains a bookmark for this path."},
    {L"无法移动书签，请确认目标文件夹及其嵌套层数。", L"Could not move the bookmark. Check the destination and its nesting depth."},
    {L"添加当前地址到书签…", L"Bookmark current folder…"},
    {L"添加文件夹…", L"Add folder…"},
    {L"展开文件夹", L"Expand folder"},
    {L"打开", L"Open"},
    {L"在后台标签打开", L"Open in background tab"},
    {L"移除书签文件夹", L"Remove bookmark folder"},
    {L"移除书签", L"Remove bookmark"},
    {L"无法在新标签中打开这个收藏。", L"Could not open this bookmark in a new tab."},
    {L"无法打开局域网互传。请重新运行 LYwintabs 安装包以修复组件。",
     L"Could not open LAN File Transfer. Run LYwintabs Setup again to repair it."},
    {L"文件夹", L"Folder"},
    {L"复制标签", L"Duplicate tab"},
    {L"关闭标签", L"Close tab"},
    {L"选择书签文件夹", L"Choose a folder to bookmark"},
    {L"无法选择文件夹。", L"Could not select the folder."},
    {L"请输入完整的文件夹地址，例如 C:\\项目 或 \\\\服务器\\共享。",
     L"Enter a full folder path, such as C:\\Projects or \\\\server\\share."},
    {L"所填地址是文件。请选择文件夹地址。", L"This path points to a file. Choose a folder instead."},
    {L"无法完成书签编辑。", L"Could not edit the bookmark."},
    {L"无法打开书签编辑窗口。", L"Could not open the bookmark editor."},
    {L"无法选择目标书签文件夹。", L"Could not select the destination folder."},
    {L"无法读取书签。", L"Could not read bookmarks."},
    {L"无法打开书签管理器。", L"Could not open Bookmark Manager."},
    {L"无法完成书签操作。", L"The bookmark operation could not be completed."},
    {L"无法保存书签。", L"Could not save the bookmark."},
    {L"请先移出这个书签文件夹中的项目，再移除文件夹。", L"Move items out of this folder before removing it."},
    {L"这里已有同名的书签文件夹。", L"A bookmark folder with this name already exists here."},
    {L"目标书签文件夹中已有相同地址的书签。", L"The destination folder already contains a bookmark for this path."},
    {L"书签文件夹的嵌套层数已达上限。", L"Bookmark folders have reached the maximum nesting depth."},
    {L"书签或目标文件夹已更改。请重新选择后再试。", L"The bookmark or destination has changed. Select it again and retry."},
    {L"编辑书签文件夹", L"Edit bookmark folder"},
    {L"新建书签文件夹", L"New bookmark folder"},
    {L"这个地址已经收藏到所选的书签文件夹。", L"This path is already bookmarked in the selected folder."},
    {L"LYwintabs 收藏栏", L"LYwintabs Bookmarks Bar"},
    {L"显示或隐藏当前资源管理器窗口的 LYwintabs 横向收藏栏", L"Show or hide the LYwintabs bookmarks bar in this File Explorer window."},
};

void TranslateWindowText(HWND window) noexcept
{
    wchar_t text[256]{};
    if (!GetWindowTextW(window, text, ARRAYSIZE(text))) return;
    const wchar_t* translated = ExplorerText(text, UiLanguage::English);
    if (translated != text) SetWindowTextW(window, translated);
}

BOOL CALLBACK TranslateChild(HWND child, LPARAM) noexcept
{
    TranslateWindowText(child);
    return TRUE;
}

} // namespace

UiLanguage ReadUiLanguage() noexcept
{
    DWORD language = 0;
    DWORD bytes = sizeof(language);
    if (RegGetValueW(HKEY_CURRENT_USER, AppearanceKey, LanguageValue, RRF_RT_REG_DWORD,
            nullptr, &language, &bytes) == ERROR_SUCCESS &&
        language <= static_cast<DWORD>(UiLanguage::English))
        return static_cast<UiLanguage>(language);
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? UiLanguage::Chinese : UiLanguage::English;
}

bool SaveUiLanguage(UiLanguage language) noexcept
{
    const auto value = static_cast<DWORD>(language);
    return RegSetKeyValueW(HKEY_CURRENT_USER, AppearanceKey, LanguageValue, REG_DWORD, &value, sizeof(value)) == ERROR_SUCCESS;
}

const wchar_t* ExplorerText(const wchar_t* chinese, UiLanguage language) noexcept
{
    if (language == UiLanguage::English) {
        const std::wstring_view source(chinese);
        for (const auto& entry : translations)
            if (entry.chinese == source) return entry.english;
    }
    return chinese;
}

const wchar_t* ExplorerText(const wchar_t* chinese) noexcept
{
    return ExplorerText(chinese, ReadUiLanguage());
}

void LocalizeExplorerDialog(HWND dialog, UiLanguage language) noexcept
{
    if (language != UiLanguage::English) return;
    TranslateWindowText(dialog);
    EnumChildWindows(dialog, TranslateChild, 0);
}
