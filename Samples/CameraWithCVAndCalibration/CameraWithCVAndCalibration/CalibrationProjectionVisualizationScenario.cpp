//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "pch.h"
#include "CalibrationProjectionVisualizationScenario.h"
#include "Common\DirectXHelper.h"

#include "content\OpenCVFrameProcessing.h"

#include <iostream>
#include <fstream>
#include <ctime>
#include <ppltasks.h> // For concurrency::create_task

extern "C"
HMODULE LoadLibraryA(
    LPCSTR lpLibFileName
);

using namespace BasicHologram;
using namespace concurrency;
using namespace Microsoft::WRL;
using namespace std::placeholders;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::Graphics::Holographic;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::UI::Input::Spatial;

static ResearchModeSensorConsent camAccessCheck;
static HANDLE camConsentGiven;

CalibrationProjectionVisualizationScenario::CalibrationProjectionVisualizationScenario(std::shared_ptr<DX::DeviceResources> const& deviceResources) :
    Scenario(deviceResources)
{
}

CalibrationProjectionVisualizationScenario::~CalibrationProjectionVisualizationScenario()
{
    if (m_pLFCameraSensor)
    {
        m_pLFCameraSensor->Release();
    }

    if (m_pSensorDevice)
    {
        m_pSensorDevice->EnableEyeSelection();
        m_pSensorDevice->Release();
    }
}

void CalibrationProjectionVisualizationScenario::CamAccessOnComplete(ResearchModeSensorConsent consent)
{
    camAccessCheck = consent;
    SetEvent(camConsentGiven);
}

void CalibrationProjectionVisualizationScenario::IntializeSensors()
{
    HRESULT hr = S_OK;
    size_t sensorCount = 0;
    camConsentGiven = CreateEvent(nullptr, true, false, nullptr);

    HMODULE hrResearchMode = LoadLibraryA("ResearchModeAPI");
    if (hrResearchMode)
    {
        typedef HRESULT(__cdecl* PFN_CREATEPROVIDER) (IResearchModeSensorDevice** ppSensorDevice);
        PFN_CREATEPROVIDER pfnCreate = reinterpret_cast<PFN_CREATEPROVIDER>(GetProcAddress(hrResearchMode, "CreateResearchModeSensorDevice"));
        if (pfnCreate)
        {
            winrt::check_hresult(pfnCreate(&m_pSensorDevice));
        }
        else
        {
            winrt::check_hresult(E_INVALIDARG);
        }
    }

    winrt::check_hresult(m_pSensorDevice->QueryInterface(IID_PPV_ARGS(&m_pSensorDeviceConsent)));
    winrt::check_hresult(m_pSensorDeviceConsent->RequestCamAccessAsync(CalibrationProjectionVisualizationScenario::CamAccessOnComplete));

    m_pSensorDevice->DisableEyeSelection();

    winrt::check_hresult(m_pSensorDevice->GetSensorCount(&sensorCount));
    m_sensorDescriptors.resize(sensorCount);

    winrt::check_hresult(m_pSensorDevice->GetSensorDescriptors(m_sensorDescriptors.data(), m_sensorDescriptors.size(), &sensorCount));

    for (auto sensorDescriptor : m_sensorDescriptors)
    {
        IResearchModeSensor *pSensor = nullptr;
        IResearchModeCameraSensor *pCameraSensor = nullptr;

        if (sensorDescriptor.sensorType == LEFT_FRONT)
        {
            winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pLFCameraSensor));

            winrt::check_hresult(m_pLFCameraSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor)));

            winrt::check_hresult(pCameraSensor->GetCameraExtrinsicsMatrix(&m_LFCameraPose));

            DirectX::XMFLOAT4 zeros = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

            DirectX::XMMATRIX cameraPose = XMLoadFloat4x4(&m_LFCameraPose);
            DirectX::XMMATRIX cameraRotation = cameraPose;
            cameraRotation.r[3] = DirectX::XMLoadFloat4(&zeros);
            XMStoreFloat4x4(&m_LFCameraRotation, cameraRotation);

            DirectX::XMVECTOR det = XMMatrixDeterminant(cameraRotation);
            XMStoreFloat4(&m_LFRotDeterminant, det);
        }

        if (sensorDescriptor.sensorType == RIGHT_FRONT)
        {
            winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pRFCameraSensor));

            winrt::check_hresult(m_pRFCameraSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor)));

            winrt::check_hresult(pCameraSensor->GetCameraExtrinsicsMatrix(&m_RFCameraPose));

            DirectX::XMFLOAT4 zeros = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

            DirectX::XMMATRIX cameraPose = XMLoadFloat4x4(&m_RFCameraPose);
            DirectX::XMMATRIX cameraRotation = cameraPose;
            cameraRotation.r[3] = DirectX::XMLoadFloat4(&zeros);
            XMStoreFloat4x4(&m_RFCameraRotation, cameraRotation);

            DirectX::XMVECTOR det = XMMatrixDeterminant(cameraRotation);
            XMStoreFloat4(&m_RFRotDeterminant, det);
        }
    }
}

void CalibrationProjectionVisualizationScenario::UpdateState()
{
}

void CalibrationProjectionVisualizationScenario::IntializeSensorFrameModelRendering()
{
    HRESULT hr = S_OK;

    DirectX::XMMATRIX cameraNodeToRigPoseInverted;
    DirectX::XMMATRIX cameraNodeToRigPose;
    DirectX::XMVECTOR det;
    float xy[2] = {0};
    float uv[2];

    //Initialize test cube
    auto cube = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.1f, DirectX::XMFLOAT3(0, 0, 1.0f));
    m_modelRenderers.push_back(cube);
    m_red_cube = cube;

    // Initialize left Vector model
    {
        IResearchModeCameraSensor *pCameraSensor = nullptr;

        cameraNodeToRigPose = DirectX::XMLoadFloat4x4(&m_RFCameraPose);
        det = XMMatrixDeterminant(cameraNodeToRigPose);
        cameraNodeToRigPoseInverted = DirectX::XMMatrixInverse(&det, cameraNodeToRigPose);

        winrt::check_hresult(m_pRFCameraSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor)));

        std::shared_ptr<VectorModel> vectorOriginRenderer;

#ifdef RENDER_CAMERA_ORIGINS
        uv[0] = 0.0f;
        uv[1] = 0.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        uv[0] = 640.0f;
        uv[1] = 0.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        uv[0] = 640.0f;
        uv[1] = 480.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        uv[0] = 0.0f;
        uv[1] = 480.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);
#endif

        uv[0] = 640.0f / 2;
        uv[1] = 480.0f / 2;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.6f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        m_rayRight = vectorOriginRenderer;

        pCameraSensor->Release();
    }

    // Initialize right Vector model
    {
        IResearchModeCameraSensor *pCameraSensor = nullptr;

        cameraNodeToRigPose = DirectX::XMLoadFloat4x4(&m_LFCameraPose);
        det = XMMatrixDeterminant(cameraNodeToRigPose);
        cameraNodeToRigPoseInverted = DirectX::XMMatrixInverse(&det, cameraNodeToRigPose);

        winrt::check_hresult(m_pLFCameraSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor)));

        std::shared_ptr<VectorModel> vectorOriginRenderer;

#ifdef RENDER_CAMERA_ORIGINS
        uv[0] = 0.0f;
        uv[1] = 0.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        uv[0] = 640.0f;
        uv[1] = 0.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        uv[0] = 640.0f;
        uv[1] = 480.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        uv[0] = 0.0f;
        uv[1] = 480.0f;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.1f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);
#endif

        uv[0] = 640.0f / 2;
        uv[1] = 480.0f / 2;
        pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
        vectorOriginRenderer = std::make_shared<VectorModel>(m_deviceResources, 0.6f, 0.0005f, DirectX::XMFLOAT3(xy[0], xy[1], 1.0f));
        vectorOriginRenderer->SetGroupScaleFactor(1.0);
        vectorOriginRenderer->SetModelTransform(cameraNodeToRigPoseInverted);
        vectorOriginRenderer->SetColor(DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        m_modelRenderers.push_back(vectorOriginRenderer);

        m_rayLeft = vectorOriginRenderer;

        pCameraSensor->Release();
    }
}

void CalibrationProjectionVisualizationScenario::InitializeArucoRendering()
{
    {
        SlateCameraRenderer* pLFSlateCameraRenderer = nullptr;

        if (m_pLFCameraSensor)
        {
            // Initialize the sample hologram.
            auto slateCameraRenderer = std::make_shared<SlateCameraRenderer>(m_deviceResources, m_pLFCameraSensor, camConsentGiven, &camAccessCheck);

            slateCameraRenderer->DisableRendering();
            m_modelRenderers.push_back(slateCameraRenderer);

            pLFSlateCameraRenderer = slateCameraRenderer.get();
        }

        auto slateTextureRenderer = std::make_shared<SlateFrameRendererWithCV>(m_deviceResources, ProcessRmFrameWithAruco);
        slateTextureRenderer->StartCVProcessing(0xff);

        slateTextureRenderer->DisableRendering();
        m_modelRenderers.push_back(slateTextureRenderer);
        m_arucoDetectorLeft = slateTextureRenderer;

        pLFSlateCameraRenderer->SetFrameCallBack(SlateFrameRendererWithCV::FrameReadyCallback, slateTextureRenderer.get());
    }

    {
        SlateCameraRenderer* pRFSlateCameraRenderer = nullptr;

        if (m_pLFCameraSensor)
        {
            // Initialize the sample hologram.
            auto slateCameraRenderer = std::make_shared<SlateCameraRenderer>(m_deviceResources, m_pRFCameraSensor, camConsentGiven, &camAccessCheck);

            slateCameraRenderer->DisableRendering();
            m_modelRenderers.push_back(slateCameraRenderer);

            pRFSlateCameraRenderer = slateCameraRenderer.get();
        }

        auto slateTextureRenderer = std::make_shared<SlateFrameRendererWithCV>(m_deviceResources, ProcessRmFrameWithAruco);
        slateTextureRenderer->StartCVProcessing(0xff);

        slateTextureRenderer->DisableRendering();
        m_modelRenderers.push_back(slateTextureRenderer);
        m_arucoDetectorRight = slateTextureRenderer;

        pRFSlateCameraRenderer->SetFrameCallBack(SlateFrameRendererWithCV::FrameReadyCallback, slateTextureRenderer.get());
    }
}

void CalibrationProjectionVisualizationScenario::IntializeModelRendering()
{
    IntializeSensorFrameModelRendering();
    InitializeArucoRendering();
}

// repositions all holograms in m_modelRenderers two meters in front of the user
void CalibrationProjectionVisualizationScenario::PositionHologram(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose, const DX::StepTimer& timer)
{
    // When a Pressed gesture is detected, the sample hologram will be repositioned
    // two meters in front of the user.
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->PositionHologram(pointerPose, timer);
    }
}

// same (no smoothing)
void CalibrationProjectionVisualizationScenario::PositionHologramNoSmoothing(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose)
{
    // When a Pressed gesture is detected, the sample hologram will be repositioned
    // two meters in front of the user.
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->PositionHologramNoSmoothing(pointerPose);
    }
    PositionCube(pointerPose);
}

winrt::fire_and_forget CalibrationProjectionVisualizationScenario::WriteToFile(float f1, float f2, float f3, float f4, float f5, float f6, float f7, float f8) {
    auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();

    // Get the file, or create it if it doesn't exist
    winrt::Windows::Storage::StorageFile file = co_await localFolder.CreateFileAsync(L"pixel log.txt", winrt::Windows::Storage::CreationCollisionOption::OpenIfExists);

    // Get the current timestamp
    std::time_t result = std::time(nullptr);
    std::string timestamp = std::asctime(std::localtime(&result));
    timestamp.pop_back(); // Remove the newline character from the end of the timestamp

    // Write to the file
    std::ofstream myfile(file.Path().c_str(), std::ios::app);
    myfile << timestamp;
    myfile << ", " << f1 << ", " << f2 << ", " << f3 << ", " << f4;
    myfile << ", " << f5 << ", " << f6 << ", " << f7 << ", " << f8;
    myfile << "\n";
    myfile.close();
};

void CalibrationProjectionVisualizationScenario::UpdateModels(DX::StepTimer &timer)
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->Update(timer);
    }

    // for each of the two Aruco Detectors (TextureRenderers) rotates the ray in the respective
    // VectorModel renderer wrt the position of the Aruco marker in the camera frame.
    float x_l[2];
    float x_r[2];
    float uv_l[2];
    float uv_r[2];
    ResearchModeSensorTimestamp timeStamp;
    bool double_detection = true;

    if (m_arucoDetectorLeft->GetFirstCenter(uv_l, uv_l + 1, &timeStamp))
    {
        HRESULT hr = S_OK;

        IResearchModeCameraSensor* pCameraSensor = nullptr;
        winrt::check_hresult(m_pLFCameraSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor)));
        pCameraSensor->MapImagePointToCameraUnitPlane(uv_l, x_l);

        m_rayLeft->SetDirection(DirectX::XMFLOAT3(x_l[0], x_l[1], 1.0f));
        m_rayLeft->EnableRendering();

        pCameraSensor->Release();
    }
    else
    {
        m_rayLeft->DisableRendering();
        double_detection = false;
    }

    if (m_arucoDetectorRight->GetFirstCenter(uv_r, uv_r + 1, &timeStamp))
    {
        HRESULT hr = S_OK;

        if (m_stationaryReferenceFrame)
            SpatialCoordinateSystem currentCoordinateSystem = m_stationaryReferenceFrame.CoordinateSystem();

        IResearchModeCameraSensor* pCameraSensor = nullptr;
        winrt::check_hresult(m_pRFCameraSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor)));
        pCameraSensor->MapImagePointToCameraUnitPlane(uv_r, x_r);

        m_rayRight->SetDirection(DirectX::XMFLOAT3(x_r[0], x_r[1], 1.0f));
        m_rayRight->EnableRendering();

        pCameraSensor->Release();
    }
    else
    {
        m_rayRight->DisableRendering();
        double_detection = false;
    }

    // if both cameras see the target, place the cube
    if (double_detection)
    {
        //WriteToFile(uv_l[0], uv_l[1], uv_r[0], uv_r[1], x_l[0], x_l[1], x_r[0], x_r[1]);

        //m_red_cube->EnableRendering();
        //m_red_cube->SetPosition(float3(x_m*20, y_m*20, z*20));
        //m_red_cube->SetPosition(float3(1.f, 0.f, 0.f));
    }
    //else
        //m_red_cube->DisableRendering();
        //m_red_cube->SetPosition(float3(0.f, 0.f, 0.f));
}

void CalibrationProjectionVisualizationScenario::PositionCube(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose)
{
    // funziona ma male:
    //m_red_cube->SetPositionRelativeToHead(float3 { .33, 0., 2. });
    // funziona bene:
    m_red_cube->SetPositionRelativeToHead(pointerPose.Head(), float3{ .33, 0., 2. });
}

// renders all holograms in m_modelRenderers
void CalibrationProjectionVisualizationScenario::RenderModels()
{
    // Draw the sample hologram.
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->Render();
    }
}

void CalibrationProjectionVisualizationScenario::OnDeviceLost()
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->ReleaseDeviceDependentResources();
    }
}

void CalibrationProjectionVisualizationScenario::OnDeviceRestored()
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->CreateDeviceDependentResources();
    }
}
