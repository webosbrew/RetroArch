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

Filename    :   Fader.cpp
Content     :   Utility classes for animation based on alpha values
Created     :   July 25, 2014
Authors     :   Jonathan E. Wright

*************************************************************************************/

#include "Fader.h"

#include "OVR_Math.h"
#include "Misc/Log.h"

#include <array>
#include <cassert>

namespace OVRFW {

//======================================================================================
// Fader
//======================================================================================

//==============================
// Fader::Fader
Fader::Fader(float const startAlpha)
    : fadeState_(FADE_NONE),
      prePauseState_(FADE_NONE),
      startAlpha_(startAlpha),
      fadeAlpha_(startAlpha) {}

//==============================
// Fader::Update
void Fader::Update(float const fadeRate, double const deltaSeconds) {
    if (fadeState_ > FADE_PAUSED && deltaSeconds > 0.0f) {
        float const fadeDelta =
            static_cast<float>(fadeRate * deltaSeconds) * (fadeState_ == FADE_IN ? 1.0f : -1.0f);
        fadeAlpha_ += fadeDelta;
        assert(fabs(fadeDelta) > MATH_FLOAT_SMALLEST_NON_DENORMAL);
        if (fabs(fadeDelta) < MATH_FLOAT_SMALLEST_NON_DENORMAL) {
            ALOG("Fader::Update fabs( fadeDelta ) < MATH_FLOAT_SMALLEST_NON_DENORMAL !!!!");
        }
        if (fadeAlpha_ < MATH_FLOAT_SMALLEST_NON_DENORMAL) {
            fadeAlpha_ = 0.0f;
            fadeState_ = FADE_NONE;
            // ALOG( "FadeState = FADE_NONE" );
        } else if (fadeAlpha_ >= 1.0f - MATH_FLOAT_SMALLEST_NON_DENORMAL) {
            fadeAlpha_ = 1.0f;
            fadeState_ = FADE_NONE;
            // ALOG( "FadeState = FADE_NONE" );
        }
        // ALOG( "fadeState = %s, fadeDelta = %.4f, fadeAlpha = %.4f", GetFadeStateName( FadeState
        // ), fadeDelta, FadeAlpha );
    }
}

//==============================
// Fader::StartFadeIn
void Fader::StartFadeIn() {
    // ALOG( "StartFadeIn" );
    fadeState_ = FADE_IN;
}

//==============================
// Fader::StartFadeOut
void Fader::StartFadeOut() {
    // ALOG( "StartFadeOut" );
    fadeState_ = FADE_OUT;
}

//==============================
// Fader::PauseFade
void Fader::PauseFade() {
    // ALOG( "PauseFade" );
    prePauseState_ = fadeState_;
    fadeState_ = FADE_PAUSED;
}

//==============================
// Fader::UnPause
void Fader::UnPause() {
    fadeState_ = prePauseState_;
}

//==============================
// Fader::GetFadeStateName
char const* Fader::GetFadeStateName(eFadeState const state) {
    constexpr auto fadeStateNames =
        std::to_array<const char*>({"FADE_NONE", "FADE_PAUSED", "FADE_IN", "FADE_OUT"});
    return fadeStateNames[state];
}

//==============================
// Fader::Reset
void Fader::Reset() {
    fadeAlpha_ = startAlpha_;
}

//==============================
// Fader::Reset
void Fader::ForceFinish() {
    fadeAlpha_ = fadeState_ == FADE_IN ? 1.0f : 0.0f;
    fadeState_ = FADE_NONE;
}

//==============================
// Fader::IsFadingInOrFadedIn
bool Fader::IsFadingInOrFadedIn() const {
    if (fadeState_ == FADE_PAUSED) {
        return IsFadingInOrFadedIn(prePauseState_);
    }
    return IsFadingInOrFadedIn(fadeState_);
}

//==============================
// Fader::IsFadingInOrFadedIn
bool Fader::IsFadingInOrFadedIn(eFadeState const state) const {
    switch (fadeState_) {
        case FADE_IN:
            return true;
        case FADE_OUT:
            return false;
        case FADE_NONE:
            return fadeAlpha_ >= 1.0f;
        default:
            assert(false); // this should never be called with state FADE_PAUSE
            return false;
    }
}

//======================================================================================
// SineFader
//======================================================================================

//==============================
// SineFader::SineFader
SineFader::SineFader(float const startAlpha) : Fader(startAlpha) {}

//==============================
// SineFader::GetFinalAlpha
float SineFader::GetFinalAlpha() const {
    // NOTE: pausing will still re-calculate the
    if (GetFadeState() == FADE_NONE) {
        return GetFadeAlpha(); // already clamped
    }
    // map to sine wave
    float radians = (1.0f - GetFadeAlpha()) * MATH_FLOAT_PI; // range 0 to pi
    return (cosf(radians) + 1.0f) * 0.5f; // range 0 to 1
}

} // namespace OVRFW
