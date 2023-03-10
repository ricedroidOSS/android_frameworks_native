/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <sys/types.h>
#include <cstdint>
#include <list>

#include <gui/ISurfaceComposerClient.h>
#include <gui/LayerState.h>
#include <renderengine/Image.h>
#include <renderengine/Mesh.h>
#include <renderengine/Texture.h>
#include <system/window.h> // For NATIVE_WINDOW_SCALING_MODE_FREEZE
#include <ui/FrameStats.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <ui/Region.h>
#include <utils/RefBase.h>
#include <utils/String8.h>
#include <utils/Timers.h>

#include "BufferLayerConsumer.h"
#include "Client.h"
#include "DisplayHardware/HWComposer.h"
#include "FrameTimeline.h"
#include "FrameTracker.h"
#include "Layer.h"
#include "LayerVector.h"
#include "MonitoredProducer.h"
#include "SurfaceFlinger.h"

namespace android {

class BufferLayer : public Layer {
public:
    explicit BufferLayer(const LayerCreationArgs& args);
    virtual ~BufferLayer() override;

    // Implements Layer.
    sp<compositionengine::LayerFE> getCompositionEngineLayerFE() const override;
    compositionengine::LayerFECompositionState* editCompositionState() override;

    // If we have received a new buffer this frame, we will pass its surface
    // damage down to hardware composer. Otherwise, we must send a region with
    // one empty rect.
    void useSurfaceDamage() override;
    void useEmptyDamage() override;

    bool isOpaque(const Layer::State& s) const override;
    bool canReceiveInput() const override;

    // isVisible - true if this layer is visible, false otherwise
    bool isVisible() const override;

    // isProtected - true if the layer may contain protected content in the
    // GRALLOC_USAGE_PROTECTED sense.
    bool isProtected() const override;

    // isFixedSize - true if content has a fixed size
    bool isFixedSize() const override;

    bool usesSourceCrop() const override;

    bool isHdrY410() const override;

    void onPostComposition(const DisplayDevice*, const std::shared_ptr<FenceTime>& glDoneFence,
                           const std::shared_ptr<FenceTime>& presentFence,
                           const CompositorTiming&) override;

    // latchBuffer - called each time the screen is redrawn and returns whether
    // the visible regions need to be recomputed (this is a fairly heavy
    // operation, so this should be set only if needed). Typically this is used
    // to figure out if the content or size of a surface has changed.
    bool latchBuffer(bool& recomputeVisibleRegions, nsecs_t latchTime,
                     nsecs_t expectedPresentTime) override;
    bool hasReadyFrame() const override;

    bool getConfigurationChanged() const { return mConfigurationChanged; }

    // Returns the current scaling mode
    uint32_t getEffectiveScalingMode() const override;

    // Calls latchBuffer if the buffer has a frame queued and then releases the buffer.
    // This is used if the buffer is just latched and releases to free up the buffer
    // and will not be shown on screen.
    // Should only be called on the main thread.
    void latchAndReleaseBuffer() override;

    bool getTransformToDisplayInverse() const override;

    Rect getBufferCrop() const override;

    uint32_t getBufferTransform() const override;

    ui::Dataspace getDataSpace() const override;

    sp<GraphicBuffer> getBuffer() const override;
    const std::shared_ptr<renderengine::ExternalTexture>& getExternalTexture() const override;

    ui::Transform::RotationFlags getTransformHint() const override { return mTransformHint; }

    // Returns true if the transformed buffer size does not match the layer size and we need
    // to apply filtering.
    virtual bool bufferNeedsFiltering() const;

protected:
    struct BufferInfo {
        nsecs_t mDesiredPresentTime;
        std::shared_ptr<FenceTime> mFenceTime;
        sp<Fence> mFence;
        uint32_t mTransform{0};
        ui::Dataspace mDataspace{ui::Dataspace::UNKNOWN};
        Rect mCrop;
        uint32_t mScaleMode{NATIVE_WINDOW_SCALING_MODE_FREEZE};
        Region mSurfaceDamage;
        HdrMetadata mHdrMetadata;
        int mApi;
        PixelFormat mPixelFormat{PIXEL_FORMAT_NONE};
        bool mTransformToDisplayInverse{false};

        std::shared_ptr<renderengine::ExternalTexture> mBuffer;
        uint64_t mFrameNumber;
        int mBufferSlot{BufferQueue::INVALID_BUFFER_SLOT};

        bool mFrameLatencyNeeded{false};
    };

    BufferInfo mBufferInfo;
    virtual void gatherBufferInfo() = 0;

    std::optional<compositionengine::LayerFE::LayerSettings> prepareClientComposition(
            compositionengine::LayerFE::ClientCompositionTargetSettings&) override;

    /*
     * compositionengine::LayerFE overrides
     */
    const compositionengine::LayerFECompositionState* getCompositionState() const override;
    bool onPreComposition(nsecs_t) override;
    void preparePerFrameCompositionState() override;

    static bool getOpacityForFormat(PixelFormat format);

    // from graphics API
    const uint32_t mTextureName;
    ui::Dataspace translateDataspace(ui::Dataspace dataspace);
    void setInitialValuesForClone(const sp<Layer>& clonedFrom);
    void updateCloneBufferInfo() override;
    uint64_t mPreviousFrameNumber = 0;

    void setTransformHint(ui::Transform::RotationFlags displayTransformHint) override;

    // Transform hint provided to the producer. This must be accessed holding
    // the mStateLock.
    ui::Transform::RotationFlags mTransformHint = ui::Transform::ROT_0;

    bool getAutoRefresh() const { return mDrawingState.autoRefresh; }
    bool getSidebandStreamChanged() const { return mSidebandStreamChanged; }

    // Returns true if the next buffer should be presented at the expected present time
    bool shouldPresentNow(nsecs_t expectedPresentTime) const;

    // Returns true if the next buffer should be presented at the expected present time,
    // overridden by BufferStateLayer and BufferQueueLayer for implementation
    // specific logic
    virtual bool isBufferDue(nsecs_t /*expectedPresentTime*/) const = 0;

    std::atomic<bool> mSidebandStreamChanged{false};

    // See IConsumerListener::onConfigurationChanged
    virtual void latchBufferConsumerFlags(){};

    // Returns true if this layer must be updated even if no new frames were
    // explicitly queued.
    virtual bool shouldAutoRefresh() const { return getAutoRefresh(); }

private:
    virtual bool fenceHasSignaled() const = 0;
    virtual bool framePresentTimeIsCurrent(nsecs_t expectedPresentTime) const = 0;

    // Latch sideband stream and returns true if the dirty region should be updated.
    virtual bool latchSidebandStream(bool& recomputeVisibleRegions) = 0;

    virtual bool hasFrameUpdate() const = 0;

    virtual status_t updateTexImage(bool& recomputeVisibleRegions, nsecs_t latchTime,
                                    nsecs_t expectedPresentTime) = 0;

    virtual status_t updateActiveBuffer() = 0;
    virtual status_t updateFrameNumber() = 0;

    // We generate InputWindowHandles for all non-cursor buffered layers regardless of whether they
    // have an InputChannel. This is to enable the InputDispatcher to do PID based occlusion
    // detection.
    bool needsInputInfo() const override { return !mPotentialCursor; }

    // Returns true if this layer requires filtering
    bool needsFiltering(const DisplayDevice*) const override;
    bool needsFilteringForScreenshots(const DisplayDevice*,
                                      const ui::Transform& inverseParentTransform) const override;

    // BufferStateLayers can return Rect::INVALID_RECT if the layer does not have a display frame
    // and its parent layer is not bounded
    Rect getBufferSize(const State& s) const override;

    PixelFormat getPixelFormat() const;

    // Computes the transform matrix using the setFilteringEnabled to determine whether the
    // transform matrix should be computed for use with bilinear filtering.
    void getDrawingTransformMatrix(bool filteringEnabled, float outMatrix[16]);

    std::unique_ptr<compositionengine::LayerFECompositionState> mCompositionState;

    FloatRect computeSourceBounds(const FloatRect& parentBounds) const override;

    void onConfigurationChanged() override;
    std::atomic<bool> mConfigurationChanged{false};
};

} // namespace android
