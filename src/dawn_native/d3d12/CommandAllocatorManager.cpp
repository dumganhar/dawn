// Copyright 2017 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dawn_native/d3d12/CommandAllocatorManager.h"

#include "dawn_native/d3d12/DeviceD3D12.h"

#include "common/Assert.h"
#include "common/BitSetIterator.h"

namespace dawn_native { namespace d3d12 {

    CommandAllocatorManager::CommandAllocatorManager(Device* device)
        : device(device), mAllocatorCount(0) {
        mFreeAllocators.set();
    }

    ComPtr<ID3D12CommandAllocator> CommandAllocatorManager::ReserveCommandAllocator() {
        // If there are no free allocators, get the oldest serial in flight and wait on it
        if (mFreeAllocators.none()) {
            const uint64_t firstSerial = mInFlightCommandAllocators.FirstSerial();
            device->WaitForSerial(firstSerial);
            Tick(firstSerial);
        }

        ASSERT(mFreeAllocators.any());

        // Get the index of the first free allocator from the bitset
        unsigned int firstFreeIndex = *(IterateBitSet(mFreeAllocators).begin());

        if (firstFreeIndex >= mAllocatorCount) {
            ASSERT(firstFreeIndex == mAllocatorCount);
            mAllocatorCount++;
            ASSERT_SUCCESS(device->GetD3D12Device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocators[firstFreeIndex])));
        }

        // Mark the command allocator as used
        mFreeAllocators.reset(firstFreeIndex);

        // Enqueue the command allocator. It will be scheduled for reset after the next
        // ExecuteCommandLists
        mInFlightCommandAllocators.Enqueue({mCommandAllocators[firstFreeIndex], firstFreeIndex},
                                           device->GetPendingCommandSerial());

        return mCommandAllocators[firstFreeIndex];
    }

    void CommandAllocatorManager::Tick(uint64_t lastCompletedSerial) {
        // Reset all command allocators that are no longer in flight
        for (auto it : mInFlightCommandAllocators.IterateUpTo(lastCompletedSerial)) {
            ASSERT_SUCCESS(it.commandAllocator->Reset());
            mFreeAllocators.set(it.index);
        }
        mInFlightCommandAllocators.ClearUpTo(lastCompletedSerial);
    }

}}  // namespace dawn_native::d3d12
