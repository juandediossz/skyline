// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <common.h>
#include <common/uuid.h>
#include <kernel/types/KEvent.h>
#include <kernel/types/KSharedMemory.h>
#include "results.h"

namespace skyline::service::timesrv {
    struct TimeSpanType {
      private:
        i64 ns{};

      public:
        constexpr TimeSpanType() {}

        constexpr TimeSpanType(i64 ns) : ns(ns) {}

        static constexpr TimeSpanType FromNanoseconds(i64 ns) {
            return {ns};
        }

        static constexpr TimeSpanType FromSeconds(i64 s) {
            return {static_cast<i64>(s * constant::NsInSecond)};
        }

        static constexpr TimeSpanType FromDays(i64 d) {
            return {static_cast<i64>(d * constant::NsInDay)};
        }

        constexpr i64 Nanoseconds() const {
            return ns;
        }

        constexpr i64 Seconds() const {
            return ns / constant::NsInSecond;
        }

        constexpr friend bool operator>(const TimeSpanType &lhs, const TimeSpanType &rhs) {
            return lhs.ns > rhs.ns;
        }

        constexpr friend bool operator<(const TimeSpanType &lhs, const TimeSpanType &rhs) {
            return lhs.ns < rhs.ns;
        }

        constexpr friend TimeSpanType operator+(const TimeSpanType &lhs, const TimeSpanType &rhs) {
            return FromNanoseconds(lhs.ns + rhs.ns);
        }

        constexpr friend TimeSpanType operator-(const TimeSpanType &lhs, const TimeSpanType &rhs) {
            return FromNanoseconds(lhs.ns - rhs.ns);
        }
    };

    struct __attribute__((packed)) SteadyClockTimePoint {
        i64 timePoint{}; //!< Measured in seconds
        UUID clockSourceId{};

        constexpr bool operator==(const SteadyClockTimePoint &other) const = default;
    };
    static_assert(sizeof(SteadyClockTimePoint) == 0x18);

    struct __attribute__((packed)) SystemClockContext {
        SteadyClockTimePoint steadyTimePoint{};
        u64 offset{}; // TODO

        constexpr bool operator==(const SystemClockContext &other) const = default;
    };
    static_assert(sizeof(SystemClockContext) == 0x20);

    namespace core {
        class SteadyClockCore {
            bool rtcResetDetected{};
            bool initialised{};

          public:
            virtual ResultValue<SteadyClockTimePoint> GetTimePoint() = 0;

            virtual TimeSpanType GetRawTimePoint();

            virtual TimeSpanType GetTestOffset() {
                return {};
            }

            virtual void SetTestOffset(TimeSpanType offset) {}

            virtual TimeSpanType GetInternalOffset() {
                return {};
            }

            virtual void SetInternalOffset(TimeSpanType offset) {
                return;
            }

            virtual ResultValue<TimeSpanType> GetRtcValue() {
                return result::Unimplemented;
            }

            virtual Result GetSetupResult() {
                return {};
            }

            ResultValue<SteadyClockTimePoint> GetCurrentTimePoint();
        };

        class StandardSteadyClockCore : public SteadyClockCore {
            std::mutex mutex; //!< Protects accesses to cachedValue
            TimeSpanType testOffset{};
            TimeSpanType internalOffset{};
            TimeSpanType rtcOffset{};
            TimeSpanType cachedValue{};
            UUID id{};

          public:
            ResultValue<SteadyClockTimePoint> GetTimePoint() override;

            TimeSpanType GetRawTimePoint() override;

            TimeSpanType GetTestOffset() override {
                return testOffset;
            }

            void SetTestOffset(TimeSpanType offset) override {
                testOffset = offset;
            }

            TimeSpanType GetInternalOffset() override {
                return internalOffset;
            }

            void SetInternalOffset(TimeSpanType offset) override {
                internalOffset = offset;
            }
        };

        class TickBasedSteadyClockCore : public SteadyClockCore {
          private:
            UUID id{};

          public:
            ResultValue<SteadyClockTimePoint> GetTimePoint() override;
        };

        class SystemClockCore {
          private:
            SteadyClockCore &steadyClock;
            bool initialised{};
            void *contextWriter; // TODO
            SystemClockContext context{};

          public:
            SystemClockCore(SteadyClockCore &steadyClock) : steadyClock(steadyClock) {}

            bool IsClockSetup();

            virtual ResultValue<SystemClockContext> GetClockContext() {
                return context;
            }

            virtual Result SetClockContext(SystemClockContext pContext) {
                context = pContext;
                return {};
            }
        };

        class StandardLocalSystemClockCore : public SystemClockCore {
          public:
            StandardLocalSystemClockCore(SteadyClockCore &steadyClock) : SystemClockCore(steadyClock) {}
        };

        class StandardNetworkSystemClockCore : public SystemClockCore {
          private:
            TimeSpanType sufficientAccuracy{TimeSpanType::FromDays(10)};

          public:
            StandardNetworkSystemClockCore(SteadyClockCore &steadyClock) : SystemClockCore(steadyClock) {}
        };

        class StandardUserSystemClockCore : public SystemClockCore {
          private:
            StandardLocalSystemClockCore &localSystemClock;
            StandardNetworkSystemClockCore &networkSystemClock;
            bool automaticCorrectionEnabled{}; //TODO
            SteadyClockTimePoint automaticCorrectionUpdatedTime;
            std::shared_ptr<kernel::type::KEvent> automaticCorrectionEvent; //TODO

          public:
            StandardUserSystemClockCore(const DeviceState &state, StandardSteadyClockCore &standardSteadyClock, StandardLocalSystemClockCore &localSystemClock, StandardNetworkSystemClockCore &networkSystemClock) : SystemClockCore(standardSteadyClock), localSystemClock(localSystemClock), networkSystemClock(networkSystemClock), automaticCorrectionEvent(std::make_shared<kernel::type::KEvent>(state, false)) {}

            ResultValue<SystemClockContext> GetClockContext() override;

            Result SetClockContext(SystemClockContext pContext) override {
                return result::Unimplemented;
            }
        };

        class EphemeralNetworkSystemClockCore : public SystemClockCore {
          public:
            EphemeralNetworkSystemClockCore(SteadyClockCore &steadyClock) : SystemClockCore(steadyClock) {}
        };

        struct TimeSharedMemoryLayout;

        class TimeSharedMemory {
          private:
            std::shared_ptr<kernel::type::KSharedMemory> kTimeSharedMemory; //!< TODO
            TimeSharedMemoryLayout *timeSharedMemory;

          public:
            TimeSharedMemory(const DeviceState &state);

            void UpdateLocalSystemClockContext(const SystemClockContext &context);

            void UpdateNetworkSystemClockContext(const SystemClockContext &context);
        };

        class SystemClockContextUpdateCallback {
          private:
            std::list<std::shared_ptr<kernel::type::KEvent>> operationEventList;
            std::mutex mutex;
            std::optional<SystemClockContext> context;

          protected:
            /**
             * @brief Updates the base callback context with the one supplied as an argument
             * @return true if the context was updated
             */
            bool UpdateBaseContext(const SystemClockContext &newContext);

            /**
             * @brief Signals all events in the operation event list
             */
            void SignalOperationEvent();

          public:
            virtual Result UpdateContext(const SystemClockContext &newContext) = 0;
        };

        class LocalSystemClockContextWriter : SystemClockContextUpdateCallback {
          private:
            TimeSharedMemory &timeSharedMemory;

          public:
            LocalSystemClockContextWriter(TimeSharedMemory &timeSharedMemory) : timeSharedMemory(timeSharedMemory) {}

            Result UpdateContext(const SystemClockContext &newContext) override;
        };

        class NetworkSystemClockContextWriter : SystemClockContextUpdateCallback {
          private:
            TimeSharedMemory &timeSharedMemory;

          public:
            NetworkSystemClockContextWriter(TimeSharedMemory &timeSharedMemory) : timeSharedMemory(timeSharedMemory) {}

            Result UpdateContext(const SystemClockContext &newContext) override;
        };

        class EphemeralNetworkSystemClockContextWriter : SystemClockContextUpdateCallback {
          public:
            Result UpdateContext(const SystemClockContext &newContext) override;
        };

        class TimeService {
          private:
            StandardSteadyClockCore standardSteadyClock;
            TickBasedSteadyClockCore tickBasedSteadyClock;
            StandardLocalSystemClockCore localSystemClock;
            StandardNetworkSystemClockCore networkSystemClock;
            StandardUserSystemClockCore userSystemClock;
            EphemeralNetworkSystemClockCore empheralNetworkClock;

            TimeSharedMemory timeSharedMemory;

            LocalSystemClockContextWriter localSystemClockContextWriter;
            NetworkSystemClockContextWriter networkSystemClockContextWriter;
            EphemeralNetworkSystemClockContextWriter ephemeralNetworkSystemClockContextWriter;

          public:
            TimeService(const DeviceState &state) : localSystemClock(standardSteadyClock), networkSystemClock(standardSteadyClock), userSystemClock(state, standardSteadyClock, localSystemClock, networkSystemClock), empheralNetworkClock(tickBasedSteadyClock), timeSharedMemory(state), localSystemClockContextWriter(timeSharedMemory), networkSystemClockContextWriter(timeSharedMemory) {}

            // TODO, shmem, alarms, powerrequest, tz
        };
    }
}
