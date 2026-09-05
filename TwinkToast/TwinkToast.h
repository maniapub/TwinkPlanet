#pragma once

#include "../imgui-dx9/imgui.h"
#include <string>
#include <vector>
#include <functional>

// ── Lightweight global toast notification system ──────────────────────────────
// Push toasts from anywhere:  TwinkToast::Get().Push("message");
// Call Render() once per frame from TwinkUi::RenderAnyways.

class TwinkToast
{
public:
    static TwinkToast& Get()
    {
        static TwinkToast instance;
        return instance;
    }

    // color: ImVec4 RGBA 0..1; onClick: optional callback fired when toast is clicked
    void Push(const std::string& msg,
              float duration                    = 3.5f,
              ImVec4 color                      = { 1.f, 1.f, 1.f, 1.f },
              std::function<void()> onClick     = nullptr)
    {
        m_Queue.push_back({ msg, color, duration, duration, std::move(onClick) });
        if ((int)m_Queue.size() > kMaxToasts)
            m_Queue.erase(m_Queue.begin());
    }

    void Render();

private:
    static constexpr int kMaxToasts = 6;

    struct Toast
    {
        std::string           msg;
        ImVec4                color;
        float                 timeLeft;
        float                 totalTime;
        std::function<void()> onClick;
    };

    std::vector<Toast> m_Queue;

    TwinkToast() = default;
};
