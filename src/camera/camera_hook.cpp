#include "pch.h"
#include "camera_hook.h"
#include "camera_math.h"
#include "ref_utils.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <reframework/API.hpp>
#include <cameraunlock/math/smoothing_utils.h>
#include <unordered_set>
#include <string>

namespace RE2HT {

constexpr int TX_WORLDMATRIX_OFFSET = 0x80;

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

static struct {
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Method* getCameraFov = nullptr;
    reframework::API::Method* getCameraProjectionMatrix = nullptr;
    void* sceneManager = nullptr;
    bool initialized = false;
    bool failed = false;
} g_fn;

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

// Per-frame transform cache
static struct {
    void* ptr = nullptr;
    ULONGLONG timeMs = 0;
} g_txCache;

// SceneManager -> MainView -> PrimaryCamera. The native singleton pointer is
// stable for the process, so it is lazily cached. Returns null on any missing
// method, missing singleton, thrown managed exception, or null link.
static reframework::API::ManagedObject* ResolvePrimaryCamera() {
    if (!g_fn.getMainView || !g_fn.getPrimaryCamera) return nullptr;

    if (!g_fn.sceneManager) {
        g_fn.sceneManager = reframework::API::get()->get_native_singleton("via.SceneManager");
        if (!g_fn.sceneManager) return nullptr;
    }

    auto mv = g_fn.getMainView->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(g_fn.sceneManager), EmptyArgs());
    if (mv.exception_thrown || !mv.ptr) return nullptr;

    auto cam = g_fn.getPrimaryCamera->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(mv.ptr), EmptyArgs());
    if (cam.exception_thrown || !cam.ptr) return nullptr;

    return reinterpret_cast<reframework::API::ManagedObject*>(cam.ptr);
}

static void* GetCameraTransform() {
    ULONGLONG now = GetTickCount64();
    if (g_txCache.ptr && now == g_txCache.timeMs) {
        return g_txCache.ptr;
    }

    auto cam = ResolvePrimaryCamera();
    if (!cam) return nullptr;
    auto go = InvokePtr(g_fn.getGameObject, cam);
    if (!go) return nullptr;
    void* tx = InvokePtr(g_fn.getTransform, go);

    g_txCache.ptr = tx;
    g_txCache.timeMs = now;
    return tx;
}

// Memoize the resolved primary camera for the duration of one render frame.
// GetLivePrimaryCameraFov and GetFocalLengthsFromProjectionMatrix both run only
// during rendering (after g_renderFrame is bumped) and each would otherwise
// re-run ResolvePrimaryCamera's two managed getters. The primary camera is
// fixed for a rendered frame, so resolving once and reusing is exact. Not used
// by GetCameraTransform, which must resolve fresh because it is also driven from
// the camera-update hooks outside the render frame.
static reframework::API::ManagedObject* ResolvePrimaryCameraForRenderFrame() {
    static reframework::API::ManagedObject* s_cam = nullptr;
    static uint64_t s_frame = UINT64_MAX;
    if (s_cam && s_frame == g_renderFrame) return s_cam;
    auto cam = ResolvePrimaryCamera();
    if (cam) {
        s_cam = cam;
        s_frame = g_renderFrame;
    }
    return cam;
}

static float GetLivePrimaryCameraFov() {
    if (!g_fn.getCameraFov) return 0.f;

    auto cam = ResolvePrimaryCameraForRenderFrame();
    if (!cam) return 0.f;

    auto fov = g_fn.getCameraFov->invoke(cam, EmptyArgs());
    if (fov.exception_thrown) return 0.f;

    float fovDeg = 0.f;
    if (fov.f >= 10.f && fov.f <= 170.f) fovDeg = fov.f;
    else { float fromD = static_cast<float>(fov.d); if (fromD >= 10.f && fromD <= 170.f) fovDeg = fromD; }
    return fovDeg;
}

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
    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    void* transform = nullptr;
    __try { transform = GetCameraTransform(); } __except(EXCEPTION_EXECUTE_HANDLER) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = nullptr;
    __try { transform = GetCameraTransform(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
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

// --- GUI compensation ---

// Read focal lengths directly from the camera's projection matrix.
// P[0][0] = 1/tan(hFovX/2), P[1][1] = 1/tan(hFovY/2) in NDC.
// Multiply by half-canvas to get pixel focal lengths.
// No guessing about horizontal vs vertical FOV convention.
static bool GetFocalLengthsFromProjectionMatrix(float& fx, float& fy) {
    if (!g_fn.getCameraProjectionMatrix) return false;

    auto cam = ResolvePrimaryCameraForRenderFrame();
    if (!cam) return false;

    // get_ProjectionMatrix is a property getter — no arguments, returns Matrix4x4
    auto ret = g_fn.getCameraProjectionMatrix->invoke(cam, EmptyArgs());
    if (ret.exception_thrown) return false;

    // Matrix4x4 (64 bytes) returned in ret.bytes — row-major [row][col]
    auto* retMat = reinterpret_cast<const float*>(ret.bytes.data());
    float p00 = retMat[0];   // m[0][0]
    float p11 = retMat[5];   // m[1][1]

    if (p00 < 0.1f || p00 > 20.f || p11 < 0.1f || p11 > 20.f) return false;

    fx = p00 * kHalfCanvasW;
    fy = p11 * kHalfCanvasH;

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        Logger::Instance().Info("Projection matrix focal lengths: P00=%.4f P11=%.4f fx=%.1f fy=%.1f", p00, p11, fx, fy);
    }
    return true;
}

static bool GetMarkerProjectionFocalLengths(float& fx, float& fy) {
    // Prefer projection matrix — exact, no FOV convention guessing
    if (GetFocalLengthsFromProjectionMatrix(fx, fy)) return true;

    // Fallback to get_FOV (assume vertical for safety)
    constexpr float kAspect = kHalfCanvasW / kHalfCanvasH;
    float fov = GetLivePrimaryCameraFov();
    if (fov < 10.f || fov > 170.f) return false;
    float tanHFovY = tanf(fov * kDegToRad * 0.5f);
    fx = kHalfCanvasW / (tanHFovY * kAspect);
    fy = kHalfCanvasH / tanHFovY;
    return true;
}

// Per-frame memo over GetMarkerProjectionFocalLengths. Multiple GUI elements
// (reticle, bullet count, world markers) are drawn each frame and would each
// otherwise re-resolve the camera and read its projection matrix to obtain
// identical focal lengths. The first call in a frame computes; the rest reuse.
static bool GetMarkerFocalLengthsCached(float& fx, float& fy) {
    if (g_crosshair.focalFrame != g_renderFrame) {
        g_crosshair.focalFrame = g_renderFrame;
        g_crosshair.focalValid =
            GetMarkerProjectionFocalLengths(g_crosshair.markerFx, g_crosshair.markerFy);
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

// Read a managed string into a char buffer. Separated from the main callback
// to keep SEH (__try) out of functions with C++ destructors.
static bool ReadGuiElementName(void* guiMo, char* out, size_t outSize) {
    out[0] = 0;
    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(guiMo);

    auto goRet = g_guiCam.elemGetGameObject
        ? g_guiCam.elemGetGameObject->invoke(mo, EmptyArgs())
        : mo->invoke("get_GameObject", EmptyArgs());
    if (goRet.exception_thrown || !goRet.ptr) return false;

    auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
    auto nameRet = g_guiCam.getGameObjectName
        ? g_guiCam.getGameObjectName->invoke(goMo, EmptyArgs())
        : goMo->invoke("get_Name", EmptyArgs());
    if (nameRet.exception_thrown || !nameRet.ptr) return false;

    // System.String layout: int32 length at +0x10, UTF-16 char data at +0x14.
    constexpr size_t kStringLengthOffset = 0x10;
    constexpr size_t kStringCharsOffset = 0x14;
    __try {
        auto* raw = reinterpret_cast<uint8_t*>(nameRet.ptr);
        uint32_t strLen = *reinterpret_cast<uint32_t*>(raw + kStringLengthOffset);
        if (strLen > outSize - 1) strLen = static_cast<uint32_t>(outSize - 1);
        auto* chars = reinterpret_cast<uint16_t*>(raw + kStringCharsOffset);
        for (uint32_t i = 0; i < strLen; i++) out[i] = (char)chars[i];
        out[strLen] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }

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
        ? g_guiCam.elemGetView->invoke(mo, EmptyArgs())
        : mo->invoke("get_View", EmptyArgs());
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
    if (g_fn.initialized) return !g_fn.failed;
    g_fn.initialized = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();
    auto smType = tdb->find_type("via.SceneManager");
    auto svType = tdb->find_type("via.SceneView");
    auto camType = tdb->find_type("via.Camera");
    auto goType = tdb->find_type("via.GameObject");

    if (!smType || !svType || !camType || !goType) { g_fn.failed = true; return false; }

    g_fn.getMainView = smType->find_method("get_MainView");
    g_fn.getPrimaryCamera = svType->find_method("get_PrimaryCamera");
    g_fn.getGameObject = camType->find_method("get_GameObject");
    g_fn.getTransform = goType->find_method("get_Transform");
    g_fn.getCameraFov = camType->find_method("get_FOV");
    g_fn.getCameraProjectionMatrix = camType->find_method("get_ProjectionMatrix");
    if (!g_fn.getCameraProjectionMatrix)
        Logger::Instance().Warning("via.Camera.get_ProjectionMatrix not found — will fall back to get_FOV");

    if (!g_fn.getMainView || !g_fn.getPrimaryCamera || !g_fn.getGameObject || !g_fn.getTransform) {
        g_fn.failed = true;
        return false;
    }

    if (!g_fn.getCameraFov) {
        Logger::Instance().Warning("via.Camera.get_FOV not found — crosshair projection will use fallback FOV");
    }

    // Hook camera controller update for save/restore
    struct CameraHookCandidate {
        const char* typeName;
        const char* methodName;
    };

    CameraHookCandidate candidates[] = {
        {"app.ropeway.camera.CameraSystem", "update"},
        {"app.ropeway.camera.CameraSystem", "lateUpdate"},
        {"app.ropeway.camera.CameraSystem", "onCameraUpdate"},
        {"app.ropeway.PlayerCameraController", "update"},
        {"app.ropeway.PlayerCameraController", "lateUpdate"},
        {"app.ropeway.PlayerCameraController", "onCameraUpdate"},
    };

    for (const auto& candidate : candidates) {
        auto pccType = tdb->find_type(candidate.typeName);
        if (pccType) {
            auto method = pccType->find_method(candidate.methodName);
            if (method) {
                auto id = method->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
                Logger::Instance().Info("Hooked %s.%s (id=%u)", candidate.typeName, candidate.methodName, id);
                break;
            }
        }
    }

    Logger::Instance().Info("Methods cached");
    return true;
}

// Project the clean aim direction into the head-tracked view to derive the
// screen-space tangents (and live FOV/roll) the GUI compensation reads. The
// smoothed state persists across frames to suppress perspective-division and
// per-frame FOV jitter.
static void UpdateCrosshairProjection(const Matrix4x4f& clean, const Matrix4x4f& head) {
    constexpr float kAimDist = 50.0f;
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (!ProjectAimToViewTangents(clean, head, kAimDist, rawTanRight, rawTanUp)) {
        g_crosshair.valid = false;
        return;
    }

    float liveFov = GetLivePrimaryCameraFov();
    float rawFov = (liveFov > 10.f) ? liveFov : g_crosshair.fovDegrees;

    float dt = Mod::Instance().GetLastDeltaTime();
    constexpr float kCrosshairSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
    float t = cameraunlock::math::CalculateSmoothingFactor(kCrosshairSmoothing, dt);

    static float s_smoothedTanRight = 0.f;
    static float s_smoothedTanUp = 0.f;
    static float s_smoothedFov = 75.f;
    static bool s_initialized = false;

    if (!s_initialized) {
        s_smoothedTanRight = rawTanRight;
        s_smoothedTanUp = rawTanUp;
        s_smoothedFov = rawFov;
        s_initialized = true;
    } else {
        s_smoothedTanRight = cameraunlock::math::Lerp(s_smoothedTanRight, rawTanRight, t);
        s_smoothedTanUp = cameraunlock::math::Lerp(s_smoothedTanUp, rawTanUp, t);
        s_smoothedFov = cameraunlock::math::Lerp(s_smoothedFov, rawFov, t);
    }

    g_crosshair.tanRight = s_smoothedTanRight;
    g_crosshair.tanUp = s_smoothedTanUp;
    g_crosshair.fovDegrees = s_smoothedFov;
    g_crosshair.valid = g_crosshair.fovDegrees > 10.f;
}

// --- Pre-BeginRendering: apply head tracking for rendering ---
void OnPreBeginRendering() {
    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    ++g_renderFrame;
    if (ShouldRecenter()) {
        Mod::Instance().Recenter();
    }

    void* transform = nullptr;
    __try { transform = GetCameraTransform(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);

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

    void* transform = nullptr;
    __try { transform = GetCameraTransform(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
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
}

} // namespace RE2HT
