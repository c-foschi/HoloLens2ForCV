
#include "pch.h"
#include "ModelEnsemble.h"
#include "Common\DirectXHelper.h"


using namespace BasicHologram;


// repositions all holograms in m_modelRenderers two meters in front of the user
void ModelEnsemble::PositionHologram(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose, const DX::StepTimer& timer)
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->PositionHologram(pointerPose, timer);
    }
}

// same (no smoothing)
void ModelEnsemble::PositionHologramNoSmoothing(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose)
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->PositionHologramNoSmoothing(pointerPose);
    }
}

void ModelEnsemble::Update(DX::StepTimer &timer)
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->Update(timer);
    }
}

// renders all holograms in m_modelRenderers
void ModelEnsemble::Render()
{
    // Draw the sample hologram.
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->Render();
    }
}

void ModelEnsemble::ReleaseDeviceDependentResources()
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->ReleaseDeviceDependentResources();
    }
}

void ModelEnsemble::CreateDeviceDependentResources()
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->CreateDeviceDependentResources();
    }
}

void ModelEnsemble::SetGroupTransform(DirectX::XMMATRIX groupRotation)
{
    for (int i = 0; i < m_modelRenderers.size(); i++)
    {
        m_modelRenderers[i]->SetGroupTransform(groupRotation);
    }
}