// MIT License
//
// Copyright(c) 2021 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "shader_utilities.h"
#include "factories.h"
#include "interfaces.h"
#include "log.h"

namespace {

    using namespace toolkit;
    using namespace toolkit::graphics;
    using namespace toolkit::log;

    const std::string QuadVertexShader = R"_(
void vsMain(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0)
{
    texcoord = float2((id == 1) ? 2.0 : 0.0, (id == 2) ? 2.0 : 0.0);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)_";

    // Wrap a pixel shader resource. Obtained from D3D12Device.
    class D3D12QuadShader : public IQuadShader {
      public:
        D3D12QuadShader(std::shared_ptr<IDevice> device, ComPtr<ID3D12PipelineState> pixelShader)
            : m_device(device), m_pixelShader(pixelShader) {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void* getNativePtr() const override {
            return m_pixelShader.Get();
        }

      private:
        const std::shared_ptr<IDevice> m_device;
        const ComPtr<ID3D12PipelineState> m_pixelShader;
    };

    // Wrap a compute shader resource. Obtained from D3D1Device.
    class D3D12ComputeShader : public IComputeShader {
      public:
        D3D12ComputeShader(std::shared_ptr<IDevice> device,
                           ComPtr<ID3D12PipelineState> computeShader,
                           const std::array<unsigned int, 3>& threadGroups)
            : m_device(device), m_computeShader(computeShader), m_threadGroups(threadGroups) {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void updateThreadGroups(const std::array<unsigned int, 3>& threadGroups) override {
            m_threadGroups = threadGroups;
        }

        const std::array<unsigned int, 3>& getThreadGroups() const {
            return m_threadGroups;
        }

        void* getNativePtr() const override {
            return m_computeShader.Get();
        }

      private:
        const std::shared_ptr<IDevice> m_device;
        ComPtr<ID3D12PipelineState> m_computeShader;
        std::array<unsigned int, 3> m_threadGroups;
    };

    // Wrap a texture shader resource view. Obtained from D3D12Texture.
    class D3D12ShaderResourceView : public IShaderInputTextureView {
      public:
        D3D12ShaderResourceView(std::shared_ptr<IDevice> device, D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceView)
            : m_device(device), m_shaderResourceView(shaderResourceView) {
        }

        ~D3D12ShaderResourceView() override {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void* getNativePtr() const override {
            return nullptr;
        }

      private:
        const std::shared_ptr<IDevice> m_device;
        const D3D12_CPU_DESCRIPTOR_HANDLE m_shaderResourceView;
    };

    // Wrap a texture unordered access view. Obtained from D3D12Texture.
    class D3D12UnorderedAccessView : public IComputeShaderOutputView {
      public:
        D3D12UnorderedAccessView(std::shared_ptr<IDevice> device, D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessView)
            : m_device(device), m_unorderedAccessView(unorderedAccessView) {
        }

        ~D3D12UnorderedAccessView() override {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void* getNativePtr() const override {
            return nullptr;
        }

      private:
        const std::shared_ptr<IDevice> m_device;
        const D3D12_CPU_DESCRIPTOR_HANDLE m_unorderedAccessView;
    };

    // Wrap a render target view view. Obtained from D3D12exture.
    class D3D12RenderTargetView : public IRenderTargetView {
      public:
        D3D12RenderTargetView(std::shared_ptr<IDevice> device, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView)
            : m_device(device), m_renderTargetView(renderTargetView) {
        }

        ~D3D12RenderTargetView() override {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void* getNativePtr() const override {
            return nullptr;
        }

      private:
        const std::shared_ptr<IDevice> m_device;
        const D3D12_CPU_DESCRIPTOR_HANDLE m_renderTargetView;
    };

    // Wrap a texture resource. Obtained from D3D12Device.
    class D3D12Texture : public ITexture {
      public:
        D3D12Texture(std::shared_ptr<IDevice> device,
                     const XrSwapchainCreateInfo& info,
                     const D3D12_RESOURCE_DESC& textureDesc,
                     ComPtr<ID3D12Resource> texture)
            : m_device(device), m_info(info), m_textureDesc(textureDesc), m_texture(texture) {
            m_shaderResourceSubView.resize(info.arraySize);
            m_unorderedAccessSubView.resize(info.arraySize);
            m_renderTargetSubView.resize(info.arraySize);
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        const XrSwapchainCreateInfo& getInfo() const override {
            return m_info;
        }

        bool isArray() const override {
            return false;
        }

        std::shared_ptr<IShaderInputTextureView> getShaderInputView() const override {
            return getShaderInputViewInternal(m_shaderResourceView, 0);
        }

        std::shared_ptr<IShaderInputTextureView> getShaderInputView(uint32_t slice) const override {
            return getShaderInputViewInternal(m_shaderResourceSubView[slice], slice);
        }

        std::shared_ptr<IComputeShaderOutputView> getComputeShaderOutputView() const override {
            return getComputeShaderOutputViewInternal(m_unorderedAccessView, 0);
        }

        std::shared_ptr<IComputeShaderOutputView> getComputeShaderOutputView(uint32_t slice) const override {
            return getComputeShaderOutputViewInternal(m_unorderedAccessSubView[slice], slice);
        }

        std::shared_ptr<IRenderTargetView> getRenderTargetView() const override {
            return getRenderTargetViewInternal(m_renderTargetView, 0);
        }

        std::shared_ptr<IRenderTargetView> getRenderTargetView(uint32_t slice) const override {
            return getRenderTargetViewInternal(m_renderTargetSubView[slice], slice);
        }

        void* getNativePtr() const override {
            return m_texture.Get();
        }

      private:
        std::shared_ptr<D3D12ShaderResourceView> getShaderInputViewInternal(
            std::shared_ptr<D3D12ShaderResourceView>& shaderResourceView, uint32_t slice = 0) const {
            if (!shaderResourceView) {
            }
            return shaderResourceView;
        }

        std::shared_ptr<D3D12UnorderedAccessView> getComputeShaderOutputViewInternal(
            std::shared_ptr<D3D12UnorderedAccessView>& unorderedAccessView, uint32_t slice = 0) const {
            if (!unorderedAccessView) {
            }
            return unorderedAccessView;
        }

        std::shared_ptr<D3D12RenderTargetView> getRenderTargetViewInternal(
            std::shared_ptr<D3D12RenderTargetView>& renderTargetView, uint32_t slice = 0) const {
            if (!renderTargetView) {
            }
            return renderTargetView;
        }

        const std::shared_ptr<IDevice> m_device;
        const XrSwapchainCreateInfo m_info;
        const D3D12_RESOURCE_DESC m_textureDesc;
        const ComPtr<ID3D12Resource> m_texture;

        mutable std::shared_ptr<D3D12ShaderResourceView> m_shaderResourceView;
        mutable std::vector<std::shared_ptr<D3D12ShaderResourceView>> m_shaderResourceSubView;
        mutable std::shared_ptr<D3D12UnorderedAccessView> m_unorderedAccessView;
        mutable std::vector<std::shared_ptr<D3D12UnorderedAccessView>> m_unorderedAccessSubView;
        mutable std::shared_ptr<D3D12RenderTargetView> m_renderTargetView;
        mutable std::vector<std::shared_ptr<D3D12RenderTargetView>> m_renderTargetSubView;
    };

    class D3D12Buffer : public IShaderBuffer {
      public:
        D3D12Buffer(std::shared_ptr<IDevice> device, D3D12_RESOURCE_DESC bufferDesc, ComPtr<ID3D12Resource> buffer)
            : m_device(device), m_bufferDesc(bufferDesc), m_buffer(buffer) {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void uploadData(void* buffer, size_t count) override {
        }

        void* getNativePtr() const override {
            return m_buffer.Get();
        }

      private:
        const std::shared_ptr<IDevice> m_device;
        const D3D12_RESOURCE_DESC m_bufferDesc;
        const ComPtr<ID3D12Resource> m_buffer;
    };

    class D3D12GpuTimer : public IGpuTimer {
      public:
        D3D12GpuTimer(std::shared_ptr<IDevice> device) : m_device(device) {
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        std::shared_ptr<IDevice> getDevice() const override {
            return m_device;
        }

        void start() override {
        }

        void stop() override {
        }

        uint64_t query(bool reset) const override {
            return 0;
        }

      private:
        const std::shared_ptr<IDevice> m_device;
    };

    class D3D12Device : public IDevice, public std::enable_shared_from_this<D3D12Device> {
      public:
        D3D12Device(ID3D12Device* device, ID3D12CommandQueue* queue) : m_device(device), m_queue(queue) {
            {
                ComPtr<IDXGIDevice> dxgiDevice;
                ComPtr<IDXGIAdapter> adapter;
                DXGI_ADAPTER_DESC desc;

                CHECK_HRCMD(m_device->QueryInterface(__uuidof(IDXGIDevice),
                                                     reinterpret_cast<void**>(dxgiDevice.GetAddressOf())));
                CHECK_HRCMD(dxgiDevice->GetAdapter(&adapter));
                CHECK_HRCMD(adapter->GetDesc(&desc));

                const std::wstring wadapterDescription(desc.Description);
                std::transform(wadapterDescription.begin(),
                               wadapterDescription.end(),
                               std::back_inserter(m_deviceName),
                               [](wchar_t c) { return (char)c; });

                // Log the adapter name to help debugging customer issues.
                Log("Using adapter: %s\n", m_deviceName.c_str());
            }
        }

        Api getApi() const override {
            return Api::D3D12;
        }

        const std::string& getDeviceName() const override {
            return m_deviceName;
        }

        int64_t getTextureFormat(TextureFormat format) const override {
            switch (format) {
            case TextureFormat::R32G32B32A32_FLOAT:
                return (int64_t)DXGI_FORMAT_R32G32B32A32_FLOAT;

            case TextureFormat::R16G16B16A16_UNORM:
                return (int64_t)DXGI_FORMAT_R16G16B16A16_UNORM;

            default:
                throw new std::runtime_error("Unknown texture format");
            };
        }

        bool isTextureFormatSRGB(int64_t format) const override {
            switch ((DXGI_FORMAT)format) {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                return true;

            default:
                return false;
            };
        }

        std::shared_ptr<ITexture> createTexture(const XrSwapchainCreateInfo& info,
                                                const std::optional<std::string>& debugName,
                                                uint32_t rowPitch = 0,
                                                uint32_t imageSize = 0,
                                                const void* initialData = nullptr) override {
            return {};
        }

        std::shared_ptr<IShaderBuffer> createBuffer(size_t size,
                                                    const std::optional<std::string>& debugName,
                                                    const void* initialData) override {
            return {};
        }

        std::shared_ptr<IQuadShader> createQuadShader(const std::string& shaderPath,
                                                      const std::string& entryPoint,
                                                      const std::optional<std::string>& debugName,
                                                      const D3D_SHADER_MACRO* defines,
                                                      const std::string includePath) override {
            return {};
        }

        std::shared_ptr<IComputeShader> createComputeShader(const std::string& shaderPath,
                                                            const std::string& entryPoint,
                                                            const std::optional<std::string>& debugName,
                                                            const std::array<unsigned int, 3>& threadGroups,
                                                            const D3D_SHADER_MACRO* defines,
                                                            const std::string includePath) override {
            return {};
        }

        std::shared_ptr<IGpuTimer> createTimer() override {
            return std::make_shared<D3D12GpuTimer>(shared_from_this());
        }

        void setShader(std::shared_ptr<IQuadShader> shader) override {
        }

        void setShader(std::shared_ptr<IComputeShader> shader) override {
        }

        void setShaderInput(uint32_t slot, std::shared_ptr<ITexture> input, int32_t slice) override {
        }

        void setShaderInput(uint32_t slot, std::shared_ptr<IShaderBuffer> input) override {
        }

        void setShaderOutput(uint32_t slot, std::shared_ptr<ITexture> output, int32_t slice) override {
        }

        void dispatchShader(bool doNotClear) const override {
        }

        void clearRenderTargets() override {
        }

        void setRenderTargets(std::vector<std::shared_ptr<ITexture>> renderTargets) override {
        }

        void setRenderTargets(std::vector<std::pair<std::shared_ptr<ITexture>, int32_t>> renderTargets) override {
        }

        void* getNativePtr() const override {
            return m_device.Get();
        }

        void* getContextPtr() const override {
            return m_context.Get();
        }

      private:
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12CommandQueue> m_queue;
        ComPtr<ID3D12CommandList> m_context;
        std::string m_deviceName;
    };

} // namespace

namespace toolkit::graphics {

    std::shared_ptr<IDevice> WrapD3D12Device(ID3D12Device* device, ID3D12CommandQueue* queue) {
        return std::make_shared<D3D12Device>(device, queue);
    }

    std::shared_ptr<ITexture> WrapD3D12Texture(std::shared_ptr<IDevice> device,
                                               const XrSwapchainCreateInfo& info,
                                               ID3D12Resource* texture,
                                               const std::optional<std::string>& debugName) {
        if (device->getApi() != Api::D3D12) {
            throw new std::runtime_error("Not a D3D12 device");
        }

        if (debugName) {
            texture->SetName(std::wstring(debugName->begin(), debugName->end()).c_str());
        }

        const D3D12_RESOURCE_DESC desc = texture->GetDesc();
        return std::make_shared<D3D12Texture>(device, info, desc, texture);
    }

} // namespace toolkit::graphics
