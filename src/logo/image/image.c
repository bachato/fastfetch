#include "image.h"

#if defined(FF_HAVE_IMAGEMAGICK7) || defined(FF_HAVE_IMAGEMAGICK6)

#define FF_KITTY_MAX_CHUNK_SIZE 4096

#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h> // memset

static bool getTermInfo(struct winsize* winsize)
{
    //Initialize every member to 0, because it isn't guaranteed that every terminal sets them all
    memset(winsize, 0, sizeof(struct winsize));

    ioctl(STDOUT_FILENO, TIOCGWINSZ, winsize);

    if(winsize->ws_row == 0 || winsize->ws_col == 0)
        ffGetTerminalResponse("\033[18t", 't', "\033[8;%hu;%hut", &winsize->ws_row, &winsize->ws_col);

    if(winsize->ws_ypixel == 0 || winsize->ws_xpixel == 0)
        ffGetTerminalResponse("\033[14t", 't', "\033[4;%hu;%hut", &winsize->ws_ypixel, &winsize->ws_xpixel);

    return
        winsize->ws_row > 0 &&
        winsize->ws_col > 0 &&
        winsize->ws_ypixel > 0 &&
        winsize->ws_xpixel > 0;
}

#ifdef FF_HAVE_ZLIB
#include <zlib.h>

static bool compressBlob(const FFinstance* instance, void** blob, size_t* length)
{
    FF_LIBRARY_LOAD(zlib, instance->config.libZ, false, "libz.so", 2)
    FF_LIBRARY_LOAD_SYMBOL(zlib, compressBound, false)
    FF_LIBRARY_LOAD_SYMBOL(zlib, compress2, false)

    uLong compressedLength = ffcompressBound(*length);
    void* compressed = malloc(compressedLength);
    if(compressed == NULL)
    {
        dlclose(zlib);
        return false;
    }

    int compressResult = ffcompress2(compressed, &compressedLength, *blob, *length, Z_BEST_COMPRESSION);
    dlclose(zlib);
    if(compressResult != Z_OK)
    {
        free(compressed);
        return false;
    }

    free(*blob);

    *length = (size_t) compressedLength;
    *blob = compressed;
    return true;
}

#endif // FF_HAVE_ZLIB

//We use only the defines from here, that are exactly the same in both versions
#ifdef FF_HAVE_IMAGEMAGICK7
    #include <MagickCore/MagickCore.h>
#else
    #include <magick/MagickCore.h>
#endif

typedef struct ImageMagickFunctions
{
    FF_LIBRARY_SYMBOL(CopyMagickString)
    FF_LIBRARY_SYMBOL(ImageToBlob)
    FF_LIBRARY_SYMBOL(Base64Encode)

    FFLogoIMWriteFunc writeFunc;
} ImageMagickFunctions;

static inline bool checkAllocationResult(void* data, size_t length)
{
    if(data == NULL)
        return false;

    if(length == 0)
    {
        free(data);
        return false;
    }

    return true;
}

static void printImageKittyChunc(char** blob, size_t* length, uint32_t chunkSize)
{
    size_t toWrite = chunkSize < *length ? chunkSize : *length;
    fputs("\033_Gm=1;", stdout);
    fwrite(*blob, sizeof(**blob), toWrite, stdout);
    fputs("\033\\", stdout);
    *blob = *blob + toWrite;
    *length -= toWrite;
}

static bool printImageKitty(const FFinstance* instance, ImageInfo* imageInfo, Image* image, ExceptionInfo* ExceptionInfo, uint32_t paddingLeft, ImageMagickFunctions* functions)
{
    functions->ffCopyMagickString(imageInfo->magick, "RGBA", 5);

    size_t length;
    void* blob = functions->ffImageToBlob(imageInfo, image, &length, ExceptionInfo);
    if(!checkAllocationResult(blob, length))
        return false;

    #ifdef FF_HAVE_ZLIB
        bool isCompressed = compressBlob(instance, &blob, &length);
    #else
        FF_UNUSED(instance)
        bool isCompressed = false;
    #endif

    char* encoded = functions->ffBase64Encode(blob, length, &length);
    free(blob);
    if(!checkAllocationResult(encoded, length))
        return false;

    ffPrintCharTimes(' ', paddingLeft);

    char* currentPos = encoded;

    printf("\033_Ga=T,f=32,s=%u,v=%u,m=1%s;\033\\", (uint32_t) image->columns, (uint32_t) image->rows, isCompressed ? ",o=z" : "");
    while (length > 0)
        printImageKittyChunc(&currentPos, &length, FF_KITTY_MAX_CHUNK_SIZE);
    fputs("\033_Gm=0;\033\\", stdout);

    free(encoded);
    return true;
}

static bool printImageSixel(ImageInfo* imageInfo, Image* image, ExceptionInfo* exceptionInfo, uint32_t paddingLeft, ImageMagickFunctions* functions)
{
    ffPrintCharTimes(' ', paddingLeft);

    functions->ffCopyMagickString(imageInfo->magick, "SIXEL", 6);
    imageInfo->file = stdout;
    bool writeResult = functions->writeFunc(imageInfo, image, exceptionInfo);

    if(!writeResult)
        printf("\033[%uD", paddingLeft);

    return writeResult;
}

FFLogoImageResult ffLogoPrintImageImpl(FFinstance* instance, void* imageMagick, FFLogoIMResizeFunc resizeFunc, FFLogoIMWriteFunc writeFunc, FFLogoType type)
{
    struct winsize winsize;
    if(!getTermInfo(&winsize))
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;

    FF_LIBRARY_LOAD_SYMBOL(imageMagick, AcquireExceptionInfo, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL(imageMagick, DestroyExceptionInfo, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL(imageMagick, AcquireImageInfo, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL(imageMagick, DestroyImageInfo, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL(imageMagick, ReadImage, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL(imageMagick, DestroyImage, FF_LOGO_IMAGE_RESULT_INIT_ERROR)

    ImageMagickFunctions functions;
    functions.writeFunc = writeFunc;

    FF_LIBRARY_LOAD_SYMBOL_ADRESS(imageMagick, functions.ffCopyMagickString, CopyMagickString, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL_ADRESS(imageMagick, functions.ffImageToBlob, ImageToBlob, FF_LOGO_IMAGE_RESULT_INIT_ERROR)
    FF_LIBRARY_LOAD_SYMBOL_ADRESS(imageMagick, functions.ffBase64Encode, Base64Encode, FF_LOGO_IMAGE_RESULT_INIT_ERROR)

    ExceptionInfo* exceptionInfo = ffAcquireExceptionInfo();
    if(exceptionInfo == NULL)
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;

    ImageInfo* imageInfoIn = ffAcquireImageInfo();
    if(imageInfoIn == NULL)
    {
        ffDestroyExceptionInfo(exceptionInfo);
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;
    }

    //+1, because we need to copy the null byte too
    functions.ffCopyMagickString(imageInfoIn->filename, instance->config.logoName.chars, instance->config.logoName.length + 1);

    Image* originalImage = ffReadImage(imageInfoIn, exceptionInfo);
    ffDestroyImageInfo(imageInfoIn);
    if(originalImage == NULL)
    {
        ffDestroyExceptionInfo(exceptionInfo);
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;
    }

    double characterPixelWidth = winsize.ws_xpixel / (double) winsize.ws_col;
    double characterPixelHeight = winsize.ws_ypixel / (double) winsize.ws_row;

    //We keep the values as double, to have more precise calculations later
    double imagePixelWidth = (double) instance->config.logoWidth * characterPixelWidth;
    double imagePixelHeight = (imagePixelWidth / (double) originalImage->columns) * (double) originalImage->rows;

    if(imagePixelWidth < 1.0 || imagePixelHeight < 1.0)
    {
        ffDestroyImage(originalImage);
        ffDestroyExceptionInfo(exceptionInfo);
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;
    }

    Image* resizedImage = (Image*) resizeFunc(originalImage, (size_t) imagePixelWidth, (size_t) imagePixelHeight, exceptionInfo);
    ffDestroyImage(originalImage);
    if(resizedImage == NULL)
    {
        ffDestroyExceptionInfo(exceptionInfo);
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;
    }

    ImageInfo* imageInfoOut = ffAcquireImageInfo();
    if(imageInfoOut == NULL)
    {
        ffDestroyImage(resizedImage);
        ffDestroyExceptionInfo(exceptionInfo);
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;
    }

    bool printSuccessful;
    if(type == FF_LOGO_TYPE_KITTY)
        printSuccessful = printImageKitty(instance, imageInfoOut, resizedImage, exceptionInfo, instance->config.logoPaddingLeft, &functions);
    else
        printSuccessful = printImageSixel(imageInfoOut, resizedImage, exceptionInfo, instance->config.logoPaddingLeft, &functions);

    ffDestroyImageInfo(imageInfoOut);
    ffDestroyImage(resizedImage);
    ffDestroyExceptionInfo(exceptionInfo);

    if(!printSuccessful)
        return FF_LOGO_IMAGE_RESULT_RUN_ERROR;

    instance->state.logoHeight = (uint32_t) (imagePixelHeight / characterPixelHeight);
    instance->state.logoWidth = instance->config.logoWidth + instance->config.logoPaddingLeft + instance->config.logoPaddingRight;

    fputs("\033[9999999D", stdout);
    printf("\033[%uA", instance->state.logoHeight);

    return FF_LOGO_IMAGE_RESULT_SUCCESS;
}

#endif //FF_HAVE_IMAGEMAGICK{6, 7}

bool ffLogoPrintImageIfExists(FFinstance* instance, FFLogoType type)
{
    #if !defined(FF_HAVE_IMAGEMAGICK7) && !defined(FF_HAVE_IMAGEMAGICK6)
        FF_UNUSED(instance);
    #endif

    #ifdef FF_HAVE_IMAGEMAGICK7
        FFLogoImageResult result = ffLogoPrintImageIM7(instance, type);
        if(result == FF_LOGO_IMAGE_RESULT_SUCCESS)
            return true;
        else if(result == FF_LOGO_IMAGE_RESULT_RUN_ERROR)
            return false;
    #endif

    #ifdef FF_HAVE_IMAGEMAGICK6
        return ffLogoPrintImageIM6(instance, type) == FF_LOGO_IMAGE_RESULT_SUCCESS;
    #endif

    return false;
}