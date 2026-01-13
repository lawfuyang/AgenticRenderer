#include "pch.h"
#include "Camera.h"
#include "Renderer.h"
#include <cmath>

Camera::Camera()
{
}

void Camera::ProcessEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        bool down = (event.type == SDL_EVENT_KEY_DOWN);
        // SDL3: scancode is available directly on SDL_KeyboardEvent as 'scancode'
        int sc = event.key.scancode;
        // W S A D
        if (sc == SDL_SCANCODE_W) m_Forward = down;
        if (sc == SDL_SCANCODE_S) m_Back = down;
        if (sc == SDL_SCANCODE_A) m_Left = down;
        if (sc == SDL_SCANCODE_D) m_Right = down;
    }
    break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
            if (event.button.button == SDL_BUTTON_RIGHT)
            {
                m_Rotating = true;
                m_LastMouseX = event.button.x;
                m_LastMouseY = event.button.y;
            }
    }
    break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
            if (event.button.button == SDL_BUTTON_RIGHT)
            {
                m_Rotating = false;
            }
    }
    break;
    case SDL_EVENT_MOUSE_MOTION:
    {
        if (m_Rotating)
        {
            int dx = event.motion.x - m_LastMouseX;
            int dy = event.motion.y - m_LastMouseY;
            // When relative mouse is set, SDL provides motion deltas directly
            if (event.motion.which == SDL_TOUCH_MOUSEID || event.motion.which == 0)
            {
                // Use event.motion.xrel/xrel if available
                dx = event.motion.xrel;
                dy = event.motion.yrel;
            }
            m_LastMouseX = event.motion.x;
            m_LastMouseY = event.motion.y;

            m_Yaw += dx * m_MouseSensitivity;
            m_Pitch += dy * m_MouseSensitivity;  // Positive dy (mouse down) should increase pitch (look down)
            // Clamp pitch
            if (m_Pitch > DirectX::XM_PIDIV2 - 0.01f) m_Pitch = DirectX::XM_PIDIV2 - 0.01f;
            if (m_Pitch < -DirectX::XM_PIDIV2 + 0.01f) m_Pitch = -DirectX::XM_PIDIV2 + 0.01f;
            m_Dirty = true;
        }
    }
    break;
    case SDL_EVENT_WINDOW_RESIZED:
    {
        int w = event.window.data1;
        int h = event.window.data2;
        if (h > 0) m_Aspect = float(w) / float(h);
        m_Dirty = true;
    }
    break;
    default:
        break;
    }
}

void Camera::Update()
{
    // Retrieve frame time from renderer (ms -> seconds)
    Renderer* renderer = Renderer::GetInstance();
    float dt = static_cast<float>(renderer->GetFrameTimeMs() * 0.001);
    if (dt <= 0.0f)
        return;

    // Movement in local space
    Vector forward = DirectX::XMVectorSet(0, 0, 1, 0);
    Vector right = DirectX::XMVectorSet(1, 0, 0, 0);

    // Construct rotation from yaw/pitch
    Vector rot = DirectX::XMQuaternionRotationRollPitchYaw(m_Pitch, m_Yaw, 0.0f);
    Vector fw = DirectX::XMVector3Rotate(forward, rot);
    Vector rt = DirectX::XMVector3Rotate(right, rot);

    Vector pos = DirectX::XMLoadFloat3(reinterpret_cast<const Vector3*>(&m_Position));
    Vector move = DirectX::XMVectorZero();
    if (m_Forward) move = DirectX::XMVectorAdd(move, fw);
    if (m_Back) move = DirectX::XMVectorSubtract(move, fw);
    if (m_Left) move = DirectX::XMVectorSubtract(move, rt);
    if (m_Right) move = DirectX::XMVectorAdd(move, rt);

    if (DirectX::XMVector3NearEqual(move, DirectX::XMVectorZero(), DirectX::XMVectorReplicate(1e-6f)) == 0)
    {
        move = DirectX::XMVector3Normalize(move);
        move = DirectX::XMVectorScale(move, m_MoveSpeed * dt);
        pos = DirectX::XMVectorAdd(pos, move);
        DirectX::XMStoreFloat3(reinterpret_cast<Vector3*>(&m_Position), pos);
        m_Dirty = true;
    }
}

Matrix Camera::GetViewMatrix() const
{
    using namespace DirectX;
    XMVECTOR pos = XMLoadFloat3(reinterpret_cast<const Vector3*>(&m_Position));
    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(m_Pitch, m_Yaw, 0.0f);
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0,0,1,0), rot);
    XMVECTOR up = XMVector3Rotate(XMVectorSet(0,1,0,0), rot);
    XMMATRIX m = XMMatrixLookToLH(pos, forward, up);
    Matrix out{};
    XMStoreFloat4x4(&out, m);
    return out;
}

Matrix Camera::GetProjMatrix() const
{
    // Create an infinite projection matrix (no far plane) for better depth precision.
    float yScale = 1.0f / std::tan(m_FovY * 0.5f);
    float xScale = yScale / m_Aspect;

    Matrix m{};
    m._11 = xScale;
    // Negate Y scale for Vulkan to match DirectX Y-up convention
    Renderer* renderer = Renderer::GetInstance();
    nvrhi::GraphicsAPI api = renderer->m_NvrhiDevice->getGraphicsAPI();
    m._22 = (api == nvrhi::GraphicsAPI::VULKAN) ? -yScale : yScale;
    m._33 = 0.0f;
    m._34 = 1.0f;
    m._43 = m_Near;
    m._44 = 0.0f;

    return m;
}

Matrix Camera::GetViewProjMatrix() const
{
    using namespace DirectX;
    // Reuse view and projection getters to avoid code duplication
    Matrix viewM = GetViewMatrix();
    Matrix projM = GetProjMatrix();
    XMMATRIX view = XMLoadFloat4x4(&viewM);
    XMMATRIX proj = XMLoadFloat4x4(&projM);
    XMMATRIX vp = XMMatrixMultiply(view, proj);
    Matrix out{};
    XMStoreFloat4x4(&out, vp);
    return out;
}
