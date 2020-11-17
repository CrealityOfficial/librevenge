/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * This file is part of the libcdr project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CDRCollector.h"

#include <math.h>
#include <stack>
#include <string.h>

#ifdef CRD_COLOR
#include <lcms2.h>
#endif

#include "CDRColorProfiles.h"
#include "libcdr_utils.h"

libcdr::CDRParserState::CDRParserState()
  : m_bmps(), m_patterns(), m_vects(), m_pages(), m_documentPalette(), m_texts(),
    m_styles(), m_fillStyles(), m_lineStyles()
#ifdef CRD_COLOR
    , m_colorTransformCMYK2RGB(nullptr), m_colorTransformLab2RGB(nullptr), m_colorTransformRGB2RGB(nullptr)
#endif
{
#ifdef CRD_COLOR
  cmsHPROFILE tmpRGBProfile = cmsCreate_sRGBProfile();
  m_colorTransformRGB2RGB = cmsCreateTransform(tmpRGBProfile, TYPE_RGB_8, tmpRGBProfile, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
  cmsHPROFILE tmpCMYKProfile = cmsOpenProfileFromMem(CMYK_icc, sizeof(CMYK_icc)/sizeof(CMYK_icc[0]));
  m_colorTransformCMYK2RGB = cmsCreateTransform(tmpCMYKProfile, TYPE_CMYK_DBL, tmpRGBProfile, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
  cmsHPROFILE tmpLabProfile = cmsCreateLab4Profile(nullptr);
  m_colorTransformLab2RGB = cmsCreateTransform(tmpLabProfile, TYPE_Lab_DBL, tmpRGBProfile, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
  cmsCloseProfile(tmpLabProfile);
  cmsCloseProfile(tmpCMYKProfile);
  cmsCloseProfile(tmpRGBProfile);
#endif
}

libcdr::CDRParserState::~CDRParserState()
{
#ifdef CRD_COLOR
  if (m_colorTransformCMYK2RGB)
    cmsDeleteTransform(m_colorTransformCMYK2RGB);
  if (m_colorTransformLab2RGB)
    cmsDeleteTransform(m_colorTransformLab2RGB);
  if (m_colorTransformRGB2RGB)
    cmsDeleteTransform(m_colorTransformRGB2RGB);
#endif
}

void libcdr::CDRParserState::setColorTransform(const std::vector<unsigned char> &profile)
{
#ifdef CRD_COLOR
  if (profile.empty())
    return;
  cmsHPROFILE tmpProfile = cmsOpenProfileFromMem(&profile[0], cmsUInt32Number(profile.size()));
  if (!tmpProfile)
    return;
  cmsHPROFILE tmpRGBProfile = cmsCreate_sRGBProfile();
  cmsColorSpaceSignature signature = cmsGetColorSpace(tmpProfile);
  switch (signature)
  {
  case cmsSigCmykData:
  {
    if (m_colorTransformCMYK2RGB)
      cmsDeleteTransform(m_colorTransformCMYK2RGB);
    m_colorTransformCMYK2RGB = cmsCreateTransform(tmpProfile, TYPE_CMYK_DBL, tmpRGBProfile, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
  }
  break;
  case cmsSigRgbData:
  {
    if (m_colorTransformRGB2RGB)
      cmsDeleteTransform(m_colorTransformRGB2RGB);
    m_colorTransformRGB2RGB = cmsCreateTransform(tmpProfile, TYPE_RGB_8, tmpRGBProfile, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
  }
  break;
  default:
    break;
  }
  cmsCloseProfile(tmpProfile);
  cmsCloseProfile(tmpRGBProfile);
#endif
}

void libcdr::CDRParserState::setColorTransform(librevenge::RVNGInputStream *input)
{
  if (!input)
    return;
  unsigned long numBytesRead = 0;
  const unsigned char *tmpProfile = input->read((unsigned long)-1, numBytesRead);
  if (!numBytesRead)
    return;
  std::vector<unsigned char> profile(numBytesRead);
  memcpy(&profile[0], tmpProfile, numBytesRead);
  setColorTransform(profile);
}

unsigned libcdr::CDRParserState::getBMPColor(const CDRColor &color)
{
  switch (color.m_colorModel)
  {
  case 0:
    return _getRGBColor(libcdr::CDRColor(0, color.m_colorValue));
  case 1:
    return _getRGBColor(libcdr::CDRColor(5, color.m_colorValue));
  case 2:
    return _getRGBColor(libcdr::CDRColor(4, color.m_colorValue));
  case 3:
    return _getRGBColor(libcdr::CDRColor(3, color.m_colorValue));
  case 4:
    return _getRGBColor(libcdr::CDRColor(6, color.m_colorValue));
  case 5:
    return _getRGBColor(libcdr::CDRColor(9, color.m_colorValue));
  case 6:
    return _getRGBColor(libcdr::CDRColor(8, color.m_colorValue));
  case 7:
    return _getRGBColor(libcdr::CDRColor(7, color.m_colorValue));
  case 8:
    return color.m_colorValue;
  case 9:
    return color.m_colorValue;
  case 10:
    return _getRGBColor(libcdr::CDRColor(5, color.m_colorValue));
  case 11:
    return _getRGBColor(libcdr::CDRColor(18, color.m_colorValue));
  default:
    return color.m_colorValue;
  }
}

unsigned libcdr::CDRParserState::_getRGBColor(const CDRColor &color)
{
  unsigned char red = 0;
  unsigned char green = 0;
  unsigned char blue = 0;
  unsigned short colorModel(color.m_colorModel);
  unsigned colorValue(color.m_colorValue);

#ifdef CRD_COLOR
  if (colorModel == 0x19) // Spot colour not handled in the parser
  {
    unsigned short colourIndex = colorValue & 0xffff;
    std::map<unsigned, CDRColor>::const_iterator iter = m_documentPalette.find(colourIndex);
    if (iter != m_documentPalette.end())
    {
      colorModel = iter->second.m_colorModel;
      colorValue = iter->second.m_colorValue;
    }
    // todo handle tint
  }
  unsigned char col0 = colorValue & 0xff;
  unsigned char col1 = (colorValue & 0xff00) >> 8;
  unsigned char col2 = (colorValue & 0xff0000) >> 16;
  unsigned char col3 = (colorValue & 0xff000000) >> 24;
  switch (colorModel)
  {
  case 0x00: // Pantone palette in CDR1
  {
    static const unsigned char WaldoColorType0_R[] =
    {
      0x00, 0xff, 0xde, 0xa1, 0xc5, 0x7d, 0x0c, 0x00, 0x00, 0x08, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xe5, 0xdc, 0xba, 0xa6, 0x82, 0xaf, 0xa9, 0x85, 0x78, 0x60, 0x44, 0xcf,
      0xca, 0xbe, 0xb0, 0x91, 0xaa, 0x91, 0x75, 0x5b, 0x4d, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xc5, 0xa4, 0x6a, 0xff, 0xff, 0xff, 0xff, 0xd9, 0xa2,
      0x8e, 0xff, 0xff, 0xff, 0xff, 0xc2, 0xa1, 0x73, 0xff, 0xff, 0xff, 0xff, 0xc3, 0x9a, 0x84, 0xff,
      0xff, 0xff, 0xff, 0xc8, 0x84, 0x52, 0xff, 0xff, 0xff, 0xff, 0xce, 0x93, 0x5a, 0xff, 0xff, 0xf2,
      0xcb, 0xc5, 0x90, 0x5b, 0xff, 0xff, 0xff, 0xff, 0xcb, 0x95, 0x7f, 0xff, 0xff, 0xf5, 0xed, 0xb2,
      0x7a, 0x4d, 0xff, 0xff, 0xff, 0xe6, 0xc2, 0x9b, 0x43, 0xff, 0xff, 0xff, 0xff, 0xb8, 0x72, 0x48,
      0xff, 0xff, 0xff, 0xc6, 0x8e, 0x58, 0xff, 0xff, 0xec, 0xcc, 0xa3, 0x74, 0x49, 0xff, 0xff, 0xdf,
      0xd6, 0x9a, 0x61, 0x44, 0xff, 0xeb, 0xca, 0xaa, 0x8d, 0x71, 0x59, 0xff, 0xe9, 0xd5, 0xaf, 0x7c,
      0x53, 0x43, 0xff, 0xe7, 0xdb, 0xb8, 0xa1, 0x67, 0x44, 0xff, 0xda, 0xbc, 0x7c, 0x65, 0x40, 0xff,
      0xe8, 0xd3, 0xb8, 0x7d, 0x57, 0x3c, 0xff, 0xe4, 0xd0, 0xa6, 0x6c, 0x4a, 0xff, 0xef, 0xc6, 0xab,
      0x80, 0x68, 0x3f, 0xff, 0xde, 0xd0, 0x8d, 0x7c, 0x66, 0x44, 0xf9, 0xe4, 0xab, 0x63, 0x4e, 0x37,
      0xe6, 0xd2, 0x91, 0x64, 0x45, 0x3b, 0x35, 0xda, 0xb1, 0x6f, 0x48, 0x36, 0x2f, 0x21, 0xc2, 0xab,
      0x8c, 0x3e, 0x25, 0x22, 0x1c, 0xc2, 0xa8, 0x7c, 0x06, 0x00, 0x0a, 0xc9, 0xa7, 0x5f, 0x25, 0x00,
      0x00, 0x00, 0xc4, 0x94, 0x75, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x6c, 0x40, 0x0f, 0x00, 0x00, 0x00,
      0xcc, 0x7f, 0x56, 0x00, 0x00, 0x00, 0x9e, 0x72, 0x2e, 0x00, 0x00, 0x00, 0x00, 0xdb, 0xc7, 0x72,
      0x00, 0x00, 0x00, 0x00, 0xb7, 0x89, 0x6b, 0x17, 0x10, 0x00, 0x16, 0xcc, 0xc0, 0x74, 0x17, 0x15,
      0x12, 0xa1, 0x82, 0x59, 0x2a, 0x1b, 0x24, 0x11, 0xc8, 0xbc, 0x8d, 0x3b, 0x30, 0x2a, 0x2d, 0xc8,
      0xbc, 0x92, 0x2b, 0x31, 0x2a, 0x27, 0xd6, 0xb7, 0x8f, 0x5e, 0x4a, 0x46, 0x3f, 0xe5, 0xda, 0xc0,
      0x89, 0x76, 0x62, 0x45, 0xf6, 0xe8, 0xcc, 0xb0, 0x98, 0x7f, 0x51, 0xf7, 0xe6, 0xd1, 0xc4, 0xac,
      0x98, 0x6e, 0xf9, 0xf4, 0xed, 0xec, 0xbb, 0x9f, 0x74, 0xfd, 0xf6, 0xf1, 0xe9, 0xbf, 0xa1, 0x8e,
      0xd3, 0xb2, 0x96, 0x82, 0x5e, 0x33, 0xd6, 0xb5, 0xaa, 0x7c, 0x54, 0x43, 0x11, 0xc8, 0xac, 0x89,
      0x65, 0x4b, 0x39, 0x13, 0xc8, 0xbc, 0x8d, 0x7f, 0x5d, 0x3a, 0x07, 0xcc, 0xb2, 0x8f, 0x78, 0x51,
      0x28, 0x0f, 0xcc, 0xc3, 0x9a, 0x78, 0x3f, 0x26, 0x21, 0xd4, 0xbd, 0x8b, 0x69, 0x47, 0x2e, 0x22,
      0x3a, 0x4f, 0x59, 0xa0, 0xad, 0xc8, 0xdd, 0x54, 0x7f, 0xa8, 0xe2, 0xea, 0xf7, 0xf7, 0x59, 0x6d,
      0x83, 0xc2, 0xd6, 0xdb, 0xea, 0x4d, 0x9f, 0xc2, 0xe9, 0xe6, 0xf0, 0xf3, 0x3b, 0x5a, 0x68, 0xa5,
      0xb1, 0xcc, 0xd6, 0x4f, 0x91, 0xc8, 0xdb, 0xef, 0xeb, 0xf5, 0x4e, 0x60, 0x70, 0xc5, 0xe8, 0xef,
      0xf4, 0x3f, 0x59, 0x72, 0xc7, 0xd5, 0xe4, 0xf0, 0x42, 0x53, 0x60, 0xbc, 0xd2, 0xe9, 0xec, 0x48,
      0x68, 0x80, 0xc2, 0xd3, 0xeb, 0xf7, 0x3b, 0x55, 0x62, 0xa6, 0xbc, 0xd5, 0xe7, 0x3c, 0x4e, 0x5b,
      0x94, 0xba, 0xd2, 0xe2, 0x21, 0x2d, 0x3e, 0x8b, 0xad, 0xb9, 0xcc, 0x18, 0x0f, 0x0f, 0x65, 0x8c,
      0xa9, 0xc3, 0x00, 0x09, 0x1f, 0x59, 0x81, 0x9f, 0xc3, 0x19, 0x18, 0x21, 0x69, 0x95, 0xa5, 0xb7,
      0x1d, 0x27, 0x39, 0x74, 0x92, 0xc4, 0xe1, 0x1b, 0x16, 0x2b, 0x67, 0x93, 0xb6, 0xd1, 0x39, 0x4e,
      0x56, 0xa6, 0xc8, 0xd5, 0xdd, 0x5a, 0x90, 0xa5, 0xd0, 0xdd, 0xe7, 0xe9, 0xff, 0xff, 0xff, 0xff,
      0xce, 0x7c, 0x46, 0xff, 0xff, 0xf5, 0xf7, 0xc2, 0x86, 0x57, 0xff, 0xff, 0xff, 0xe6, 0xc2, 0x7d,
      0x4d, 0xff, 0xff, 0xff, 0xde, 0xc6, 0x78, 0x4f, 0xca, 0xbc, 0x80, 0x59, 0x4c, 0x38, 0x2f, 0xbf,
      0xae, 0x8a, 0x47, 0x3d, 0x35, 0x28, 0xcf, 0xa8, 0x6c, 0x51, 0x20, 0x1e, 0x9c, 0x8e, 0x6c, 0x33,
      0x26, 0x1d, 0x17, 0xbc, 0x7c, 0x56, 0x21, 0x13, 0x00, 0x00, 0x9e, 0x72, 0x00, 0x0d, 0x00, 0x00,
      0x00, 0xae, 0x7e, 0x50, 0x00, 0x00, 0x00, 0x00, 0xac, 0x7c, 0x5e, 0x00, 0x0a, 0x0c, 0x0d, 0x86,
      0x70, 0x29, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x7c, 0x5c, 0x34, 0x30, 0x24, 0x11, 0xff, 0xfb, 0xf8,
      0xe9, 0xab, 0x85, 0x59, 0x4f, 0x77, 0x8a, 0xbb, 0xbf, 0xca, 0xd6, 0x45, 0x7e, 0xa1, 0xbe, 0xc9,
      0xc9, 0xd3, 0x4d, 0x6a, 0x92, 0xae, 0xbc, 0xc8, 0xd1, 0x43, 0x71, 0x92, 0xc6, 0xd5, 0xdb, 0xdd,
      0x3f, 0x63, 0x8c, 0xba, 0xc0, 0xce, 0xd9, 0x2d, 0x44, 0x69, 0x90, 0xac, 0xc2, 0xd7, 0x1e, 0x3a,
      0x42, 0x73, 0x9b, 0xb7, 0xcc, 0x06, 0x1b, 0x4d, 0x75, 0x9a, 0xb0, 0xc3, 0x00, 0x1a, 0x51, 0x89,
      0xa2, 0xbc, 0xd1, 0x00, 0x18, 0x37, 0x77, 0x94, 0xad, 0xc8, 0x28, 0x4c, 0x6a, 0x7b, 0xa1, 0xaf,
      0xc1, 0x17, 0x3a, 0x57, 0x90, 0xac, 0xc0, 0xd6, 0x34, 0x4a, 0x5d, 0x9e, 0xb1, 0xc9, 0xd8, 0x3f,
      0x5d, 0x7e, 0xb1, 0xc8, 0xd5, 0xdd, 0x49, 0x71, 0xa5, 0xc4, 0xca, 0xd3, 0xda,
    };

    static const unsigned char WaldoColorType0_G[] =
    {
      0x00, 0xee, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x75, 0xa3, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xde, 0xd9, 0xad, 0x96, 0x78, 0x9b, 0x98, 0x7a, 0x6e, 0x58, 0x3c, 0xc9,
      0xc6, 0xb5, 0xa6, 0x8e, 0x9b, 0x8d, 0x74, 0x5a, 0x4d, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xff, 0xf6, 0xfb, 0xf4, 0xb0, 0x93, 0x60, 0xf3, 0xf4, 0xef, 0xed, 0xbd, 0x8b,
      0x7e, 0xee, 0xef, 0xea, 0xd9, 0xa1, 0x85, 0x69, 0xed, 0xee, 0xe2, 0xd2, 0x98, 0x7f, 0x72, 0xe9,
      0xea, 0xe0, 0xc7, 0x99, 0x6d, 0x4d, 0xe0, 0xd6, 0xc5, 0xac, 0x8b, 0x6c, 0x4e, 0xd5, 0xce, 0xb5,
      0x83, 0x7e, 0x6f, 0x51, 0xd7, 0xca, 0xa7, 0x98, 0x7d, 0x65, 0x58, 0xe4, 0xd4, 0x9a, 0x7f, 0x58,
      0x47, 0x35, 0xcb, 0xb0, 0x93, 0x75, 0x62, 0x54, 0x29, 0xc1, 0x9e, 0x85, 0x6d, 0x58, 0x3d, 0x29,
      0xbd, 0x8e, 0x70, 0x42, 0x38, 0x2a, 0xc5, 0x8e, 0x76, 0x30, 0x22, 0x10, 0x10, 0xb2, 0x98, 0x59,
      0x26, 0x24, 0x13, 0x18, 0xc3, 0xaa, 0x35, 0x17, 0x00, 0x18, 0x1d, 0xc0, 0x7d, 0x56, 0x07, 0x15,
      0x0b, 0x10, 0xa8, 0x6d, 0x33, 0x10, 0x18, 0x15, 0x09, 0xce, 0x74, 0x1a, 0x00, 0x00, 0x00, 0x7a,
      0x5d, 0x31, 0x00, 0x00, 0x00, 0x10, 0xba, 0x6c, 0x34, 0x00, 0x00, 0x00, 0xa1, 0x60, 0x3c, 0x00,
      0x00, 0x00, 0x00, 0xd0, 0x95, 0x67, 0x00, 0x00, 0x00, 0x00, 0xd5, 0x93, 0x35, 0x00, 0x00, 0x00,
      0xb9, 0x9b, 0x45, 0x0a, 0x0f, 0x17, 0x1d, 0xc4, 0x8a, 0x2b, 0x12, 0x05, 0x0a, 0x05, 0xa6, 0x89,
      0x6f, 0x1d, 0x07, 0x0c, 0x0d, 0xc9, 0xab, 0x89, 0x10, 0x0e, 0x0c, 0xd5, 0xb8, 0x74, 0x3f, 0x17,
      0x00, 0x0e, 0xd5, 0xb0, 0x92, 0x00, 0x2a, 0x28, 0x1c, 0xb0, 0x97, 0x7f, 0x59, 0x40, 0x2f, 0x29,
      0xea, 0xcd, 0xa9, 0x67, 0x42, 0x2f, 0xd7, 0xb7, 0x8a, 0x72, 0x62, 0x44, 0x29, 0xff, 0xf5, 0xd6,
      0x96, 0x78, 0x6b, 0x4b, 0xdf, 0xd0, 0xb9, 0x84, 0x5f, 0x4f, 0x37, 0xf5, 0xf0, 0xe2, 0x85, 0x6e,
      0x58, 0xe8, 0xce, 0xaa, 0x91, 0x6d, 0x5c, 0x41, 0xee, 0xf4, 0xee, 0xb2, 0x95, 0x6f, 0x4a, 0xf6,
      0xff, 0xf5, 0xcb, 0x8b, 0x67, 0x47, 0xf0, 0xe3, 0xd4, 0xa2, 0x7d, 0x6c, 0x5c, 0xf4, 0xf1, 0xe7,
      0xca, 0xa6, 0x80, 0x52, 0xff, 0xff, 0xe7, 0xde, 0xbd, 0x93, 0x57, 0xf6, 0xf1, 0xe0, 0xd6, 0xae,
      0x96, 0x6b, 0xfa, 0xfa, 0xfa, 0xff, 0xbf, 0x99, 0x73, 0xf3, 0xf9, 0xf2, 0xf2, 0xba, 0x9b, 0x86,
      0xc9, 0xa3, 0x8d, 0x7a, 0x58, 0x31, 0xca, 0xa6, 0x9c, 0x73, 0x4a, 0x3e, 0x0f, 0xc0, 0xa3, 0x86,
      0x67, 0x4d, 0x3b, 0x13, 0xc3, 0xb7, 0x8b, 0x7d, 0x5b, 0x3a, 0x0b, 0xc7, 0xac, 0x8f, 0x78, 0x55,
      0x2e, 0x10, 0xb0, 0xa4, 0x7b, 0x60, 0x2d, 0x1a, 0x1b, 0xd2, 0xbb, 0x8e, 0x6a, 0x48, 0x2e, 0x24,
      0x31, 0x46, 0x4e, 0x8f, 0x9d, 0xba, 0xd8, 0x4b, 0x70, 0x8f, 0xd3, 0xe1, 0xef, 0xee, 0x45, 0x4f,
      0x5a, 0xa0, 0xba, 0xc5, 0xd9, 0x35, 0x61, 0x6d, 0xa8, 0xb4, 0xcf, 0xd5, 0x29, 0x3d, 0x3e, 0x7f,
      0x92, 0xab, 0xc4, 0x30, 0x41, 0x3f, 0x92, 0xab, 0xb2, 0xcf, 0x2b, 0x27, 0x22, 0x70, 0x9b, 0xb5,
      0xca, 0x29, 0x33, 0x3c, 0x80, 0x98, 0xb1, 0xcd, 0x1f, 0x21, 0x29, 0x6c, 0x83, 0xa6, 0xb8, 0x26,
      0x2f, 0x2e, 0x6a, 0x8b, 0xa5, 0xc7, 0x1d, 0x29, 0x29, 0x72, 0x8d, 0xaa, 0xc7, 0x20, 0x20, 0x1c,
      0x62, 0x86, 0xa2, 0xc5, 0x1e, 0x23, 0x37, 0x80, 0x9e, 0xaf, 0xc8, 0x23, 0x2d, 0x33, 0x7b, 0x9b,
      0xb1, 0xcd, 0x1b, 0x31, 0x47, 0x7a, 0x9c, 0xab, 0xcf, 0x2b, 0x3e, 0x4e, 0x86, 0xa6, 0xb1, 0xc5,
      0x30, 0x50, 0x6c, 0xa1, 0xb4, 0xdd, 0xf6, 0x38, 0x4f, 0x6c, 0xa6, 0xc7, 0xd9, 0xe8, 0x41, 0x62,
      0x77, 0xbe, 0xd8, 0xde, 0xe4, 0x56, 0x91, 0xae, 0xda, 0xe3, 0xec, 0xef, 0xd1, 0xcd, 0xb6, 0x9c,
      0x85, 0x5a, 0x34, 0xcc, 0xa9, 0x85, 0x78, 0x63, 0x4e, 0x31, 0xb6, 0xa5, 0x83, 0x60, 0x56, 0x3b,
      0x27, 0xa7, 0x88, 0x63, 0x2c, 0x2d, 0x19, 0x18, 0x61, 0x39, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x6b,
      0x3d, 0x20, 0x00, 0x00, 0x00, 0x00, 0x99, 0x4e, 0x0f, 0x00, 0x00, 0x12, 0x79, 0x74, 0x22, 0x00,
      0x00, 0x00, 0x00, 0xd5, 0xac, 0x9a, 0x76, 0x52, 0x39, 0x25, 0xd7, 0xb4, 0x92, 0x76, 0x68, 0x47,
      0x35, 0xe9, 0xe3, 0xc3, 0x94, 0x78, 0x51, 0x32, 0xdf, 0xd0, 0xb9, 0x91, 0x71, 0x5b, 0x37, 0xd6,
      0xcd, 0xa8, 0x8a, 0x6e, 0x57, 0x2e, 0xe9, 0xe5, 0xd4, 0xa5, 0x80, 0x5c, 0x38, 0xff, 0xfe, 0xfa,
      0xec, 0xa2, 0x81, 0x56, 0x42, 0x69, 0x7c, 0xab, 0xb0, 0xc4, 0xd1, 0x2a, 0x59, 0x7a, 0x96, 0xa9,
      0xac, 0xba, 0x2b, 0x3e, 0x6e, 0x84, 0x95, 0xac, 0xb7, 0x2d, 0x43, 0x60, 0x94, 0xa4, 0xc2, 0xcc,
      0x18, 0x2e, 0x56, 0x89, 0x9a, 0xb1, 0xc8, 0x1b, 0x29, 0x48, 0x72, 0x8d, 0xaa, 0xc7, 0x00, 0x20,
      0x1c, 0x5c, 0x84, 0xa4, 0xc3, 0x1c, 0x33, 0x5f, 0x80, 0x9e, 0xaf, 0xc8, 0x25, 0x47, 0x74, 0x9f,
      0xb4, 0xca, 0xdc, 0x1b, 0x39, 0x53, 0x87, 0xa0, 0xb4, 0xd0, 0x38, 0x64, 0x83, 0x8f, 0xad, 0xb9,
      0xcc, 0x27, 0x50, 0x69, 0x9d, 0xb1, 0xcc, 0xde, 0x3d, 0x57, 0x6c, 0xa1, 0xb0, 0xcb, 0xd9, 0x45,
      0x66, 0x87, 0xb2, 0xcc, 0xd9, 0xde, 0x4a, 0x74, 0xa0, 0xc0, 0xc7, 0xd0, 0xd7,
    };

    static const unsigned char WaldoColorType0_B[] =
    {
      0x00, 0x00, 0x16, 0x6a, 0x8e, 0x89, 0x87, 0xad, 0x6e, 0x02, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xc5, 0xc7, 0xa4, 0x8d, 0x72, 0x8f, 0x8d, 0x74, 0x6b, 0x57, 0x3e, 0xb5,
      0xba, 0xb2, 0xa6, 0x92, 0x98, 0x90, 0x7e, 0x68, 0x5c, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xff, 0x81, 0x6b, 0x00, 0x0a, 0x00, 0x00, 0x7a, 0x6b, 0x5e, 0x2f, 0x11, 0x00,
      0x00, 0x7a, 0x6a, 0x57, 0x00, 0x00, 0x0d, 0x0f, 0x7a, 0x76, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x7a,
      0x78, 0x1a, 0x00, 0x00, 0x00, 0x05, 0x72, 0x5f, 0x1a, 0x00, 0x00, 0x00, 0x09, 0x61, 0x4c, 0x2b,
      0x00, 0x00, 0x00, 0x01, 0x88, 0x74, 0x19, 0x00, 0x00, 0x00, 0x00, 0xa3, 0x83, 0x4c, 0x2e, 0x2a,
      0x02, 0x06, 0x8b, 0x73, 0x51, 0x00, 0x00, 0x05, 0x0d, 0x98, 0x7c, 0x5d, 0x0d, 0x02, 0x07, 0x04,
      0xab, 0x83, 0x61, 0x30, 0x25, 0x23, 0xba, 0x9e, 0x8e, 0x4f, 0x3a, 0x19, 0x10, 0xb1, 0xa9, 0x78,
      0x54, 0x47, 0x33, 0x2e, 0xbd, 0xb1, 0x6a, 0x43, 0x34, 0x39, 0x1f, 0xc3, 0xa2, 0x86, 0x55, 0x49,
      0x34, 0x30, 0xb9, 0x9c, 0x81, 0x63, 0x5b, 0x44, 0x30, 0xdf, 0xb1, 0x88, 0x56, 0x49, 0x31, 0xc5,
      0xac, 0x98, 0x81, 0x5e, 0x46, 0x36, 0xd4, 0xb2, 0x9e, 0x7b, 0x59, 0x40, 0xd8, 0xc6, 0xa5, 0x89,
      0x71, 0x5d, 0x3c, 0xed, 0xc5, 0xc2, 0x8a, 0x7c, 0x6e, 0x4d, 0xee, 0xff, 0xbe, 0x74, 0x58, 0x47,
      0xd3, 0xc6, 0xa5, 0x7f, 0x56, 0x4e, 0x4a, 0xde, 0xd0, 0xb1, 0x8a, 0x70, 0x5e, 0x4d, 0xcc, 0xc4,
      0xba, 0x88, 0x65, 0x57, 0x45, 0xdc, 0xdd, 0xc4, 0x6a, 0x4f, 0x44, 0xf0, 0xdf, 0xe2, 0xb6, 0x69,
      0x67, 0x44, 0xe5, 0xdc, 0xc7, 0xb4, 0x7f, 0x6c, 0x48, 0xc9, 0xb8, 0xb3, 0xa0, 0x85, 0x5d, 0x4d,
      0xe4, 0xe1, 0xc7, 0x95, 0x61, 0x4e, 0xd2, 0xc1, 0xa1, 0x91, 0x81, 0x5f, 0x3c, 0xe4, 0xdf, 0xc1,
      0x96, 0x7d, 0x71, 0x55, 0xc2, 0xb0, 0xa1, 0x79, 0x5e, 0x50, 0x3f, 0xc2, 0xbe, 0xa3, 0x5c, 0x54,
      0x4b, 0xb1, 0x9d, 0x80, 0x67, 0x52, 0x4a, 0x3d, 0xa5, 0xa2, 0x88, 0x53, 0x40, 0x36, 0x1f, 0xa6,
      0xa0, 0x8f, 0x4c, 0x40, 0x39, 0x30, 0x94, 0x6f, 0x4e, 0x23, 0x22, 0x18, 0x12, 0x91, 0x73, 0x52,
      0x03, 0x00, 0x00, 0x0a, 0x74, 0x6c, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x66, 0x57, 0x22, 0x00, 0x00,
      0x00, 0x00, 0x7e, 0x62, 0x37, 0x0d, 0x00, 0x00, 0x00, 0x78, 0x4f, 0x20, 0x00, 0x00, 0x00, 0x03,
      0xaf, 0x90, 0x80, 0x6d, 0x54, 0x2e, 0xb5, 0x9a, 0x97, 0x71, 0x48, 0x44, 0x0a, 0xa6, 0x96, 0x7d,
      0x64, 0x4b, 0x3b, 0x17, 0xbb, 0xb8, 0x92, 0x82, 0x60, 0x48, 0x1c, 0xbb, 0xb0, 0x99, 0x82, 0x67,
      0x48, 0x20, 0xa6, 0x9d, 0x7f, 0x6a, 0x3d, 0x25, 0x22, 0xc6, 0xb8, 0x91, 0x6d, 0x50, 0x3b, 0x22,
      0x16, 0x3a, 0x2d, 0x72, 0x86, 0x95, 0xad, 0x25, 0x19, 0x2f, 0x60, 0x71, 0x83, 0x98, 0x35, 0x2b,
      0x35, 0x75, 0x8a, 0x8e, 0xa3, 0x22, 0x2e, 0x21, 0x72, 0x80, 0x99, 0x9e, 0x2d, 0x3b, 0x3b, 0x6f,
      0x86, 0x97, 0xaf, 0x20, 0x20, 0x31, 0x79, 0x8d, 0x98, 0xb0, 0x33, 0x32, 0x37, 0x7d, 0x9e, 0xb1,
      0xb6, 0x1b, 0x31, 0x38, 0x7e, 0x90, 0xa5, 0xb9, 0x31, 0x38, 0x44, 0x83, 0x90, 0xa9, 0xb8, 0x4a,
      0x6f, 0x83, 0xa9, 0xb4, 0xc2, 0xcf, 0x3e, 0x61, 0x6f, 0x9a, 0xa6, 0xb9, 0xce, 0x56, 0x77, 0x8b,
      0xa8, 0xc1, 0xcd, 0xe1, 0x3c, 0x4e, 0x6b, 0x99, 0xb5, 0xc2, 0xd0, 0x4c, 0x67, 0x7c, 0xab, 0xbc,
      0xc6, 0xdc, 0x39, 0x51, 0x69, 0x96, 0xac, 0xb3, 0xcd, 0x00, 0x00, 0x27, 0x6d, 0x90, 0x9a, 0xaa,
      0x27, 0x46, 0x5d, 0x85, 0x99, 0xbe, 0xd7, 0x2d, 0x3b, 0x55, 0x84, 0x9e, 0xb2, 0xc1, 0x00, 0x00,
      0x00, 0x64, 0x85, 0x87, 0x9a, 0x00, 0x00, 0x00, 0x5b, 0x65, 0x73, 0x8d, 0x70, 0x64, 0x15, 0x00,
      0x00, 0x00, 0x00, 0x8f, 0x63, 0x32, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x7b, 0x4f, 0x00, 0x00, 0x00,
      0x00, 0xa5, 0x8f, 0x75, 0x1a, 0x37, 0x15, 0x18, 0xff, 0xff, 0xbe, 0x89, 0x74, 0x58, 0x4e, 0xff,
      0xff, 0xdb, 0x80, 0x70, 0x63, 0x52, 0xff, 0xff, 0xc0, 0xa0, 0x5f, 0x4c, 0xcc, 0xcc, 0xcf, 0x8e,
      0x6f, 0x57, 0x45, 0xda, 0xd1, 0xc9, 0xc1, 0x8a, 0x5d, 0x40, 0xca, 0xb6, 0xa1, 0x8b, 0x7c, 0x5b,
      0x48, 0xc3, 0xb7, 0x9e, 0x7c, 0x6c, 0x4f, 0x38, 0xb7, 0xa6, 0x96, 0x76, 0x60, 0x51, 0x32, 0xb0,
      0xa6, 0x89, 0x6e, 0x5c, 0x49, 0x26, 0xa7, 0x95, 0x81, 0x62, 0x57, 0x44, 0x23, 0x78, 0x4f, 0x20,
      0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x3e, 0x7e, 0x84, 0x9b, 0xa9, 0x00, 0x43, 0x62, 0x75, 0x8c,
      0x91, 0x9c, 0x32, 0x3e, 0x66, 0x74, 0x80, 0x99, 0x9e, 0x3a, 0x55, 0x6d, 0x9a, 0xa7, 0xb9, 0xc3,
      0x4a, 0x6f, 0x91, 0xad, 0xb3, 0xc2, 0xcd, 0x3e, 0x52, 0x70, 0x8b, 0x9f, 0xb3, 0xc6, 0x4b, 0x77,
      0x8b, 0xac, 0xc1, 0xcd, 0xe1, 0x50, 0x65, 0x85, 0x9c, 0xb8, 0xc5, 0xd4, 0x3e, 0x5b, 0x83, 0xa3,
      0xb4, 0xc2, 0xd0, 0x31, 0x4c, 0x63, 0x8c, 0xa0, 0xb0, 0xc7, 0x40, 0x5c, 0x77, 0x82, 0x9f, 0xa8,
      0xb7, 0x21, 0x4d, 0x63, 0x91, 0xa2, 0xb5, 0xc8, 0x29, 0x35, 0x3d, 0x75, 0x7e, 0xa2, 0xae, 0x11,
      0x00, 0x46, 0x73, 0x91, 0xa2, 0xad, 0x2a, 0x3d, 0x69, 0x7d, 0x85, 0x8f, 0xa3,
    };

    auto pantoneIndex = (unsigned short)(((int)col1 << 8) | (int)col0);
    double pantoneSaturation = (double)(((unsigned short)col3 << 8) | (unsigned short)col2) / 100.0;
    typedef struct
    {
      unsigned char r;
      unsigned char g;
      unsigned char b;
    } RGBColor;
    RGBColor pureColor = { 0, 0, 0 };
    if (pantoneIndex < sizeof(WaldoColorType0_R)/sizeof(WaldoColorType0_R[0])
        && pantoneIndex < sizeof(WaldoColorType0_G)/sizeof(WaldoColorType0_G[0])
        && pantoneIndex < sizeof(WaldoColorType0_B)/sizeof(WaldoColorType0_B[0]))
    {
      pureColor.r = WaldoColorType0_R[pantoneIndex];
      pureColor.g = WaldoColorType0_G[pantoneIndex];
      pureColor.b = WaldoColorType0_B[pantoneIndex];
    }
    auto tmpRed = (unsigned)cdr_round(255.0*(1-pantoneSaturation) + (double)pureColor.r*pantoneSaturation);
    auto tmpGreen = (unsigned)cdr_round(255.0*(1-pantoneSaturation) + (double)pureColor.g*pantoneSaturation);
    auto tmpBlue = (unsigned)cdr_round(255.0*(1-pantoneSaturation) + (double)pureColor.b*pantoneSaturation);
    red = (tmpRed < 255 ? (unsigned char)tmpRed : 255);
    green = (tmpGreen < 255 ? (unsigned char)tmpGreen : 255);
    blue = (tmpBlue < 255 ? (unsigned char)tmpBlue : 255);
    break;
  }
  // CMYK 100
  case 0x01:
  case 0x02:
  case 0x15:
  {
    double cmyk[4] =
    {
      (double)col0,
      (double)col1,
      (double)col2,
      (double)col3
    };
    unsigned char rgb[3] = { 0, 0, 0 };
    cmsDoTransform(m_colorTransformCMYK2RGB, cmyk, rgb, 1);
    red = rgb[0];
    green = rgb[1];
    blue = rgb[2];
    break;
  }
  // CMYK 255
  case 0x03:
  case 0x11:
  {
    double cmyk[4] =
    {
      (double)col0*100.0/255.0,
      (double)col1*100.0/255.0,
      (double)col2*100.0/255.0,
      (double)col3*100.0/255.0
    };
    unsigned char rgb[3] = { 0, 0, 0 };
    cmsDoTransform(m_colorTransformCMYK2RGB, cmyk, rgb, 1);
    red = rgb[0];
    green = rgb[1];
    blue = rgb[2];
    break;
  }
  // CMY
  case 0x04:
  {
    red = 255 - col0;
    green = 255 - col1;
    blue = 255 - col2;
    break;
  }
  // RGB
  case 0x05:
  {
    unsigned char input[3] = { col2, col1, col0 };
    unsigned char output[3] = { 0, 0, 0 };
    cmsDoTransform(m_colorTransformRGB2RGB, input, output, 1);
    red = output[0];
    green = output[1];
    blue = output[2];
    break;
  }
  // HSB
  case 0x06:
  {
    auto hue = (unsigned short)(((int)col1<<8) | col0);
    double saturation = (double)col2/255.0;
    double brightness = (double)col3/255.0;

    while (hue > 360)
      hue -= 360;

    double satRed, satGreen, satBlue;

    if (hue < 120)
    {
      satRed = (double)(120 - hue) / 60.0;
      satGreen = (double)hue / 60.0;
      satBlue = 0;
    }
    else if (hue < 240)
    {
      satRed = 0;
      satGreen = (double)(240 - hue) / 60.0;
      satBlue = (double)(hue - 120) / 60.0;
    }
    else
    {
      satRed = (double)(hue - 240) / 60.0;
      satGreen = 0.0;
      satBlue = (double)(360 - hue) / 60.0;
    }
    red = (unsigned char)cdr_round(255*(1 - saturation + saturation * (satRed > 1 ? 1 : satRed)) * brightness);
    green = (unsigned char)cdr_round(255*(1 - saturation + saturation * (satGreen > 1 ? 1 : satGreen)) * brightness);
    blue = (unsigned char)cdr_round(255*(1 - saturation + saturation * (satBlue > 1 ? 1 : satBlue)) * brightness);
    break;
  }
  // HLS
  case 0x07:
  {
    auto hue = (unsigned short)(((int)col1<<8) | col0);
    double lightness = (double)col2/255.0;
    double saturation = (double)col3/255.0;

    while (hue > 360)
      hue -= 360;

    double satRed, satGreen, satBlue;

    if (hue < 120)
    {
      satRed = (double)(120 - hue) / 60.0;
      satGreen = (double)hue/60.0;
      satBlue = 0.0;
    }
    else if (hue < 240)
    {
      satRed = 0;
      satGreen = (double)(240 - hue) / 60.0;
      satBlue = (double)(hue - 120) / 60.0;
    }
    else
    {
      satRed = (double)(hue - 240) / 60.0;
      satGreen = 0;
      satBlue = (double)(360 - hue) / 60.0;
    }

    double tmpRed = 2*saturation*(satRed > 1 ? 1 : satRed) + 1 - saturation;
    double tmpGreen = 2*saturation*(satGreen > 1 ? 1 : satGreen) + 1 - saturation;
    double tmpBlue = 2*saturation*(satBlue > 1 ? 1 : satBlue) + 1 - saturation;

    if (lightness < 0.5)
    {
      red = (unsigned char)cdr_round(255.0*lightness*tmpRed);
      green = (unsigned char)cdr_round(255.0*lightness*tmpGreen);
      blue = (unsigned char)cdr_round(255.0*lightness*tmpBlue);
    }
    else
    {
      red = (unsigned char)cdr_round(255*((1 - lightness) * tmpRed + 2 * lightness - 1));
      green = (unsigned char)cdr_round(255*((1 - lightness) * tmpGreen + 2 * lightness - 1));
      blue = (unsigned char)cdr_round(255*((1 - lightness) * tmpBlue + 2 * lightness - 1));
    }
    break;
  }
  // BW
  case 0x08:
  {
    red = col0 ? 0 : 0xff;
    green = col0 ? 0 : 0xff;
    blue = col0 ? 0 : 0xff;
    break;
  }
  // Grayscale
  case 0x09:
  {
    red = col0;
    green = col0;
    blue = col0;
    break;
  }
  // YIQ255
  case 0x0b:
  {
    auto y = (double)col0;
    auto i = (double)col1;
    auto q = (double)col2;

    y -= 100.0;
    if (y < 0.0)
      y /= 100.0;
    else
      y /= 155.0;
    y *= 0.5;
    y += 0.5;

    i -= 100.0;
    if (i <= 0.0)
      i /= 100.0;
    else
      i /= 155;
    i *= 0.5957;

    q -= 100.0;
    if (q <= 0)
      q /= 100.0;
    else
      q /= 155;
    q *= 0.5226;

    double RR = y + 0.9563*i + 0.6210*q;
    double GG = y - 0.2127*i - 0.6474*q;
    double BB = y - 1.1070*i + 1.7046*q;
    if (RR > 1.0)
      RR = 1.0;
    if (RR < 0.0)
      RR = 0.0;
    if (GG > 1.0)
      GG = 1.0;
    if (GG < 0.0)
      GG = 0.0;
    if (BB > 1.0)
      BB = 1.0;
    if (BB < 0.0)
      BB = 0.0;
    red = (unsigned char)cdr_round(255*RR);
    green = (unsigned char)cdr_round(255*GG);
    blue = (unsigned char)cdr_round(255*BB);
    break;
  }
  // Lab
  case 0x0c:
  {
    cmsCIELab Lab;
    Lab.L = (double)col0*100.0/255.0;
    Lab.a = (double)(signed char)col1;
    Lab.b = (double)(signed char)col2;
    unsigned char rgb[3] = { 0, 0, 0 };
    cmsDoTransform(m_colorTransformLab2RGB, &Lab, rgb, 1);
    red = rgb[0];
    green = rgb[1];
    blue = rgb[2];
    break;
  }
  // Lab
  case 0x12:
  {
    cmsCIELab Lab;
    Lab.L = (double)col0*100.0/255.0;
    Lab.a = (double)((signed char)(col1 - 0x80));
    Lab.b = (double)((signed char)(col2 - 0x80));
    unsigned char rgb[3] = { 0, 0, 0 };
    cmsDoTransform(m_colorTransformLab2RGB, &Lab, rgb, 1);
    red = rgb[0];
    green = rgb[1];
    blue = rgb[2];
    break;
  }
  // Registration colour
  case 0x14:
  {
    red = (unsigned char)cdr_round(255.0 * col0 / 100.0);
    green = (unsigned char)cdr_round(255.0 * col0 / 100.0);
    blue = (unsigned char)cdr_round(255.0 * col0 / 100.0);
    break;
  }

  default:
    break;
  }
#endif
  return (unsigned)((red << 16) | (green << 8) | blue);
}

librevenge::RVNGString libcdr::CDRParserState::getRGBColorString(const libcdr::CDRColor &color)
{
  librevenge::RVNGString tempString;
  tempString.sprintf("#%.6x", _getRGBColor(color));
  return tempString;
}

void libcdr::CDRParserState::getRecursedStyle(CDRStyle &style, unsigned styleId)
{
  std::map<unsigned, CDRStyle>::const_iterator iter = m_styles.find(styleId);
  if (iter == m_styles.end())
    return;

  std::stack<CDRStyle> styleStack;
  styleStack.push(iter->second);
  if (iter->second.m_parentId)
  {
    std::map<unsigned, CDRStyle>::const_iterator iter2 = m_styles.find(iter->second.m_parentId);
    while (iter2 != m_styles.end())
    {
      styleStack.push(iter2->second);
      if (iter2->second.m_parentId)
        iter2 = m_styles.find(iter2->second.m_parentId);
      else
        iter2 = m_styles.end();
    }
  }
  while (!styleStack.empty())
  {
    style.overrideStyle(styleStack.top());
    styleStack.pop();
  }
}

/* vim:set shiftwidth=2 softtabstop=2 expandtab: */