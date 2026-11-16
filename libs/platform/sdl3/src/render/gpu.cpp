// Définitions de sdl3/gpu.hpp
#include "sdl3/render/gpu.hpp"

namespace sdl3 {

// ── GpuBuffer ────────────────────────────────────────────────────────────────

void GpuBuffer::SetName(const char *name) {
    if (mHandle && mDevice)
        SDL_SetGPUBufferName(mDevice, mHandle, name);
}

// ── GpuMappedBuffer ──────────────────────────────────────────────────────────

GpuMappedBuffer::~GpuMappedBuffer() {
    if (m_ptr && m_device && m_buf)
        SDL_UnmapGPUTransferBuffer(m_device, m_buf);
}

// ── GpuTransferBuffer ────────────────────────────────────────────────────────

GpuMappedBuffer GpuTransferBuffer::Map(bool cycle) noexcept {
    return GpuMappedBuffer(mDevice, mHandle, cycle);
}

// ── GpuTexture ───────────────────────────────────────────────────────────────

void GpuTexture::SetName(const char *name) {
    if (mHandle && mDevice)
        SDL_SetGPUTextureName(mDevice, mHandle, name);
}

// ── GpuComputePipeline ───────────────────────────────────────────────────────

GpuComputePipeline::~GpuComputePipeline() {
    if (m_handle && m_device)
        SDL_ReleaseGPUComputePipeline(m_device, m_handle);
}

// ── GpuRenderPass ────────────────────────────────────────────────────────────

void GpuRenderPass::End() noexcept {
    if (mHandle) {
        SDL_EndGPURenderPass(mHandle);
        mHandle = nullptr;
    }
}

void GpuRenderPass::BindPipeline(const GpuGraphicsPipeline &p) noexcept {
    if (mHandle)
        SDL_BindGPUGraphicsPipeline(mHandle, p.Get());
}

void GpuRenderPass::SetViewport(const GpuViewport &vp) noexcept {
    if (mHandle)
        SDL_SetGPUViewport(mHandle, &vp);
}

void GpuRenderPass::SetScissor(const SDL_Rect &r) noexcept {
    if (mHandle)
        SDL_SetGPUScissor(mHandle, &r);
}

void GpuRenderPass::SetBlendConstants(SDL_FColor color) noexcept {
    if (mHandle)
        SDL_SetGPUBlendConstants(mHandle, color);
}

void GpuRenderPass::SetStencilReference(uint8_t ref) noexcept {
    if (mHandle)
        SDL_SetGPUStencilReference(mHandle, ref);
}

void GpuRenderPass::BindVertexBuffers(uint32_t firstSlot, std::span<const GpuBufferBinding> bindings) noexcept {
    if (mHandle)
        SDL_BindGPUVertexBuffers(mHandle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuRenderPass::BindVertexBuffer(uint32_t slot, const GpuBufferBinding &binding) noexcept {
    BindVertexBuffers(slot, {&binding, 1});
}

void GpuRenderPass::BindIndexBuffer(const GpuBufferBinding &binding, GpuIndexElementSize elemSize) noexcept {
    if (mHandle)
        SDL_BindGPUIndexBuffer(mHandle, &binding, elemSize);
}

void GpuRenderPass::BindVertexSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
    if (mHandle)
        SDL_BindGPUVertexSamplers(mHandle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuRenderPass::BindVertexStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
    if (!mHandle)
        return;
    auto raw = ::detail::ToRawHandles(textures);
    SDL_BindGPUVertexStorageTextures(mHandle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::BindVertexStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
    if (!mHandle)
        return;
    auto raw = ::detail::ToRawHandles(buffers);
    SDL_BindGPUVertexStorageBuffers(mHandle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::BindFragmentSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
    if (mHandle)
        SDL_BindGPUFragmentSamplers(mHandle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuRenderPass::BindFragmentStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
    if (!mHandle)
        return;
    auto raw = ::detail::ToRawHandles(textures);
    SDL_BindGPUFragmentStorageTextures(mHandle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::BindFragmentStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
    if (!mHandle)
        return;
    auto raw = ::detail::ToRawHandles(buffers);
    SDL_BindGPUFragmentStorageBuffers(mHandle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuRenderPass::DrawPrimitives(uint32_t numVertices, uint32_t numInstances, uint32_t firstVertex,
		uint32_t firstInstance) noexcept {
    if (mHandle)
        SDL_DrawGPUPrimitives(mHandle, numVertices, numInstances, firstVertex, firstInstance);
}

void GpuRenderPass::DrawIndexedPrimitives(uint32_t numIndices, uint32_t numInstances, uint32_t firstIndex,
		int32_t vertexOffset, uint32_t firstInstance) noexcept {
    if (mHandle)
        SDL_DrawGPUIndexedPrimitives(mHandle, numIndices, numInstances, firstIndex, vertexOffset, firstInstance);
}

void GpuRenderPass::DrawPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept {
    if (mHandle)
        SDL_DrawGPUPrimitivesIndirect(mHandle, buf.Get(), offset, drawCount);
}

void GpuRenderPass::DrawIndexedPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept {
    if (mHandle)
        SDL_DrawGPUIndexedPrimitivesIndirect(mHandle, buf.Get(), offset, drawCount);
}

// ── GpuComputePass ───────────────────────────────────────────────────────────

void GpuComputePass::End() noexcept {
    if (mHandle) {
        SDL_EndGPUComputePass(mHandle);
        mHandle = nullptr;
    }
}

void GpuComputePass::BindPipeline(const GpuComputePipeline &p) noexcept {
    if (mHandle)
        SDL_BindGPUComputePipeline(mHandle, p.Get());
}

void GpuComputePass::BindSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
    if (mHandle)
        SDL_BindGPUComputeSamplers(mHandle, firstSlot, bindings.data(), uint32_t(bindings.size()));
}

void GpuComputePass::BindStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
    if (!mHandle)
        return;
    auto raw = ::detail::ToRawHandles(textures);
    SDL_BindGPUComputeStorageTextures(mHandle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuComputePass::BindStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
    if (!mHandle)
        return;
    auto raw = ::detail::ToRawHandles(buffers);
    SDL_BindGPUComputeStorageBuffers(mHandle, firstSlot, raw.data(), uint32_t(raw.size()));
}

void GpuComputePass::Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) noexcept {
    if (mHandle)
        SDL_DispatchGPUCompute(mHandle, groupX, groupY, groupZ);
}

void GpuComputePass::DispatchIndirect(const GpuBuffer &buf, uint32_t offset) noexcept {
    if (mHandle)
        SDL_DispatchGPUComputeIndirect(mHandle, buf.Get(), offset);
}

// ── GpuCopyPass ──────────────────────────────────────────────────────────────

void GpuCopyPass::End() noexcept {
    if (mHandle) {
        SDL_EndGPUCopyPass(mHandle);
        mHandle = nullptr;
    }
}

void GpuCopyPass::UploadToTexture(const GpuTextureTransferInfo &src, const GpuTextureRegion &dst, bool cycle) noexcept {
    if (mHandle)
        SDL_UploadToGPUTexture(mHandle, &src, &dst, cycle);
}

void GpuCopyPass::UploadToBuffer(const GpuTransferBufferLocation &src, const GpuBufferRegion &dst, bool cycle) noexcept {
    if (mHandle)
        SDL_UploadToGPUBuffer(mHandle, &src, &dst, cycle);
}

void GpuCopyPass::CopyTextureToTexture(const GpuTextureLocation &src, const GpuTextureLocation &dst, uint32_t w, uint32_t h,
		uint32_t d, bool cycle) noexcept {
    if (mHandle)
        SDL_CopyGPUTextureToTexture(mHandle, &src, &dst, w, h, d, cycle);
}

void GpuCopyPass::CopyBufferToBuffer(const GpuBufferLocation &src, const GpuBufferLocation &dst, uint32_t size,
		bool cycle) noexcept {
    if (mHandle)
        SDL_CopyGPUBufferToBuffer(mHandle, &src, &dst, size, cycle);
}

void GpuCopyPass::DownloadFromTexture(const GpuTextureRegion &src, const GpuTextureTransferInfo &dst) noexcept {
    if (mHandle)
        SDL_DownloadFromGPUTexture(mHandle, &src, &dst);
}

void GpuCopyPass::DownloadFromBuffer(const GpuBufferRegion &src, const GpuTransferBufferLocation &dst) noexcept {
    if (mHandle)
        SDL_DownloadFromGPUBuffer(mHandle, &src, &dst);
}

void CancelGPUCommandBuffer(SDL_GPUDevice *d, SDL_GPUCommandBuffer* b) {
    (void)d;
    SDL_CancelGPUCommandBuffer(b);
}

// ── GpuCommandBuffer ─────────────────────────────────────────────────────────

void GpuCommandBuffer::InsertDebugLabel(const char *label) noexcept {
    if (mHandle)
        SDL_InsertGPUDebugLabel(mHandle, label);
}

void GpuCommandBuffer::PushDebugGroup(const char *name) noexcept {
    if (mHandle)
        SDL_PushGPUDebugGroup(mHandle, name);
}

void GpuCommandBuffer::PopDebugGroup() noexcept {
    if (mHandle)
        SDL_PopGPUDebugGroup(mHandle);
}

GpuRenderPass GpuCommandBuffer::BeginRenderPass(std::span<const GpuColorTargetInfo> colorTargets,
		const GpuDepthStencilTargetInfo *depthStencil) noexcept {
    if (!mHandle)
        return {};
    return GpuRenderPass(
        SDL_BeginGPURenderPass(mHandle, colorTargets.data(), uint32_t(colorTargets.size()), depthStencil));
}

GpuRenderPass GpuCommandBuffer::BeginRenderPass(const GpuColorTargetInfo &colorTarget,
		const GpuDepthStencilTargetInfo *depthStencil) noexcept {
    return BeginRenderPass({&colorTarget, 1}, depthStencil);
}

GpuComputePass GpuCommandBuffer::BeginComputePass(std::span<const GpuStorageTextureReadWriteBinding> storageTextures,
		std::span<const GpuStorageBufferReadWriteBinding> storageBuffers) noexcept {
    if (!mHandle)
        return {};
    return GpuComputePass(SDL_BeginGPUComputePass(mHandle, storageTextures.data(),
                                                  uint32_t(storageTextures.size()), storageBuffers.data(),
                                                  uint32_t(storageBuffers.size())));
}

GpuCopyPass GpuCommandBuffer::BeginCopyPass() noexcept {
    if (!mHandle)
        return {};
    return GpuCopyPass(SDL_BeginGPUCopyPass(mHandle));
}

void GpuCommandBuffer::GenerateMipmaps(const GpuTexture &tex) noexcept {
    if (mHandle)
        SDL_GenerateMipmapsForGPUTexture(mHandle, tex.Get());
}

void GpuCommandBuffer::BlitTexture(const GpuBlitInfo &info) noexcept {
    if (mHandle)
        SDL_BlitGPUTexture(mHandle, &info);
}

Option<Borrowed<SDL_GPUTexture>> GpuCommandBuffer::AcquireSwapchainTexture(Ref<Window> win, uint32_t *outW,
		uint32_t *outH) noexcept {
    if (!mHandle)
        return NONE;
    SDL_GPUTexture *tex = nullptr;
    SDL_AcquireGPUSwapchainTexture(mHandle, win->Get(), &tex, outW, outH);
    if (!tex)
        return NONE;
    return Some(Borrowed<SDL_GPUTexture>(tex));
}

Option<Borrowed<SDL_GPUTexture>> GpuCommandBuffer::WaitAndAcquireSwapchainTexture(Ref<Window> win, uint32_t *outW, uint32_t *outH) noexcept {
    if (!mHandle)
        return NONE;
    SDL_GPUTexture *tex = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(mHandle, win->Get(), &tex, outW, outH);
    if (!tex)
        return NONE;
    return Some(Borrowed<SDL_GPUTexture>(tex));
}

bool GpuCommandBuffer::Submit() noexcept {
    if (!mHandle)
        return false;
    bool ok = SDL_SubmitGPUCommandBuffer(mHandle);
    mHandle = nullptr;
    return ok;
}

Option<GpuFence> GpuCommandBuffer::SubmitAndAcquireFence() noexcept {
    if (!mHandle)
        return NONE;
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(mHandle);
    mHandle = nullptr;
    if (!fence)
        return NONE;
    return Some(GpuFence(mDevice, fence));
}

bool GpuCommandBuffer::Cancel() noexcept {
    if (!mHandle)
        return false;
    bool ok = SDL_CancelGPUCommandBuffer(mHandle);
    mHandle = nullptr;
    return ok;
}

// ── GpuDevice ────────────────────────────────────────────────────────────────

Result<GpuDevice, Error> GpuDevice::Create(SDL_GPUShaderFormat formats, bool debugMode, const char *driverName) {
    auto *d = SDL_CreateGPUDevice(formats, debugMode, driverName);
    if (!d)
        return Err(GetError());
    return Ok(GpuDevice(d));
}

Result<GpuDevice, StringView> GpuDevice::CreateWithProperties(SDL_PropertiesID props) {
    auto *d = SDL_CreateGPUDeviceWithProperties(props);
    if (!d)
        return Err(GetError());
    return Ok(GpuDevice(d));
}

SDL_GPUShaderFormat GpuDevice::ShaderFormats() const noexcept {
    return mHandle ? SDL_GetGPUShaderFormats(mHandle) : SDL_GPUShaderFormat(SDL_GPU_SHADERFORMAT_INVALID);
}

SDL_PropertiesID GpuDevice::Properties() const noexcept {
    return mHandle ? SDL_GetGPUDeviceProperties(mHandle) : 0;
}

bool GpuDevice::TextureSupportsFormat(SDL_GPUTextureFormat format, SDL_GPUTextureType type,
		SDL_GPUTextureUsageFlags usage) const noexcept {
    return mHandle && SDL_GPUTextureSupportsFormat(mHandle, format, type, usage);
}

bool GpuDevice::TextureSupportsSampleCount(SDL_GPUTextureFormat format, SDL_GPUSampleCount count) const noexcept {
    return mHandle && SDL_GPUTextureSupportsSampleCount(mHandle, format, count);
}

bool GpuDevice::ClaimWindow(Ref<Window> win) noexcept {
    return mHandle && SDL_ClaimWindowForGPUDevice(mHandle, win->Get());
}

void GpuDevice::ReleaseWindow(Ref<Window> win) noexcept {
    if (mHandle)
        SDL_ReleaseWindowFromGPUDevice(mHandle, win->Get());
}

bool GpuDevice::SetSwapchainParameters(Ref<Window> win, SDL_GPUSwapchainComposition composition,
		SDL_GPUPresentMode presentMode) noexcept {
    return mHandle && SDL_SetGPUSwapchainParameters(mHandle, win->Get(), composition, presentMode);
}

bool GpuDevice::SetAllowedFramesInFlight(uint32_t n) noexcept {
    return mHandle && SDL_SetGPUAllowedFramesInFlight(mHandle, n);
}

SDL_GPUTextureFormat GpuDevice::SwapchainTextureFormat(Ref<Window> win) const noexcept {
    return mHandle ? SDL_GetGPUSwapchainTextureFormat(mHandle, win->Get()) : SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool GpuDevice::WindowSupportsComposition(Ref<Window> win, SDL_GPUSwapchainComposition c) const noexcept {
    return mHandle && SDL_WindowSupportsGPUSwapchainComposition(mHandle, win->Get(), c);
}

bool GpuDevice::WindowSupportsPresentMode(Ref<Window> win, SDL_GPUPresentMode mode) const noexcept {
    return mHandle && SDL_WindowSupportsGPUPresentMode(mHandle, win->Get(), mode);
}

bool GpuDevice::WaitForSwapchain(Ref<Window> win) noexcept {
    return mHandle && SDL_WaitForGPUSwapchain(mHandle, win->Get());
}

GpuCommandBuffer GpuDevice::AcquireCommandBuffer() noexcept {
    if (!mHandle)
        return {};
    return GpuCommandBuffer(mHandle, SDL_AcquireGPUCommandBuffer(mHandle));
}

bool GpuDevice::WaitForFences(bool waitAll, std::span<const Ref<GpuFence>> fences) {
    if (!mHandle)
        return false;
    auto raw = ::detail::ToRawHandles(fences);
    return SDL_WaitForGPUFences(mHandle, waitAll, raw.data(), uint32_t(raw.size()));
}

Result<GpuBuffer, StringView> GpuDevice::CreateBuffer(const GpuBufferCreateInfo &info) {
    auto *b = SDL_CreateGPUBuffer(mHandle, &info);
    if (!b)
        return Err(GetError());
    return Ok(GpuBuffer(mHandle, b));
}

Result<GpuBuffer, StringView> GpuDevice::CreateBuffer(SDL_GPUBufferUsageFlags usage, uint32_t size) {
    GpuBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    return CreateBuffer(info);
}

Result<GpuTransferBuffer, StringView> GpuDevice::CreateTransferBuffer(const GpuTransferBufferCreateInfo &info) {
    auto *b = SDL_CreateGPUTransferBuffer(mHandle, &info);
    if (!b)
        return Err(GetError());
    return Ok(GpuTransferBuffer(mHandle, b));
}

Result<GpuTransferBuffer, StringView> GpuDevice::CreateTransferBuffer(SDL_GPUTransferBufferUsage usage, uint32_t size) {
    GpuTransferBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    return CreateTransferBuffer(info);
}

Result<GpuTexture, StringView> GpuDevice::CreateTexture(const GpuTextureCreateInfo &info) {
    auto *t = SDL_CreateGPUTexture(mHandle, &info);
    if (!t)
        return Err(GetError());
    return Ok(GpuTexture(mHandle, t));
}

Result<GpuSampler, StringView> GpuDevice::CreateSampler(const GpuSamplerCreateInfo &info) {
    auto *s = SDL_CreateGPUSampler(mHandle, &info);
    if (!s)
        return Err(GetError());
    return Ok(GpuSampler(mHandle, s));
}

Result<GpuShader, StringView> GpuDevice::CreateShader(const GpuShaderCreateInfo &info) {
    auto *s = SDL_CreateGPUShader(mHandle, &info);
    if (!s)
        return Err(GetError());
    return Ok(GpuShader(mHandle, s));
}

Result<GpuGraphicsPipeline, StringView> GpuDevice::CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) {
    auto *p = SDL_CreateGPUGraphicsPipeline(mHandle, &info);
    if (!p)
        return Err(GetError());
    return Ok(GpuGraphicsPipeline(mHandle, p));
}

Result<GpuComputePipeline, StringView> GpuDevice::CreateComputePipeline(const GpuComputePipelineCreateInfo &info) {
    auto *p = SDL_CreateGPUComputePipeline(mHandle, &info);
    if (!p)
        return Err(GetError());
    return Ok(GpuComputePipeline(mHandle, p));
}

Result<GpuDevice, StringView> Renderer::GetGPUDevice() const {
    SDL_GPUDevice *gpu = SDL_GetGPURendererDevice(mHandle);
    if (!gpu)
        return Err(GetError());
    return Ok(GpuDevice(gpu));
}

bool GpuSupportsShaderFormats(SDL_GPUShaderFormat formats, const char *name) noexcept {
    return SDL_GPUSupportsShaderFormats(formats, name);
}

bool GpuSupportsProperties(SDL_PropertiesID props) noexcept {
    return SDL_GPUSupportsProperties(props);
}

uint32_t GpuTexelBlockSize(SDL_GPUTextureFormat fmt) noexcept {
    return SDL_GPUTextureFormatTexelBlockSize(fmt);
}

uint32_t GpuCalculateTextureSize(SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h, uint32_t depthOrLayers) noexcept {
    return SDL_CalculateGPUTextureFormatSize(fmt, w, h, depthOrLayers);
}

SDL_PixelFormat GpuTextureFormatToPixel(SDL_GPUTextureFormat fmt) noexcept {
    return SDL_GetPixelFormatFromGPUTextureFormat(fmt);
}

SDL_GPUTextureFormat GpuTextureFormatFromPixel(SDL_PixelFormat fmt) noexcept {
    return SDL_GetGPUTextureFormatFromPixelFormat(fmt);
}

} // namespace sdl3
