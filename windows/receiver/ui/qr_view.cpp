#include "qr_view.h"
#include <algorithm>

namespace km::ui {

QrView::QrView() = default;

void QrView::SetText(const std::string& text) {
    text_ = text;
    qrMatrix_.clear();
    qrSize_ = 0;

    if (text.empty()) return;

    try {
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        qrSize_ = qr.getSize();
        qrMatrix_.resize(qrSize_, std::vector<bool>(qrSize_));
        for (int y = 0; y < qrSize_; ++y) {
            for (int x = 0; x < qrSize_; ++x) {
                qrMatrix_[y][x] = qr.getModule(x, y);
            }
        }
    } catch (...) {
        qrSize_ = 0;
    }
}

void QrView::Draw(HDC hdc, int x, int y, int width, int height) {
    // Fill white background
    RECT rect{ x, y, x + width, y + height };
    HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rect, whiteBrush);
    DeleteObject(whiteBrush);

    if (qrSize_ <= 0 || qrMatrix_.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(128, 128, 128));
        DrawTextW(hdc, L"QRコード未生成", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    int border = 4; // Quiet zone
    int totalModules = qrSize_ + border * 2;
    int cellSize = (std::min)(width, height) / totalModules;
    if (cellSize <= 0) cellSize = 1;

    int qrPixelSize = cellSize * totalModules;
    int startX = x + (width - qrPixelSize) / 2 + border * cellSize;
    int startY = y + (height - qrPixelSize) / 2 + border * cellSize;

    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));

    for (int r = 0; r < qrSize_; ++r) {
        for (int c = 0; c < qrSize_; ++c) {
            if (qrMatrix_[r][c]) {
                RECT cellRect{
                    startX + c * cellSize,
                    startY + r * cellSize,
                    startX + (c + 1) * cellSize,
                    startY + (r + 1) * cellSize
                };
                FillRect(hdc, &cellRect, blackBrush);
            }
        }
    }

    DeleteObject(blackBrush);
}

} // namespace km::ui
