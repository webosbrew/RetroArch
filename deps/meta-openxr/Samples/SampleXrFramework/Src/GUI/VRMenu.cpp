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

Filename    :   VRMenu.cpp
Content     :   Class that implements the basic framework for a VR menu, holds all the
                components for a single menu, and updates the VR menu event handler to
                process menu events for a single menu.
Created     :   June 30, 2014
Authors     :   Jonathan E. Wright

*************************************************************************************/

// includes required for VRMenu.h

#include "VRMenu.h"

#include "VRMenuMgr.h"
#include "VRMenuEventHandler.h"
#include "GuiSys.h"
#include "Reflection.h"

#include "OVR_FileSys.h"
#include "Misc/Log.h"

using OVR::Bounds3f;
using OVR::Matrix4f;
using OVR::Posef;
using OVR::Quatf;
using OVR::Vector2f;
using OVR::Vector3f;
using OVR::Vector4f;

inline Vector3f GetViewMatrixPosition(Matrix4f const& m) {
    return m.Inverted().GetTranslation();
}

inline Vector3f GetViewMatrixForward(Matrix4f const& m) {
    return Vector3f(-m.M[2][0], -m.M[2][1], -m.M[2][2]).Normalized();
}

namespace OVRFW {

char const* VRMenu::menuStateNames[MENUSTATE_MAX] = {
    "MENUSTATE_OPENING",
    "MENUSTATE_OPEN",
    "MENUSTATE_CLOSING",
    "MENUSTATE_CLOSED"};

// singleton so that other ids initialized during static initialization can be based off tis
VRMenuId_t VRMenu::GetRootId() {
    VRMenuId_t ID_ROOT(-1);
    return ID_ROOT;
}

//==============================
// VRMenu::VRMenu
VRMenu::VRMenu(char const* name)
    : curMenuState_(MENUSTATE_CLOSED),
      nextMenuState_(MENUSTATE_CLOSED),
      eventHandler_(nullptr),
      name_(name),
      menuDistance_(1.45f),
      isInitialized_(false),
      componentsInitialized_(false),
      postInitialized_(false) {
    eventHandler_ = new VRMenuEventHandler;
}

//==============================
// VRMenu::~VRMenu
VRMenu::~VRMenu() {
    delete eventHandler_;
    eventHandler_ = nullptr;
}

//==============================
// VRMenu::Create
VRMenu* VRMenu::Create(char const* menuName) {
    return new VRMenu(menuName);
}

//==============================
// VRMenu::Init
void VRMenu::Init(
    OvrGuiSys& guiSys,
    float const menuDistance,
    VRMenuFlags_t const& flags,
    std::vector<VRMenuComponent*> comps /*= std::vector< VRMenuComponent* >()*/) {
    assert(!rootHandle_.IsValid());
    assert(!name_.empty());

    flags_ = flags;
    menuDistance_ = menuDistance;

    // create an empty root item
    VRMenuObjectParms rootParms(
        VRMENU_CONTAINER,
        comps,
        VRMenuSurfaceParms("root"),
        "Root",
        Posef(),
        Vector3f(1.0f, 1.0f, 1.0f),
        VRMenuFontParms(),
        GetRootId(),
        VRMenuObjectFlags_t(),
        VRMenuObjectInitFlags_t());
    rootHandle_ = guiSys.GetVRMenuMgr().CreateObject(rootParms);
    VRMenuObject* root = guiSys.GetVRMenuMgr().ToObject(rootHandle_);
    if (root == nullptr) {
        ALOGW("rootHandle_ (%" PRIu64 ") is invalid!", rootHandle_.Get());
        return;
    }

    isInitialized_ = true;
    componentsInitialized_ = false;
}

void VRMenu::InitWithItems(
    OvrGuiSys& guiSys,
    float const menuDistance,
    VRMenuFlags_t const& flags,
    std::vector<VRMenuObjectParms const*>& itemParms) {
    Init(guiSys, menuDistance, flags);

    if (Init_Impl(guiSys, menuDistance, flags, itemParms)) {
        AddItems(guiSys, itemParms, GetRootHandle(), true);
    }
}

//==============================================================
// ChildParmsPair
class ChildParmsPair {
   public:
    ChildParmsPair(menuHandle_t const handle, VRMenuObjectParms const* parms)
        : Handle(handle), Parms(parms) {}
    ChildParmsPair() : Parms(nullptr) {}

    menuHandle_t Handle;
    VRMenuObjectParms const* Parms;
};

/// OVR_PERF_ACCUMULATOR_EXTERN( VerifyImageParms );
/// OVR_PERF_ACCUMULATOR_EXTERN( FindSurfaceForGeoSizing );
/// OVR_PERF_ACCUMULATOR_EXTERN( CreateImageGeometry );
/// OVR_PERF_ACCUMULATOR_EXTERN( SelectProgramType );

//==============================
// VRMenu::AddItems
void VRMenu::AddItems(
    OvrGuiSys& guiSys,
    std::vector<VRMenuObjectParms const*>& itemParms,
    menuHandle_t parentHandle_,
    bool const recenter) {
    // OVR_PERF_TIMER( AddItems );

    const Vector3f up(0.0f, 1.0f, 0.0f);
    const Vector3f left(1.0f, 0.0f, 0.0f);

    // create all items in the itemParms array, add each one to the parent, and position all items
    // without the INIT_FORCE_POSITION flag vertically, one on top of the other
    VRMenuObject* root = guiSys.GetVRMenuMgr().ToObject(parentHandle_);
    assert(root != nullptr);

#if defined(OVR_USE_PERF_TIMER)
    double const createStartTime = SystemClock::GetTimeInSeconds();
    double createObjectTotal = 0.0;
#endif

    std::vector<ChildParmsPair> pairs;

    Vector3f nextItemPos(0.0f);
    int childIndex = 0;
    for (int i = 0; i < static_cast<int>(itemParms.size()); ++i) {
        VRMenuObjectParms const* parms = itemParms[i];

#if defined(OVR_BUILD_DEBUG)
        ALOG(
            "## Menu[%d] Id=%d Text=%s", i, static_cast<int>(parms->Id.Get()), parms->Text.c_str());
#endif

#if defined(OVR_BUILD_DEBUG)
        // assure all ids are different
        for (int j = 0; j < static_cast<int>(itemParms.size()); ++j) {
            if (j != i && parms->Id != VRMenuId_t() && parms->Id == itemParms[j]->Id) {
                ALOG(
                    "Duplicate menu object ids for '%s' and '%s'!",
                    parms->Text.c_str(),
                    itemParms[j]->Text.c_str());
            }
        }
#endif

#if defined(OVR_USE_PERF_TIMER)
        double const createObjectStartTime = SystemClock::GetTimeInSeconds();
#endif
        menuHandle_t handle = guiSys.GetVRMenuMgr().CreateObject(*parms);
#if defined(OVR_USE_PERF_TIMER)
        createObjectTotal += SystemClock::GetTimeInSeconds() - createObjectStartTime;
#endif

        if (handle.IsValid() && root != nullptr) {
            if (parms->ParentId != root->GetId() &&
                (parms->ParentId.IsValid() || !parms->ParentName.empty())) {
                pairs.push_back(ChildParmsPair(handle, parms));
            }
            root->AddChild(guiSys.GetVRMenuMgr(), handle);
            VRMenuObject* obj =
                guiSys.GetVRMenuMgr().ToObject(root->GetChildHandleForIndex(childIndex++));
            if (obj != nullptr && (parms->InitFlags & VRMENUOBJECT_INIT_FORCE_POSITION) == 0) {
                Bounds3f const& lb = obj->GetLocalBounds(guiSys.GetDefaultFont());
                Vector3f size = lb.GetSize() * obj->GetLocalScale();
                Vector3f centerOfs(left * (size.x * -0.5f));
                if (!parms->ParentId.IsValid()) // only contribute to height if not being reparented
                {
                    // stack the items
                    obj->SetLocalPose(Posef(Quatf(), nextItemPos + centerOfs));
                    // calculate the total height
                    nextItemPos += up * size.y;
                } else // otherwise center local to parent
                {
                    obj->SetLocalPose(Posef(Quatf(), centerOfs));
                }
            }
        }
    }

#if defined(OVR_USE_PERF_TIMER)
    ALOG("AddItems create took %f seconds", SystemClock::GetTimeInSeconds() - createStartTime);
    ALOG("Creating Objects took %f seconds", createObjectTotal);
#endif

    {
        // OVR_PERF_TIMER( Reparenting );

        // reparent
        std::vector<menuHandle_t> reparented;
        for (ChildParmsPair const& pair : pairs) {
            menuHandle_t handle;

            if (!pair.Parms->ParentName.empty()) {
                handle = HandleForName(guiSys.GetVRMenuMgr(), pair.Parms->ParentName.c_str());
            } else if (pair.Parms->ParentId.IsValid()) {
                handle = HandleForId(guiSys.GetVRMenuMgr(), pair.Parms->ParentId);
            }
            VRMenuObject* parent = guiSys.GetVRMenuMgr().ToObject(handle);
            assert(parent != nullptr);
            if (parent != nullptr) {
                root->RemoveChild(guiSys.GetVRMenuMgr(), pair.Handle);
                parent->AddChild(guiSys.GetVRMenuMgr(), pair.Handle);
            }
        }
    }

    if (recenter) {
        // center the menu based on the height of the auto-placed children
        float offset = nextItemPos.y * 0.5f;
        Vector3f rootPos = root->GetLocalPosition();
        rootPos -= offset * up;
        root->SetLocalPosition(rootPos);
    }

    // make sure VRMENU_EVENT_INIT is sent for any new components
    componentsInitialized_ = false;

#if 0
 OVR_PERF_REPORT( VerifyImageParms );
 OVR_PERF_REPORT( FindSurfaceForGeoSizing );
 OVR_PERF_REPORT( CreateImageGeometry );
 OVR_PERF_REPORT( SelectProgramType );
#endif
}

//==============================
// VRMenu::Shutdown
void VRMenu::Shutdown(OvrGuiSys& guiSys) {
    // OVR_ASSERT_WITH_TAG( IsInitialized, "VrMenu" );

    Shutdown_Impl(guiSys);

    // this will free the root and all children
    if (rootHandle_.IsValid()) {
        guiSys.GetVRMenuMgr().FreeObject(rootHandle_);
        rootHandle_.Release();
    }
}

//==============================
// VRMenu::RepositionMenu
void VRMenu::RepositionMenu(Matrix4f const& viewMatrix) {
    if (flags_ & VRMENU_FLAG_TRACK_GAZE) {
        menuPose_ = CalcMenuPosition(viewMatrix);
    } else if (flags_ & VRMENU_FLAG_PLACE_ON_HORIZON) {
        menuPose_ = CalcMenuPositionOnHorizon(viewMatrix);
    }
}

//==============================
// VRMenu::Frame
void VRMenu::Frame(
    OvrGuiSys& guiSys,
    ovrApplFrameIn const& vrFrame,
    Matrix4f const& centerViewMatrix,
    Matrix4f const& traceMat) {
    // OVR_PERF_TIMER( VRMenu_Frame );

    std::vector<VRMenuEvent> events;
    events.reserve(1024);
    // copy any pending events
    for (const auto& pendingEvent : pendingEvents_) {
        events.push_back(pendingEvent);
    }
    pendingEvents_.resize(0);

    if (!componentsInitialized_) {
        eventHandler_->InitComponents(events);
        componentsInitialized_ = true;
    }

    // note we never enter this block unless our next state is different -- the switch statement
    // inside this block is dependent on this.
    if (nextMenuState_ != curMenuState_) {
        ALOG("NextMenuState for '%s': %s", GetName(), menuStateNames[nextMenuState_]);
        switch (nextMenuState_) {
            case MENUSTATE_OPENING:
                assert(curMenuState_ != nextMenuState_); // logic below is dependent on this!!
                if (curMenuState_ == MENUSTATE_OPEN) {
                    // we're already open, so just set next to OPEN, too so we don't do any
                    // transition
                    nextMenuState_ = MENUSTATE_OPEN;
                } else {
                    RepositionMenu(centerViewMatrix);
                    eventHandler_->Opening(events);
                    Open_Impl(guiSys);
                }
                break;
            case MENUSTATE_OPEN: {
                assert(curMenuState_ != nextMenuState_); // logic below is dependent on this!!
                if (curMenuState_ != MENUSTATE_OPENING) {
                    ALOG("Instant open");
                }
                openSoundLimiter_.PlayMenuSound(guiSys, name_.c_str(), "sv_release_active", 0.1);
                eventHandler_->Opened(events);
            } break;
            case MENUSTATE_CLOSING:
                assert(curMenuState_ != nextMenuState_); // logic below is dependent on this!!
                if (curMenuState_ == MENUSTATE_CLOSED) {
                    // we're already closed, so just set next to CLOSED, too so we don't do any
                    // transition
                    nextMenuState_ = MENUSTATE_CLOSED;
                } else {
                    eventHandler_->Closing(events);
                }
                break;
            case MENUSTATE_CLOSED: {
                assert(curMenuState_ != nextMenuState_); // logic below is dependent on this!!
                if (curMenuState_ != MENUSTATE_CLOSING) {
                    ALOG("Instant close");
                }
                closeSoundLimiter_.PlayMenuSound(guiSys, name_.c_str(), "sv_deselect", 0.1);
                eventHandler_->Closed(events);
                Close_Impl(guiSys);
            } break;
            default:
                assert(!(bool)"Unhandled menu state!");
                break;
        }
        curMenuState_ = nextMenuState_;
    }

    switch (curMenuState_) {
        case MENUSTATE_OPENING:
            if (IsFinishedOpening()) {
                nextMenuState_ = MENUSTATE_OPEN;
            }
            break;
        case MENUSTATE_OPEN:
            break;
        case MENUSTATE_CLOSING:
            if (IsFinishedClosing()) {
                nextMenuState_ = MENUSTATE_CLOSED;
            }
            break;
        case MENUSTATE_CLOSED:
            // handle remaining events -- note focus path is empty right now, but this may still
            // broadcast messages to controls
            eventHandler_->HandleEvents(guiSys, vrFrame, rootHandle_, events);
            /// OVR_PERF_TIMER_STOP_MSG( VRMenu_Frame, name_.c_str() );
            return;
        default:
            assert(!(bool)"Unhandled menu state!");
            break;
    }

    if (flags_ & VRMENU_FLAG_TRACK_GAZE) {
        menuPose_ = CalcMenuPosition(centerViewMatrix);
    } else if (flags_ & VRMENU_FLAG_TRACK_GAZE_HORIZONTAL) {
        menuPose_ = CalcMenuPositionOnHorizon(centerViewMatrix);
    }

    Frame_Impl(guiSys, vrFrame);

    {
        // OVR_PERF_TIMER( VRMenu_Frame_EventHandler_Frame );
        eventHandler_->Frame(guiSys, vrFrame, rootHandle_, menuPose_, traceMat, events);
        /// OVR_PERF_TIMER_STOP_MSG( VRMenu_Frame_EventHandler_Frame, name_.c_str() );
    }
    {
        // OVR_PERF_TIMER( VRMenu_Frame_EventHandler_HandleEvents );
        eventHandler_->HandleEvents(guiSys, vrFrame, rootHandle_, events);
        /// OVR_PERF_TIMER_STOP_MSG( VRMenu_Frame_EventHandler_HandleEvents, name_.c_str() );
    }

    VRMenuObject* root = guiSys.GetVRMenuMgr().ToObject(rootHandle_);
    if (root != nullptr) {
        // OVR_PERF_TIMER( VRMenu_Frame_SubmitForRendering );
        VRMenuRenderFlags_t renderFlags;
        guiSys.GetVRMenuMgr().SubmitForRendering(
            guiSys, centerViewMatrix, rootHandle_, menuPose_, renderFlags);
        /// OVR_PERF_TIMER_STOP_MSG( VRMenu_Frame_SubmitForRendering, name_.c_str() );
    }

    if (!postInitialized_) {
        PostInit_Impl(guiSys, vrFrame);
        postInitialized_ = true;
    }

    /// OVR_PERF_TIMER_STOP_MSG( VRMenu_Frame, name_.c_str() );
}

//==============================
// VRMenu::OnKeyEvent
bool VRMenu::OnKeyEvent(OvrGuiSys& guiSys, int const keyCode, const int action) {
    if (OnKeyEvent_Impl(guiSys, keyCode, action)) {
        // consumed by sub class
        return true;
    }
    return false;
}

//==============================
// VRMenu::Open
void VRMenu::Open(OvrGuiSys& guiSys) {
    ALOG(
        "VRMenu::Open - '%s', pre - c: %s n: %s",
        GetName(),
        menuStateNames[curMenuState_],
        menuStateNames[nextMenuState_]);
    if (curMenuState_ == MENUSTATE_OPENING) {
        // this is a NOP, never allow transitioning back to the same state
        return;
    }
    nextMenuState_ = MENUSTATE_OPENING;
    guiSys.MakeActive(this);
    ALOG(
        "VRMenu::Open - %s, post - c: %s n: %s",
        GetName(),
        menuStateNames[curMenuState_],
        menuStateNames[nextMenuState_]);
}

//==============================
// VRMenu::Close
void VRMenu::Close(OvrGuiSys& guiSys, bool const instant) {
    ALOG(
        "VRMenu::Close - %s, pre - c: %s n: %s",
        GetName(),
        menuStateNames[curMenuState_],
        menuStateNames[nextMenuState_]);
    if (curMenuState_ == MENUSTATE_CLOSING) {
        // this is a NOP, never allow transitioning back to the same state
        return;
    }
    nextMenuState_ = instant ? MENUSTATE_CLOSED : MENUSTATE_CLOSING;
    ALOG(
        "VRMenu::Close - %s, post - c: %s n: %s",
        GetName(),
        menuStateNames[curMenuState_],
        menuStateNames[nextMenuState_]);
}

//==============================
// VRMenu::HandleForId
menuHandle_t VRMenu::HandleForId(OvrVRMenuMgr const& menuMgr, VRMenuId_t const id) const {
    VRMenuObject* root = menuMgr.ToObject(rootHandle_);
    assert(root != nullptr);
    return root->ChildHandleForId(menuMgr, id);
}

//==============================
// VRMenu::ObjectForId
VRMenuObject* VRMenu::ObjectForId(OvrGuiSys const& guiSys, VRMenuId_t const id) const {
    VRMenuObject* root = guiSys.GetVRMenuMgr().ToObject(rootHandle_);
    assert(root != nullptr);
    menuHandle_t handle = root->ChildHandleForId(guiSys.GetVRMenuMgr(), id);
    return guiSys.GetVRMenuMgr().ToObject(handle);
}

//==============================
// VRMenu::HandleForName
menuHandle_t VRMenu::HandleForName(OvrVRMenuMgr const& menuMgr, char const* name) const {
    VRMenuObject* root = menuMgr.ToObject(rootHandle_);
    assert(root != nullptr);
    return root->ChildHandleForName(menuMgr, name);
}

//==============================
// VRMenu::ObjectForName
VRMenuObject* VRMenu::ObjectForName(OvrGuiSys const& guiSys, char const* name) const {
    VRMenuObject* root = guiSys.GetVRMenuMgr().ToObject(rootHandle_);
    assert(root != nullptr);
    menuHandle_t handle = root->ChildHandleForName(guiSys.GetVRMenuMgr(), name);
    return guiSys.GetVRMenuMgr().ToObject(handle);
}

//==============================
// VRMenu::IdForName
VRMenuId_t VRMenu::IdForName(OvrGuiSys const& guiSys, char const* name) const {
    VRMenuObject* obj = ObjectForName(guiSys, name);
    if (obj == nullptr) {
        return VRMenuId_t();
    }
    return obj->GetId();
}

//==============================
// VRMenu::CalcMenuPosition
Posef VRMenu::CalcMenuPosition(Matrix4f const& viewMatrix) const {
    const Matrix4f invViewMatrix = viewMatrix.Inverted();
    const Vector3f viewPos(GetViewMatrixPosition(viewMatrix));
    const Vector3f viewFwd(GetViewMatrixForward(viewMatrix));

    // spawn directly in front
    Quatf rotation(-viewFwd, 0.0f);
    Quatf viewRot(invViewMatrix);
    Quatf fullRotation = rotation * viewRot;
    fullRotation.Normalize();

    Vector3f position(viewPos + viewFwd * menuDistance_);

    return Posef(fullRotation, position);
}

//==============================
// VRMenu::CalcMenuPositionOnHorizon
Posef VRMenu::CalcMenuPositionOnHorizon(Matrix4f const& viewMatrix) const {
    const Vector3f viewPos(GetViewMatrixPosition(viewMatrix));
    const Vector3f viewFwd(GetViewMatrixForward(viewMatrix));

    // project the forward view onto the horizontal plane
    Vector3f const up(0.0f, 1.0f, 0.0f);
    float dot = viewFwd.Dot(up);
    Vector3f horizontalFwd =
        (dot < -0.99999f || dot > 0.99999f) ? Vector3f(1.0f, 0.0f, 0.0f) : viewFwd - (up * dot);
    horizontalFwd.Normalize();

    Matrix4f horizontalViewMatrix = Matrix4f::LookAtRH(Vector3f(0), horizontalFwd, up);
    horizontalViewMatrix
        .Transpose(); // transpose because we want the rotation opposite of where we're looking

    // this was only here to test rotation about the local axis
    // Quatf rotation( -horizontalFwd, 0.0f );

    Quatf viewRot(horizontalViewMatrix);
    Quatf fullRotation = /*rotation * */ viewRot;
    fullRotation.Normalize();

    Vector3f position(viewPos + horizontalFwd * menuDistance_);

    return Posef(fullRotation, position);
}

//==============================
// VRMenu::OnItemEvent
void VRMenu::OnItemEvent(
    OvrGuiSys& guiSys,
    ovrApplFrameIn const& vrFrame,
    VRMenuId_t const itemId,
    VRMenuEvent const& event) {
    OnItemEvent_Impl(guiSys, vrFrame, itemId, event);
}

//==============================
// VRMenu::Init_Impl
bool VRMenu::Init_Impl(
    OvrGuiSys& guiSys,
    float const menuDistance,
    VRMenuFlags_t const& flags,
    std::vector<VRMenuObjectParms const*>& itemParms) {
    return true;
}

//==============================
// VRMenu::PostInit_Impl
void VRMenu::PostInit_Impl(OvrGuiSys& guiSys, ovrApplFrameIn const& vrFrame) {}

//==============================
// VRMenu::Shutdown_Impl
void VRMenu::Shutdown_Impl(OvrGuiSys& guiSys) {}

//==============================
// VRMenu::Frame_Impl
void VRMenu::Frame_Impl(OvrGuiSys& guiSys, ovrApplFrameIn const& vrFrame) {}

//==============================
// VRMenu::OnKeyEvent_Impl
bool VRMenu::OnKeyEvent_Impl(OvrGuiSys& guiSys, int const keyCode, const int action) {
    return false;
}

//==============================
// VRMenu::Open_Impl
void VRMenu::Open_Impl(OvrGuiSys& guiSys) {}

//==============================
// VRMenu::Close_Impl
void VRMenu::Close_Impl(OvrGuiSys& guiSys) {}

//==============================
// VRMenu::OnItemEvent_Impl
void VRMenu::OnItemEvent_Impl(
    OvrGuiSys& guiSys,
    ovrApplFrameIn const& vrFrame,
    VRMenuId_t const itemId,
    VRMenuEvent const& event) {}

//==============================
// VRMenu::GetFocusedHandle()
menuHandle_t VRMenu::GetFocusedHandle() const {
    if (eventHandler_ != nullptr) {
        return eventHandler_->GetFocusedHandle();
    }
    return menuHandle_t();
}

//==============================
// VRMenu::ResetMenuOrientation
void VRMenu::ResetMenuOrientation(Matrix4f const& viewMatrix) {
    ALOG("ResetMenuOrientation for '%s'", GetName());
    ResetMenuOrientation_Impl(viewMatrix);
}

//==============================
// VRMenu::ResetMenuOrientation_Impl
void VRMenu::ResetMenuOrientation_Impl(Matrix4f const& viewMatrix) {
    RepositionMenu(viewMatrix);
}

//==============================
// VRMenu::SetSelected
void VRMenu::SetSelected(VRMenuObject* obj, bool const selected) {
    if (obj != nullptr) {
        if (obj->IsSelected() != selected) {
            obj->SetSelected(selected);
            VRMenuEvent ev(
                selected ? VRMENU_EVENT_SELECTED : VRMENU_EVENT_DESELECTED,
                EVENT_DISPATCH_TARGET,
                obj->GetHandle(),
                Vector3f(0.0f),
                HitTestResult(),
                "");
            pendingEvents_.push_back(ev);
        }
    }
}

//==============================
// VRMenu::SetSelected
void VRMenu::SetSelected(OvrGuiSys& guiSys, VRMenuId_t const itemId, bool const selected) {
    VRMenuObject* obj = ObjectForId(guiSys, itemId);
    return SetSelected(obj, selected);
}

//==============================
// VRMenu::InitFromReflectionData
bool VRMenu::InitFromReflectionData(
    OvrGuiSys& guiSys,
    ovrFileSys& fileSys,
    ovrReflection& refl,
    ovrLocale const& locale,
    char const* fileNames[],
    float const menuDistance,
    VRMenuFlags_t const& flags) {
    std::vector<VRMenuObjectParms const*> itemParms;
    for (int i = 0; fileNames[i] != nullptr; ++i) {
        std::vector<uint8_t> parmBuffer;
        if (!fileSys.ReadFile(fileNames[i], parmBuffer)) {
            DeletePointerArray(itemParms);
            ALOG("Failed to load reflection file '%s'.", fileNames[i]);
            return false;
        }

        // Add a null terminator
        parmBuffer.push_back('\0');

#if defined(OVR_BUILD_DEBUG)
///  ALOG( "Loaded reflection file:\n==============\n%s\n=================\n", &parmBuffer[0] );
#endif

        ovrParseResult parseResult =
            VRMenuObject::ParseItemParms(refl, locale, fileNames[i], parmBuffer, itemParms);
        if (!parseResult) {
            DeletePointerArray(itemParms);
            ALOG("%s", parseResult.GetErrorText());
            return false;
        }
    }

    InitWithItems(guiSys, menuDistance, flags, itemParms);
    return true;
}

} // namespace OVRFW
