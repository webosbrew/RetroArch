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
#include <OVR_Math.h>
#include <XrApp.h>

#include <Input/ControllerRenderer.h>
#include <Input/HandRenderer.h>
#include <Input/TinyUI.h>
#include <Render/SimpleBeamRenderer.h>

#include "XrHandHelper.h"

enum struct InputHandedness { Unknown, Left, Right };

class XrVirtualKeyboardApp : public OVRFW::XrApp {
   private:
    static constexpr std::string_view kSampleExplanation =
        "Overlay Keyboard is a feature that allows VR developers to       \n"
        "integrate a system keyboard directly into their applications     \n"
        "without having to create their own keyboard UI. The keyboard     \n"
        "appears as an overlay on top of the application, maintaining     \n"
        "the user's context and immersion while providing text input.     \n"
        "                                                                 \n"
        "This implementation offers a consistent and familiar typing      \n"
        "experience across all Meta Quest applications, with automatic    \n"
        "updates and improvements delivered through the OS without        \n"
        "requiring developer intervention.                                \n"
        "                                                                 \n"
        "Users can interact with the keyboard using hand tracking for     \n"
        "direct touch typing, controller rays for pointing and selecting, \n"
        "or even swipe typing for faster text entry with either method.     ";

    // Static pointer to the app instance for JNI access
    static XrVirtualKeyboardApp* instance_;

   public:
    XrVirtualKeyboardApp() {
        instance_ = this;
    }

    ~XrVirtualKeyboardApp() override {
        instance_ = nullptr;
    }

    // Static method to get the app instance
    static XrVirtualKeyboardApp* GetAppInstance() {
        return instance_;
    }

    // Method to update text input buffer from JNI
    void UpdateTextInputBufferFromJava(const std::string& text) {
        textInputBuffer_ = text;
        if (textInput_ != nullptr) {
            textInput_->SetText(textInputBuffer_.c_str());
        }
        if (eventLog_ != nullptr) {
            eventLog_->SetText("Text updated from Android");
        }
        ALOGV("Text input buffer updated: %s", text.c_str());
    }

    // Returns a list of OpenXr extensions needed for this app
    virtual std::vector<const char*> GetExtensions() override {
        std::vector<const char*> extensions = XrApp::GetExtensions();
        // Add hand extensions
        for (const auto& handExtension : XrHandHelper::RequiredExtensionNames()) {
            extensions.push_back(handExtension);
        }

        // Log all extensions
        ALOG("XrVirtualKeyboardApp requesting extensions:");
        for (const auto& e : extensions) {
            ALOG("   --> %s", e);
        }

        return extensions;
    }

    // Must return true if the application initializes successfully
    virtual bool AppInit(const xrJava* context) override {
        if (false == ui_.Init(context, GetFileSys(), false)) {
            ALOG("TinyUI::Init FAILED.");
            return false;
        }

        // Hand tracking
        handsExtensionAvailable_ = ExtensionsArePresent(XrHandHelper::RequiredExtensionNames());
        if (handsExtensionAvailable_) {
            handL_ = std::make_unique<XrHandHelper>(GetInstance(), true);
            OXR(handL_->GetLastError());
            handR_ = std::make_unique<XrHandHelper>(GetInstance(), false);
            OXR(handR_->GetLastError());
        }

        return true;
    }

    virtual void AppShutdown(const xrJava* context) override {
        handL_ = nullptr;
        handR_ = nullptr;

        uiInitialized_ = false;
        handsExtensionAvailable_ = false;

        ui_.Shutdown();
        OVRFW::XrApp::AppShutdown(context);
    }

    virtual bool SessionInit() override {
        // Use LocalSpace instead of Stage Space
        CurrentSpace = LocalSpace;

        // Init session bound objects
        if (false == controllerRenderL_.Init(true)) {
            ALOG("SessionInit::Init L controller renderer FAILED.");
            return false;
        }
        if (false == controllerRenderR_.Init(false)) {
            ALOG("SessionInit::Init R controller renderer FAILED.");
            return false;
        }
        beamRenderer_.Init(GetFileSys(), nullptr, OVR::Vector4f(1.0f), 1.0f);
        particleSystem_.Init(10, nullptr, OVRFW::ovrParticleSystem::GetDefaultGpuState(), false);

        if (handsExtensionAvailable_) {
            handL_->SessionInit(GetSession());
            handR_->SessionInit(GetSession());
            handRendererL_.Init(&handL_->Mesh(), handL_->IsLeft());
            handRendererR_.Init(&handR_->Mesh(), handR_->IsLeft());
        }

        return true;
    }

    virtual void SessionEnd() override {
        if (handsExtensionAvailable_) {
            handL_->SessionEnd();
            handR_->SessionEnd();
            handRendererL_.Shutdown();
            handRendererR_.Shutdown();
        }

        controllerRenderL_.Shutdown();
        controllerRenderR_.Shutdown();
        particleSystem_.Shutdown();
        beamRenderer_.Shutdown();
    }

    virtual void HandleXrEvents() override {
        XrEventDataBuffer eventDataBuffer = {};

        // Poll for events
        for (;;) {
            XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&eventDataBuffer);
            baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
            baseEventHeader->next = NULL;
            XrResult r;
            OXR(r = xrPollEvent(Instance, &eventDataBuffer));
            if (r != XR_SUCCESS) {
                break;
            }

            switch (baseEventHeader->type) {
                case XR_TYPE_EVENT_DATA_EVENTS_LOST:
                    ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_EVENTS_LOST event");
                    break;
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                    ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event");
                    break;
                case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                    ALOGV(
                        "xrPollEvent: received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event");
                    break;
                case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
                    const XrEventDataPerfSettingsEXT* perf_settings_event =
                        (XrEventDataPerfSettingsEXT*)(baseEventHeader);
                    ALOGV(
                        "xrPollEvent: received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type %d subdomain %d : level %d -> level %d",
                        perf_settings_event->type,
                        perf_settings_event->subDomain,
                        perf_settings_event->fromLevel,
                        perf_settings_event->toLevel);
                } break;
                case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                    ALOGV(
                        "xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event");
                    break;
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    const XrEventDataSessionStateChanged* session_state_changed_event =
                        (XrEventDataSessionStateChanged*)(baseEventHeader);
                    ALOGV(
                        "xrPollEvent: received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d for session %p at time %f",
                        session_state_changed_event->state,
                        (void*)session_state_changed_event->session,
                        FromXrTime(session_state_changed_event->time));

                    switch (session_state_changed_event->state) {
                        case XR_SESSION_STATE_FOCUSED: {
                            Focused = true;
                        } break;
                        case XR_SESSION_STATE_VISIBLE:
                            Focused = false;
                            break;
                        case XR_SESSION_STATE_READY:
                        case XR_SESSION_STATE_STOPPING:
                            HandleSessionStateChanges(session_state_changed_event->state);
                            break;
                        case XR_SESSION_STATE_EXITING:
                            ShouldExit = true;
                            break;
                        default:
                            break;
                    }
                } break;
                default:
                    ALOGV("xrPollEvent: Unknown event");
                    break;
            }
        }
    }

    virtual void Update(const OVRFW::ovrApplFrameIn& in) override {
        InitializeUI();

        XrSpace currentSpace = GetCurrentSpace();
        XrTime predictedDisplayTime = ToXrTime(in.PredictedDisplayTime);

        // hands
        if (handsExtensionAvailable_) {
            handL_->Update(currentSpace, predictedDisplayTime);
            handR_->Update(currentSpace, predictedDisplayTime);
        }

        UpdateUIHitTests(in);

        // Update controller poses
        leftAdjustedRemotePose_ = in.LeftRemotePose;
        rightAdjustedRemotePose_ = in.RightRemotePose;

        // Hands
        if (handsExtensionAvailable_) {
            if (handL_->AreLocationsActive()) {
                handRendererL_.Update(handL_->Joints(), handL_->RenderScale());
            }
            if (handR_->AreLocationsActive()) {
                handRendererR_.Update(handR_->Joints(), handR_->RenderScale());
            }
        }

        // Controllers
        if (in.LeftRemoteTracked) {
            controllerRenderL_.Update(leftAdjustedRemotePose_);
        }
        if (in.RightRemoteTracked) {
            controllerRenderR_.Update(rightAdjustedRemotePose_);
        }
    }

    virtual void Render(const OVRFW::ovrApplFrameIn& in, OVRFW::ovrRendererOutput& out) override {
        ui_.Render(in, out);

        if (handsExtensionAvailable_ && handL_->AreLocationsActive() && handL_->IsPositionValid()) {
            handRendererL_.Render(out.Surfaces);
        } else if (in.LeftRemoteTracked) {
            controllerRenderL_.Render(out.Surfaces);
        }

        if (handsExtensionAvailable_ && handR_->AreLocationsActive() && handR_->IsPositionValid()) {
            handRendererR_.Render(out.Surfaces);
        } else if (in.RightRemoteTracked) {
            controllerRenderR_.Render(out.Surfaces);
        }

        // Render beams last for proper blending
        particleSystem_.Frame(in, nullptr, out.FrameMatrices.CenterView);
        particleSystem_.RenderEyeView(
            out.FrameMatrices.CenterView, out.FrameMatrices.EyeProjection[0], out.Surfaces);
        beamRenderer_.Render(in, out);
    }

   private:
    enum class HitTestRayDeviceNums {
        LeftHand,
        LeftRemote,
        RightHand,
        RightRemote,
    };

#ifdef ANDROID
    void FocusTextView() {
        // Get the context from XrApp
        const xrJava* context = GetContext();
        if (context == nullptr) {
            ALOGE("Failed to get context");
            return;
        }

        // Get JNI environment
        JNIEnv* env = context->Env;
        if (env == nullptr) {
            ALOGE("Failed to get JNI environment from context");
            return;
        }

        // Get the activity object
        jobject activity = context->ActivityObject;
        if (activity == nullptr) {
            ALOGE("Failed to get activity object from context");
            return;
        }

        jclass activityClass = env->GetObjectClass(activity);
        if (activityClass == nullptr) {
            ALOGE("Failed to get activity class");
            return;
        }

        // Find the focusTextView method
        jmethodID focusMethod = env->GetMethodID(activityClass, "focusTextView", "()V");
        if (focusMethod == nullptr) {
            ALOGE("Failed to find focusTextView method");
            env->DeleteLocalRef(activityClass);
            return;
        }

        // Call the focusTextView method
        env->CallVoidMethod(activity, focusMethod);

        // Clean up local references
        env->DeleteLocalRef(activityClass);
    }

    void ClearFocusTextView() {
        // Get the context from XrApp
        const xrJava* context = GetContext();
        if (context == nullptr) {
            ALOGE("Failed to get context");
            return;
        }

        // Get JNI environment
        JNIEnv* env = context->Env;
        if (env == nullptr) {
            ALOGE("Failed to get JNI environment from context");
            return;
        }

        // Get the activity object
        jobject activity = context->ActivityObject;
        if (activity == nullptr) {
            ALOGE("Failed to get activity object from context");
            return;
        }

        jclass activityClass = env->GetObjectClass(activity);
        if (activityClass == nullptr) {
            ALOGE("Failed to get activity class");
            return;
        }

        // Find the clearFocusTextView method
        jmethodID clearFocusMethod = env->GetMethodID(activityClass, "clearFocusTextView", "()V");
        if (clearFocusMethod == nullptr) {
            ALOGE("Failed to find clearFocusTextView method");
            env->DeleteLocalRef(activityClass);
            return;
        }

        // Call the clearFocusTextView method
        env->CallVoidMethod(activity, clearFocusMethod);

        // Clean up local references
        env->DeleteLocalRef(activityClass);
    }

    void ShowKeyboard() {
        // Focus on the Android TextView to show the soft keyboard
        FocusTextView();
    }

    void HideKeyboard() {
        // Clear focus from the Android TextView to hide the soft keyboard
        ClearFocusTextView();
    }
#else
    void ShowKeyboard() {
        // Keyboard not available on non-Android platforms
        ALOG("Keyboard functionality not available on this platform");
        if (eventLog_ != nullptr) {
            eventLog_->SetText("Keyboard not available on this platform");
        }
    }

    void HideKeyboard() {
        // Keyboard not available on non-Android platforms
        ALOG("Keyboard functionality not available on this platform");
    }
#endif

    bool ExtensionsArePresent(const std::vector<const char*>& extensionList) const {
        const auto extensionProperties = GetXrExtensionProperties();
        bool foundAllExtensions = true;
        for (const auto& extension : extensionList) {
            bool foundExtension = false;
            for (const auto& extensionProperty : extensionProperties) {
                if (!strcmp(extension, extensionProperty.extensionName)) {
                    foundExtension = true;
                    break;
                }
            }
            if (!foundExtension) {
                foundAllExtensions = false;
                break;
            }
        }
        return foundAllExtensions;
    }

    void InitializeUI() {
        if (uiInitialized_) {
            return;
        }
        uiInitialized_ = true;

        keyboardHitTest_ = ui_.AddLabel("", {0.0f, 0.0f, 0.0f}, {100.0f, 100.0f});
        keyboardHitTest_->SetColor({0, 0, 0, 0});
        keyboardHitTest_->AddFlags(OVRFW::VRMENUOBJECT_FLAG_NO_DEPTH_MASK);

        eventLog_ = ui_.AddLabel("", {0.0f, 0.5f, -1.5f}, {600.0f, 50.0f});

        // Build UI
        CreateSampleDescriptionPanel();

        textInput_ = ui_.AddLabel("", {0.0f, 0.1f, -1.5f}, {600.0f, 320.0f});
        OVRFW::VRMenuFontParms fontParms = textInput_->GetFontParms();
        fontParms.AlignHoriz = OVRFW::HORIZONTAL_LEFT;
        fontParms.AlignVert = OVRFW::VERTICAL_BASELINE;
        fontParms.WrapWidth = 1.1f;
        fontParms.MaxLines = 10;
        textInput_->SetFontParms(fontParms);
        textInput_->SetTextLocalPosition({-0.55f, 0.25f, 0.0f});

        // Keyboard visibility controls
        showKeyboardButton_ = ui_.AddButton(
            "Show Keyboard", {-0.4f, 0.9f, -1.5f}, {200.0f, 50.0f}, [this]() { ShowKeyboard(); });
        hideKeyboardButton_ = ui_.AddButton(
            "Hide Keyboard", {-0.0f, 0.9f, -1.5f}, {200.0f, 50.0f}, [this]() { HideKeyboard(); });

        // Clear text
        clearTextButton_ =
            ui_.AddButton("Clear Text", {0.4f, 0.9f, -1.5f}, {200.0f, 50.0f}, [this]() {
                textInputBuffer_.clear();
                textInput_->SetText(textInputBuffer_.c_str());
                eventLog_->SetText("Text Cleared");
            });
    }

    void CreateSampleDescriptionPanel() {
        // Panel to provide sample description to the user for context
        auto descriptionLabel = ui_.AddLabel(
            static_cast<std::string>(kSampleExplanation), {1.5f, 0.355f, -1.0f}, {750.0f, 400.0f});

        // Align and size the description text for readability
        OVRFW::VRMenuFontParms fontParams{};
        fontParams.Scale = 0.5f;
        fontParams.AlignHoriz = OVRFW::HORIZONTAL_LEFT;
        descriptionLabel->SetFontParms(fontParams);
        descriptionLabel->SetTextLocalPosition({-0.65f, 0, 0});

        // Tilt the description billboard 45 degrees towards the user
        descriptionLabel->SetLocalRotation(
            OVR::Quat<float>::FromRotationVector({0, OVR::DegreeToRad(-30.0f), 0}));
    }

    void DetermineHandedness(const OVRFW::ovrApplFrameIn& in) {
        if ((handsExtensionAvailable_ && handL_->AreLocationsActive()) || in.LeftRemoteTracked) {
            if (currentHandedness_ == InputHandedness::Unknown ||
                (handsExtensionAvailable_ && handL_->IndexPinching()) ||
                in.LeftRemoteIndexTrigger > 0.25f) {
                currentHandedness_ = InputHandedness::Left;
            }
        } else if (currentHandedness_ == InputHandedness::Left) {
            currentHandedness_ = InputHandedness::Unknown;
        }
        if ((handsExtensionAvailable_ && handR_->AreLocationsActive()) || in.RightRemoteTracked) {
            if (currentHandedness_ == InputHandedness::Unknown ||
                (handsExtensionAvailable_ && handR_->IndexPinching()) ||
                in.RightRemoteIndexTrigger > 0.25f) {
                currentHandedness_ = InputHandedness::Right;
            }
        } else if (currentHandedness_ == InputHandedness::Right) {
            currentHandedness_ = InputHandedness::Unknown;
        }
    }

    OVRFW::ovrParticleSystem::handle_t AddParticle(
        const OVRFW::ovrApplFrameIn& in,
        const OVR::Vector3f& position) {
        return particleSystem_.AddParticle(
            in,
            position,
            0.0f,
            OVR::Vector3f(0.0f),
            OVR::Vector3f(0.0f),
            beamRenderer_.PointerParticleColor,
            OVRFW::ovrEaseFunc::NONE,
            0.0f,
            0.03f,
            0.1f,
            0);
    }

    void UpdateUIHitTests(const OVRFW::ovrApplFrameIn& in) {
        ui_.HitTestDevices().clear();
        particleSystem_.RemoveParticle(leftControllerPoint_);
        particleSystem_.RemoveParticle(rightControllerPoint_);

        // The controller actions are still triggered with hand tracking
        if (handsExtensionAvailable_ && handL_->IsPositionValid()) {
            UpdateRemoteTrackedUIHitTest(
                FromXrPosef(handL_->AimPose()),
                handL_->IndexPinching() ? 1.0f : 0.0f,
                handL_.get(),
                HitTestRayDeviceNums::LeftHand);
        } else if (in.LeftRemoteTracked) {
            UpdateRemoteTrackedUIHitTest(
                in.LeftRemotePointPose,
                in.LeftRemoteIndexTrigger,
                handL_.get(),
                HitTestRayDeviceNums::LeftRemote);
            leftControllerPoint_ = AddParticle(in, in.LeftRemotePointPose.Translation);
        }

        if (handsExtensionAvailable_ && handR_->IsPositionValid()) {
            UpdateRemoteTrackedUIHitTest(
                FromXrPosef(handR_->AimPose()),
                handR_->IndexPinching() ? 1.0f : 0.0f,
                handR_.get(),
                HitTestRayDeviceNums::RightHand);
        } else if (in.RightRemoteTracked) {
            UpdateRemoteTrackedUIHitTest(
                in.RightRemotePointPose,
                in.RightRemoteIndexTrigger,
                handR_.get(),
                HitTestRayDeviceNums::RightRemote);
            rightControllerPoint_ = AddParticle(in, in.RightRemotePointPose.Translation);
        }

        ui_.Update(in);
        beamRenderer_.Update(in, ui_.HitTestDevices());
    }

    void UpdateRemoteTrackedUIHitTest(
        const OVR::Posef& remotePose,
        float remoteIndexTrigger,
        XrHandHelper* hand,
        HitTestRayDeviceNums device) {
        const bool didPinch = remoteIndexTrigger > 0.25f;
        ui_.AddHitTestRay(remotePose, didPinch, static_cast<int>(device));
    }

   private:
    bool handsExtensionAvailable_ = false;
    bool uiInitialized_ = false;

    // hands - xr interface
    std::unique_ptr<XrHandHelper> handL_;
    std::unique_ptr<XrHandHelper> handR_;
    // hands/controllers - rendering
    OVRFW::HandRenderer handRendererL_;
    OVRFW::HandRenderer handRendererR_;
    OVRFW::ControllerRenderer controllerRenderL_;
    OVRFW::ControllerRenderer controllerRenderR_;

    // UI
    OVRFW::TinyUI ui_;
    OVRFW::SimpleBeamRenderer beamRenderer_;
    std::vector<OVRFW::ovrBeamRenderer::handle_t> beams_;
    OVRFW::ovrParticleSystem particleSystem_;
    OVRFW::ovrParticleSystem::handle_t leftControllerPoint_;
    OVRFW::ovrParticleSystem::handle_t rightControllerPoint_;

    OVRFW::VRMenuObject* textInput_ = nullptr;
    std::string textInputBuffer_;
    OVRFW::VRMenuObject* eventLog_ = nullptr;

    OVRFW::VRMenuObject* keyboardHitTest_ = nullptr;

    OVRFW::VRMenuObject* showKeyboardButton_ = nullptr;
    OVRFW::VRMenuObject* hideKeyboardButton_ = nullptr;

    OVRFW::VRMenuObject* clearTextButton_ = nullptr;

    InputHandedness currentHandedness_ = InputHandedness::Unknown;
    OVR::Posef leftAdjustedRemotePose_ = OVR::Posef::Identity();
    OVR::Posef rightAdjustedRemotePose_ = OVR::Posef::Identity();
};

// Static member definition
XrVirtualKeyboardApp* XrVirtualKeyboardApp::instance_ = nullptr;

#ifdef ANDROID
// JNI function to update textInputBuffer_ from Java
extern "C" JNIEXPORT void JNICALL
Java_com_oculus_sdk_xroverlaykeyboard_MainActivity_updateTextInputBuffer(
    JNIEnv* env,
    jobject /* this */,
    jstring text) {
    // Get the native string from Java string
    const char* nativeText = env->GetStringUTFChars(text, nullptr);
    if (nativeText == nullptr) {
        ALOGE("Failed to get native string from Java");
        return;
    }

    // Get the app instance and update the text input buffer
    XrVirtualKeyboardApp* app = XrVirtualKeyboardApp::GetAppInstance();
    if (app != nullptr) {
        std::string textStr(nativeText);
        app->UpdateTextInputBufferFromJava(textStr);
    } else {
        ALOGE("App instance is null, cannot update text input buffer");
    }

    // Release the native string
    env->ReleaseStringUTFChars(text, nativeText);
}
#endif

ENTRY_POINT(XrVirtualKeyboardApp)
