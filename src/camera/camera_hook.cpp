#include "pch.h"
#include "camera_hook.h"
#include "math_types.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <reframework/API.hpp>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>
#include <unordered_set>
#include <string>

namespace RE2HT {

namespace ref = cameraunlock::reframework;

// Cap on unique GUI element names recorded during discovery logging.
constexpr size_t kMaxLoggedGuiNames = 100;

// Half of the 1920x1080 reference canvas. Multiplying NDC focal lengths by
// these yields pixel focal lengths for GUI compensation.
constexpr float kHalfCanvasW = 960.f;
constexpr float kHalfCanvasH = 540.f;

// Crosshair projection state shared between OnPreBeginRendering (writer) and
// OnPreGuiDrawElement (reader).
struct CrosshairProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    float fovDegrees = 75.0f;
    bool valid = false;

    // Per-frame memoized marker focal lengths. The camera's projection is fixed
    // for a rendered frame, so every GUI element drawn in that frame shares the
    // same focal lengths; computing them once avoids redundant managed invokes
    // scaling with the number of compensated GUI elements.
    float markerFx = 0.0f;
    float markerFy = 0.0f;
    bool focalValid = false;
    uint64_t focalFrame = 0;
};

static CrosshairProjection g_crosshair;

// Incremented once per processed render frame in OnPreBeginRendering; used to
// invalidate the per-frame focal-length memo above.
static uint64_t g_renderFrame = 0;

// Saved game rotation — what the game INTENDED before we modified it
static struct {
    Matrix4x4f gameMatrix;     // The game's clean matrix (saved after game updates)
    bool hasGameMatrix = false;
} g_saved;

// Clean camera matrix saved at the start of each rendering frame
static struct {
    Matrix4x4f matrix;
    bool valid = false;
} g_cleanCameraMatrix;

static bool g_trackingAppliedThisFrame = false;

static ref::CameraTransformResolver g_cameraResolver;

// via.Camera.get_ProjectionMatrix — not part of the standard chain, resolved
// separately for exact focal-length reads.
static reframework::API::Method* g_getProjectionMatrix = nullptr;

// Per-frame camera/transform cache. Invalidated together at the
// camera-controller update pre-hook and at the end of OnPostBeginRendering,
// so within a single render frame they hold the live primary camera and its
// transform without re-walking the SceneManager chain.
static void* g_cachedTransform = nullptr;
static void* g_cachedCamera = nullptr;

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = g_cameraResolver.ResolveTransform(&g_cachedCamera);
    return g_cachedTransform;
}

// GUI compensation method cache
static struct {
    reframework::API::Method* guiFindObjectsByType = nullptr;
    reframework::API::Method* transformSetPosition = nullptr;
    reframework::API::Method* transformGetGlobalPosition = nullptr;
    reframework::API::TypeDefinition* playObjectRuntimeType = nullptr;
    bool resolved = false;
    bool giveUp = false;

    // Instance getters invoked per GUI element per frame. Resolving the
    // API::Method* once and calling method->invoke(obj, ...) directly avoids the
    // per-call get_type_definition() + find_method(name) string search that
    // ManagedObject::invoke("name", ...) performs on every call. via.gui
    // elements share these on their common base type, so a Method* resolved from
    // one element dispatches correctly on every element.
    reframework::API::Method* getGameObjectName = nullptr;  // via.GameObject.get_Name
    reframework::API::Method* elemGetGameObject = nullptr;  // <element>.get_GameObject
    reframework::API::Method* elemGetView = nullptr;        // <element>.get_View
    bool elemMethodsResolved = false;
} g_guiCam;

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    // Save the game's rotation axes BEFORE applying head rotation.
    Matrix4x4f preRotationAxes = *worldMat;

    float yr = -yaw * kDegToRad;
    float pr = pitch * kDegToRad;
    float rr = roll * kDegToRad;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        ApplyWorldSpaceHeadRotation(*worldMat, yr, pr, rr);
    } else {
        ApplyCameraLocalHeadRotation(*worldMat, yr, pr, rr);
    }

    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        ApplyViewSpacePositionOffset(*worldMat, preRotationAxes, px, py, pz);
    }
}

// --- Hook on camera controller update ---
static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    void* transform = GetCameraTransformCached();
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        g_saved.gameMatrix = *worldMat;
        g_saved.hasGameMatrix = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        REQuat q = MatrixToQuat(g_saved.gameMatrix);
        Logger::Instance().Info("Hook save/restore active: gameQ=%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        s_loggedOnce = true;
    }
}

// --- Camera controller discovery ---

// RE2's game code lives under app.ropeway.* (the game's internal codename).
// Fast-path candidates; the hooker's parent-chain walk discovers the real
// controller dynamically and logs the component tree if none of these match.
static const char* const kControllerTypeCandidates[] = {
    "app.ropeway.camera.CameraSystem",
    "app.ropeway.PlayerCameraController",
};

static ref::CameraControllerHooker g_controllerHooker{
    kControllerTypeCandidates,
    static_cast<int>(std::size(kControllerTypeCandidates)),
    CameraUpdatePreHook,
    CameraUpdatePostHook};

// Run camera-controller discovery once we are in gameplay, retrying each
// frame until it succeeds. Deferring past the menu avoids latching onto a
// render effect controller before the gameplay camera rig exists.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.TryHook(GetCameraTransformCached())) return;

    int attempts = g_controllerHooker.AttemptCount();
    if (attempts == 1 || (attempts % 300) == 0) {
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) - head tracking "
            "still active via the BeginRendering restore path", attempts);
    }
}

// --- GUI compensation ---

// Pixel focal lengths for GUI marker compensation, preferring the camera's
// projection matrix (exact, no FOV convention guessing) with a get_FOV
// fallback.
static bool ComputeMarkerFocalLengths(float& fx, float& fy) {
    void* cam = g_cachedCamera ? g_cachedCamera : g_cameraResolver.ResolveCamera();
    if (!cam) return false;

    if (g_getProjectionMatrix) {
        // get_ProjectionMatrix is a property getter — no arguments, returns Matrix4x4
        auto ret = g_getProjectionMatrix->invoke(
            reinterpret_cast<reframework::API::ManagedObject*>(cam), ref::EmptyArgs());
        if (!ret.exception_thrown) {
            // Matrix4x4 (64 bytes) returned in ret.bytes — row-major [row][col]
            auto* retMat = reinterpret_cast<const float*>(ret.bytes.data());
            if (cameraunlock::rendering::FocalLengthsFromProjection(
                    retMat[0], retMat[5], kHalfCanvasW, kHalfCanvasH, fx, fy)) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    Logger::Instance().Info("Projection matrix focal lengths: P00=%.4f P11=%.4f fx=%.1f fy=%.1f",
                                            retMat[0], retMat[5], fx, fy);
                }
                return true;
            }
        }
    }

    // Fallback to get_FOV (assume vertical for safety)
    float fov = g_cameraResolver.ResolveFovDegrees(cam);
    return cameraunlock::rendering::FocalLengthsFromVerticalFov(fov, kHalfCanvasW, kHalfCanvasH, fx, fy);
}

// Per-frame memo over ComputeMarkerFocalLengths. Multiple GUI elements
// (reticle, bullet count, world markers) are drawn each frame and would each
// otherwise re-resolve the camera and read its projection matrix to obtain
// identical focal lengths. The first call in a frame computes; the rest reuse.
static bool GetMarkerFocalLengthsCached(float& fx, float& fy) {
    if (g_crosshair.focalFrame != g_renderFrame) {
        g_crosshair.focalFrame = g_renderFrame;
        g_crosshair.focalValid =
            ComputeMarkerFocalLengths(g_crosshair.markerFx, g_crosshair.markerFy);
    }
    if (!g_crosshair.focalValid) return false;
    fx = g_crosshair.markerFx;
    fy = g_crosshair.markerFy;
    return true;
}

static void InitGUICompensationMethods() {
    if (g_guiCam.resolved || g_guiCam.giveUp) return;
    g_guiCam.resolved = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();

    auto guiType = tdb->find_type("via.gui.GUI");
    auto transformType = tdb->find_type("via.gui.TransformObject");
    auto playObjType = tdb->find_type("via.gui.PlayObject");

    if (!guiType || !transformType || !playObjType) {
        Logger::Instance().Warning("GUI compensation: missing via.gui types");
        g_guiCam.giveUp = true;
        return;
    }

    g_guiCam.guiFindObjectsByType = guiType->find_method("findObjects(System.Type)");
    g_guiCam.transformSetPosition = transformType->find_method("set_Position");
    g_guiCam.transformGetGlobalPosition = transformType->find_method("get_GlobalPosition");

    auto goType = tdb->find_type("via.GameObject");
    if (goType) g_guiCam.getGameObjectName = goType->find_method("get_Name");

    auto runtimeType = playObjType->get_runtime_type();
    if (runtimeType) {
        g_guiCam.playObjectRuntimeType = reinterpret_cast<reframework::API::TypeDefinition*>(runtimeType);
    }

    bool ready = g_guiCam.guiFindObjectsByType && g_guiCam.transformSetPosition && g_guiCam.playObjectRuntimeType;
    if (!ready) {
        Logger::Instance().Warning("GUI compensation: some methods not found (findObjects=%p setPos=%p playObjRT=%p)",
            g_guiCam.guiFindObjectsByType, g_guiCam.transformSetPosition, g_guiCam.playObjectRuntimeType);
        g_guiCam.giveUp = true;
        return;
    }

    Logger::Instance().Info("GUI compensation methods resolved");
}

// --- OnPreGuiDrawElement callback ---
// Logs unique GUI element names for discovery, then applies marker/crosshair compensation.

// Resolve the per-element instance getters (get_GameObject, get_View) once from
// the first element's type. find_method walks base types, so the descriptors
// are the shared via.gui base methods and dispatch correctly on every element.
static void ResolveGuiElementMethods(reframework::API::ManagedObject* element) {
    if (g_guiCam.elemMethodsResolved) return;
    auto td = element->get_type_definition();
    if (!td) return;
    g_guiCam.elemGetGameObject = td->find_method("get_GameObject");
    g_guiCam.elemGetView = td->find_method("get_View");
    if (g_guiCam.elemGetGameObject && g_guiCam.elemGetView) {
        g_guiCam.elemMethodsResolved = true;
    }
}

// Resolve a GUI element's GameObject name into a char buffer.
static bool ReadGuiElementName(void* guiMo, char* out, size_t outSize) {
    out[0] = 0;
    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(guiMo);

    auto goRet = g_guiCam.elemGetGameObject
        ? g_guiCam.elemGetGameObject->invoke(mo, ref::EmptyArgs())
        : mo->invoke("get_GameObject", ref::EmptyArgs());
    if (goRet.exception_thrown || !goRet.ptr) return false;

    auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
    auto nameRet = g_guiCam.getGameObjectName
        ? g_guiCam.getGameObjectName->invoke(goMo, ref::EmptyArgs())
        : goMo->invoke("get_Name", ref::EmptyArgs());
    if (nameRet.exception_thrown || !nameRet.ptr) return false;

    ref::ReadManagedString(nameRet.ptr, out, outSize);
    return out[0] != 0;
}

// Shift a GUI element's View to the head-tracked screen position of the clean
// aim point, so world-anchored markers and the HUD/crosshair stay locked to
// where the game is actually aiming while the head turns the view.
static void ApplyGuiViewCenterOffset(reframework::API::ManagedObject* mo) {
    float fx = 0.f, fy = 0.f;
    if (!GetMarkerFocalLengthsCached(fx, fy)) return;

    float deltaX = -g_crosshair.tanRight * fx;
    float deltaY =  g_crosshair.tanUp * fy;

    auto viewRet = g_guiCam.elemGetView
        ? g_guiCam.elemGetView->invoke(mo, ref::EmptyArgs())
        : mo->invoke("get_View", ref::EmptyArgs());
    if (viewRet.exception_thrown || !viewRet.ptr) return;

    auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);
    float pos[3] = { deltaX, deltaY, 0.f };
    std::vector<void*> setArgs = { (void*)&pos[0] };
    g_guiCam.transformSetPosition->invoke(view, setArgs);

    static bool s_diagOnce = false;
    if (!s_diagOnce) {
        s_diagOnce = true;
        Logger::Instance().Info("GUI center offset applied: fov=%.1f tanR=%.4f tanU=%.4f dX=%.1f dY=%.1f",
            g_crosshair.fovDegrees, g_crosshair.tanRight, g_crosshair.tanUp, deltaX, deltaY);
    }
}

// RE2's GUI tree is simpler than requiem's: both world-anchored markers and the
// crosshair/HUD are compensated by the same center-delta on their View.
static bool IsCompensatedGuiElement(const char* goName) {
    return strcmp(goName, "GUI_FloatIcon") == 0
        || strcmp(goName, "GUI_Purpose") == 0
        || strcmp(goName, "GUI_Reticle") == 0
        || strcmp(goName, "GUI_RemainingBullet") == 0;
}

bool OnPreGuiDrawElement(void* element, void* context) {
    if (!Mod::Instance().IsEnabled()) return true;

    // In this REFramework SDK version, 'element' is the GUI ManagedObject directly.
    if (!element) return true;

    auto* mo = reinterpret_cast<reframework::API::ManagedObject*>(element);

    // Side-effect-free: resolves the per-element instance getters so the name
    // read below takes the cached Method* path instead of re-searching the
    // method table by string. Does no logging, so it does not perturb the
    // discovery/init log ordering.
    ResolveGuiElementMethods(mo);

    char goName[128] = {};
    if (!ReadGuiElementName(mo, goName, sizeof(goName))) return true;

    // Log unique GUI element names for discovery
    static std::unordered_set<std::string> s_loggedNames;
    if (s_loggedNames.size() < kMaxLoggedGuiNames && s_loggedNames.insert(std::string(goName)).second) {
        Logger::Instance().Info("GUI element: \"%s\"", goName);
    }

    // Initialize GUI compensation methods on first element
    if (!g_guiCam.resolved && !g_guiCam.giveUp) {
        InitGUICompensationMethods();
    }

    if (g_crosshair.valid && g_guiCam.transformSetPosition && IsCompensatedGuiElement(goName)) {
        if (!IsInGameplay()) return true;
        ApplyGuiViewCenterOffset(mo);
    }

    return true;
}

// --- Initialization ---

static bool InitCachedFunctions() {
    static bool s_attempted = false;
    if (s_attempted) return !g_cameraResolver.HasFailed();
    s_attempted = true;

    if (!g_cameraResolver.Initialize()) return false;

    auto tdb = reframework::API::get()->tdb();
    auto camType = tdb->find_type("via.Camera");
    g_getProjectionMatrix = camType ? camType->find_method("get_ProjectionMatrix") : nullptr;
    if (!g_getProjectionMatrix) {
        Logger::Instance().Warning("via.Camera.get_ProjectionMatrix not found - will fall back to get_FOV");
    }

    // Camera controller discovery is deferred to gameplay (see
    // OnPreBeginRendering). At the main menu the primary camera GameObject
    // typically carries only render/effect controllers; the real player camera
    // controller component exists once gameplay starts.

    Logger::Instance().Info("Methods cached");
    return true;
}

// Project the clean aim direction into the head-tracked view to derive the
// screen-space tangents (and live FOV) the GUI compensation reads. The
// smoothed state persists across frames to suppress perspective-division and
// per-frame FOV jitter.
static void UpdateCrosshairProjection(const Matrix4x4f& clean, const Matrix4x4f& head) {
    constexpr float kAimDist = 50.0f;
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (!ProjectAimToViewTangents(clean, head, kAimDist, rawTanRight, rawTanUp)) {
        g_crosshair.valid = false;
        return;
    }

    float rawFov = g_cameraResolver.ResolveFovDegrees(g_cachedCamera);
    if (rawFov <= 0.f) rawFov = g_crosshair.fovDegrees;

    float dt = Mod::Instance().GetLastDeltaTime();
    constexpr float kCrosshairSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);

    static cameraunlock::math::SmoothedFloat s_tanRight;
    static cameraunlock::math::SmoothedFloat s_tanUp;
    static cameraunlock::math::SmoothedFloat s_fov;

    g_crosshair.tanRight = s_tanRight.Update(rawTanRight, kCrosshairSmoothing, dt);
    g_crosshair.tanUp = s_tanUp.Update(rawTanUp, kCrosshairSmoothing, dt);
    g_crosshair.fovDegrees = s_fov.Update(rawFov, kCrosshairSmoothing, dt);
    g_crosshair.valid = g_crosshair.fovDegrees > 10.f;
}

// --- Pre-BeginRendering: apply head tracking for rendering ---
void OnPreBeginRendering() {
    // Drain hotkey requests on the render thread so recenter / mode-cycle
    // never mutate session state concurrently with the pipeline tick below.
    Mod::Instance().ProcessDeferredActions();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    EnsureCameraControllerHooked();
    ++g_renderFrame;
    if (ShouldRecenter()) {
        Mod::Instance().Recenter();
    }

    // Advance interpolation + smoothing once per render frame. Every
    // downstream consumer (ApplyHeadTracking, crosshair projection, GUI
    // compensation) reads cached values from this tick.
    Mod::Instance().TickFrame();

    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);

    // Save the clean matrix before applying head tracking
    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    UpdateCrosshairProjection(g_cleanCameraMatrix.matrix, *worldMat);
}

// Post-BeginRendering: restore clean ROTATION so aim direction follows the
// mouse, but keep head-tracked POSITION so the aim origin matches the lean.
// The GUI camera captures position during rendering, shifting the reticle.
// If we also restore clean position, bullets fire from a different origin
// than what the reticle shows → shots miss where the reticle points.
void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    // OnPreBeginRendering populated the per-frame transform cache this frame
    // (g_trackingAppliedThisFrame is only set after that succeeded), so reuse
    // it rather than re-walking the SceneManager chain.
    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        // Save head-tracked position before restoring
        float hx = worldMat->m[3][0];
        float hy = worldMat->m[3][1];
        float hz = worldMat->m[3][2];

        // Restore clean rotation (3x3) + clean row 3 w component
        *worldMat = g_cleanCameraMatrix.matrix;

        // Re-apply head-tracked position so aim origin matches lean
        worldMat->m[3][0] = hx;
        worldMat->m[3][1] = hy;
        worldMat->m[3][2] = hz;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

} // namespace RE2HT
