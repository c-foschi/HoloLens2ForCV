
// currently unused class

#pragma once
#include "BasicHologramMain.h"

namespace BasicHologram
{
    class ModelEnsemble : public ModelRenderer
    {
    public:

        void Update(DX::StepTimer &timer);
        void PositionHologram(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose, const DX::StepTimer& timer);
        void PositionHologramNoSmoothing(winrt::Windows::UI::Input::Spatial::SpatialPointerPose const& pointerPose);
        winrt::Windows::Foundation::Numerics::float3 const& GetPosition()
        {
            return m_modelRenderers[0]->GetPosition();
        }
        void Render();
        void ReleaseDeviceDependentResources();
        void CreateDeviceDependentResources();
        void SetGroupTransform(DirectX::XMMATRIX groupRotation);

    protected:
        std::vector<std::shared_ptr<ModelRenderer>> m_modelRenderers;
    };
}
