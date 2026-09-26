/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * Licensed under the Oculus SDK License Agreement (the "License");
 * you may not use the Oculus SDK except in compliance with the License,
 * which is provided at the time of installation or download, or which
 * otherwise accompanies this software in either electronic or hard copy form.
 *
 * You may obtain a copy of the License at
 * https://developer.oculus.com/licenses/oculussdk/
 *
 * Unless required by applicable law or agreed to in writing, the Oculus SDK
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/************************************************************************************

Filename    :   Fader.h
Content     :   Utility classes for animation based on alpha values
Created     :   July 25, 2014
Authors     :   Jonathan E. Wright

*************************************************************************************/

#pragma once

namespace OVRFW {

//==============================================================
// Fader
// Fades a value between 0 and 1
class Fader {
   public:
    enum eFadeState {
        FADE_NONE, // only the state when the alpha is either 1 or 0
        FADE_PAUSED, // value may be in the middle
        FADE_IN,
        FADE_OUT,
        FADE_MAX
    };

    Fader(float const startAlpha);

    void Update(float const fadeRate, double const deltaSeconds);

    [[nodiscard]] float GetFadeAlpha() const {
        return fadeAlpha_;
    }
    [[nodiscard]] eFadeState GetFadeState() const {
        return fadeState_;
    }

    void StartFadeIn();
    void StartFadeOut();
    void PauseFade();
    void UnPause();
    void Reset();
    void ForceFinish();
    void SetFadeAlpha(float const fa) {
        fadeAlpha_ = fa;
    }

    static char const* GetFadeStateName(eFadeState const state);

    [[nodiscard]] bool IsFadingInOrFadedIn() const;

   private:
    eFadeState fadeState_;
    eFadeState prePauseState_;
    float startAlpha_;
    float fadeAlpha_;

   private:
    [[nodiscard]] bool IsFadingInOrFadedIn(eFadeState const state) const;
};

//==============================================================
// SineFader
// Maps fade alpha to a sine curve
class SineFader : public Fader {
   public:
    SineFader(float const startAlpha);

    [[nodiscard]] float GetFinalAlpha() const;
};

} // namespace OVRFW
