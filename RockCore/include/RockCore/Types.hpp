//----------------------------------------------------------------------------
//! @file   Types.hpp
//! @brief  RockCore 共通の整数型エイリアス
//! @detail エンジンの Tsukino/Core/typedef.hpp と同じ綴りの別名を定義します。
//!         RockCore はエンジンへ依存しないため、同じものを自前で持ちます。
//----------------------------------------------------------------------------
#pragma once
#include <cstdint>

// 名前空間 RockCore
namespace RockCore {

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

}    // namespace RockCore
