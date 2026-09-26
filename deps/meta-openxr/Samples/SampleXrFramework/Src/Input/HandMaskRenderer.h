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

Filename    :   HandMaskRenderer.h
Content     :   A one stop for rendering hand masks
Created     :   03/24/2020
Authors     :   Federico Schliemann

************************************************************************************/

#pragma once

#include <vector>
#include <string>
#include <memory>

/// Sample Framework
#include "Misc/Log.h"
#include "Render/GlProgram.h"
#include "Render/SurfaceRender.h"

#include "OVR_Math.h"

namespace OVRFW {

class HandMaskRenderer {
   public:
    HandMaskRenderer() = default;
    ~HandMaskRenderer() = default;

    void Init(bool leftHand);
    void Shutdown();
    void Update(
        const OVR::Posef& headPose,
        const OVR::Posef& handPose,
        const std::vector<OVR::Matrix4f>& jointTransforms,
        const float handSize = 1.0f);
    void Render(std::vector<ovrDrawSurface>& surfaceList);

   public:
    float layerBlend;
    float falloff;
    float intensity;
    float fadeIntensity;
    bool useBorderFade;
    float borderFadeSize;
    float alphaMaskSize;
    bool renderInverseSubtract;
    ovrSurfaceDef handMaskSurfaceDef;

   private:
    GlProgram progHandMaskAlphaGradient_;
    GlProgram progHandMaskBorderFade_;
    ovrDrawSurface handMaskSurface_;
    std::vector<OVR::Matrix4f> handMaskMatrices_;
    std::vector<OVR::Vector3f> handMaskColors_;
    GlBuffer handMaskUniformBuffer_;
    GlBuffer handColorUniformBuffer_;
    bool isLeftHand_;
};

} // namespace OVRFW
