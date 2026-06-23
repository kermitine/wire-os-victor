/**
* File: spriteWrapper.cpp
*
* Author: Kevin M. Karol
* Created: 4/12/2018
*
* Description: Provides an interface to access a sprite's contents
* regardless of whether it's currently in memory or needs to be read in
*
* Copyright: Anki, Inc. 2018
*
**/


#include "coretech/vision/shared/spriteCache/spriteWrapper.h"

#include "coretech/vision/engine/image.h"
#include "util/cladHelpers/cladEnumToStringMap.h"
#include "util/helpers/templateHelpers.h"
#include "anki/cozmo/shared/cozmoConfig.h"

#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

#include <algorithm>

namespace Anki {
namespace Vision {

namespace {

void MakeBlackTransparent(ImageRGBA& image)
{
  const s32 numRows = image.GetNumRows();
  const s32 numCols = image.GetNumCols();
  for(s32 row = 0; row < numRows; ++row) {
    PixelRGBA* pixels = image.GetRow(row);
    for(s32 col = 0; col < numCols; ++col) {
      PixelRGBA& pixel = pixels[col];
      if(pixel.r() == 0 && pixel.g() == 0 && pixel.b() == 0 && pixel.a() == 255) {
        pixel.a() = 0;
      }
    }
  }
}

void ResizeWithPremultipliedAlpha(ImageRGBA& image, s32 desiredRows, s32 desiredCols)
{
  if(image.GetNumRows() == desiredRows && image.GetNumCols() == desiredCols) {
    return;
  }

  for(s32 row = 0; row < image.GetNumRows(); ++row) {
    PixelRGBA* pixels = image.GetRow(row);
    for(s32 col = 0; col < image.GetNumCols(); ++col) {
      PixelRGBA& pixel = pixels[col];
      const u32 alpha = pixel.a();
      pixel.r() = static_cast<u8>((pixel.r() * alpha + 127) / 255);
      pixel.g() = static_cast<u8>((pixel.g() * alpha + 127) / 255);
      pixel.b() = static_cast<u8>((pixel.b() * alpha + 127) / 255);
    }
  }

  image.Resize(desiredRows, desiredCols, ResizeMethod::Linear);

  for(s32 row = 0; row < image.GetNumRows(); ++row) {
    PixelRGBA* pixels = image.GetRow(row);
    for(s32 col = 0; col < image.GetNumCols(); ++col) {
      PixelRGBA& pixel = pixels[col];
      const u32 alpha = pixel.a();
      if(alpha == 0) {
        pixel.r() = pixel.g() = pixel.b() = 0;
      } else {
        pixel.r() = static_cast<u8>(std::min<u32>(255, (pixel.r() * 255 + alpha / 2) / alpha));
        pixel.g() = static_cast<u8>(std::min<u32>(255, (pixel.g() * 255 + alpha / 2) / alpha));
        pixel.b() = static_cast<u8>(std::min<u32>(255, (pixel.b() * 255 + alpha / 2) / alpha));
      }
    }
  }
}

} // anonymous namespace


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteWrapper::SpriteWrapper(const std::string& fullSpritePath)
: _fullSpritePath(fullSpritePath)
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteWrapper::SpriteWrapper(ImageRGBA* sprite)
{
  if(sprite != nullptr) {
    MakeBlackTransparent(*sprite);
  }
  _spriteRGBA.reset(sprite);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteWrapper::SpriteWrapper(Image* sprite)
{
  _spriteGrayscale.reset(sprite);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SpriteWrapper::~SpriteWrapper()
{

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ISpriteWrapper::ImgTypeCacheSpec SpriteWrapper::IsContentCached(const HSImageHandle& hsImage) const
{
  ISpriteWrapper::ImgTypeCacheSpec whatsCached;

  if(_spriteGrayscale != nullptr){
    whatsCached.grayscale = true;
  }

  if(_spriteRGBA != nullptr){
    if(ImageMatchesStoredID(hsImage)){
      whatsCached.rgba = true;
    }
  }

  return whatsCached;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ImageRGBA SpriteWrapper::GetSpriteContentsRGBA(const HSImageHandle& hsImage)
{
  // Return cached RGBA if possible
  if (_spriteRGBA != nullptr) {
    return *_spriteRGBA;
  }

  // Otherwise, see if hue can be applied to cached grayscale image
  if((_spriteGrayscale != nullptr) &&
     (hsImage != nullptr) &&
     (hsImage->GetHSID() != 0)){
    ImageRGBA outImage;
    ApplyHS(*_spriteGrayscale, hsImage, &outImage);
    return outImage;
  }

  // Last resort - load from disk and apply hue/saturation directly
  ImageRGBA outImage;
  LoadSprite(&outImage, hsImage);
  return outImage;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Image SpriteWrapper::GetSpriteContentsGrayscale()
{
  // Try returning cached memory
  if(_spriteGrayscale != nullptr){
    return *_spriteGrayscale;
  }else if(_spriteRGBA != nullptr){
    Image outImage = _spriteRGBA->ToGray();
    return outImage;
  }

  // Otherwise load from disk
  Image outImage;
  LoadSprite(&outImage);
  return outImage;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const ImageRGBA& SpriteWrapper::GetCachedSpriteContentsRGBA(const HSImageHandle& hsImage)
{
  if((_spriteRGBA == nullptr) ||
     !ImageMatchesStoredID(hsImage)){
    PRINT_NAMED_ERROR("SpriteWrapper.GetCachedGetCachedSpriteContents.InvalidContentAccess",
                      "Access to %s was requested as a reference, but sprite is not cached",
                      _fullSpritePath.c_str());

    ISpriteWrapper::ImgTypeCacheSpec typesToCache = {false, true};
    CacheSprite(typesToCache, hsImage);
  }
  return *_spriteRGBA;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const Image& SpriteWrapper::GetCachedSpriteContentsGrayscale()
{
  if(_spriteGrayscale == nullptr){
    PRINT_NAMED_ERROR("SpriteWrapper.GetCachedGetCachedSpriteContents.InvalidContentAccess",
                      "Access to %s was requested as a reference, but sprite is not cached",
                      _fullSpritePath.c_str());
    ISpriteWrapper::ImgTypeCacheSpec typesToCache = {true, false};
    CacheSprite(typesToCache);
  }
  return *_spriteGrayscale;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SpriteWrapper::GetFullSpritePath(std::string& fullSpritePath)
{
  fullSpritePath = _fullSpritePath;
  if(ANKI_DEV_CHEATS && _fullSpritePath.empty()){
    PRINT_NAMED_ERROR("SpriteWrapper.GetFullSpritePath.PathIsEmpty",
                      "The image stored in this wrapper does not reference a sprite saved on disk");
  }
  return !_fullSpritePath.empty();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteWrapper::CacheSprite(const ImgTypeCacheSpec& typesToCache, const HSImageHandle& hsImage)
{
  // Check to see if Grayscale is already cached
  if(typesToCache.grayscale &&
    _spriteGrayscale != nullptr){
    PRINT_NAMED_WARNING("SpriteWrapper.CacheSprite.GrayscaleSpriteAlreadyCached",
                        "CacheSprite called on %s which is already cached",
                        _fullSpritePath.c_str());
  }

  // Check to see if RGBA is already cached
  const bool isRGBACached = (_spriteRGBA != nullptr) && (hsImage->GetHSID() == _hsID);
  if(typesToCache.rgba &&
     isRGBACached){
    PRINT_NAMED_WARNING("SpriteWrapper.CacheSprite.RGBASpriteAlreadyCached",
                        "CacheSprite called on %s which is already cached",
                        _fullSpritePath.c_str());
    return;
  }

  // Cache Grayscale sprite if appropriate
  if(typesToCache.grayscale &&
     _spriteGrayscale == nullptr){
    _spriteGrayscale = std::unique_ptr<Image>(new Image());
    LoadSprite(_spriteGrayscale.get());
  }

  // Cache RGBA sprite if appropritae
  if(typesToCache.rgba &&
     !isRGBACached){
    if((_spriteGrayscale != nullptr) &&
       (hsImage != nullptr) &&
       (hsImage->GetHSID() == 0)){
      ApplyHS(*_spriteGrayscale, hsImage, _spriteRGBA.get());
    }else{
      _spriteRGBA = std::unique_ptr<ImageRGBA>(new ImageRGBA());
      LoadSprite(_spriteRGBA.get(), hsImage);
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteWrapper::ClearCachedSprite()
{
  if((_spriteRGBA == nullptr) &&
     (_spriteGrayscale == nullptr)){
    PRINT_NAMED_WARNING("SpriteWrapper.ClearCachedSprite.NoSpritesCached",
                        "ClearCachedSprite called on %s which is not cached",
                        _fullSpritePath.c_str());
    return;
  }

  if(_fullSpritePath.empty()){
    PRINT_NAMED_WARNING("SpriteWrapper.ClearCachedSprite.NoSprite Path",
                        "ClearCachedSprite called on SpriteWrapper which stores image directly - sprite cannot be recovered");
  }

  _spriteRGBA.reset();
  _spriteGrayscale.reset();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SpriteWrapper::ImageMatchesStoredID(const HSImageHandle& hsImage) const
{
  return (hsImage == nullptr && _hsID == 0) ||
           ((hsImage != nullptr) && (hsImage->GetHSID() == _hsID));
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteWrapper::LoadSprite(Image* outImage) const
{
  if(outImage == nullptr){
    PRINT_NAMED_ERROR("SpriteWrapper.LoadSprite.OutImageIsNull", "");
    return;
  }

  auto res = outImage->Load(_fullSpritePath.c_str());
  ANKI_VERIFY(RESULT_OK == res,
              "CompositeImage.SpriteBoxImpl.Constructor.GrayLoadFailed",
              "Failed to load sprite %s",
              _fullSpritePath.c_str());
  if(Vector::IsXray()) {
    outImage->Resize(Vector::FACE_DISPLAY_HEIGHT, Vector::FACE_DISPLAY_WIDTH);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteWrapper::LoadSprite(ImageRGBA* outImage, const HSImageHandle& hsImage) const
{
  if(_fullSpritePath.empty()){
    PRINT_NAMED_ERROR("SpriteWrapper.LoadSprite.NoPathToLoadFrom", "");
    return;
  }

  if(outImage == nullptr){
    PRINT_NAMED_ERROR("SpriteWrapper.LoadSprite.OutImageIsNull", "");
    return;
  }

  if((hsImage != nullptr) &&
     hsImage->GetHSID() != 0){
    // Load the image as a grayscale image and merge it with a hue image
    Image grayImg;
    grayImg.Load(_fullSpritePath.c_str());
    outImage->Allocate(grayImg.GetNumRows(), grayImg.GetNumCols());
    ApplyHS(grayImg, hsImage, outImage);
  }else{
    // Preserve a PNG's alpha channel when present. Legacy three-channel
    // sprites use exact black as their transparent color.
    cv::Mat showableImage;
    try {
      showableImage = cv::imread(_fullSpritePath.c_str(), cv::IMREAD_UNCHANGED);
    }
    catch(const cv::Exception& e) {
      PRINT_NAMED_ERROR("CompositeImage.SpriteBoxImpl.Constructor.ColorLoadFailed",
                        "Failed to load sprite %s: %s",
                        _fullSpritePath.c_str(), e.what());
      return;
    }

    if(showableImage.empty() ||
       (showableImage.channels() != 1 && showableImage.channels() != 3 && showableImage.channels() != 4)) {
      PRINT_NAMED_ERROR("CompositeImage.SpriteBoxImpl.Constructor.ColorLoadFailed",
                        "Sprite %s must contain one, three, or four channels",
                        _fullSpritePath.c_str());
      return;
    }

    if(showableImage.channels() == 1) {
      cv::Mat showableRGBA;
      cv::cvtColor(showableImage, showableRGBA, cv::COLOR_GRAY2BGRA);
      outImage->SetFromShowableFormat(showableRGBA);
    } else {
      outImage->SetFromShowableFormat(showableImage);
    }
    MakeBlackTransparent(*outImage);
  }
  if(Vector::IsXray()) {
    ResizeWithPremultipliedAlpha(*outImage,
                                 outImage->GetNumRows() * 80 / 96,
                                 outImage->GetNumCols() * 160 / 184);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SpriteWrapper::ApplyHS(const Image& grayImg, const HSImageHandle& hsImage, ImageRGBA* outImg) const
{
  if(outImg == nullptr){
    PRINT_NAMED_WARNING("SpriteWrapper.ApplyHS.OutImgIsNull",
                        "Cannot pass in nullptr");
    return;
  }

  const bool dimensionsMatch = (outImg->GetNumRows() == grayImg.GetNumRows()) &&
                               (outImg->GetNumCols() == grayImg.GetNumCols());
  if(!dimensionsMatch){
    PRINT_NAMED_WARNING("SpriteWrapper.ApplyHS.SizeMismatch",
                        "Existing dimensions (%d,%d) did not match grayscale dimensions (%d,%d)",
                        outImg->GetNumCols(), outImg->GetNumRows(),
                        grayImg.GetNumCols(),  grayImg.GetNumRows());
    return;
  }

  Vision::HueSatWrapper::ImageSize imageSize(static_cast<uint32_t>(grayImg.GetNumRows()),
                                             static_cast<uint32_t>(grayImg.GetNumCols()));
  Vision::Image* hueImage = nullptr;
  Vision::Image* saturationImage = nullptr;
  bool memoryAllocated = false;


  if(hsImage == nullptr){
    PRINT_NAMED_ERROR("SpriteWrapper.ApplyHS.HSImageNull",
                      "Cannot apply null HS image to grayImg");
    outImg->SetFromGray(grayImg);
    return;
  }else if(hsImage->AreImagesCached(imageSize)){
    hsImage->GetCachedImages(hueImage, saturationImage, imageSize);
  }else{
    hsImage->AllocateImages(hueImage, saturationImage, imageSize);
    memoryAllocated = true;
  }

  // Create an HSV image from the gray image, replacing the 'hue' channel
  // with the specified value
  const std::vector<cv::Mat> channels {
    hueImage->get_CvMat_(),
    saturationImage->get_CvMat_(),
    grayImg.get_CvMat_()
  };
  ImageRGB imageHSV;
  cv::merge(channels, imageHSV.get_CvMat_());
  ImageRGB565 im565;
  imageHSV.ConvertHSV2RGB565(im565);

  outImg->SetFromRGB565(im565);
  MakeBlackTransparent(*outImg);

  if(memoryAllocated){
    Util::SafeDelete(hueImage);
    Util::SafeDelete(saturationImage);
  }
}


} // namespace Vision
} // namespace Anki
