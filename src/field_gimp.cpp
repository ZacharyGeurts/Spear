// SPDX-License-Identifier: MIT
// field-gimp — Field paint / image (GIMP rewrite, C++ only, PPM core)
// new · info · invert · flip · gray · noise · ascii · no GTK · GitHub-cap friendly
#include "spear_common.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace spear;

struct Img {
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;  // w*h*3
};

static void usage() {
  std::puts(
      "field-gimp — Field image tools (GIMP rewrite · C++ · PPM)\n"
      "  field-gimp new  <w> <h> [out.ppm]     solid Field green canvas\n"
      "  field-gimp info <in.ppm>\n"
      "  field-gimp invert <in> <out>\n"
      "  field-gimp flip  <in> <out>           horizontal\n"
      "  field-gimp gray  <in> <out>\n"
      "  field-gimp noise <in> <out> [seed]\n"
      "  field-gimp ascii <in> [cols]          terminal preview\n"
      "  field-gimp chips <in> <out>           Field Die fun grade\n"
      "  field-gimp status\n"
      "Doctrine: local files only · plain PPM · tiny ELF · God Bless");
}

static bool load_ppm(const char* path, Img& im) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  char magic[8]{};
  if (std::fscanf(f, "%7s", magic) != 1) {
    std::fclose(f);
    return false;
  }
  if (std::strcmp(magic, "P6") != 0 && std::strcmp(magic, "P3") != 0) {
    std::fclose(f);
    return false;
  }
  int c;
  // skip comments
  auto skip = [&]() {
    while ((c = std::fgetc(f)) != EOF) {
      if (c == '#')
        while ((c = std::fgetc(f)) != EOF && c != '\n') {
        }
      else if (!std::isspace(c)) {
        std::ungetc(c, f);
        break;
      }
    }
  };
  skip();
  if (std::fscanf(f, "%d", &im.w) != 1) {
    std::fclose(f);
    return false;
  }
  skip();
  if (std::fscanf(f, "%d", &im.h) != 1) {
    std::fclose(f);
    return false;
  }
  int maxv = 0;
  skip();
  if (std::fscanf(f, "%d", &maxv) != 1) {
    std::fclose(f);
    return false;
  }
  std::fgetc(f);  // single whitespace
  size_t n = (size_t)im.w * im.h * 3;
  im.rgb.resize(n);
  if (std::strcmp(magic, "P6") == 0) {
    if (std::fread(im.rgb.data(), 1, n, f) != n) {
      std::fclose(f);
      return false;
    }
  } else {
    for (size_t i = 0; i < n; ++i) {
      int v = 0;
      if (std::fscanf(f, "%d", &v) != 1) {
        std::fclose(f);
        return false;
      }
      im.rgb[i] = (uint8_t)(maxv == 255 ? v : (v * 255 / maxv));
    }
  }
  std::fclose(f);
  return true;
}

static bool save_ppm(const char* path, const Img& im) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
  size_t n = (size_t)im.w * im.h * 3;
  bool ok = std::fwrite(im.rgb.data(), 1, n, f) == n;
  std::fclose(f);
  return ok;
}

static int cmd_new(int argc, char** argv) {
  if (argc < 4) {
    usage();
    return 1;
  }
  int w = std::atoi(argv[2]), h = std::atoi(argv[3]);
  if (w < 1 || h < 1 || w > 8192 || h > 8192) {
    std::fprintf(stderr, "bad size\n");
    return 1;
  }
  const char* out = argc >= 5 ? argv[4] : "field-canvas.ppm";
  Img im;
  im.w = w;
  im.h = h;
  im.rgb.assign((size_t)w * h * 3, 0);
  // Field phosphor green gradient
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t i = ((size_t)y * w + x) * 3;
      im.rgb[i + 0] = (uint8_t)(8 + (x * 20) / w);
      im.rgb[i + 1] = (uint8_t)(40 + (y * 180) / h);
      im.rgb[i + 2] = (uint8_t)(16 + ((x + y) * 10) / (w + h));
    }
  }
  if (!save_ppm(out, im)) {
    std::perror(out);
    return 1;
  }
  std::printf("field-gimp new %dx%d → %s\n", w, h, out);
  return 0;
}

static int cmd_info(int argc, char** argv) {
  if (argc < 3) return 1;
  Img im;
  if (!load_ppm(argv[2], im)) {
    std::perror(argv[2]);
    return 1;
  }
  std::printf("field-gimp info %s\n  size: %dx%d\n  bytes: %zu\n  format: PPM P6 RGB\n", argv[2],
              im.w, im.h, im.rgb.size());
  return 0;
}

static int cmd_invert(int argc, char** argv) {
  if (argc < 4) return 1;
  Img im;
  if (!load_ppm(argv[2], im)) return 1;
  for (auto& b : im.rgb) b = 255 - b;
  if (!save_ppm(argv[3], im)) return 1;
  std::printf("field-gimp invert → %s\n", argv[3]);
  return 0;
}

static int cmd_flip(int argc, char** argv) {
  if (argc < 4) return 1;
  Img im;
  if (!load_ppm(argv[2], im)) return 1;
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w / 2; ++x) {
      size_t a = ((size_t)y * im.w + x) * 3;
      size_t b = ((size_t)y * im.w + (im.w - 1 - x)) * 3;
      for (int k = 0; k < 3; ++k) std::swap(im.rgb[a + k], im.rgb[b + k]);
    }
  }
  if (!save_ppm(argv[3], im)) return 1;
  std::printf("field-gimp flip → %s\n", argv[3]);
  return 0;
}

static int cmd_gray(int argc, char** argv) {
  if (argc < 4) return 1;
  Img im;
  if (!load_ppm(argv[2], im)) return 1;
  for (size_t i = 0; i < im.rgb.size(); i += 3) {
    uint8_t g = (uint8_t)((im.rgb[i] * 30 + im.rgb[i + 1] * 59 + im.rgb[i + 2] * 11) / 100);
    im.rgb[i] = im.rgb[i + 1] = im.rgb[i + 2] = g;
  }
  if (!save_ppm(argv[3], im)) return 1;
  std::printf("field-gimp gray → %s\n", argv[3]);
  return 0;
}

static int cmd_noise(int argc, char** argv) {
  if (argc < 4) return 1;
  unsigned seed = argc >= 5 ? (unsigned)std::strtoul(argv[4], nullptr, 10) : 0xC0FFEEu;
  Img im;
  if (!load_ppm(argv[2], im)) return 1;
  for (auto& b : im.rgb) {
    seed = seed * 1664525u + 1013904223u;
    int n = (int)(seed >> 24) - 128;
    int v = (int)b + n / 4;
    b = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  if (!save_ppm(argv[3], im)) return 1;
  std::printf("field-gimp noise → %s\n", argv[3]);
  return 0;
}

static int cmd_chips(int argc, char** argv) {
  // Fun Field Die grade: phi-blend green CRT look
  if (argc < 4) return 1;
  Img im;
  if (!load_ppm(argv[2], im)) return 1;
  const float phi = 0.6180339887f;
  for (size_t i = 0; i < im.rgb.size(); i += 3) {
    float r = im.rgb[i] / 255.f, g = im.rgb[i + 1] / 255.f, b = im.rgb[i + 2] / 255.f;
    float y = 0.299f * r + 0.587f * g + 0.114f * b;
    float ng = y * (1.f - phi) + g * phi;
    float nr = y * 0.15f;
    float nb = y * 0.25f + b * 0.1f;
    auto cl = [](float x) {
      int v = (int)(x * 255.f);
      return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    im.rgb[i] = cl(nr);
    im.rgb[i + 1] = cl(ng + 0.08f);
    im.rgb[i + 2] = cl(nb);
  }
  if (!save_ppm(argv[3], im)) return 1;
  std::printf("field-gimp chips (Field Die grade) → %s\n", argv[3]);
  return 0;
}

static int cmd_ascii(int argc, char** argv) {
  if (argc < 3) return 1;
  Img im;
  if (!load_ppm(argv[2], im)) return 1;
  int cols = argc >= 4 ? std::atoi(argv[3]) : 80;
  if (cols < 16) cols = 16;
  if (cols > 200) cols = 200;
  const char* ramp = " .:-=+*#%@";
  int rows = cols * im.h / (im.w * 2);
  if (rows < 1) rows = 1;
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      int sx = x * im.w / cols;
      int sy = y * im.h / rows;
      size_t i = ((size_t)sy * im.w + sx) * 3;
      int yv = (im.rgb[i] * 30 + im.rgb[i + 1] * 59 + im.rgb[i + 2] * 11) / 100;
      int idx = yv * 9 / 255;
      std::fputc(ramp[idx], stdout);
    }
    std::fputc('\n', stdout);
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }
  std::string c = argv[1];
  if (c == "new") return cmd_new(argc, argv);
  if (c == "info") return cmd_info(argc, argv);
  if (c == "invert") return cmd_invert(argc, argv);
  if (c == "flip") return cmd_flip(argc, argv);
  if (c == "gray") return cmd_gray(argc, argv);
  if (c == "noise") return cmd_noise(argc, argv);
  if (c == "chips") return cmd_chips(argc, argv);
  if (c == "ascii" || c == "view") return cmd_ascii(argc, argv);
  if (c == "status") {
    std::puts("field-gimp status\n  stack: C++ · GIMP rewrite · PPM core\n  no GTK · no plugins cloud\n  "
              "sister: field-obs · field-nvtop · fieldbox\n  github_cap: tiny ELF");
    return 0;
  }
  if (c == "-h" || c == "--help" || c == "help") {
    usage();
    return 0;
  }
  usage();
  return 1;
}
