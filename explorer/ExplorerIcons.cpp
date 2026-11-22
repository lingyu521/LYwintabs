#include "ExplorerIcons.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>

namespace {

// Lucide star (assets/star.svg). SVG circular arcs are represented as cubic curves.
void AddStar(Gdiplus::GraphicsPath& path)
{
    path.AddBezier(11.5250f, 2.2950f, 11.6144f, 2.1144f, 11.7985f, 2.0001f, 12.0000f, 2.0001f);
    path.AddBezier(12.0000f, 2.0001f, 12.2015f, 2.0001f, 12.3856f, 2.1144f, 12.4750f, 2.2950f);
    path.AddLine(12.4750f, 2.2950f, 14.7850f, 6.9740f);
    path.AddBezier(14.7850f, 6.9740f, 15.0939f, 7.5991f, 15.6901f, 8.0327f, 16.3800f, 8.1340f);
    path.AddLine(16.3800f, 8.1340f, 21.5460f, 8.8900f);
    path.AddBezier(21.5460f, 8.8900f, 21.7457f, 8.9189f, 21.9116f, 9.0587f, 21.9740f, 9.2506f);
    path.AddBezier(21.9740f, 9.2506f, 22.0364f, 9.4425f, 21.9845f, 9.6531f, 21.8400f, 9.7940f);
    path.AddLine(21.8400f, 9.7940f, 18.1040f, 13.4320f);
    path.AddBezier(18.1040f, 13.4320f, 17.6038f, 13.9194f, 17.3754f, 14.6216f, 17.4930f, 15.3100f);
    path.AddLine(17.4930f, 15.3100f, 18.3750f, 20.4500f);
    path.AddBezier(18.3750f, 20.4500f, 18.4103f, 20.6496f, 18.3286f, 20.8519f, 18.1645f, 20.9710f);
    path.AddBezier(18.1645f, 20.9710f, 18.0005f, 21.0901f, 17.7829f, 21.1053f, 17.6040f, 21.0100f);
    path.AddLine(17.6040f, 21.0100f, 12.9860f, 18.5820f);
    path.AddBezier(12.9860f, 18.5820f, 12.3683f, 18.2577f, 11.6307f, 18.2577f, 11.0130f, 18.5820f);
    path.AddLine(11.0130f, 18.5820f, 6.3960f, 21.0100f);
    path.AddBezier(6.3960f, 21.0100f, 6.2171f, 21.1047f, 6.0000f, 21.0893f, 5.8363f, 20.9702f);
    path.AddBezier(5.8363f, 20.9702f, 5.6726f, 20.8512f, 5.5910f, 20.6493f, 5.6260f, 20.4500f);
    path.AddLine(5.6260f, 20.4500f, 6.5070f, 15.3110f);
    path.AddBezier(6.5070f, 15.3110f, 6.6251f, 14.6223f, 6.3966f, 13.9195f, 5.8960f, 13.4320f);
    path.AddLine(5.8960f, 13.4320f, 2.1600f, 9.7950f);
    path.AddBezier(2.1600f, 9.7950f, 2.0143f, 9.6543f, 1.9616f, 9.4428f, 2.0241f, 9.2502f);
    path.AddBezier(2.0241f, 9.2502f, 2.0866f, 9.0575f, 2.2534f, 8.9174f, 2.4540f, 8.8890f);
    path.AddLine(2.4540f, 8.8890f, 7.6190f, 8.1340f);
    path.AddBezier(7.6190f, 8.1340f, 8.3097f, 8.0335f, 8.9068f, 7.5998f, 9.2160f, 6.9740f);
    path.AddLine(9.2160f, 6.9740f, 11.5250f, 2.2950f);
    path.CloseFigure();
}

// Lucide settings (assets/settings.svg). SVG circular arcs are represented as cubic curves.
void AddSettings(Gdiplus::GraphicsPath& path)
{
    path.AddBezier(9.6710f, 4.1360f, 9.7852f, 2.9348f, 10.7939f, 2.0174f, 12.0005f, 2.0174f);
    path.AddBezier(12.0005f, 2.0174f, 13.2071f, 2.0174f, 14.2158f, 2.9348f, 14.3300f, 4.1360f);
    path.AddBezier(14.3300f, 4.1360f, 14.3972f, 4.8959f, 14.8307f, 5.5754f, 15.4915f, 5.9567f);
    path.AddBezier(15.4915f, 5.9567f, 16.1523f, 6.3379f, 16.9574f, 6.3731f, 17.6490f, 6.0510f);
    path.AddBezier(17.6490f, 6.0510f, 18.7452f, 5.5533f, 20.0402f, 5.9686f, 20.6425f, 7.0111f);
    path.AddBezier(20.6425f, 7.0111f, 21.2448f, 8.0536f, 20.9577f, 9.3829f, 19.9790f, 10.0840f);
    path.AddBezier(19.9790f, 10.0840f, 19.3547f, 10.5220f, 18.9830f, 11.2369f, 18.9830f, 11.9995f);
    path.AddBezier(18.9830f, 11.9995f, 18.9830f, 12.7621f, 19.3547f, 13.4770f, 19.9790f, 13.9150f);
    path.AddBezier(19.9790f, 13.9150f, 20.9577f, 14.6161f, 21.2448f, 15.9454f, 20.6425f, 16.9879f);
    path.AddBezier(20.6425f, 16.9879f, 20.0402f, 18.0304f, 18.7452f, 18.4457f, 17.6490f, 17.9480f);
    path.AddBezier(17.6490f, 17.9480f, 16.9574f, 17.6259f, 16.1523f, 17.6611f, 15.4915f, 18.0423f);
    path.AddBezier(15.4915f, 18.0423f, 14.8307f, 18.4236f, 14.3972f, 19.1031f, 14.3300f, 19.8630f);
    path.AddBezier(14.3300f, 19.8630f, 14.2158f, 21.0642f, 13.2071f, 21.9816f, 12.0005f, 21.9816f);
    path.AddBezier(12.0005f, 21.9816f, 10.7939f, 21.9816f, 9.7852f, 21.0642f, 9.6710f, 19.8630f);
    path.AddBezier(9.6710f, 19.8630f, 9.6039f, 19.1028f, 9.1703f, 18.4230f, 8.5092f, 18.0417f);
    path.AddBezier(8.5092f, 18.0417f, 7.8481f, 17.6604f, 7.0427f, 17.6254f, 6.3510f, 17.9480f);
    path.AddBezier(6.3510f, 17.9480f, 5.2548f, 18.4457f, 3.9598f, 18.0304f, 3.3575f, 16.9879f);
    path.AddBezier(3.3575f, 16.9879f, 2.7552f, 15.9454f, 3.0423f, 14.6161f, 4.0210f, 13.9150f);
    path.AddBezier(4.0210f, 13.9150f, 4.6453f, 13.4770f, 5.0170f, 12.7621f, 5.0170f, 11.9995f);
    path.AddBezier(5.0170f, 11.9995f, 5.0170f, 11.2369f, 4.6453f, 10.5220f, 4.0210f, 10.0840f);
    path.AddBezier(4.0210f, 10.0840f, 3.0437f, 9.3826f, 2.7574f, 8.0544f, 3.3590f, 7.0127f);
    path.AddBezier(3.3590f, 7.0127f, 3.9606f, 5.9710f, 5.2540f, 5.5551f, 6.3500f, 6.0510f);
    path.AddBezier(6.3500f, 6.0510f, 7.0416f, 6.3731f, 7.8467f, 6.3379f, 8.5075f, 5.9567f);
    path.AddBezier(8.5075f, 5.9567f, 9.1683f, 5.5754f, 9.6018f, 4.8959f, 9.6690f, 4.1360f);
    path.StartFigure();
    path.AddEllipse(9.0000f, 9.0000f, 6.0000f, 6.0000f);
}

// Filled glyph from assets/lan-transfer.svg.
void AddLanTransfer(Gdiplus::GraphicsPath& path)
{
    path.StartFigure();
    path.AddBezier(4.00000f, 13.00000f, 4.00000f, 8.58172f, 7.58172f, 5.00000f, 12.00000f, 5.00000f);
    path.AddBezier(12.00000f, 5.00000f, 15.81820f, 5.00000f, 19.01160f, 7.67484f, 19.80870f, 11.25290f);
    path.AddBezier(19.80870f, 11.25290f, 20.92790f, 11.70440f, 21.76020f, 12.71780f, 21.95600f, 13.94360f);
    path.AddBezier(21.95600f, 13.94360f, 21.98510f, 13.63300f, 22.00000f, 13.31820f, 22.00000f, 13.00000f);
    path.AddBezier(22.00000f, 13.00000f, 22.00000f, 7.47715f, 17.52280f, 3.00000f, 12.00000f, 3.00000f);
    path.AddBezier(12.00000f, 3.00000f, 6.47715f, 3.00000f, 2.00000f, 7.47715f, 2.00000f, 13.00000f);
    path.AddBezier(2.00000f, 13.00000f, 2.00000f, 15.75780f, 3.11770f, 18.25650f, 4.92245f, 20.06460f);
    path.AddBezier(4.92245f, 20.06460f, 5.31262f, 20.45550f, 5.94578f, 20.45600f, 6.33666f, 20.06590f);
    path.AddBezier(6.33666f, 20.06590f, 6.72754f, 19.67570f, 6.72812f, 19.04250f, 6.33795f, 18.65170f);
    path.AddBezier(6.33795f, 18.65170f, 4.89220f, 17.20330f, 4.00000f, 15.20680f, 4.00000f, 13.00000f);
    path.CloseFigure();
    path.StartFigure();
    path.AddBezier(17.69090f, 11.09400f, 16.89440f, 8.71464f, 14.64740f, 7.00000f, 12.00000f, 7.00000f);
    path.AddBezier(12.00000f, 7.00000f, 8.68629f, 7.00000f, 6.00000f, 9.68629f, 6.00000f, 13.00000f);
    path.AddBezier(6.00000f, 13.00000f, 6.00000f, 14.65660f, 6.67267f, 16.15790f, 7.75736f, 17.24260f);
    path.AddBezier(7.75736f, 17.24260f, 8.14788f, 17.63320f, 8.78105f, 17.63320f, 9.17157f, 17.24260f);
    path.AddBezier(9.17157f, 17.24260f, 9.56210f, 16.85210f, 9.56210f, 16.21900f, 9.17157f, 15.82840f);
    path.AddBezier(9.17157f, 15.82840f, 8.44662f, 15.10350f, 8.00000f, 14.10480f, 8.00000f, 13.00000f);
    path.AddBezier(8.00000f, 13.00000f, 8.00000f, 10.79090f, 9.79086f, 9.00000f, 12.00000f, 9.00000f);
    path.AddBezier(12.00000f, 9.00000f, 13.91640f, 9.00000f, 15.51810f, 10.34770f, 15.90890f, 12.14710f);
    path.AddBezier(15.90890f, 12.14710f, 16.37550f, 11.63350f, 16.99280f, 11.25920f, 17.69090f, 11.09400f);
    path.CloseFigure();
    path.StartFigure();
    path.AddBezier(10.00000f, 13.00000f, 10.00000f, 11.89540f, 10.89540f, 11.00000f, 12.00000f, 11.00000f);
    path.AddBezier(12.00000f, 11.00000f, 13.10460f, 11.00000f, 14.00000f, 11.89540f, 14.00000f, 13.00000f);
    path.AddBezier(14.00000f, 13.00000f, 14.00000f, 14.10460f, 13.10460f, 15.00000f, 12.00000f, 15.00000f);
    path.AddBezier(12.00000f, 15.00000f, 10.89540f, 15.00000f, 10.00000f, 14.10460f, 10.00000f, 13.00000f);
    path.CloseFigure();
    path.StartFigure();
    path.AddBezier(21.00000f, 14.50000f, 21.00000f, 15.88070f, 19.88070f, 17.00000f, 18.50000f, 17.00000f);
    path.AddBezier(18.50000f, 17.00000f, 17.11930f, 17.00000f, 16.00000f, 15.88070f, 16.00000f, 14.50000f);
    path.AddBezier(16.00000f, 14.50000f, 16.00000f, 13.11930f, 17.11930f, 12.00000f, 18.50000f, 12.00000f);
    path.AddBezier(18.50000f, 12.00000f, 19.88070f, 12.00000f, 21.00000f, 13.11930f, 21.00000f, 14.50000f);
    path.CloseFigure();
    path.StartFigure();
    path.AddBezier(23.00000f, 19.87500f, 23.00000f, 21.43150f, 21.71430f, 23.00000f, 18.50000f, 23.00000f);
    path.AddBezier(18.50000f, 23.00000f, 15.28570f, 23.00000f, 14.00000f, 21.43740f, 14.00000f, 19.87500f);
    path.AddLine(14.00000f, 19.87500f, 14.00000f, 19.77200f);
    path.AddBezier(14.00000f, 19.77200f, 14.00000f, 18.79290f, 14.79370f, 18.00000f, 15.77270f, 18.00000f);
    path.AddLine(15.77270f, 18.00000f, 21.22730f, 18.00000f);
    path.AddBezier(21.22730f, 18.00000f, 22.20630f, 18.00000f, 23.00000f, 18.79300f, 23.00000f, 19.77200f);
    path.AddLine(23.00000f, 19.77200f, 23.00000f, 19.87500f);
    path.CloseFigure();
}

} // namespace

bool DrawExplorerIcon(HDC dc, const RECT& bounds, int preferredSize, ExplorerIcon icon, COLORREF color)
{
    using namespace Gdiplus;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int size = std::min({preferredSize, width, height});
    if (!dc || size <= 0) return false;

    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL scale = static_cast<REAL>(size) / 24.0f;
    const auto x = static_cast<REAL>(bounds.left + (width - size) / 2);
    const auto y = static_cast<REAL>(bounds.top + (height - size) / 2);
    const Matrix transform(scale, 0, 0, scale, x, y);
    if (graphics.SetTransform(&transform) != Ok) return false;

    GraphicsPath path;
    const Color ink(GetRValue(color), GetGValue(color), GetBValue(color));
    if (icon == ExplorerIcon::LanTransfer) {
        AddLanTransfer(path);
        SolidBrush fill(ink);
        return graphics.FillPath(&fill, &path) == Ok;
    }
    if (icon == ExplorerIcon::Star) AddStar(path);
    else AddSettings(path);
    Pen stroke(ink, 2.0f);
    stroke.SetStartCap(LineCapRound);
    stroke.SetEndCap(LineCapRound);
    stroke.SetLineJoin(LineJoinRound);
    return graphics.DrawPath(&stroke, &path) == Ok;
}
