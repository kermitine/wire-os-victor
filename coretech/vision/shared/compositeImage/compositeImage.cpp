/**
* File: compositeImage.cpp
*
* Author: Sam Russell 
* Created: 2/16/2018
* Refactor: 4/23/2019
*
* Description: Defines an image as an aggregate of `SpriteBox`es
*   1) Defines How SpriteBoxes should be rendered to a final image
*   2) Layers are drawn on top of each other in a strict priority order
*
* Copyright: Anki, Inc. 2019
*
**/

#include "coretech/vision/shared/compositeImage/compositeImage.h"
#include "coretech/vision/engine/image.h"

#include "util/cpuProfiler/cpuProfiler.h"
#include "util/logging/logging.h"

#include <algorithm>
#include <cmath>

#define LOG_CHANNEL "CompositeImage"

namespace Anki {
namespace Vision {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CompositeImage::~CompositeImage()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CompositeImage::MergeInImage(const CompositeImage& otherImage)
{
  for(const auto& layerIter : otherImage._layerMap){
    const auto& layer = layerIter.second;
    for(const auto& sprite : layer){
      AddImage(sprite.spriteBox, sprite.spriteHandle);
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CompositeImage::ClearLayerByName(LayerName name)
{
  const auto numRemoved = _layerMap.erase(name);
  if(numRemoved == 0){
    LOG_WARNING("CompositeImage.ClearLayerByName.LayerNotFound",
                "Layer %s not found in composite image",
                LayerNameToString(name));
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CompositeImage::AddImage(const SpriteBox& spriteBox, const SpriteHandle& spriteHandle)
{
  const auto& iter = _spriteBoxMap.find(spriteBox.name);
  if(_spriteBoxMap.end() == iter){
    // New, unique SpriteBox. Add layer and add to layer as appropriate
    const auto& layerEmplacePair = _layerMap.emplace(spriteBox.layer, ImageLayer());
    auto& layer = layerEmplacePair.first->second;
    layer.emplace_back(spriteBox, spriteHandle);
  } else {
    // Duplicate SpriteBox. Notify and replace.
    LOG_WARNING("CompositeImage.AddImage.SpriteBoxOverwritten",
                "SpriteBoxes must be unique within a CompositeImage. Overwriting prior SpriteBox named: %s",
                EnumToString(spriteBox.name));
    (*_spriteBoxMap[spriteBox.name]).spriteBox = spriteBox;
    (*_spriteBoxMap[spriteBox.name]).spriteHandle = spriteHandle;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CompositeImage::DrawIntoImage(ImageRGBA& baseImage, const LayerName firstLayer, const LayerName lastLayer) const
{
  ANKI_CPU_PROFILE("CompositeImage::DrawIntoImage");

  if(firstLayer > lastLayer){
    LOG_ERROR("CompositeImage.DrawIntoImage.InvalidLayers",
              "Requested firstLayer %s is drawn after reqested lastLayer %s",
              EnumToString(firstLayer),
              EnumToString(lastLayer));
    return;
  }

  // The face display is opaque. Start with an opaque black canvas and use
  // source-over compositing for every sprite placed on it.
  baseImage.FillWith(Vision::PixelRGBA());

  for(const auto& layerPair : _layerMap){
    if(layerPair.first < firstLayer){
      continue;
    }
    if(layerPair.first > lastLayer){
      break;
    }
    for(const auto& sprite : layerPair.second){
      DrawSpriteIntoImage(sprite, baseImage);
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CompositeImage::DrawSpriteIntoImage(const Sprite& sprite, ImageRGBA& baseImage) const
{
  Point2f topCorner = {static_cast<float>(sprite.spriteBox.xPos), static_cast<float>(sprite.spriteBox.yPos)};

  switch(sprite.spriteBox.renderMethod){
    case SpriteRenderMethod::RGBA:
    {
      // Check to see if the RGBA image is cached
      if(sprite.spriteHandle->IsContentCached().rgba){
        const ImageRGBA& spriteImage = sprite.spriteHandle->GetCachedSpriteContentsRGBA();
        DrawSpriteImage(spriteImage, baseImage, topCorner, sprite.spriteBox.alpha);
      }else{
        const ImageRGBA& spriteImage = sprite.spriteHandle->GetSpriteContentsRGBA();
        DrawSpriteImage(spriteImage, baseImage, topCorner, sprite.spriteBox.alpha);
      }
      break;
    }
    case SpriteRenderMethod::EyeColor:
    {
      Vision::HueSatWrapper::ImageSize imageSize(static_cast<uint32_t>(sprite.spriteBox.height),
                                                 static_cast<uint32_t>(sprite.spriteBox.width));
      std::shared_ptr<Vision::HueSatWrapper> hsImageHandle;
      
      if(nullptr == _faceHSImageHandle){
        LOG_ERROR("CompositeImage.DrawSpriteIntoImage.ShouldRenderInEyeHueButCant",
                  "HS Image handle missing - image will be renderd with 0,0 hue saturation");
      }
      
        // TODO: Kevin K. Copy is happening here due to way we can resize image handles with cached data
        // do something better
      auto hue = _faceHSImageHandle->GetHue();
      auto sat = _faceHSImageHandle->GetSaturation();
      hsImageHandle = std::make_shared<Vision::HueSatWrapper>(hue,
                                                              sat,
                                                              imageSize);
      
      // Render the sprite - use the cached RGBA image if possible
      if(sprite.spriteHandle->IsContentCached(hsImageHandle).rgba){
        const ImageRGBA& spriteImage = sprite.spriteHandle->GetCachedSpriteContentsRGBA(hsImageHandle);
        DrawSpriteImage(spriteImage, baseImage, topCorner, sprite.spriteBox.alpha);
      }else{
        const ImageRGBA& spriteImage = sprite.spriteHandle->GetSpriteContentsRGBA(hsImageHandle);
        DrawSpriteImage(spriteImage, baseImage, topCorner, sprite.spriteBox.alpha);
      }
      break;
    }
    default:
    {
      LOG_ERROR("CompositeImage.DrawSpriteIntoImage.InvalidRenderMethod",
                "Sprite Box %s does not have a valid render method",
                SpriteBoxNameToString(sprite.spriteBox.name));
      break;
    }
  } // end switch
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CompositeImage::DrawSpriteImage(const ImageRGBA& spriteImage,
                                     ImageRGBA& baseImage,
                                     const Point2f& topCorner,
                                     float spriteAlpha) const
{
  const float spriteAlphaScale = std::max(0.0f, std::min(100.0f, spriteAlpha)) / 100.0f;
  if(spriteAlphaScale <= 0.0f) {
    return;
  }

  const s32 destLeft = std::max(0, static_cast<s32>(topCorner.x()));
  const s32 destTop = std::max(0, static_cast<s32>(topCorner.y()));
  const s32 destRight = std::min(baseImage.GetNumCols(),
                                 static_cast<s32>(topCorner.x()) + spriteImage.GetNumCols());
  const s32 destBottom = std::min(baseImage.GetNumRows(),
                                  static_cast<s32>(topCorner.y()) + spriteImage.GetNumRows());
  const s32 srcLeft = destLeft - static_cast<s32>(topCorner.x());
  const s32 srcTop = destTop - static_cast<s32>(topCorner.y());

  for(s32 y = destTop; y < destBottom; ++y) {
    const PixelRGBA* src = spriteImage.GetRow(srcTop + y - destTop) + srcLeft;
    PixelRGBA* dst = baseImage.GetRow(y) + destLeft;

    for(s32 x = destLeft; x < destRight; ++x, ++src, ++dst) {
      const u32 alpha = static_cast<u32>(std::round(src->a() * spriteAlphaScale));
      if(alpha == 0) {
        continue;
      }
      if(alpha >= 255) {
        dst->r() = src->r();
        dst->g() = src->g();
        dst->b() = src->b();
        dst->a() = 255;
        continue;
      }

      const u32 invAlpha = 255 - alpha;
      dst->r() = static_cast<u8>((src->r() * alpha + dst->r() * invAlpha + 127) / 255);
      dst->g() = static_cast<u8>((src->g() * alpha + dst->g() * invAlpha + 127) / 255);
      dst->b() = static_cast<u8>((src->b() * alpha + dst->b() * invAlpha + 127) / 255);
      dst->a() = 255;
    }
  }
}

} // namespace Vision
} // namespace Anki
