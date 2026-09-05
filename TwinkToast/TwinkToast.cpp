#include "TwinkToast.h"
#include <algorithm>

void TwinkToast::Render()
{
    if (m_Queue.empty()) return;

    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 screen = ImGui::GetIO().DisplaySize;

    const float toastH   = 36.f;
    const float gap      = 6.f;
    const float padX     = 14.f;
    const float padY     = 14.f;
    const float fadeIn   = 0.20f;
    const float fadeOut  = 0.40f;
    const float corner   = 5.f;

    ImVec2 mousePos    = ImGui::GetIO().MousePos;
    bool   mouseClick  = ImGui::GetIO().MouseClicked[0];

    // Draw bottom-up (newest at bottom-right, stacking upward)
    int n = (int)m_Queue.size();
    for (int i = 0; i < n; i++)
    {
        Toast& t = m_Queue[i];

        // Alpha envelope: fade in, hold, fade out
        float alpha;
        float elapsed = t.totalTime - t.timeLeft;
        if (elapsed < fadeIn)
            alpha = elapsed / fadeIn;
        else if (t.timeLeft < fadeOut)
            alpha = t.timeLeft / fadeOut;
        else
            alpha = 1.f;
        alpha = std::max(0.f, std::min(1.f, alpha));

        // Width fits the text with padding on both sides
        ImVec2 ts      = ImGui::CalcTextSize(t.msg.c_str());
        float  textPad = 20.f;   // padding left of text (after accent bar)
        float  toastW  = 4.f + textPad + ts.x + textPad;  // accent + left pad + text + right pad

        // Position: bottom-right, stacking upward
        float x = screen.x - toastW - padX;
        float y = screen.y - padY - (n - i) * (toastH + gap);

        // Click handling (only for toasts with a callback)
        bool hovered = t.onClick
                    && mousePos.x >= x && mousePos.x <= x + toastW
                    && mousePos.y >= y && mousePos.y <= y + toastH;
        if (hovered && mouseClick)
        {
            t.onClick();
            t.timeLeft = 0.f;   // dismiss on click
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // Background (slightly brighter when hovered)
        int bgAlpha = hovered ? (int)(230 * alpha) : (int)(200 * alpha);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + toastW, y + toastH),
                          IM_COL32(28, 28, 32, bgAlpha), corner);
        // Left accent bar
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 4.f, y + toastH),
                          IM_COL32((int)(t.color.x*255),(int)(t.color.y*255),
                                   (int)(t.color.z*255),(int)(220*alpha)),
                          corner, ImDrawFlags_RoundCornersLeft);
        // Border (highlight when hovered)
        ImU32 borderCol = hovered
            ? IM_COL32((int)(t.color.x*200),(int)(t.color.y*200),(int)(t.color.z*200),(int)(200*alpha))
            : IM_COL32(70, 70, 80, (int)(160 * alpha));
        dl->AddRect(ImVec2(x, y), ImVec2(x + toastW, y + toastH),
                    borderCol, corner, 0, hovered ? 1.4f : 0.8f);

        // Text — vertically centered, after accent bar
        float ty = y + (toastH - ts.y) * 0.5f;
        dl->AddText(ImVec2(x + 4.f + textPad, ty),
                    IM_COL32(230,230,230,(int)(230*alpha)), t.msg.c_str());

        t.timeLeft -= dt;
    }

    // Remove expired toasts
    m_Queue.erase(
        std::remove_if(m_Queue.begin(), m_Queue.end(),
                       [](const Toast& t){ return t.timeLeft <= 0.f; }),
        m_Queue.end());
}
