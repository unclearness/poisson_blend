#define _CRT_SECURE_NO_WARNINGS

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>

#include "lodepng.h"
#include "nanopb/nanopb.h"

using namespace nanopb;

// load image, and perform gamma correction on it.
void loadImage(const char* file, ImageData& image,
               float gamma = DEFAULT_GAMMA) {
  std::vector<unsigned char> buf;

  unsigned error = lodepng::decode(buf, image.width, image.height, file);
  if (error) {
    printf("could not open input image %s: %s\n", file,
           lodepng_error_text(error));
    exit(1);
  }

  for (unsigned int i = 0; i < buf.size(); i += 4) {
    vec3 v = vec3(pow(buf[i + 0] / 255.0f, 1.0f / gamma),
                  pow(buf[i + 1] / 255.0f, 1.0f / gamma),
                  pow(buf[i + 2] / 255.0f, 1.0f / gamma));

    image.data.push_back(v);
  }
}

const char* findToken(const char* param, int argc, char* argv[]) {
  const char* token = nullptr;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], param) == 0) {
      if (i + 1 < argc) {
        token = argv[i + 1];
        break;
      }
    }
  }

  if (token == nullptr) {
    printf("Could not find command-line parameter %s\n", param);
    return nullptr;
  }

  return token;
}

const char* parseStringParam(const char* param, int argc, char* argv[]) {
  const char* token = findToken(param, argc, argv);
  return token;
}

bool parseIntParam(const char* param, int argc, char* argv[],
                   unsigned int& out) {
  const char* token = findToken(param, argc, argv);
  if (token == nullptr) return false;

  int r = sscanf(token, "%u,", &out);
  if (r != 1 || r == EOF) {
    return false;
  } else {
    return true;
  }
}

void printHelpExit() {
  printf("Invalid command line arguments specified!\n\n");

  printf("USAGE: poisson_blend [options]\n\n");
  printf(
      "NOTE: it is not allowed to blend an image to the exact borders of the "
      "image.\n      i.e., you can't set something like mx=0, my=0\n\n");

  printf("OPTIONS: \n");

  printf("  -target\t\t\ttarget image\n");
  printf("  -source\t\t\tsource image\n");
  printf("  -output\t\t\toutput image\n");

  printf("  -mask  \t\t\tmask image\n");

  printf("  -mx       \t\t\tblending target x-position\n");
  printf("  -my       \t\t\tblending target y-position\n");

  exit(1);
}

int main(int argc, char* argv[]) {
  // this is the position into which the source image is pasted.
  unsigned int mx;
  unsigned int my;

  //
  // begin with some command line parsing.
  //

  const char* targetFile = parseStringParam("-target", argc, argv);
  if (targetFile == nullptr) printHelpExit();

  const char* sourceFile = parseStringParam("-source", argc, argv);
  if (sourceFile == nullptr) printHelpExit();

  const char* outputFile = parseStringParam("-output", argc, argv);
  if (outputFile == nullptr) printHelpExit();

  const char* maskFile = parseStringParam("-mask", argc, argv);
  if (maskFile == nullptr) printHelpExit();

  if (!parseIntParam("-mx", argc, argv, mx)) printHelpExit();
  if (!parseIntParam("-my", argc, argv, my)) printHelpExit();

  ImageData maskImage;
  ImageData sourceImage;
  ImageData targetImage;

  // load all three images.
  loadImage(targetFile, targetImage);
  loadImage(maskFile, maskImage);
  loadImage(sourceFile, sourceImage);

  std::vector<unsigned char> outImage;
  nanopb::PoissonBlend(maskImage, sourceImage, targetImage, mx, my, outImage);

  lodepng::encode(outputFile, outImage, targetImage.width, targetImage.height);

  return 0;
}
