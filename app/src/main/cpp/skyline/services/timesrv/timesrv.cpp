// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "timesrv.h"

namespace skyline::service::timesrv::core {
    struct __attribute__((packed)) TimeSharedMemoryLayout {
        struct SystemClockContextEntry {
            u32 updateCount;
            u32 _pad_;
            std::array<SystemClockContext, 2> context;
        };

        u8 _tmp_[0x38];
        SystemClockContextEntry localSystemClockContextEntry;
        SystemClockContextEntry networkSystemClockContextEntry;
        struct __attribute__((packed)) {
            u32 updateCount;
            u8 automaticCorrectionEnabled;
        } automaticCorrectionEnabledEntry;
    };
    static_assert(offsetof(TimeSharedMemoryLayout, networkSystemClockContextEntry) == (0x80));
    static_assert(offsetof(TimeSharedMemoryLayout, automaticCorrectionEnabledEntry) == (0xC8));

    template<typename T>
    static void UpdateTimeSharedMemoryItem(u32 &updateCount, std::array<T, 2> &item, const T &newValue) {
        u32 newCount{updateCount + 1};
        item.at(newCount & 1) = newValue;
        asm volatile("DMB ISHST");
        updateCount = newCount;
    }

    TimeSpanType SteadyClockCore::GetRawTimePoint() {
        auto timePoint{GetTimePoint()};

        if (timePoint)
            return TimeSpanType::FromSeconds(timePoint->timePoint);
        else
            throw exception("Error reading timepoint");
    }

    ResultValue<SteadyClockTimePoint> SteadyClockCore::GetCurrentTimePoint() {
        auto timePoint{GetTimePoint()};
        if (timePoint)
            timePoint->timePoint += (GetTestOffset() + GetInternalOffset()).Seconds();

        return timePoint;
    }


    ResultValue<SteadyClockTimePoint> StandardSteadyClockCore::GetTimePoint() {
        SteadyClockTimePoint timePoint{
            .timePoint = GetRawTimePoint().Seconds(),
            .clockSourceId = id,
        };

        return timePoint;
    }

    TimeSpanType StandardSteadyClockCore::GetRawTimePoint() {
        std::lock_guard lock(mutex);

        auto timePoint{TimeSpanType::FromNanoseconds(util::GetTimeNs()) + rtcOffset};

        if (timePoint > cachedValue)
            cachedValue = timePoint;

        return timePoint;
    }

    ResultValue<SteadyClockTimePoint> TickBasedSteadyClockCore::GetTimePoint() {
        SteadyClockTimePoint timePoint{
            .timePoint = TimeSpanType::FromNanoseconds(util::GetTimeNs()).Seconds(),
            .clockSourceId = id,
        };
    
        return timePoint;
    }

    bool SystemClockCore::IsClockSetup() {
        if (GetClockContext()) {
            auto timePoint{steadyClock.GetCurrentTimePoint()};
            if (timePoint)
                return timePoint->clockSourceId.Valid();
        }

        return false;
    }

    ResultValue<SystemClockContext> StandardUserSystemClockCore::GetClockContext() {
        if (automaticCorrectionEnabled && networkSystemClock.IsClockSetup()) {
            auto ctx{networkSystemClock.GetClockContext()};
            if (!ctx)
                return ctx;

            auto ret{localSystemClock.SetClockContext(*ctx)};
            if (ret)
                return ret;
        }

        return localSystemClock.GetClockContext();
    }

    namespace constant {
        constexpr size_t TimeSharedMemorySize{0x1000};
    }

    TimeSharedMemory::TimeSharedMemory(const DeviceState &state) : kTimeSharedMemory(std::make_shared<kernel::type::KSharedMemory>(state, constant::TimeSharedMemorySize)), timeSharedMemory(reinterpret_cast<TimeSharedMemoryLayout *>(kTimeSharedMemory->kernel.ptr)) {}

    void TimeSharedMemory::UpdateLocalSystemClockContext(const SystemClockContext &context) {
        UpdateTimeSharedMemoryItem(timeSharedMemory->localSystemClockContextEntry.updateCount, timeSharedMemory->localSystemClockContextEntry.context, context);
    }

    void TimeSharedMemory::UpdateNetworkSystemClockContext(const SystemClockContext &context) {
        UpdateTimeSharedMemoryItem(timeSharedMemory->networkSystemClockContextEntry.updateCount, timeSharedMemory->networkSystemClockContextEntry.context, context);
    }

    bool SystemClockContextUpdateCallback::UpdateBaseContext(const SystemClockContext &newContext) {
        if (context && context == newContext)
            return false;

        context = newContext;
        return true;

    }

    void SystemClockContextUpdateCallback::SignalOperationEvent() {
        std::lock_guard lock(mutex);

        for (const auto &event : operationEventList)
            event->Signal();
    }

    Result LocalSystemClockContextWriter::UpdateContext(const SystemClockContext &newContext) {
        // No need to update shmem state redundantly
        if (!UpdateBaseContext(newContext))
            return {};

        timeSharedMemory.UpdateLocalSystemClockContext(newContext);

        SignalOperationEvent();
    }

    Result NetworkSystemClockContextWriter::UpdateContext(const SystemClockContext &newContext) {
        // No need to update shmem state redundantly
        if (!UpdateBaseContext(newContext))
            return {};

        timeSharedMemory.UpdateNetworkSystemClockContext(newContext);

        SignalOperationEvent();
    }

    Result EphemeralNetworkSystemClockContextWriter::UpdateContext(const SystemClockContext &newContext) {
        // Avoid signalling the event when there is no change in context
        if (!UpdateBaseContext(newContext))
            return {};

        SignalOperationEvent();
    }
}