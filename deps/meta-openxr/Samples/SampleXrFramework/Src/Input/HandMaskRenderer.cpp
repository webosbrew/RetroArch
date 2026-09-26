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

Filename    :   HandMaskRenderer.cpp
Content     :   A one stop for rendering hand masks
Created     :   May 2021
Authors     :   Federico Schliemann

************************************************************************************/

#include "HandMaskRenderer.h"

using OVR::Matrix4f;
using OVR::Posef;
using OVR::Quatf;
using OVR::Vector3f;
using OVR::Vector4f;

#include <openxr/openxr.h>

namespace OVRFW {

static_assert(MAX_JOINTS == 64, "MAX_JOINTS != 64");

const char* VertexShaderSrc = R"glsl(
  uniform JointMatrices
  {
    highp mat4 Joints[64];
  } jb;

  uniform JointColors
  {
    highp vec3 Colors[64];
  } jc;

  attribute highp vec4 Position;
  attribute highp vec2 TexCoord;
  varying highp vec2 oTexCoord;
  varying highp vec3 oInstanceColor;

  void main()
  {
    highp vec4 localPos = jb.Joints[ gl_InstanceID ] * Position;
    gl_Position = TransformVertex( localPos );
    oTexCoord = TexCoord;
    oInstanceColor = jc.Colors[ gl_InstanceID ];
  }
)glsl";

static const char* FragmentShaderSrc = R"glsl(
    precision highp float;

    varying highp vec2 oTexCoord;
    varying highp vec3 oInstanceColor;

    uniform float LayerBlend;
    uniform float Falloff;
    uniform float Intensity;
    uniform float FadeIntensity;

    float BorderFade(vec2 uv, float falloff, float intensity)
    {
        uv *= 1.0 - uv.yx;
        float fade = uv.x * uv.y * intensity;
        fade = pow(fade, falloff);
        return clamp(fade, 0.0, 1.0) * FadeIntensity;
    }

    float AlphaGradient(vec2 uv)
    {
        vec2 v = (uv - vec2(0.5)) * vec2(2.0);
        float r = 1.0 - clamp(length(v), 0.0, 1.0);
        r = smoothstep(0.0, 1.0, sqrt(r));
        return r;
    }

    void main()
    {
#ifdef USE_BORDER_FADE
        float r = BorderFade(oTexCoord, 4.0, 15.0);
#else
        float r = AlphaGradient(oTexCoord);
#endif /// USE_BORDER_FADE

        float a = r * LayerBlend;
        gl_FragColor = vec4(oInstanceColor,a);
    }
)glsl";

/* clang-format off */
std::vector<OVR::Vector3f> cells = {
    {0.00500, 0.00000, 0.00000},
    {0.01740, -0.00030, -0.00030},
    {0.00000, 0.00000, 0.00000},
    {0.01370, 0.00000, 0.00000},
    {0.00253, 0.00000, 0.00000},
    {0.01443, 0.00000, 0.00000},
    {0.02646, 0.00000, 0.00000},
    {0.00000, 0.00000, 0.00000},
    {0.01530, -0.00040, -0.00030},
    {0.00000, 0.00000, 0.00000},
    {0.01470, 0.00000, 0.00000},
    {0.00000, 0.00000, 0.00000},
    {0.01570, 0.00000, 0.00000},
    {0.02890, 0.00000, 0.00000},
    {0.00430, 0.00000, 0.00000},
    {0.01680, 0.00000, 0.00000},
    {-0.00170, 0.00000, 0.00000},
    {0.01110, 0.00000, 0.00000},
    {0.00100, 0.00000, 0.00000},
    {0.01460, 0.00000, 0.00000},
    {0.00260, 0.00000, 0.00000},
    {0.01610, 0.00000, -0.00010},
    {0.00000, 0.00000, 0.00000},
    {0.01400, 0.00000, 0.00000},
    {-0.01100, 0.00000, 0.00000},
    {0.00200, 0.00000, 0.00000},
    {0.01690, 0.00000, 0.00000},
    {0.00000, 0.00000, 0.00000},
    {0.01320, 0.00000, 0.00000},
    {0.00000, 0.00000, 0.00000},
    {0.01660, 0.00000, 0.00000},
    {0.00000, 0.00000, 0.00000},
    {0.01620, 0.00000, 0.00000},
    {0.08120, -0.01130, 0.02410},
    {0.03450, -0.01130, 0.01390},
    {0.06310, -0.01130, -0.03170},
    {0.03250, -0.01130, -0.01460},
    {0.08050, -0.01130, 0.00180},
    {0.06470, -0.01130, 0.02340},
    {0.04860, -0.01130, -0.00030},
    {0.04480, -0.01130, -0.02370},
    {0.07980, -0.01130, -0.01600},
    {0.03250, -0.01130, 0.00060},
    {0.06350, -0.01130, 0.01030},
    {0.04820, -0.01130, 0.01390},
    {0.04820, -0.01130, 0.02790},
    {0.06060, -0.01130, 0.03870},
    {0.06350, -0.01130, -0.00600},
    {0.06350, -0.01130, -0.01690},
    {0.04860, -0.01130, -0.01290}
};

std::vector<int16_t> cellParents = {
    XR_HAND_JOINT_INDEX_DISTAL_EXT,
    XR_HAND_JOINT_INDEX_DISTAL_EXT,
    XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,
    XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
    XR_HAND_JOINT_MIDDLE_DISTAL_EXT,
    XR_HAND_JOINT_MIDDLE_DISTAL_EXT,
    XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
    XR_HAND_JOINT_LITTLE_DISTAL_EXT,
    XR_HAND_JOINT_LITTLE_DISTAL_EXT,
    XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
    XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
    XR_HAND_JOINT_RING_DISTAL_EXT,
    XR_HAND_JOINT_RING_DISTAL_EXT,
    XR_HAND_JOINT_RING_INTERMEDIATE_EXT,
    XR_HAND_JOINT_RING_INTERMEDIATE_EXT,
    XR_HAND_JOINT_RING_INTERMEDIATE_EXT,
    XR_HAND_JOINT_RING_PROXIMAL_EXT,
    XR_HAND_JOINT_RING_PROXIMAL_EXT,
    XR_HAND_JOINT_THUMB_DISTAL_EXT,
    XR_HAND_JOINT_THUMB_DISTAL_EXT,
    XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
    XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
    XR_HAND_JOINT_THUMB_METACARPAL_EXT,
    XR_HAND_JOINT_THUMB_METACARPAL_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_WRIST_EXT
};

std::vector<OVR::Vector3f> cellColors = {
    {1.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
    {1.0, 0.2, 0.0},
    {1.0, 0.2, 0.0},
    {1.0, 0.4, 0.0},
    {1.0, 0.4, 0.0},
    {1.0, 0.4, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    {0.0, 0.0, 0.5}
};

const OVR::Quatf cellAdjustL =
    OVR::Quatf({1.0f, 0.0f, 0.0f}, OVR::DegreeToRad(180.0f)) *
    OVR::Quatf({0.0f, 1.0f, 0.0f}, OVR::DegreeToRad(-90.0f));
const OVR::Quatf cellAdjustR =
    OVR::Quatf({0.0f, 1.0f, 0.0f}, OVR::DegreeToRad(90.0f));

/* clang-format off */


void HandMaskRenderer::Init(bool leftHand) {
    /// Shader
    static ovrProgramParm UniformParms[] = {
        {.Name="JointMatrices", .Type=ovrProgramParmType::BUFFER_UNIFORM},
        {.Name="JointColors", .Type=ovrProgramParmType::BUFFER_UNIFORM},
        {.Name="LayerBlend", .Type=ovrProgramParmType::FLOAT},
        {.Name="Falloff", .Type=ovrProgramParmType::FLOAT},
        {.Name="Intensity", .Type=ovrProgramParmType::FLOAT},
        {.Name="FadeIntensity", .Type=ovrProgramParmType::FLOAT},
    };

    progHandMaskAlphaGradient_ = GlProgram::Build(
        "",
        VertexShaderSrc,
        "",
        FragmentShaderSrc,
        UniformParms,
        sizeof(UniformParms) / sizeof(ovrProgramParm));
    progHandMaskBorderFade_ = GlProgram::Build(
        "#define USE_BORDER_FADE 1",
        VertexShaderSrc,
        "#define USE_BORDER_FADE 1",
        FragmentShaderSrc,
        UniformParms,
        sizeof(UniformParms) / sizeof(ovrProgramParm));

    /// Shader instance buffer
    handMaskMatrices_.resize(MAX_JOINTS, OVR::Matrix4f::Identity());
    handMaskUniformBuffer_.Create(
        GLBUFFER_TYPE_UNIFORM, MAX_JOINTS * sizeof(Matrix4f), handMaskMatrices_.data());

    handMaskColors_.resize(MAX_JOINTS, OVR::Vector3f(0.0f,0.0f,0.0f));
    handColorUniformBuffer_.Create(
        GLBUFFER_TYPE_UNIFORM, MAX_JOINTS * sizeof(Vector3f), handMaskColors_.data());

    /// Create surface definition
    handMaskSurfaceDef.surfaceName = leftHand ? "HandMaskSurfaceL" : "HandMaskSurfaceR";
    handMaskSurfaceDef.geo = BuildTesselatedQuad(1, 1, false);
    handMaskSurfaceDef.numInstances = 0;
    /// Build the graphics command
    auto& gc = handMaskSurfaceDef.graphicsCommand;
    gc.Program = progHandMaskBorderFade_;
    gc.UniformData[0].Data = &handMaskUniformBuffer_;
    gc.UniformData[1].Data = &handColorUniformBuffer_;
    gc.UniformData[2].Data = &layerBlend;
    gc.UniformData[3].Data = &falloff;
    gc.UniformData[4].Data = &intensity;
    gc.UniformData[5].Data = &fadeIntensity;
    gc.GpuState.blendEnable = ovrGpuState::BLEND_ENABLE;
    gc.GpuState.blendMode = ovrGpuState::kGL_FUNC_REVERSE_SUBTRACT;
    gc.GpuState.blendSrc = ovrGpuState::kGL_SRC_ALPHA;
    gc.GpuState.blendDst = ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
    gc.GpuState.depthEnable = false;
    gc.GpuState.depthMaskEnable = false;
    /// Add surface
    handMaskSurface_.surface = &(handMaskSurfaceDef);

    /// Set defaults
    layerBlend = 1.0f;
    falloff = 4.0;
    intensity = 15.0f;
    fadeIntensity = 0.75f;
    useBorderFade = false;
    borderFadeSize = 0.01f;
    alphaMaskSize = 0.0175f;
    renderInverseSubtract = false;

    /// Set hand
    isLeftHand_ = leftHand;
}

void HandMaskRenderer::Shutdown() {}

inline Vector3f GetViewMatrixForward(const Matrix4f& m) {
    return Vector3f(-m.M[2][0], -m.M[2][1], -m.M[2][2]).Normalized();
}

inline Matrix4f GetViewMatrixFromPose(const OVR::Posef& pose) {
    const Matrix4f transform = Matrix4f(pose);
    return transform.Inverted();
}

void HandMaskRenderer::Update(
    const OVR::Posef& headPose,
    const OVR::Posef& handPose,
    const std::vector<OVR::Matrix4f>& jointTransforms,
    const float handSize) {
    /// get view position
    const Matrix4f centerEyeViewMatrix = GetViewMatrixFromPose(headPose);
    const Matrix4f invViewMatrix = centerEyeViewMatrix.Inverted();
    const Vector3f viewPos = invViewMatrix.GetTranslation();

    /// apply hand transform to the bones
    const Matrix4f matDeviceModel = Matrix4f(handPose);
    const float particleSize = (useBorderFade ? borderFadeSize : alphaMaskSize) * handSize;

    auto& gc = handMaskSurfaceDef.graphicsCommand;
    gc.Program = useBorderFade ? progHandMaskBorderFade_ : progHandMaskAlphaGradient_;
    if (renderInverseSubtract) {
        gc.GpuState.blendMode = ovrGpuState::kGL_FUNC_REVERSE_SUBTRACT;
        gc.GpuState.blendSrc = ovrGpuState::kGL_SRC_ALPHA;
        gc.GpuState.blendDst = ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
    } else {
        gc.GpuState.blendMode = ovrGpuState::kGL_FUNC_ADD;
        gc.GpuState.blendSrc = ovrGpuState::kGL_SRC_ALPHA;
        gc.GpuState.blendDst = ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
    }

    const OVR::Quatf q = (isLeftHand_ ? cellAdjustL : cellAdjustR).Inverted();
    for (uint32_t i = 0; i < cells.size(); ++i) {
        const uint32_t parent = cellParents[i];

        /// convert the cells to the adjusted screen space from the initial
        /// space provided by design
        Vector3f offset = isLeftHand_ ? cells[i] * -1.0f : cells[i];
        offset.x *= -1.0f;
        offset = q.Rotate(offset);
        const Matrix4f cellOffset = Matrix4f::Translation(offset);

        const Matrix4f m = (matDeviceModel * (jointTransforms[parent] * cellOffset));
        const Vector3f pos = m.GetTranslation();
        Vector3f normal = (viewPos - pos).Normalized();
        if (normal.LengthSq() < 0.999f) {
            normal = GetViewMatrixForward(centerEyeViewMatrix);
        }
        Matrix4f t = Matrix4f::CreateFromBasisVectors(normal, Vector3f(0.0f, 1.0f, 0.0f));
        t.SetTranslation(pos);
        t = t * Matrix4f::Scaling(particleSize);
        handMaskMatrices_[i] = t.Transposed();
/// colorize mask for debug purposes
#if 0
        handMaskColors_[i] = cellColors[i];
#endif
    }
    handMaskSurface_.modelMatrix = Matrix4f();
    handMaskSurfaceDef.numInstances = cells.size();
    handMaskUniformBuffer_.Update(
        handMaskMatrices_.size() * sizeof(Matrix4f), handMaskMatrices_.data());
    handColorUniformBuffer_.Update(
        handMaskColors_.size() * sizeof(Vector3f), handMaskColors_.data());
}

void HandMaskRenderer::Render(std::vector<ovrDrawSurface>& surfaceList) {
    surfaceList.push_back(handMaskSurface_);
}

} // namespace OVRFW
