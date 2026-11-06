#include "image_decode.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PNGdec.h>

// PNG decode callback context
struct PngContext {
    uint8_t *framebuffer;
    int fbWidth;
    int fbHeight;
    int offsetX;
    int offsetY;
};

// Set a single pixel in the 4bpp framebuffer
// EPD47 convention: even x -> low nibble, odd x -> high nibble
// 0x0 = black, 0xF = white
void ImageDecoder::setPixel4bpp(uint8_t *fb, int fbWidth, int x, int y, uint8_t gray4) {
    if (x < 0 || x >= fbWidth || y < 0 || y >= DISPLAY_HEIGHT) return;
    int idx = (y * fbWidth + x) / 2;
    if (x & 1) {
        fb[idx] = (fb[idx] & 0x0F) | ((gray4 & 0x0F) << 4);
    } else {
        fb[idx] = (fb[idx] & 0xF0) | (gray4 & 0x0F);
    }
}

// Read sample number `idx` from a packed PNG scanline with the given bit depth
// (bits per channel: 1, 2, 4, 8 or 16). With `scale`, the value is expanded to
// 0-255; without it the raw value is returned (used for palette indices).
// 16-bit samples are big-endian, so the first byte is the high byte.
static inline uint8_t pngSample(const uint8_t *line, int idx, int depth, bool scale) {
    switch (depth) {
        case 1: {
            uint8_t v = (line[idx >> 3] >> (7 - (idx & 7))) & 0x01;
            return scale ? v * 255 : v;
        }
        case 2: {
            uint8_t v = (line[idx >> 2] >> (6 - 2 * (idx & 3))) & 0x03;
            return scale ? v * 85 : v;
        }
        case 4: {
            uint8_t v = (line[idx >> 1] >> (4 - 4 * (idx & 1))) & 0x0F;
            return scale ? v * 17 : v;
        }
        case 16:
            return line[idx * 2];
        default: // 8
            return line[idx];
    }
}

// Rec. 601 luma with integer weights (77 + 150 + 29 = 256)
static inline uint8_t luma(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

// Composite a gray value over a white background (the e-paper's blank color)
static inline uint8_t overWhite(uint8_t gray, uint8_t alpha) {
    return (uint8_t)((gray * alpha + 255 * (255 - alpha)) / 255);
}

// PNGdec draw callback. Note: pDraw->iBpp is bits per *channel* (PNG bit
// depth), not bits per pixel, so the pixel layout depends on iPixelType too.
static int pngDrawCallback(PNGDRAW *pDraw) {
    PngContext *ctx = (PngContext *)pDraw->pUser;
    int destY = ctx->offsetY + pDraw->y;
    if (destY < 0 || destY >= ctx->fbHeight) return 0;

    const uint8_t *line = pDraw->pPixels;
    const uint8_t *palette = pDraw->pPalette; // RGB triplets, then 256 alpha bytes at +768
    int srcWidth = pDraw->iWidth;
    int depth = pDraw->iBpp;

    for (int i = 0; i < srcWidth; i++) {
        int destX = ctx->offsetX + i;
        if (destX < 0 || destX >= ctx->fbWidth) continue;

        uint8_t gray8;
        switch (pDraw->iPixelType) {
            case PNG_PIXEL_GRAYSCALE:
                gray8 = pngSample(line, i, depth, true);
                break;
            case PNG_PIXEL_TRUECOLOR:
                gray8 = luma(pngSample(line, i * 3, depth, true),
                             pngSample(line, i * 3 + 1, depth, true),
                             pngSample(line, i * 3 + 2, depth, true));
                break;
            case PNG_PIXEL_INDEXED: {
                uint8_t idx = pngSample(line, i, depth, false);
                const uint8_t *rgb = palette + idx * 3;
                gray8 = luma(rgb[0], rgb[1], rgb[2]);
                if (pDraw->iHasAlpha) gray8 = overWhite(gray8, palette[768 + idx]);
                break;
            }
            case PNG_PIXEL_GRAY_ALPHA:
                gray8 = overWhite(pngSample(line, i * 2, depth, true),
                                  pngSample(line, i * 2 + 1, depth, true));
                break;
            case PNG_PIXEL_TRUECOLOR_ALPHA:
                gray8 = overWhite(luma(pngSample(line, i * 4, depth, true),
                                       pngSample(line, i * 4 + 1, depth, true),
                                       pngSample(line, i * 4 + 2, depth, true)),
                                  pngSample(line, i * 4 + 3, depth, true));
                break;
            default:
                gray8 = 255;
                break;
        }

        // Map 0-255 to 4-bit: 0x0=black, 0xF=white
        uint8_t gray4 = gray8 >> 4;

        int idx = (destY * ctx->fbWidth + destX) / 2;
        if (destX & 1) {
            ctx->framebuffer[idx] = (ctx->framebuffer[idx] & 0x0F) | ((gray4 & 0x0F) << 4);
        } else {
            ctx->framebuffer[idx] = (ctx->framebuffer[idx] & 0xF0) | (gray4 & 0x0F);
        }
    }
    return 1;
}

DecodeResult ImageDecoder::downloadAndDecode(const String &url, uint8_t *framebuffer, int fbWidth, int fbHeight) {
    Serial.printf("[TRMNL] Downloading image: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(30000);
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        Serial.println("[TRMNL] HTTP begin failed for image download");
        return DecodeResult::DOWNLOAD_FAILED;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[TRMNL] Image download HTTP %d\n", httpCode);
        http.end();
        return DecodeResult::DOWNLOAD_FAILED;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        contentLength = 1024 * 1024; // Assume max 1MB if unknown
    }

    Serial.printf("[TRMNL] Image size: %d bytes\n", contentLength);

    uint8_t *imgBuf = (uint8_t *)heap_caps_malloc(contentLength, MALLOC_CAP_SPIRAM);
    if (!imgBuf) {
        Serial.println("[TRMNL] Failed to allocate image buffer in PSRAM");
        http.end();
        return DecodeResult::OUT_OF_MEMORY;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t totalRead = 0;
    unsigned long lastData = millis();

    while (totalRead < (size_t)contentLength && (millis() - lastData < 30000)) {
        if (stream->available()) {
            size_t toRead = stream->available();
            if (toRead > (size_t)contentLength - totalRead) {
                toRead = (size_t)contentLength - totalRead;
            }
            size_t bytesRead = stream->readBytes(imgBuf + totalRead, toRead);
            totalRead += bytesRead;
            lastData = millis();
        } else {
            delay(10);
        }
    }

    http.end();

    if (totalRead < 4) {
        Serial.printf("[TRMNL] Image too small: %d bytes\n", totalRead);
        heap_caps_free(imgBuf);
        return DecodeResult::DOWNLOAD_FAILED;
    }

    Serial.printf("[TRMNL] Downloaded %d bytes\n", totalRead);

    // Detect format by magic bytes
    DecodeResult result;
    if (imgBuf[0] == 'B' && imgBuf[1] == 'M') {
        Serial.println("[TRMNL] Detected BMP format");
        result = decodeBMP(imgBuf, totalRead, framebuffer, fbWidth, fbHeight);
    } else if (imgBuf[0] == 0x89 && imgBuf[1] == 0x50 && imgBuf[2] == 0x4E && imgBuf[3] == 0x47) {
        Serial.println("[TRMNL] Detected PNG format");
        result = decodePNG(imgBuf, totalRead, framebuffer, fbWidth, fbHeight);
    } else {
        Serial.printf("[TRMNL] Unknown format: %02X %02X %02X %02X\n",
                      imgBuf[0], imgBuf[1], imgBuf[2], imgBuf[3]);
        result = DecodeResult::UNSUPPORTED_FORMAT;
    }

    heap_caps_free(imgBuf);
    return result;
}

DecodeResult ImageDecoder::decodeBMP(uint8_t *data, size_t length, uint8_t *framebuffer, int fbWidth, int fbHeight) {
    if (length < 54) return DecodeResult::DECODE_FAILED;

    // BMP header
    uint32_t dataOffset = *(uint32_t *)(data + 10);
    int32_t imgWidth = *(int32_t *)(data + 18);
    int32_t imgHeight = *(int32_t *)(data + 22);
    uint16_t bitsPerPixel = *(uint16_t *)(data + 28);
    uint32_t compression = *(uint32_t *)(data + 30);

    Serial.printf("[TRMNL] BMP: %dx%d, %d bpp, compression=%d\n",
                  imgWidth, imgHeight, bitsPerPixel, compression);

    if (compression != 0) {
        Serial.println("[TRMNL] Compressed BMP not supported");
        return DecodeResult::DECODE_FAILED;
    }

    bool bottomUp = (imgHeight > 0);
    if (imgHeight < 0) imgHeight = -imgHeight;

    int offsetX = (fbWidth - imgWidth) / 2;
    int offsetY = (fbHeight - imgHeight) / 2;
    if (offsetX < 0) offsetX = 0;
    if (offsetY < 0) offsetY = 0;

    // Row stride is padded to 4 bytes
    int rowStride;
    switch (bitsPerPixel) {
        case 1:
            rowStride = ((imgWidth + 31) / 32) * 4;
            break;
        case 8:
            rowStride = ((imgWidth + 3) / 4) * 4;
            break;
        case 24:
            rowStride = ((imgWidth * 3 + 3) / 4) * 4;
            break;
        default:
            Serial.printf("[TRMNL] Unsupported BMP bpp: %d\n", bitsPerPixel);
            return DecodeResult::UNSUPPORTED_FORMAT;
    }

    // Optional: read palette for 1-bit and 8-bit BMPs
    uint8_t palette[256]; // Grayscale values
    if (bitsPerPixel <= 8) {
        int paletteOffset = 14 + *(uint32_t *)(data + 14); // After DIB header
        int numColors = (bitsPerPixel == 1) ? 2 : 256;
        // BMP palette is BGRA, 4 bytes per entry
        for (int i = 0; i < numColors && (paletteOffset + i * 4 + 2) < (int)length; i++) {
            uint8_t b = data[paletteOffset + i * 4];
            uint8_t g = data[paletteOffset + i * 4 + 1];
            uint8_t r = data[paletteOffset + i * 4 + 2];
            palette[i] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
    }

    int renderH = (imgHeight < fbHeight) ? imgHeight : fbHeight;
    int renderW = (imgWidth < fbWidth) ? imgWidth : fbWidth;

    for (int row = 0; row < renderH; row++) {
        int srcRow = bottomUp ? (imgHeight - 1 - row) : row;
        uint8_t *rowPtr = data + dataOffset + srcRow * rowStride;

        if ((size_t)(rowPtr - data + rowStride) > length) break;

        int destY = offsetY + row;
        if (destY >= fbHeight) break;

        for (int col = 0; col < renderW; col++) {
            int destX = offsetX + col;
            if (destX >= fbWidth) break;

            uint8_t gray8;
            switch (bitsPerPixel) {
                case 1: {
                    int byteIdx = col / 8;
                    int bitIdx = 7 - (col & 7);
                    uint8_t bit = (rowPtr[byteIdx] >> bitIdx) & 1;
                    gray8 = palette[bit];
                    break;
                }
                case 8:
                    gray8 = palette[rowPtr[col]];
                    break;
                case 24: {
                    int off = col * 3;
                    uint8_t b = rowPtr[off];
                    uint8_t g = rowPtr[off + 1];
                    uint8_t r = rowPtr[off + 2];
                    gray8 = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
                    break;
                }
                default:
                    gray8 = 128;
                    break;
            }

            uint8_t gray4 = gray8 >> 4;
            setPixel4bpp(framebuffer, fbWidth, destX, destY, gray4);
        }
    }

    Serial.println("[TRMNL] BMP decode complete");
    return DecodeResult::SUCCESS;
}

DecodeResult ImageDecoder::decodePNG(uint8_t *data, size_t length, uint8_t *framebuffer, int fbWidth, int fbHeight) {
    PNG *png = new PNG();
    if (!png) {
        Serial.println("[TRMNL] Failed to allocate PNG decoder");
        return DecodeResult::OUT_OF_MEMORY;
    }

    int rc = png->openRAM(data, length, pngDrawCallback);
    if (rc != PNG_SUCCESS) {
        Serial.printf("[TRMNL] PNG open failed: %d\n", rc);
        free(png);
        return DecodeResult::DECODE_FAILED;
    }

    int imgWidth = png->getWidth();
    int imgHeight = png->getHeight();
    Serial.printf("[TRMNL] PNG: %dx%d, depth=%d, type=%d\n",
                  imgWidth, imgHeight, png->getBpp(), png->getPixelType());

    PngContext ctx;
    ctx.framebuffer = framebuffer;
    ctx.fbWidth = fbWidth;
    ctx.fbHeight = fbHeight;
    ctx.offsetX = (fbWidth - imgWidth) / 2;
    ctx.offsetY = (fbHeight - imgHeight) / 2;
    if (ctx.offsetX < 0) ctx.offsetX = 0;
    if (ctx.offsetY < 0) ctx.offsetY = 0;

    rc = png->decode(&ctx, 0);
    png->close();
    free(png);

    if (rc != PNG_SUCCESS) {
        Serial.printf("[TRMNL] PNG decode failed: %d\n", rc);
        return DecodeResult::DECODE_FAILED;
    }

    Serial.println("[TRMNL] PNG decode complete");
    return DecodeResult::SUCCESS;
}
