//----------------------------------------------------------------------------
//! @file   RockPresets.cpp
//! @brief  岩のプリセットの実装
//----------------------------------------------------------------------------
#include <RockCore/Shape/RockPresets.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! プリセットの識別子を返します。
    //------------------------------------------------------------------------
    const char* GetRockPresetName(RockPreset preset) {
        switch(preset) {
        case RockPreset::Weathered: return "Weathered";
        case RockPreset::Angular:   return "Angular";
        case RockPreset::Boulder:   return "Boulder";
        case RockPreset::Slab:      return "Slab";
        default:                    return "?";
        }
    }

    //------------------------------------------------------------------------
    //! プリセットのパラメータを作ります。
    //------------------------------------------------------------------------
    RockParams MakeRockPreset(RockPreset preset, u64 seed) {
        // 既定値（＝風化した露岩）から始めて、差分だけを書く。
        // 全項目を4回書き下すと、共通部分を直したときに追従漏れが出る
        RockParams params{};
        params.seed = seed;

        switch(preset) {
        case RockPreset::Weathered:
            // 既定値がそのまま風化した露岩
            break;

        case RockPreset::Angular:
            //----------------------------------------------------------------
            // 割れたて。面を増やし、稜線の丸めをほぼ切る。
            // 割れ目も深く鋭くする
            //----------------------------------------------------------------
            params.planeCutCount  = 9;
            params.planeCutDepth  = 0.48f;
            params.edgeRounding   = 0.02f;
            params.creaseAngleDeg = 25.0f;

            params.noiseLayers = {
                NoiseLayerParams{true, NoiseLayerKind::Fbm, 0.030f, 0.5f, FbmParams{1.6f, 3, 2.0f, 0.45f}},
                NoiseLayerParams{true, NoiseLayerKind::Worley, 0.016f, 0.06f, FbmParams{2.0f, 1, 2.0f, 0.5f}},
                NoiseLayerParams{true, NoiseLayerKind::Fbm, 0.014f, 0.5f, FbmParams{7.5f, 3, 2.0f, 0.45f}},
            };
            break;

        case RockPreset::Boulder:
            //----------------------------------------------------------------
            // 河原の石。破断面をほぼ消し、残った面も大きく丸める。
            // 割れ目は浅く、代わりに低周波のうねりを強くする
            //----------------------------------------------------------------
            params.planeCutCount = 2;
            params.planeCutDepth = 0.14f;
            params.edgeRounding  = 0.40f;
            params.creaseAngleDeg = 55.0f;

            params.noiseLayers = {
                NoiseLayerParams{true, NoiseLayerKind::Fbm, 0.170f, 0.5f, FbmParams{1.2f, 4, 2.0f, 0.45f}},
                NoiseLayerParams{true, NoiseLayerKind::Worley, 0.008f, 0.14f, FbmParams{1.2f, 1, 2.0f, 0.5f}},
                NoiseLayerParams{true, NoiseLayerKind::Fbm, 0.014f, 0.5f, FbmParams{6.0f, 3, 2.0f, 0.45f}},
            };
            break;

        case RockPreset::Slab:
            //----------------------------------------------------------------
            // 板状。平面法線を上下へ寄せて、さらに全体を Y 方向へ潰す。
            // 軸バイアスだけだと「上下が平らな球」になるので、
            // anisoScale と組み合わせる必要がある
            //----------------------------------------------------------------
            params.planeCutCount = 7;
            params.planeCutDepth = 0.34f;
            params.planeAxisBias = 0.55f;
            params.edgeRounding  = 0.05f;
            params.anisoScale    = Vec3{1.0f, 0.32f, 1.0f};

            params.noiseLayers = {
                NoiseLayerParams{true, NoiseLayerKind::Fbm, 0.045f, 0.5f, FbmParams{1.5f, 4, 2.0f, 0.45f}},
                NoiseLayerParams{true, NoiseLayerKind::Ridged, 0.022f, 0.8f, FbmParams{2.8f, 3, 2.1f, 0.5f}},
                NoiseLayerParams{true, NoiseLayerKind::Worley, 0.014f, 0.08f, FbmParams{1.8f, 1, 2.0f, 0.5f}},
            };
            break;

        default:
            break;
        }

        return params;
    }

}    // namespace RockCore
