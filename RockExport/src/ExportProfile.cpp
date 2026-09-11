//----------------------------------------------------------------------------
//! @file   ExportProfile.cpp
//! @brief  出力形式ごとの約束事の実装
//----------------------------------------------------------------------------
#include <RockExport/ExportProfile.hpp>

// 名前空間 RockExport
namespace RockExport {

    //------------------------------------------------------------------------
    //! 形式に対応する約束事を返します。
    //------------------------------------------------------------------------
    ExportProfile GetExportProfile(ExportFormat format) {
        ExportProfile profile{};
        profile.embedTextures = true;

        switch(format) {
        case ExportFormat::Glb:
            // glTF 経路では aiProcess_FlipUVs が往復で相殺される（Phase 0 で実測）
            profile.flipV = false;
            break;

        case ExportFormat::Fbx:
            // FBX 経路では V の反転が残るので、書く前に反転しておく
            profile.flipV = true;
            break;
        }

        return profile;
    }

}    // namespace RockExport
