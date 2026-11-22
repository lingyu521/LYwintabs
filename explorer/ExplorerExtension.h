#pragma once

#include <windows.h>

inline constexpr wchar_t ExplorerVersion[] = L"0.1.0-preview.27";
inline constexpr wchar_t ExplorerClassId[] = L"{B5ABC93E-9B6E-4A71-8AAA-2DE01C5A918B}";
inline constexpr wchar_t AttachMessageName[] = L"LYwintabs.Attach.{B5ABC93E-9B6E-4A71-8AAA-2DE01C5A918B}";
inline constexpr wchar_t TransferRelativePath[] = L"Transfer\\LYwintabs.Transfer.exe";
inline constexpr wchar_t TransferPanelProperty[] = L"LYwintabs.Transfer.Panel";
inline constexpr wchar_t ActivateTransferMessage[] = L"LYwintabs.Transfer.Activate";
// Installed windows from earlier releases accept the same detach message.
inline constexpr wchar_t DetachMessageName[] = L"LYwintabs.Detach.{D1FBC879-9A83-4D9F-957F-30E19FB22C4F}";
// Keep the marker stable so older loaded versions can be detected before uninstall.
inline constexpr wchar_t BarProperty[] = L"LYwintabs.FavoritesBar.{4A917ED1-591A-4EB4-AFF4-262CDCAB2152}";
inline constexpr wchar_t SuppressedProperty[] = L"LYwintabs.FavoritesBar.Suppressed";

inline constexpr GUID FavoritesExtensionId{
    0xb5abc93e, 0x9b6e, 0x4a71, {0x8a, 0xaa, 0x2d, 0xe0, 0x1c, 0x5a, 0x91, 0x8b}
};

class ModuleReference {
public:
    ModuleReference() noexcept;
    ~ModuleReference();
    ModuleReference(const ModuleReference&) = delete;
    ModuleReference& operator=(const ModuleReference&) = delete;
};

HRESULT ToggleFavoritesBar(HWND owner);
