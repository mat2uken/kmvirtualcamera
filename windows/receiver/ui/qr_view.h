#pragma once

#include <windows.h>
#include <string>
#include "../../third_party/qr/qrcodegen.hpp"

namespace km::ui {

class QrView {
public:
    QrView();
    ~QrView() = default;

    void SetText(const std::string& text);
    void Draw(HDC hdc, int x, int y, int width, int height);

private:
    std::string text_;
    std::vector<std::vector<bool>> qrMatrix_;
    int qrSize_{0};
};

} // namespace km::ui
