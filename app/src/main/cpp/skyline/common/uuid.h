#include <common.h>

namespace skyline {
    /**
     * @brief Contains an RFC4122 BE format UUID
     */
    struct UUID {
        u128 raw{};

        /**
         * @brief Generates a random version 4 UUID
         */
        static UUID GenerateUuidV4();

        /**
         * @brief Checks if a UUID is an invalid nil UUID
         */
        bool Valid() {
            return raw != 0;
        }

        constexpr bool operator==(const UUID &other) const {
            return raw == other.raw;
        }
    };
}