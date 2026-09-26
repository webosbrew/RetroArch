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

Filename    :   ParticleSystem.h
Content     :   A simple particle system for System Activities.
Created     :   October 12, 2015
Authors     :   Jonathan E. Wright

************************************************************************************/

#include "ParticleSystem.h"

#include "TextureAtlas.h"
#include "Render/GlGeometry.h"

#include <cassert>

using OVR::Matrix4f;
using OVR::Posef;
using OVR::Quatf;
using OVR::Vector2f;
using OVR::Vector3f;
using OVR::Vector4f;

inline Vector3f GetViewMatrixForward(Matrix4f const& m) {
    return Vector3f(-m.M[2][0], -m.M[2][1], -m.M[2][2]).Normalized();
}

namespace OVRFW {

static const char* particleVertexSrc = R"glsl(
attribute vec4 Position;
attribute vec2 TexCoord;
attribute vec4 VertexColor;
varying highp vec2 oTexCoord;
varying lowp vec4 oColor;
void main()
{
    gl_Position = TransformVertex( Position );
    oTexCoord = TexCoord;
    oColor = VertexColor;
}
)glsl";

static const char* particleFragmentSrc = R"glsl(
uniform sampler2D Texture0;
varying highp vec2 oTexCoord;
varying lowp vec4 oColor;
void main()
{
    gl_FragColor = oColor * texture2D( Texture0, oTexCoord );
}
)glsl";

static const char* particleGeoFragmentSrc = R"glsl(
precision highp float;

varying highp vec2 oTexCoord;
varying lowp vec4 oColor;
void main()
{
    float dist = distance(oTexCoord, vec2(0.0f));
    float alpha = smoothstep(0.6f, 0.35f, dist);
    gl_FragColor = mix(vec4(0.0f), oColor, alpha);
}
)glsl";

static Vector3f quadVertPos[4] = {
    {-0.5f, 0.5f, 0.0f},
    {0.5f, 0.5f, 0.0f},
    {0.5f, -0.5f, 0.0f},
    {-0.5f, -0.5f, 0.0f}};

static Vector2f quadUVs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

ovrParticleSystem::ovrParticleSystem() : maxParticles_(0) {}

ovrParticleSystem::~ovrParticleSystem() {
    Shutdown();
}

void ovrParticleSystem::Init(
    const size_t maxParticles,
    const ovrTextureAtlas* atlas,
    const ovrGpuState& gpuState,
    bool const sortParticles) {
    // this can be called multiple times
    Shutdown();

    maxParticles_ = maxParticles;

    // free any existing particles
    particles_.reserve(maxParticles);
    freeParticles_.reserve(maxParticles);
    activeParticles_.reserve(maxParticles);

    // create the geometry
    CreateGeometry(maxParticles);

    {
        OVRFW::ovrProgramParm uniformParms[] = {
            /// Vertex
            /// Fragment
            {.Name = "Texture0", .Type = OVRFW::ovrProgramParmType::TEXTURE_SAMPLED},
        };
        const int uniformCount = sizeof(uniformParms) / sizeof(OVRFW::ovrProgramParm);
        if (atlas != nullptr) {
            program_ = OVRFW::GlProgram::Build(
                particleVertexSrc, particleFragmentSrc, uniformParms, uniformCount);
            surfaceDef_.surfaceName = std::string("particles_") + atlas->GetTextureName();
            surfaceDef_.graphicsCommand.Textures[0] = atlas->GetTexture();
        } else {
            program_ = OVRFW::GlProgram::Build(
                particleVertexSrc, particleGeoFragmentSrc, uniformParms, uniformCount);
        }
    }

    surfaceDef_.graphicsCommand.Program = program_;
    surfaceDef_.graphicsCommand.BindUniformTextures();

    surfaceDef_.graphicsCommand.GpuState = gpuState;

    sortParticles_ = sortParticles;

    derived_.reserve(maxParticles);
    sortIndices_.reserve(maxParticles);
    attr_.position.reserve(maxParticles * 4);
    attr_.color.reserve(maxParticles * 4);
    attr_.uv0.reserve(maxParticles * 4);
    packedAttr_.reserve(maxParticles * 12 * 16 * 8);
}

ovrGpuState ovrParticleSystem::GetDefaultGpuState() {
    ovrGpuState s;
    s.blendEnable = ovrGpuState::BLEND_ENABLE;
    s.blendSrc = ovrGpuState::kGL_SRC_ALPHA;
    s.blendDst = ovrGpuState::kGL_ONE;
    s.depthEnable = true;
    s.depthMaskEnable = false;
    s.cullEnable = true;
    return s;
}

int ParticleSortFn(void const* a, void const* b) {
    if (static_cast<const particleSort_t*>(b)->distanceSq <
        static_cast<const particleSort_t*>(a)->distanceSq) {
        return -1;
    }
    return 1;
}

void ovrParticleSystem::Frame(
    const OVRFW::ovrApplFrameIn& frame,
    const ovrTextureAtlas* atlas,
    const Matrix4f& centerEyeViewMatrix) {
    // OVR_PERF_TIMER( ovrParticleSystem_Frame );

    if (activeParticles_.empty()) {
        return;
    }

    // update particles
    Matrix4f invViewMatrix = centerEyeViewMatrix.Inverted();
    Vector3f viewPos = invViewMatrix.GetTranslation();

    int activeCount = 0;

    // update existing particles and its vertices, deriving the current state of each particle based
    // on it's current age store the derived state of each particle in the derived array. Also, make
    // another array that is just the index of each particle and its distance to the view position.
    // This array will be sorted by distance and then used to index into the derived array the
    // vertices are transformed.

    std::vector<particleDerived_t>& derived = derived_;
    std::vector<particleSort_t>& sortIndices = sortIndices_;
    derived.resize(activeParticles_.size());
    sortIndices.resize(activeParticles_.size());

    for (size_t i = 0; i < activeParticles_.size(); ++i) {
        const handle_t handle = activeParticles_[i];
        ovrParticle& p = particles_[handle.Get()];

        if (frame.PredictedDisplayTime - p.startTime > p.lifeTime) {
            // free expired particle
            p.startTime = -1.0; // mark as unused
            freeParticles_.push_back(handle);
            // Swap this slot with the last item, order doesn't matter
            activeParticles_.at(i) = activeParticles_.back();
            activeParticles_.pop_back();
            i--; // last particle was moved into current slot, so don't skip it
            continue;
        }

        float t = static_cast<float>(frame.PredictedDisplayTime - p.startTime);
        float tSq = t * t;

        particleDerived_t& d = derived[activeCount];
        // x = x0 + v0 * t + 0.5f * a * t^2
        d.pos = p.initialPosition + p.initialVelocity * t + p.halfAcceleration * tSq;
        d.orientation = (p.rotationRate * t) + p.initialOrientation;

        d.color = easeFunctions[p.easeFunc](p.initialColor, t / p.lifeTime);

        d.scale = p.initialScale;
        d.spriteIndex = p.spriteIndex;

        particleSort_t& ps = sortIndices[activeCount];
        ps.activeIndex = activeCount;
        ps.distanceSq =
            (d.pos - viewPos).LengthSq(); // Dot( centerEyeViewMatrix.GetZBasis() );//.LengthSq();

        activeCount++;
    }

    assert(activeParticles_.size() == (size_t)activeCount);

    if (activeCount > 0) {
        // sort by distance to view pos
        if (sortParticles_) {
            qsort(&sortIndices[0], activeCount, sizeof(sortIndices[0]), ParticleSortFn);
        }

        attr_.position.resize(activeCount * 4);
        attr_.color.resize(activeCount * 4);
        attr_.uv0.resize(activeCount * 4);

        // transform vertices for each particle quad
        for (int i = 0; i < activeCount; ++i) {
            particleSort_t const& si = sortIndices[i];
            particleDerived_t const& p = derived[si.activeIndex];

            // transform vertices on the CPU
            Matrix4f rotMatrix = Matrix4f::RotationZ(p.orientation);
            // This always aligns the particle to the direction of the particle to the view
            // position. This looks a little better but is more expensive and only really makes a
            // difference for large particles.
            Vector3f normal = (viewPos - p.pos).Normalized();
            if (normal.LengthSq() < 0.999f) {
                normal = GetViewMatrixForward(centerEyeViewMatrix);
            }
            Matrix4f particleTransform =
                Matrix4f::CreateFromBasisVectors(normal, Vector3f(0.0f, 1.0f, 0.0f));
            particleTransform.SetTranslation(p.pos);

            for (int v = 0; v < 4; ++v) {
                attr_.position[i * 4 + v] =
                    particleTransform.Transform(rotMatrix.Transform(quadVertPos[v] * p.scale));
                attr_.color[i * 4 + v] = p.color;
            }

            if (atlas != nullptr) {
                // set UVs of this sprite in the atlas
                const ovrTextureAtlas::ovrSpriteDef& sd = atlas->GetSpriteDef(p.spriteIndex);
                attr_.uv0[i * 4 + 0] = Vector2f(sd.uvMins.x, sd.uvMins.y);
                attr_.uv0[i * 4 + 1] = Vector2f(sd.uvMaxs.x, sd.uvMins.y);
                attr_.uv0[i * 4 + 2] = Vector2f(sd.uvMaxs.x, sd.uvMaxs.y);
                attr_.uv0[i * 4 + 3] = Vector2f(sd.uvMins.x, sd.uvMaxs.y);
            } else {
                attr_.uv0[i * 4 + 0] = Vector2f(-1, -1);
                attr_.uv0[i * 4 + 1] = Vector2f(1, -1);
                attr_.uv0[i * 4 + 2] = Vector2f(1, 1);
                attr_.uv0[i * 4 + 3] = Vector2f(-1, 1);
            }
        }
    }

    // update the geometry with new vertex attributes
    surfaceDef_.geo.Update(attr_);
}

void ovrParticleSystem::Shutdown() {
    surfaceDef_.geo.Free();
    OVRFW::GlProgram::Free(program_);
}

void ovrParticleSystem::RenderEyeView(
    Matrix4f const& viewMatrix,
    Matrix4f const& projectionMatrix,
    std::vector<ovrDrawSurface>& surfaceList) const {
    // OVR_UNUSED( viewMatrix );
    // OVR_UNUSED( projectionMatrix );

    // Don't even add a surface if not needed
    if (activeParticles_.empty()) {
        return;
    }

    // add a surface
    ovrDrawSurface surf;
    surf.modelMatrix = modelMatrix_;
    surf.surface = &surfaceDef_;
    surfaceList.push_back(surf);
}

ovrParticleSystem::handle_t ovrParticleSystem::AddParticle(
    const OVRFW::ovrApplFrameIn& frame,
    const Vector3f& initialPosition,
    const float initialOrientation,
    const Vector3f& initialVelocity,
    const Vector3f& acceleration,
    const Vector4f& initialColor,
    const ovrEaseFunc easeFunc,
    const float rotationRate,
    const float scale,
    const float lifeTime,
    const uint16_t spriteIndex) {
    ovrParticle* p = nullptr;

    handle_t particleHandle;
    if (!freeParticles_.empty()) {
        particleHandle = handle_t(freeParticles_[freeParticles_.size() - 1]);
        freeParticles_.pop_back();
        activeParticles_.push_back(particleHandle);
        assert(particleHandle.IsValid());
        assert((size_t)particleHandle.Get() < particles_.size());
        p = &particles_[particleHandle.Get()];
    } else {
        if (particles_.size() >= maxParticles_) {
            return handle_t(); // adding more would overflow the VAO
        }
        particleHandle = handle_t(particles_.size());
        particles_.emplace_back();
        activeParticles_.push_back(particleHandle);
        p = &particles_[particles_.size() - 1];
    }

    p->startTime = frame.PredictedDisplayTime;
    p->lifeTime = lifeTime;
    p->initialPosition = initialPosition;
    p->initialOrientation = initialOrientation;
    p->initialVelocity = initialVelocity;
    p->halfAcceleration = acceleration * 0.5f;
    p->initialColor = initialColor;
    p->easeFunc = easeFunc;
    p->rotationRate = rotationRate;
    p->initialScale = scale;
    p->spriteIndex = spriteIndex;

    return particleHandle;
}

void ovrParticleSystem::UpdateParticle(
    const OVRFW::ovrApplFrameIn& frame,
    const handle_t handle,
    const Vector3f& position,
    const float orientation,
    const Vector3f& velocity,
    const Vector3f& acceleration,
    const Vector4f& color,
    const ovrEaseFunc easeFunc,
    const float rotationRate,
    const float scale,
    const float lifeTime,
    const uint16_t spriteIndex) {
    if (!handle.IsValid() || (size_t)handle.Get() >= particles_.size()) {
        assert(handle.IsValid() && (size_t)handle.Get() < particles_.size());
        return;
    }
    ovrParticle& p = particles_[handle.Get()];
    p.initialPosition = position;
    p.initialOrientation = orientation;
    p.initialVelocity = velocity;
    p.halfAcceleration = acceleration * 0.5f;
    p.initialColor = color;
    p.easeFunc = easeFunc;
    p.rotationRate = rotationRate;
    p.initialScale = scale;
    p.spriteIndex = spriteIndex;
    p.startTime = frame.PredictedDisplayTime;
    p.lifeTime = lifeTime;
}

void ovrParticleSystem::RemoveParticle(const handle_t handle) {
    if (!handle.IsValid() || (size_t)handle.Get() >= particles_.size()) {
        return;
    }
    // particle will get removed in the next update
    particles_[handle.Get()].startTime = -1.0; // mark as unused
    particles_[handle.Get()].lifeTime = 0.0;
}

void ovrParticleSystem::CreateGeometry(const int maxParticles) {
    surfaceDef_.geo.Free();

    VertexAttribs attr;
    const int numVerts = maxParticles * 4;

    attr.position.resize(numVerts);
    attr.normal.resize(numVerts);
    attr.color.resize(numVerts);
    attr.uv0.resize(numVerts);

    std::vector<TriangleIndex> indices;
    const int numIndices = maxParticles * 6;
    indices.resize(numIndices);

    for (int i = 0; i < maxParticles; ++i) {
        for (int v = 0; v < 4; v++) {
            attr.position[i * 4 + v] = quadVertPos[v];
            attr.normal[i * 4 + v] = {0.0f, 0.0f, 1.0f};
            attr.color[i * 4 + v] = {1.0f, 0.0f, 1.0f, 1.0f};
            attr.uv0[i * 4 + v] = quadUVs[v];
        }

        indices[i * 6 + 0] = static_cast<uint16_t>(i * 4 + 0);
        indices[i * 6 + 1] = static_cast<uint16_t>(i * 4 + 3);
        indices[i * 6 + 2] = static_cast<uint16_t>(i * 4 + 1);
        indices[i * 6 + 3] = static_cast<uint16_t>(i * 4 + 1);
        indices[i * 6 + 4] = static_cast<uint16_t>(i * 4 + 3);
        indices[i * 6 + 5] = static_cast<uint16_t>(i * 4 + 2);
    }

    surfaceDef_.geo.Create(attr, indices);
}

} // namespace OVRFW
