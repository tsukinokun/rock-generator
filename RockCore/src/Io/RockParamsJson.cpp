//----------------------------------------------------------------------------
//! @file   RockParamsJson.cpp
//! @brief  RockParams の JSON 保存と読み込みの実装
//----------------------------------------------------------------------------
#include <RockCore/Io/RockParamsJson.hpp>

#include <RockCore/Math/Vec.hpp>
#include <RockCore/Noise/Fbm.hpp>
#include <RockCore/Noise/NoiseStack.hpp>

#include <cereal/archives/json.hpp>
#include <cereal/types/vector.hpp>

#include <fstream>
#include <sstream>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    // cereal のシリアライズ関数。
    //
    // 各構造体のメンバ関数ではなく、この .cpp に閉じた自由関数として
    // 定義している。こうすると RockParams.hpp が cereal に依存せず、
    // RockCore の他のコードが cereal のヘッダを引きずり込まない
    // （ADL で見つかるので cereal 側は自由関数でも同じように扱う）。
    //------------------------------------------------------------------------

    //------------------------------------------------------------------------
    //! Vec3 を直列化します。
    //! @tparam Archive     アーカイブの型
    //! @param  [in,out] ar アーカイブ
    //! @param  [in,out] v  対象のベクトル
    //------------------------------------------------------------------------
    template <class Archive>
    void serialize(Archive& ar, Vec3& v) {
        ar(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z));
    }

    //------------------------------------------------------------------------
    //! FbmParams を直列化します。
    //! @tparam Archive         アーカイブの型
    //! @param  [in,out] ar     アーカイブ
    //! @param  [in,out] params 対象のパラメータ
    //------------------------------------------------------------------------
    template <class Archive>
    void serialize(Archive& ar, FbmParams& params) {
        ar(cereal::make_nvp("frequency", params.frequency),
           cereal::make_nvp("octaves", params.octaves),
           cereal::make_nvp("lacunarity", params.lacunarity),
           cereal::make_nvp("gain", params.gain));
    }

    //------------------------------------------------------------------------
    //! NoiseLayerParams を直列化します。
    //! @tparam Archive         アーカイブの型
    //! @param  [in,out] ar     アーカイブ
    //! @param  [in,out] params 対象のパラメータ
    //------------------------------------------------------------------------
    template <class Archive>
    void serialize(Archive& ar, NoiseLayerParams& params) {
        ar(cereal::make_nvp("enabled", params.enabled),
           cereal::make_nvp("kind", params.kind),
           cereal::make_nvp("amplitude", params.amplitude),
           cereal::make_nvp("sharpness", params.sharpness),
           cereal::make_nvp("fbm", params.fbm));
    }

    //------------------------------------------------------------------------
    //! RockParams を直列化します。
    //! @tparam Archive         アーカイブの型
    //! @param  [in,out] ar     アーカイブ
    //! @param  [in,out] params 対象のパラメータ
    //------------------------------------------------------------------------
    template <class Archive>
    void serialize(Archive& ar, RockParams& params) {
        ar(cereal::make_nvp("seed", params.seed),
           cereal::make_nvp("radius", params.radius),
           cereal::make_nvp("anisoScale", params.anisoScale),
           cereal::make_nvp("targetEdgeLength", params.targetEdgeLength),
           cereal::make_nvp("baseSubdivision", params.baseSubdivision),
           cereal::make_nvp("noiseLayers", params.noiseLayers),
           cereal::make_nvp("planeCutCount", params.planeCutCount),
           cereal::make_nvp("planeCutDepth", params.planeCutDepth),
           cereal::make_nvp("planeAxisBias", params.planeAxisBias),
           cereal::make_nvp("edgeRounding", params.edgeRounding),
           cereal::make_nvp("creaseAngleDeg", params.creaseAngleDeg),
           cereal::make_nvp("unwrapMethod", params.unwrapMethod),
           cereal::make_nvp("texelsPerUnit", params.texelsPerUnit),
           cereal::make_nvp("uvPadding", params.uvPadding),
           cereal::make_nvp("textureSize", params.textureSize),
           cereal::make_nvp("dilatePasses", params.dilatePasses),
           cereal::make_nvp("bakeMaxFrequency", params.bakeMaxFrequency),
           cereal::make_nvp("baseColor", params.baseColor),
           cereal::make_nvp("roughness", params.roughness),
           cereal::make_nvp("metallic", params.metallic),
           cereal::make_nvp("aoRayCount", params.aoRayCount),
           cereal::make_nvp("aoDistance", params.aoDistance));
    }

    //------------------------------------------------------------------------
    //! パラメータを JSON 文字列へ書き出します。
    //------------------------------------------------------------------------
    std::string SaveRockParamsToString(const RockParams& params) {
        std::ostringstream stream;
        {
            // JSONOutputArchive は破棄時に閉じ括弧を書くため、
            // stream.str() より先にスコープを閉じる必要がある
            cereal::JSONOutputArchive archive(stream);
            archive(cereal::make_nvp("rockParams", params));
        }
        return stream.str();
    }

    //------------------------------------------------------------------------
    //! JSON 文字列からパラメータを読み込みます。
    //------------------------------------------------------------------------
    bool LoadRockParamsFromString(RockParams& outParams, const std::string& json, std::string* outError) {
        try {
            // 失敗したときに呼び出し側を半端な状態にしないよう、一時物へ読む
            RockParams loaded{};

            std::istringstream         stream(json);
            cereal::JSONInputArchive   archive(stream);
            archive(cereal::make_nvp("rockParams", loaded));

            outParams = loaded;
            return true;
        } catch(const std::exception& error) {
            if(outError) {
                *outError = error.what();
            }
            return false;
        }
    }

    //------------------------------------------------------------------------
    //! パラメータを JSON ファイルへ保存します。
    //------------------------------------------------------------------------
    bool SaveRockParamsToFile(const RockParams& params, const std::string& filePath, std::string* outError) {
        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if(!file) {
            if(outError) {
                *outError = "Failed to open the file for writing: " + filePath;
            }
            return false;
        }

        const std::string json = SaveRockParamsToString(params);
        file.write(json.data(), static_cast<std::streamsize>(json.size()));

        if(!file) {
            if(outError) {
                *outError = "Failed to write the file: " + filePath;
            }
            return false;
        }
        return true;
    }

    //------------------------------------------------------------------------
    //! JSON ファイルからパラメータを読み込みます。
    //------------------------------------------------------------------------
    bool LoadRockParamsFromFile(RockParams& outParams, const std::string& filePath, std::string* outError) {
        std::ifstream file(filePath, std::ios::binary);
        if(!file) {
            if(outError) {
                *outError = "Failed to open the file for reading: " + filePath;
            }
            return false;
        }

        std::ostringstream contents;
        contents << file.rdbuf();

        return LoadRockParamsFromString(outParams, contents.str(), outError);
    }

}    // namespace RockCore
