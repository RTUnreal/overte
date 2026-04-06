#include "Image.h"
#include "ImageLogging.h"
#include "TextureProcessing.h"

#include <basisu/encoder/basisu_comp.h>

using namespace image;

Image::Image(int width, int height, Format format) : _dims(width, height), _format(format) {
    if (_format == Format_RGBAF) {
        _floatData.resize(width * height);
    } else {
        _packedData = QImage(width, height, (QImage::Format)format);
    }
}

size_t Image::getByteCount() const {
    if (_format == Format_RGBAF) {
        return sizeof(FloatPixels::value_type) * _floatData.size();
    } else {
        return _packedData.sizeInBytes();
    }
}

size_t Image::getBytesPerLineCount() const {
    if (_format == Format_RGBAF) {
        return sizeof(FloatPixels::value_type) * _dims.x;
    } else {
        return _packedData.bytesPerLine();
    }
}

glm::uint8* Image::editScanLine(int y) {
    if (_format == Format_RGBAF) {
        return reinterpret_cast<glm::uint8*>(_floatData.data() + y * _dims.x);
    } else {
        return _packedData.scanLine(y);
    }
}

const glm::uint8* Image::getScanLine(int y) const {
    if (_format == Format_RGBAF) {
        return reinterpret_cast<const glm::uint8*>(_floatData.data() + y * _dims.x);
    } else {
        return _packedData.scanLine(y);
    }
}

glm::uint8* Image::editBits() {
    if (_format == Format_RGBAF) {
        return reinterpret_cast<glm::uint8*>(_floatData.data());
    } else {
        return _packedData.bits();
    }
}

const glm::uint8* Image::getBits() const {
    if (_format == Format_RGBAF) {
        return reinterpret_cast<const glm::uint8*>(_floatData.data());
    } else {
        return _packedData.bits();
    }
}

std::vector<image::rust::Pixel> Image::getPixels() const {
    Q_ASSERT(_format == Format_RGBAF || _format == Format_PACKED_FLOAT);

    basisu::imagef src_img;
    std::vector<image::rust::Pixel> fpixels;
    fpixels.reserve(getWidth() * getHeight());

    if (_format == Format_RGBAF) {
        for (const auto& el : _floatData) {
            fpixels.emplace_back(el[0], el[1], el[2], el[3]);
        }
    } else {
        // Start by converting to full float
        auto unpackFunc = getHDRUnpackingFunction();
        for (glm::uint32 lineNb = 0; lineNb < getHeight(); lineNb++) {
            const glm::uint32* srcPixelIt = reinterpret_cast<const glm::uint32*>(getScanLine((int)lineNb));
            const glm::uint32* srcPixelEnd = srcPixelIt + getWidth();

            while (srcPixelIt < srcPixelEnd) {
                auto out = unpackFunc(*srcPixelIt);
                fpixels.emplace_back(out[0], out[1], out[2], 1.0f);
            }
        }
    }
    return fpixels;
}

Image Image::getScaled(glm::uvec2 dstSize, AspectRatioMode ratioMode, TransformationMode transformMode) const {
    if (_format == Format_PACKED_FLOAT || _format == Format_RGBAF) {
        std::vector<image::rust::Pixel> fpixels = getPixels();

        auto out = image::rust::scale_float_image(fpixels, getWidth(), getHeight(), dstSize.x, dstSize.y,
                                                  transformMode == Qt::TransformationMode::FastTransformation
                                                      ? image::rust::ResizeFilter::Fast
                                                      : image::rust::ResizeFilter::Smooth);

        if (_format == Format_RGBAF) {
            Image output(dstSize.x, dstSize.y, Image::Format_RGBAF);

            std::transform(out.cbegin(), out.cend(), output._floatData.begin(),
                           [](auto p) { return glm::vec4(p.red, p.green, p.blue, p.alpha); });

            return output;
        } else {
            // And convert back to original format
            QImage resizedImage((int)dstSize.x, (int)dstSize.y, (QImage::Format)Image::Format_PACKED_FLOAT);

            auto packFunc = getHDRPackingFunction();
            auto outIt = out.cbegin();
            for (glm::uint32 lineNb = 0; lineNb < dstSize.y; lineNb++) {
                glm::uint32* dstPixelIt = reinterpret_cast<glm::uint32*>(resizedImage.scanLine((int)lineNb));
                auto outItEnd = outIt + dstSize.x;

                std::transform(outIt, outItEnd, dstPixelIt,
                               [&](auto p) { return packFunc(glm::vec3(p.red, p.green, p.blue)); });
                outIt = outItEnd;
            }
            return resizedImage;
        }
    } else {
        return _packedData.scaled(fromGlm(dstSize), ratioMode, transformMode);
    }
}

Image Image::getConvertedToFormat(Format newFormat) const {
    const float MAX_COLOR_VALUE = 255.0f;

    if (newFormat == _format) {
        return *this;
    } else if ((_format != Format_R11G11B10F && _format != Format_RGBAF) &&
               (newFormat != Format_R11G11B10F && newFormat != Format_RGBAF)) {
        return _packedData.convertToFormat((QImage::Format)newFormat);
    } else if (_format == Format_PACKED_FLOAT) {
        Image newImage(_dims.x, _dims.y, newFormat);

        switch (newFormat) {
            case Format_RGBAF:
                convertToFloatFromPacked(getBits(), _dims.x, _dims.y, getBytesPerLineCount(), gpu::Element::COLOR_R11G11B10,
                                         newImage._floatData.data(), _dims.x);
                break;

            default: {
                auto unpackFunc = getHDRUnpackingFunction();
                const glm::uint32* srcIt = reinterpret_cast<const glm::uint32*>(getBits());

                for (int y = 0; y < _dims.y; y++) {
                    for (int x = 0; x < _dims.x; x++) {
                        auto color = glm::clamp(unpackFunc(*srcIt) * MAX_COLOR_VALUE, 0.0f, 255.0f);
                        newImage.setPackedPixel(x, y, qRgb(color.r, color.g, color.b));
                        srcIt++;
                    }
                }
                break;
            }
        }
        return newImage;
    } else if (_format == Format_RGBAF) {
        Image newImage(_dims.x, _dims.y, newFormat);

        switch (newFormat) {
            case Format_R11G11B10F:
                convertToPackedFromFloat(newImage.editBits(), _dims.x, _dims.y, getBytesPerLineCount(),
                                         gpu::Element::COLOR_R11G11B10, _floatData.data(), _dims.x);
                break;

            default: {
                FloatPixels::const_iterator srcIt = _floatData.begin();

                for (int y = 0; y < _dims.y; y++) {
                    for (int x = 0; x < _dims.x; x++) {
                        auto color = glm::clamp((*srcIt) * MAX_COLOR_VALUE, 0.0f, 255.0f);
                        newImage.setPackedPixel(x, y, qRgba(color.r, color.g, color.b, color.a));
                        srcIt++;
                    }
                }
                break;
            }
        }
        return newImage;
    } else {
        Image newImage(_dims.x, _dims.y, newFormat);
        assert(newImage.hasFloatFormat());

        if (newFormat == Format_RGBAF) {
            FloatPixels::iterator dstIt = newImage._floatData.begin();

            for (int y = 0; y < _dims.y; y++) {
                auto line = (const QRgb*)getScanLine(y);
                for (int x = 0; x < _dims.x; x++) {
                    QRgb pixel = line[x];
                    *dstIt = glm::vec4(qRed(pixel), qGreen(pixel), qBlue(pixel), qAlpha(pixel)) / MAX_COLOR_VALUE;
                    dstIt++;
                }
            }
        } else {
            auto packFunc = getHDRPackingFunction();
            glm::uint32* dstIt = reinterpret_cast<glm::uint32*>(newImage.editBits());

            for (int y = 0; y < _dims.y; y++) {
                auto line = (const QRgb*)getScanLine(y);
                for (int x = 0; x < _dims.x; x++) {
                    QRgb pixel = line[x];
                    *dstIt = packFunc(glm::vec3(qRed(pixel), qGreen(pixel), qBlue(pixel)) / MAX_COLOR_VALUE);
                    dstIt++;
                }
            }
        }
        return newImage;
    }
}

void Image::invertPixels() {
    assert(_format != Format_PACKED_FLOAT && _format != Format_RGBAF);
    _packedData.invertPixels(QImage::InvertRgba);
}

Image Image::getSubImage(QRect rect) const {
    assert(_format != Format_RGBAF);
    return _packedData.copy(rect);
}

Image Image::getMirrored(bool horizontal, bool vertical) const {
    assert(_format != Format_RGBAF);
    return _packedData.mirrored(horizontal, vertical);
}
